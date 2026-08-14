/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

/* Logic<->render bridge: ring alloc/teardown + non-inline logic endpoints.
 * Hot-path push/pop and render-master endpoints stay inlined in render_bridge.h.
 * Public contract: include/anoptic_render.h. */

#include "render_bridge.h"
#include "render_command_contract.h"
#include "bulk_update_plan.h"
#include <anoptic_meta.h>

using namespace ano;

#include <stdint.h>
#include <string.h>

#include <anoptic_log.h>


// Events-ring element size (copied per push/pop). Cap at 32 B.
static_assert(sizeof(RenderEvent) <= 32u, "RenderEvent grew past 32 bytes; revisit the events ring");


/* Bridge Lifecycle */

bool ano_render_bridge_init(AnoRenderBridge *bridge, mi_heap_t *heap,
                            uint32_t cmd_capacity_pow2, uint32_t evt_capacity_pow2)
{
    if (!bridge || !heap) return false;
    const auto allocate = [heap](size_t count, size_t width) -> void* {
        return mi_heap_calloc(heap, count, width);
    };
    if (!bridge->commands.initialize(cmd_capacity_pow2, allocate))
        return false;
    if (!bridge->events.initialize(evt_capacity_pow2, allocate)) {
        bridge->commands.destroy([](void* memory) { mi_free(memory); });
        return false;
    }
    bridge->snapshot.initialize();
    bridge->viewState.initialize();
    bridge->viewRejectWarned = false;
    return true;
}

// inv: reflected ownership map is total over RenderCommandKind. Ownership rides bulk_owned.
void ano_render_command_release(const RenderCommand *cmd)
{
    if (!cmd || !cmd->bulk_owned) return;
    if (const void *blk = ano::render_contract::owned_payload(*cmd))
        mi_free(const_cast<void *>(blk));
}

// inv: caller quiesces both roles first (main.c joins producer, then teardown on consumer thread).
void ano_render_bridge_destroy(AnoRenderBridge *bridge)
{
    if (!bridge) return;
    // Discharge undelivered payloads (ring frees buffer only).
    RenderCommand cmd;
    while (bridge->commands.pop(cmd))
        ano_render_command_release(&cmd);
    bridge->commands.destroy([](void* memory) { mi_free(memory); });
    bridge->events.destroy([](void* memory) { mi_free(memory); });
}


/* Logic Master Endpoints */

RenderResult<> ano::ano_render_submit(AnoRenderBridge *bridge, const RenderCommand *cmd)
{
    if (!bridge || !cmd)
        return failure(RenderError::invalid_argument);
    return result_if(bridge->commands.push(*cmd),
                     RenderError::backpressure);
}

RenderResult<> ano::ano_render_light_attach(AnoRenderBridge *bridge, uint32_t light_id, uint32_t parent_render_id,
                             const RenderLightParams *params, float ox, float oy, float oz)
{
    if (!bridge || !params)
        return failure(RenderError::invalid_argument);
    RenderCommand c = { .kind = RCMD_LIGHT_ATTACH, .render_id = parent_render_id, .light_id = light_id };
    c.light = *params;
    c.light_offset[0] = ox; c.light_offset[1] = oy; c.light_offset[2] = oz;
    return ano_render_submit(bridge, &c);
}

RenderResult<> ano::ano_render_light_update(AnoRenderBridge *bridge, uint32_t light_id,
                             const RenderLightParams *params, float ox, float oy, float oz)
{
    return ano_render_light_update_fields(bridge, light_id, params, ox, oy, oz, ANO_LIGHT_FIELD_ALL);
}

RenderResult<> ano::ano_render_light_update_fields(AnoRenderBridge *bridge, uint32_t light_id,
                                    const RenderLightParams *params, float ox, float oy, float oz,
                                    uint32_t fields)
{
    if (!bridge || !params || (fields & ~(ANO_LIGHT_FIELD_ALL | ANO_LIGHT_FIELD_CAST)))
        return failure(RenderError::invalid_argument);
    RenderCommand c = { .kind = RCMD_LIGHT_UPDATE, .light_id = light_id, .light_fields = fields };
    c.light = *params;
    c.light_offset[0] = ox; c.light_offset[1] = oy; c.light_offset[2] = oz;
    return ano_render_submit(bridge, &c);
}

