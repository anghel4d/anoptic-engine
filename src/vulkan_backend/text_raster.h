/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Text overlay: glyph GPU buffers, per-frame overlay targets, TEXTRASTER + composite. Gate: textOverlay.

#ifndef ANO_TEXT_RASTER_H
#define ANO_TEXT_RASTER_H

#include "vulkan_backend/structs.h"

// TextRasterPush.flags bits (wire format, mirrored in textraster.comp).
#define ANO_TEXT_RASTER_OPAQUE 0x1u // opaque black backdrop + full-canvas dispatch (self-test)
#define ANO_TEXT_RASTER_UIONLY 0x2u // skip the glyph loop (the UI self-test isolates prims)
#define ANO_TEXT_RASTER_NODITHER 0x4u // skip the 1-LSB store dither (exact self-test compare)
#define ANO_TEXT_RASTER_TILED    0x8u // UI prims come from per-tile lists, not the brute scan

// Push-constant block shared with textraster.comp (68 B, the shader may declare a
// prefix). The ui counts stay 0 with no UI compose.
typedef struct TextRasterPush {
    uint32_t instanceCount;
    uint32_t flags;
    uint32_t extentW;
    uint32_t extentH;
    uint32_t originX; // dispatch region offset, pixels (tile-aligned)
    uint32_t originY;
    uint32_t uiPrimCount;
    uint32_t uiClipCount;
    uint32_t uiPaintCount;
    int32_t  uiTileOx; // UI tile grid (TILED only), origin/extent in TILES
    int32_t  uiTileOy; // keyed on the UI bounds alone
    uint32_t uiTileGx;
    uint32_t uiTileGy;
    float    textB[4]; // pending text px AABB: tiles outside skip the glyph walk
} TextRasterPush;

// Push-constant block shared with textworld.vert/.frag (96 B).
typedef struct TextWorldPush {
    float    mvp[4][4];   // proj * view * model, this view
    float    panel[4];    // panel pixel W,H | panel world W,H
    float    viewport[2]; // this view's viewport W,H in screen px (quad pad scale)
    uint32_t first;       // == ANO_TEXT_WORLD_FIRST
    uint32_t count;
} TextWorldPush;

// Render-thread init. Always true: failure logs and clears textOverlay.
bool ano_vk_text_init(VulkanContext* ctx, RendererState* state);

// createDataBuffer with optional CONCURRENT graphics+compute sharing.
bool ano_vk_text_create_buffer(VulkanContext* ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                               VkMemoryPropertyFlags props, bool shared,
                               VkBuffer* buffer, GpuAllocation* alloc);

// Overlay images (one per frame, swapchain extent). From createColorResources.
// A refused image view logs and clears textOverlay/asyncText/textWorld.
void ano_vk_text_create_overlay(VulkanContext* ctx, RendererState* state);

// Destroys the overlay images/views (handle-guarded).
void ano_vk_text_destroy_overlay(VulkanContext* ctx, RendererState* state);

// Allocates and writes the per-frame descriptor sets from the global pool.
// Failure logs and clears state->textOverlay.
void ano_vk_text_create_sets(VulkanContext* ctx, RendererState* state);

// (Re)writes the per-frame sets. Rerun after a swapchain resize.
void ano_vk_text_update_sets(VulkanContext* ctx, RendererState* state);

// Replaces the on-screen text: shapes the string into the pending canonical array and
// bumps textVersion. Render thread only. No-op when the overlay is off or pinned.
void ano_vk_text_set(RendererState* state, anostr_t text, float sizePx,
                     const float origin[2], const float color[4]);

// ano_vk_text_set over a string literal.
#define ano_vk_text_set_lit(state, textlit, sizePx, origin, color) \
    ano_vk_text_set((state), anostr_lit(textlit), (sizePx), (origin), (color))

// ano_vk_text_set with per-glyph style runs (see AnoTextRun). Same semantics and gating.
void ano_vk_text_set_runs(RendererState* state, anostr_t text, const AnoTextRun* runs,
                          uint32_t runCount, const float origin[2]);

// Logic text blocks. block_set ADOPTS blk. Clear idempotent. Recompose + bump textVersion.
void ano_vk_text_block_set(RendererState* state, uint32_t text_id, const RenderTextBlock* blk);
void ano_vk_text_block_clear(RendererState* state, uint32_t text_id);

// Re-folds the retained logic blocks after state->uiScale changed. The OSD stays device-px.
void ano_vk_text_rescale(RendererState* state);

// Copies pending text into this slot's mapped frame buffer when stale. Call after the
// slot's fence wait and before its record/submit.
void ano_vk_text_frame_refresh(RendererState* state, uint32_t frameIndex);

// In-frame raster record (the async lane's fallback): clear, dispatch, hand the image
// to the fragment stage. No-op when asyncText.
void ano_vk_text_record(RendererState* state, VkCommandBuffer cmd, uint32_t frameIndex);

// Async raster CB on compute queue, signals textTimeline == ordinal (failed submit host-signals). No-op unless asyncText.
void ano_vk_text_submit_async(VulkanContext* ctx, RendererState* state, uint32_t frameIndex,
                              uint64_t ordinal);

// The composite-side overlay draw: one fullscreen premultiplied blend, recorded after
// the PiP insets.
void ano_vk_text_record_composite(RendererState* state, VkCommandBuffer cmd, uint32_t frameIndex);

// The world-space text panel: one bufferless quad draw, recorded inside a view's
// additive pass. No-op unless textWorld.
void ano_vk_text_record_world(RendererState* state, VkCommandBuffer cmd, uint32_t frameIndex,
                              uint32_t view);

// Teardown: frame-data + curve buffers + bake heap + FreeType. Pipelines/overlay cleaned elsewhere.
void ano_vk_text_destroy(VulkanContext* ctx, RendererState* state);

#endif
