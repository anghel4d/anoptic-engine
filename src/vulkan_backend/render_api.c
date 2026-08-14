/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_log.h>

using namespace ano;
#include <anoptic_memory.h>

#include <anoptic_meta.h>
#include <anoptic_render_resources.h>
#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/components.h"
#include "vulkan_backend/frame/frame.h"
#include "vulkan_backend/bridge/bridge.h"
#include "vulkan_backend/render_api.h"
#include "vulkan_backend/resources/resources.h"

static AnoRenderResidency *g_resourceResidency;

struct ano::AnoRenderResourcePublication {
    AnoResourceReload *reload;
    AnoRenderResidency *residency;
    uint64_t retireAfter;
    AnoRenderResourcePublication *next;
};

static AnoRenderResourcePublication *g_retiredResidencies;
static bool g_resourceEpochPending;

static void destroy_publication(AnoRenderResourcePublication *publication)
{
    if (!publication) return;
    ano_vk_resource_residency_destroy(publication->residency);
    if (publication->reload)
        ano_resource_reload_abort(publication->reload);
    mi_free(publication);
}

// Material SSBO row 0, claimed before any glTF parse.
#define ANO_DEFAULT_MATERIAL_INDEX 0u

namespace {

using LightingMode = ano::EnumValue<AnoLightingMode>;
inline constexpr auto LIGHTING_MODE_NAMES =
    ano::reflect_enum_names<AnoLightingMode>(
        "ANO_LIGHTING_", ano::EnumNameCase::upper);

static_assert(ano::Data<LightingMode>);
static_assert(ano::Data<decltype(LIGHTING_MODE_NAMES)>);

} // namespace

RenderResult<uint32_t> ano::anoRenderAssetPrimitives(
    AnoAssetId asset, const mat4 root, AnoRenderableDesc* out, uint32_t cap) {
    return ano_vk_resource_scene_primitives(
        g_resourceResidency, asset, root, out, cap);
}

RenderResult<uint32_t> ano::anoRenderAssetLights(
    AnoAssetId asset, const mat4 root, AnoSceneLightDesc* out, uint32_t cap) {
    return ano_vk_resource_scene_lights(
        g_resourceResidency, asset, root, out, cap);
}

uint32_t ano::anoRenderFallbackMesh(void)    { return FALLBACK_MESH_INDEX; }
uint32_t ano::anoRenderDefaultMaterial(void) {
    return ano_vk_resource_default_material(g_resourceResidency);
}
uint32_t ano::anoRenderStaticLightBase(void) { return ANO_STATIC_LIGHT_COUNT; }

RenderResult<const AnoFontBake *> ano::anoRenderTextBake(void)
{
    return result_if(rendererState.textOverlay,
                     &rendererState.textBake, RenderError::unavailable);
}
// Lighting-mode control. Published into the GlobalUBO tail by updateCullingBuffers.
RenderResult<> ano::ano_render_set_lighting_mode(AnoLightingMode mode) {
    if (!LightingMode::from(mode))
        return failure(RenderError::invalid_argument);
    if (rendererState.lightingMode != (uint32_t)mode) {
        rendererState.lightingMode = (uint32_t)mode;
        // Discard the in-progress timing window.
        ano_profile_reset_window();
    }
    return {};
}

AnoLightingMode ano::ano_render_get_lighting_mode(void) {
    return (AnoLightingMode)rendererState.lightingMode;
}

const char *ano::ano_render_lighting_mode_name(AnoLightingMode mode) {
    const auto parsed = LightingMode::from(mode);
    return parsed ? LIGHTING_MODE_NAMES.values[parsed->index()] : "?";
}

// Per-view screen-area cull threshold. Squared into CullUBO.viewCullParams[view][1] by updateCullingBuffers.
// Pixels of projected bounding-sphere radius; 0 disables the test, negative clamps to 0.
RenderResult<> ano::ano_render_set_view_cull_threshold(uint32_t view, float pixels) {
    if (view >= ANO_VIEW_COUNT)
        return failure(RenderError::invalid_argument);
    rendererState.cullPixelThreshold[view] = (pixels > 0.0f) ? pixels : 0.0f;
    return {};
}