RenderResult<> ano::ano_render_light_detach(AnoRenderBridge *bridge, uint32_t light_id)
{
    RenderCommand c = { .kind = RCMD_LIGHT_DETACH, .light_id = light_id };
    return ano_render_submit(bridge, &c);
}


/* Bulk */

// inv: NULL batch before count; zero count before any array touch. Only mask-named arrays are read.
RenderResult<> ano::ano_render_submit_bulk_update(AnoRenderBridge *bridge, const RenderUpdateBatch *batch)
{
    if (bridge == NULL || batch == NULL)
        return failure(RenderError::invalid_argument);
    if (batch->count == 0u)
        return {};
    uint32_t count = batch->count, fields = batch->fields;
    const ano::render_bulk::BulkPackPlan *plan = ano::render_bulk::find_plan(fields);
    if (plan == nullptr)
        return failure(RenderError::invalid_argument);

    const void *sources[ano::render_bulk::segmentCount];
    size_t bytes = sizeof(RenderUpdateBatch);
    for (size_t i = 0u; i < plan->count; ++i) {
        const ano::render_bulk::BulkSegment& segment = ano::render_bulk::segment(*plan, i);
        sources[i] = segment.source(*batch);
        if (sources[i] == nullptr
            || !ano_size_align_up(&bytes, segment.alignment)
            || !ano_size_add_array(&bytes, count, segment.stride))
            return failure(RenderError::invalid_argument);
    }

    char *blk = static_cast<char *>(mi_malloc(bytes));
    if (!blk)
        return failure(RenderError::out_of_memory);
    RenderUpdateBatch *b = (RenderUpdateBatch *)blk;
    *b = (RenderUpdateBatch){ .count = count, .fields = fields };
    size_t offset = sizeof(RenderUpdateBatch);
    for (size_t i = 0u; i < plan->count; ++i) {
        const ano::render_bulk::BulkSegment& segment = ano::render_bulk::segment(*plan, i);
        const bool aligned = ano_size_align_up(&offset, segment.alignment);
        ano::assume(aligned);
        segment.copy(*b, blk + offset, sources[i], count);
        offset += static_cast<size_t>(count) * segment.stride;
    }
    ano::assume(offset == bytes);

    RenderCommand cmd = { .kind = RCMD_BULK_UPDATE, .update = b, .bulk_owned = true };
    if (auto submitted = ano_render_submit(bridge, &cmd); !submitted) {
        ano_render_command_release(&cmd);
        return failure(submitted.error());
    }
    return {};
}

// Mass despawn into one render-owned block. Zero count before id read.
RenderResult<> ano::ano_render_submit_bulk_destroy(AnoRenderBridge *bridge, const uint32_t *render_ids, uint32_t count)
{
    if (bridge == NULL)
        return failure(RenderError::invalid_argument);
    if (count == 0u)
        return {};
    if (render_ids == NULL)
        return failure(RenderError::invalid_argument);
    size_t bytes = sizeof(RenderDestroyBatch);
    if (!ano_size_add_array(&bytes, count, sizeof(uint32_t)))
        return failure(RenderError::invalid_argument);
    char *blk = static_cast<char *>(mi_malloc(bytes));
    if (!blk)
        return failure(RenderError::out_of_memory);
    RenderDestroyBatch *b = (RenderDestroyBatch *)blk;
    uint32_t *ids = (uint32_t *)(blk + sizeof(RenderDestroyBatch));
    memcpy(ids, render_ids, (size_t)count * sizeof(uint32_t));
    b->count = count;
    b->render_ids = ids;
    RenderCommand cmd = { .kind = RCMD_BULK_DESTROY, .destroy = b, .bulk_owned = true };
    if (auto submitted = ano_render_submit(bridge, &cmd); !submitted) {
        ano_render_command_release(&cmd);
        return failure(submitted.error());
    }
    return {};
}


/* Screen Text */

static_assert(ANO_RENDER_TEXT_MAX <= (SIZE_MAX - sizeof(RenderTextBlock)) / sizeof(AnoGlyphInstance),
               "a full screen-text block must fit size_t");

// Packs header + instances into one render-owned block. count 0 -> clear.
RenderResult<> ano::ano_render_text_set(AnoRenderBridge *bridge, uint32_t text_id,
                                          const AnoGlyphInstance *instances, uint32_t count)
{
    if (bridge == NULL)
        return failure(RenderError::invalid_argument);
    if (count == 0u)
        return ano_render_text_clear(bridge, text_id);
    if (instances == NULL)
        return failure(RenderError::invalid_argument);
    if (count > ANO_RENDER_TEXT_MAX)
        count = ANO_RENDER_TEXT_MAX;
    size_t bytes = sizeof(RenderTextBlock) + (size_t)count * sizeof(AnoGlyphInstance);
    char *blk = static_cast<char *>(mi_malloc(bytes));
    if (blk == NULL)
        return failure(RenderError::out_of_memory);
    RenderTextBlock *b = (RenderTextBlock *)blk;
    AnoGlyphInstance *inst = (AnoGlyphInstance *)(blk + sizeof(RenderTextBlock));
    memcpy(inst, instances, (size_t)count * sizeof(AnoGlyphInstance));
    b->count = count;
    b->instances = inst;
    RenderCommand c = { .kind = RCMD_TEXT_SET, .text = b, .text_id = text_id, .bulk_owned = true };
    if (!bridge->commands.push(c)) {
        ano_render_command_release(&c);
        return failure(RenderError::backpressure);
    }
    return {};
}

RenderResult<> ano::ano_render_text_clear(AnoRenderBridge *bridge, uint32_t text_id)
{
    RenderCommand c = { .kind = RCMD_TEXT_CLEAR, .text_id = text_id };
    return ano_render_submit(bridge, &c);
}


/* UI */

// Bounds PATH prim curve walk (aux0 = stream offset, aux1 = monotone-quad count):
// start word, then per quad optional SENTINEL + restart ahead of control + end.
static bool ui_path_walk_valid(const uint32_t *curves, uint32_t curveCount,
                               uint32_t off, uint32_t quads)
{
    if (quads == 0u || off >= curveCount || curves[off] == ANO_UI_CURVE_SENTINEL)
        return false;
    uint32_t i = off + 1u;
    for (uint32_t c = 0; c < quads; c++) {
        if (i >= curveCount)
            return false;
        if (curves[i] == ANO_UI_CURVE_SENTINEL) {
            i++;
            if (i >= curveCount || curves[i] == ANO_UI_CURVE_SENTINEL)
                return false; // contour restart must be a point
            i++;
        }
        if (i + 1u >= curveCount)
            return false;
        i += 2u;
    }
    return true;
}

// Block-local refs, including the paint's stop window. Failure -> INVALID.
static bool ui_prim_valid(const AnoUiPrim *p, uint32_t clips, uint32_t paints, uint32_t glyphs,
                          const uint32_t *curves, uint32_t curveCount,
                          const AnoUiPaint *paintTab, uint32_t stops)
{
    if (p->clipRef != ANO_UI_REF_NONE && p->clipRef >= clips)
        return false;
    if (p->paintRef != ANO_UI_REF_NONE) {
        if (p->paintRef >= paints)
            return false;
        const AnoUiPaint *pa = &paintTab[p->paintRef];
        // Overflow-safe stop window.
        if (pa->stopFirst > stops || pa->stopCount > stops - pa->stopFirst)
            return false;
    }
    if (p->kind == ANO_UI_GLYPHS && (p->aux0 > glyphs || p->aux1 > glyphs - p->aux0))
        return false;
    if (p->kind == ANO_UI_PATH && !ui_path_walk_valid(curves, curveCount, p->aux0, p->aux1))
        return false;
    return true;
}

static_assert((size_t)ANO_RENDER_UI_MAX_PRIMS  * sizeof(AnoUiPrim)
             + (size_t)ANO_RENDER_UI_MAX_CLIPS  * sizeof(AnoUiClip)
             + (size_t)ANO_RENDER_UI_MAX_PAINTS * sizeof(AnoUiPaint)
             + (size_t)ANO_RENDER_UI_MAX_STOPS  * sizeof(AnoUiStop)
             + (size_t)ANO_RENDER_UI_MAX_CURVES * sizeof(uint32_t)
             + (size_t)ANO_RENDER_UI_MAX_GLYPHS * sizeof(AnoGlyphInstance)
               <= SIZE_MAX - sizeof(RenderUiBlock),
               "a maximal UI block must fit size_t");