RenderResult<float> ano::ano_render_get_view_cull_threshold(uint32_t view) {
    if (view >= ANO_VIEW_COUNT)
        return failure(RenderError::invalid_argument);
    return rendererState.cullPixelThreshold[view];
}

// Per-view LOD threshold. Copied into CullUBO.viewCullParams[view][2] by updateCullingBuffers.
// Pixels of projected radius at which level 1 begins; 0 disables LOD, negative clamps to 0.
RenderResult<> ano::ano_render_set_view_lod_threshold(uint32_t view, float pixels) {
    if (view >= ANO_VIEW_COUNT)
        return failure(RenderError::invalid_argument);
    rendererState.lodPixelThreshold[view] = (pixels > 0.0f) ? pixels : 0.0f;
    if (view == 0u) rendererState.shadowGlobalDirty = true; // shadow LOD tracks view 0
    return {};
}

RenderResult<float> ano::ano_render_get_view_lod_threshold(uint32_t view) {
    if (view >= ANO_VIEW_COUNT)
        return failure(RenderError::invalid_argument);
    return rendererState.lodPixelThreshold[view];
}

// Global LOD bias added to every entity's level in cull.comp. + = coarser, - = finer.
// Published into viewCullParams[v][3] by updateCullingBuffers. Clamped to +/- ANO_MAX_LOD.
void ano::ano_render_set_lod_bias(int32_t bias) {
    int32_t lim = (int32_t)ANO_MAX_LOD;
    rendererState.lodBias = bias < -lim ? -lim : (bias > lim ? lim : bias);
    rendererState.shadowGlobalDirty = true; // cached shadow layers hold the old LOD
}

int32_t ano::ano_render_get_lod_bias(void) {
    return rendererState.lodBias;
}

// Shadow LOD offset relative to view 0's LOD (0 = exact match, + = coarser shadow).
// Published into CullUBO.shadowLodBias by updateCullingBuffers. Clamped to [0, ANO_MAX_LOD].
void ano::ano_render_set_shadow_lod_bias(int32_t bias) {
    int32_t lim = (int32_t)ANO_MAX_LOD;
    rendererState.shadowLodBias = bias < 0 ? 0 : (bias > lim ? lim : bias);
    rendererState.shadowGlobalDirty = true; // cached shadow layers hold the old LOD
}

int32_t ano::ano_render_get_shadow_lod_bias(void) {
    return rendererState.shadowLodBias;
}

// Per-view Hi-Z occlusion toggle. Rejects entities behind last frame's depth pyramid.
// Published into CullUBO.hizParams[view].z (mipCount when on, 0 when off) by updateCullingBuffers. Default off.
RenderResult<> ano::ano_render_set_view_hiz_enable(uint32_t view, bool enable) {
    if (view >= ANO_VIEW_COUNT)
        return failure(RenderError::invalid_argument);
    rendererState.hizEnable[view] = enable ? 1u : 0u;
    return {};
}

RenderResult<bool> ano::ano_render_get_view_hiz_enable(uint32_t view) {
    if (view >= ANO_VIEW_COUNT)
        return failure(RenderError::invalid_argument);
    return rendererState.hizEnable[view] != 0u;
}

RenderResult<> ano::ano_render_capture_next_frame(const char *path)
{
    if (!path)
        return failure(RenderError::invalid_argument);
    return result_if(ano_frame_capture_request(&ctx, &rendererState, path),
                     RenderError::unavailable);
}

// Claim material SSBO row 0 with stock white PBR before resource realization.
static void register_default_material(void)
{
	if (rendererState.materialBuffer.capacity == 0u || rendererState.materialBuffer.count != 0u)
		return;

	MaterialData mat;
	ano_vk_init_default_material_data(&mat);
	for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
		rendererState.materialBuffer.mapped[frame][ANO_DEFAULT_MATERIAL_INDEX] = mat;
	rendererState.materialBuffer.count = 1u;
}

bool ano_render_load_scene_assets(AnoResourceManager *resources)
{
	register_default_material();
	const AnoResourceError realized = ano_vk_resource_residency_create(
		resources, &g_resourceResidency);
	if (realized != ANO_RESOURCE_OK) {
		ano_log(ANO_ERROR, "Render residency realization failed: %s",
		        ano_resource_error_string(realized));
		return false;
	}
	return true;
}