// Packs tables + glyphs into one render-owned block.
// Zero prims -> clear. NULL builder -> INVALID.
RenderResult<> ano::ano_render_ui_set(AnoRenderBridge *bridge, uint32_t ui_id, uint32_t layer,
                                        const AnoUiBuilder *ui,
                                        const AnoGlyphInstance *glyphs, uint32_t glyphCount)
{
    if (bridge == NULL || ui == NULL)
        return failure(RenderError::invalid_argument);
    if (ui->primCount == 0u)
        return ano_render_ui_clear(bridge, ui_id);
    if (ui->primCount > ANO_RENDER_UI_MAX_PRIMS || ui->clipCount > ANO_RENDER_UI_MAX_CLIPS
        || ui->paintCount > ANO_RENDER_UI_MAX_PAINTS || ui->stopCount > ANO_RENDER_UI_MAX_STOPS
        || ui->curveCount > ANO_RENDER_UI_MAX_CURVES
        || glyphCount > ANO_RENDER_UI_MAX_GLYPHS || (glyphCount > 0u && glyphs == NULL)
        || ui->prims == NULL
        || (ui->clipCount  > 0u && ui->clips  == NULL)
        || (ui->paintCount > 0u && ui->paints == NULL)
        || (ui->stopCount  > 0u && ui->stops  == NULL)
        || (ui->curveCount > 0u && ui->curves == NULL)) {
        ano_log(ANO_WARN, "UI bridge: ui_id %u dropped (per-block caps or a count over an absent table).", ui_id);
        return failure(RenderError::invalid_argument);
    }
    for (uint32_t i = 0; i < ui->primCount; i++) {
        if (!ui_prim_valid(&ui->prims[i], ui->clipCount, ui->paintCount, glyphCount,
                           ui->curves, ui->curveCount, ui->paints, ui->stopCount)) {
            ano_log(ANO_WARN, "UI bridge: ui_id %u dropped (prim %u invalid).", ui_id, i);
            return failure(RenderError::invalid_argument);
        }
    }
    size_t primB = (size_t)ui->primCount * sizeof(AnoUiPrim);
    size_t clipB = (size_t)ui->clipCount * sizeof(AnoUiClip);
    size_t paintB = (size_t)ui->paintCount * sizeof(AnoUiPaint);
    size_t stopB = (size_t)ui->stopCount * sizeof(AnoUiStop);
    size_t curveB = (size_t)ui->curveCount * sizeof(uint32_t);
    size_t glyphB = (size_t)glyphCount * sizeof(AnoGlyphInstance);
    char *blk = static_cast<char *>(
        mi_malloc(sizeof(RenderUiBlock) + primB + clipB + paintB + stopB + curveB + glyphB));
    if (blk == NULL)
        return failure(RenderError::out_of_memory);
    RenderUiBlock *b = (RenderUiBlock *)blk;
    char *at = blk + sizeof(RenderUiBlock);
    b->layer = layer;
    b->surface = ANO_UI_SURFACE_OVERLAY;
    b->scroll[0] = 0.0f;
    b->scroll[1] = 0.0f;
    b->primCount = ui->primCount;
    b->clipCount = ui->clipCount;
    b->paintCount = ui->paintCount;
    b->stopCount = ui->stopCount;
    b->curveCount = ui->curveCount;
    b->glyphCount = glyphCount;
    b->prims = (const AnoUiPrim *)at;
    memcpy(at, ui->prims, primB);
    at += primB;
    b->clips = (const AnoUiClip *)at;
    if (clipB) memcpy(at, ui->clips, clipB);
    at += clipB;
    b->paints = (const AnoUiPaint *)at;
    if (paintB) memcpy(at, ui->paints, paintB);
    at += paintB;
    b->stops = (const AnoUiStop *)at;
    if (stopB) memcpy(at, ui->stops, stopB);
    at += stopB;
    b->curves = (const uint32_t *)at;
    if (curveB) memcpy(at, ui->curves, curveB);
    at += curveB;
    b->glyphs = (const AnoGlyphInstance *)at;
    if (glyphB) memcpy(at, glyphs, glyphB);
    RenderCommand c = { .kind = RCMD_UI_SET, .ui = b, .ui_id = ui_id, .bulk_owned = true };
    if (!bridge->commands.push(c)) {
        ano_render_command_release(&c);
        return failure(RenderError::backpressure);
    }
    return {};
}