ResourceResult<AnoRenderResourcePublication *>
ano::ano_render_resources_prepare_reload(
    AnoResourceReload *reload, mi_heap_t *preparationHeap)
{
    if (reload == nullptr || preparationHeap == nullptr)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    AnoRenderResourcePublication *prepared =
        mi_zalloc_tp(AnoRenderResourcePublication);
    if (prepared == nullptr) {
        ano_resource_reload_abort(reload);
        return failure(ANO_RESOURCE_OUT_OF_MEMORY);
    }
    prepared->reload = reload;
    AnoResourceError result = ANO_RESOURCE_OK;
    if (ano_resource_reload_has_changes(reload)) {
        result = ano_vk_resource_residency_prepare_from_epoch(
            ano_resource_reload_epoch(reload), g_resourceResidency,
            preparationHeap, &prepared->residency);
    }
    if (result != ANO_RESOURCE_OK) {
        destroy_publication(prepared);
        return failure(result);
    }
    return prepared;
}

void ano::ano_render_resources_cancel_reload(
    AnoRenderResourcePublication *publication)
{
    if (publication == nullptr)
        return;
    destroy_publication(publication);
}

AnoRenderResourceReloadStatus ano::ano_render_resources_poll_reload(
    AnoRenderResourcePublication **slot)
{
    if (slot == nullptr || *slot == nullptr)
        return ANO_RENDER_RESOURCE_RELOAD_REJECTED;
    AnoRenderResourcePublication *publication = *slot;
    bool realized = publication->residency == nullptr;
    AnoResourceError realization = ANO_RESOURCE_OK;
    if (publication->residency)
        realization = ano_vk_resource_residency_realize_step(
            publication->residency, 4, &realized);
    if (realization != ANO_RESOURCE_OK) {
        *slot = nullptr;
        ano_render_resources_cancel_reload(publication);
        return ANO_RENDER_RESOURCE_RELOAD_REJECTED;
    }
    if (!realized)
        return ANO_RENDER_RESOURCE_RELOAD_PENDING;
    const VkResult upload = publication->residency
        ? ano_vk_resource_residency_poll_upload(publication->residency)
        : VK_SUCCESS;
    if (upload == VK_NOT_READY)
        return ANO_RENDER_RESOURCE_RELOAD_PENDING;
    *slot = nullptr;
    if (upload != VK_SUCCESS) {
        ano_render_resources_cancel_reload(publication);
        return ANO_RENDER_RESOURCE_RELOAD_REJECTED;
    }

    const auto result = ano_resource_reload_commit(publication->reload);
    publication->reload = nullptr;
    if (!result) {
        destroy_publication(publication);
        return ANO_RENDER_RESOURCE_RELOAD_REJECTED;
    }
    if (publication->residency == nullptr) {
        destroy_publication(publication);
        return ANO_RENDER_RESOURCE_RELOAD_COMMITTED;
    }

    AnoRenderResidency *retired = g_resourceResidency;
    g_resourceResidency = publication->residency;
    publication->residency = retired;
    g_resourceEpochPending = true;
    publication->retireAfter = rendererState.timelineOrdinal;
    if (retired == nullptr
        || rendererState.completedFrameSerial >= publication->retireAfter) {
        destroy_publication(publication);
    } else {
        publication->next = g_retiredResidencies;
        g_retiredResidencies = publication;
    }
    return ANO_RENDER_RESOURCE_RELOAD_COMMITTED;
}

void ano_render_resources_apply_pending(uint32_t frameIndex)
{
    if (!g_resourceEpochPending)
        return;
    render_apply_resource_epoch(&rendererState, g_resourceResidency,
                                frameIndex);
    g_resourceEpochPending = false;
}

void ano_render_resources_collect_retired(uint64_t completedFrameSerial)
{
    AnoRenderResourcePublication **link = &g_retiredResidencies;
    while (*link != nullptr) {
        AnoRenderResourcePublication *entry = *link;
        if (completedFrameSerial < entry->retireAfter) {
            link = &entry->next;
            continue;
        }
        *link = entry->next;
        destroy_publication(entry);
    }
}

void ano_render_unload_scene_assets(void)
{
	ano_render_resources_collect_retired(UINT64_MAX);
	ano_vk_resource_residency_destroy(g_resourceResidency);
	g_resourceResidency = nullptr;
	g_resourceEpochPending = false;
}