RenderResult<> ano::ano_render_ui_clear(AnoRenderBridge *bridge, uint32_t ui_id)
{
    RenderCommand c = { .kind = RCMD_UI_CLEAR, .ui_id = ui_id };
    return ano_render_submit(bridge, &c);
}


/* Back-Channel */

RenderResult<bool> ano::ano_render_poll_event(
    AnoRenderBridge *bridge, RenderEvent *out)
{
    if (!bridge || !out)
        return failure(RenderError::invalid_argument);
    return bridge->events.pop(*out);
}

RenderResult<bool> ano::ano_render_acquire_snapshot(
    AnoRenderBridge *bridge, RenderSnapshot *out)
{
    if (!bridge || !out)
        return failure(RenderError::invalid_argument);
    return bridge->snapshot.acquire(*out);
}

// Accept-form compares need float fields.
static_assert(std::same_as<std::remove_cvref_t<decltype(((AnoViewState *)nullptr)->eye[0])>, float>
           && std::same_as<std::remove_cvref_t<decltype(((AnoViewState *)nullptr)->center[0])>, float>
           && std::same_as<std::remove_cvref_t<decltype(((AnoViewState *)nullptr)->up[0])>, float>
           && std::same_as<std::remove_cvref_t<decltype(((AnoViewState *)nullptr)->fovYDeg)>, float>,
              "view pose guard assumes float eye/center/up/fovYDeg");
static_assert(sizeof ((AnoViewState *)0)->eye    == 3u * sizeof(float)
                   && sizeof ((AnoViewState *)0)->center == 3u * sizeof(float)
                   && sizeof ((AnoViewState *)0)->up     == 3u * sizeof(float),
               "view pose guard reads 3-component eye/center/up");

// in:  view (logic-published camera pose)
// out: true if lookAt(eye, center, up) yields a finite orthonormal basis
// inv: no sqrt, no divide. Accept-form: NaN/inf fails every compare -> reject.
static bool view_pose_valid(const AnoViewState *view)
{
    const float eps2 = 1e-8f; // sin(theta)^2 floor: ~0.006 deg off-parallel
    float d[3] = { view->center[0] - view->eye[0],
                   view->center[1] - view->eye[1],
                   view->center[2] - view->eye[2] };
    const float *u = view->up;
    float c[3] = { d[1] * u[2] - d[2] * u[1],
                   d[2] * u[0] - d[0] * u[2],
                   d[0] * u[1] - d[1] * u[0] };
    float d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    float u2 = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
    float c2 = c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
    // |d x u|^2 > eps2 |d|^2 |u|^2 rejects eye==center, zero up, up parallel to forward, NaN/inf.
    if (!(c2 > eps2 * d2 * u2)) return false;
    return view->fovYDeg > 0.0f && view->fovYDeg < 180.0f; // the other NaN inlet into proj
}

// in:  bridge, view (logic-owned camera pose)
// out: nothing; degenerate pose dropped, last accepted stands
// inv: single producer. Validity checked here once.
RenderResult<> ano::ano_render_publish_view(
    AnoRenderBridge *bridge, const AnoViewState *view)
{
    if (!bridge || !view)
        return failure(RenderError::invalid_argument);
    if (!view_pose_valid(view)) {
        if (!bridge->viewRejectWarned) {
            bridge->viewRejectWarned = true;
            ano_log(ANO_WARN, "Render bridge: degenerate camera pose rejected at seq %llu "
                              "(coincident eye/center, zero or parallel up, bad fovY, or "
                              "non-finite field); previous pose stands.",
                    (unsigned long long)view->seq);
        }
        return failure(RenderError::invalid_argument);
    }
    bridge->viewState.publish(*view);
    return {};
}
