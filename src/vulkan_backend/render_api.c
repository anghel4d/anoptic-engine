/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_log.h>

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

uint32_t anoRenderAssetPrimitives(AnoAssetId asset, const mat4 root,
                                  AnoRenderableDesc* out, uint32_t cap) {
    return ano_vk_resource_scene_primitives(
        g_resourceResidency, asset, root, out, cap);
}

uint32_t anoRenderAssetLights(AnoAssetId asset, const mat4 root,
                              AnoSceneLightDesc* out, uint32_t cap) {
    return ano_vk_resource_scene_lights(
        g_resourceResidency, asset, root, out, cap);
}

uint32_t anoRenderFallbackMesh(void)    { return FALLBACK_MESH_INDEX; }
uint32_t anoRenderDefaultMaterial(void) {
    return ano_vk_resource_default_material(g_resourceResidency);
}
uint32_t anoRenderStaticLightBase(void) { return ANO_STATIC_LIGHT_COUNT; }

// Baked font for logic-side shaping (anoptic_render.h). NULL when the text stack is down.
const AnoFontBake* anoRenderTextBake(void)
{
    return rendererState.textOverlay ? &rendererState.textBake : NULL;
}
// Lighting-mode control. Published into the GlobalUBO tail by updateCullingBuffers.
void ano_render_set_lighting_mode(AnoLightingMode mode) {
    if (!LightingMode::from(mode)) return;
    if (rendererState.lightingMode != (uint32_t)mode) {
        rendererState.lightingMode = (uint32_t)mode;
        // Discard the in-progress timing window.
        ano_profile_reset_window();
    }
}

AnoLightingMode ano_render_get_lighting_mode(void) {
    return (AnoLightingMode)rendererState.lightingMode;
}

const char *ano_render_lighting_mode_name(AnoLightingMode mode) {
    const auto parsed = LightingMode::from(mode);
    return parsed ? LIGHTING_MODE_NAMES.values[parsed->index()] : "?";
}

// Per-view screen-area cull threshold. Squared into CullUBO.viewCullParams[view][1] by updateCullingBuffers.
// Pixels of projected bounding-sphere radius; 0 disables the test, negative clamps to 0.
void ano_render_set_view_cull_threshold(uint32_t view, float pixels) {
    if (view >= ANO_VIEW_COUNT) return;
    rendererState.cullPixelThreshold[view] = (pixels > 0.0f) ? pixels : 0.0f;
}

float ano_render_get_view_cull_threshold(uint32_t view) {
    if (view >= ANO_VIEW_COUNT) return 0.0f;
    return rendererState.cullPixelThreshold[view];
}

// Per-view LOD threshold. Copied into CullUBO.viewCullParams[view][2] by updateCullingBuffers.
// Pixels of projected radius at which level 1 begins; 0 disables LOD, negative clamps to 0.
void ano_render_set_view_lod_threshold(uint32_t view, float pixels) {
    if (view >= ANO_VIEW_COUNT) return;
    rendererState.lodPixelThreshold[view] = (pixels > 0.0f) ? pixels : 0.0f;
    if (view == 0u) rendererState.shadowGlobalDirty = true; // shadow LOD tracks view 0
}

float ano_render_get_view_lod_threshold(uint32_t view) {
    if (view >= ANO_VIEW_COUNT) return 0.0f;
    return rendererState.lodPixelThreshold[view];
}

// Global LOD bias added to every entity's level in cull.comp. + = coarser, - = finer.
// Published into viewCullParams[v][3] by updateCullingBuffers. Clamped to +/- ANO_MAX_LOD.
void ano_render_set_lod_bias(int32_t bias) {
    int32_t lim = (int32_t)ANO_MAX_LOD;
    rendererState.lodBias = bias < -lim ? -lim : (bias > lim ? lim : bias);
    rendererState.shadowGlobalDirty = true; // cached shadow layers hold the old LOD
}

int32_t ano_render_get_lod_bias(void) {
    return rendererState.lodBias;
}

// Shadow LOD offset relative to view 0's LOD (0 = exact match, + = coarser shadow).
// Published into CullUBO.shadowLodBias by updateCullingBuffers. Clamped to [0, ANO_MAX_LOD].
void ano_render_set_shadow_lod_bias(int32_t bias) {
    int32_t lim = (int32_t)ANO_MAX_LOD;
    rendererState.shadowLodBias = bias < 0 ? 0 : (bias > lim ? lim : bias);
    rendererState.shadowGlobalDirty = true; // cached shadow layers hold the old LOD
}

int32_t ano_render_get_shadow_lod_bias(void) {
    return rendererState.shadowLodBias;
}

// Per-view Hi-Z occlusion toggle. Rejects entities behind last frame's depth pyramid.
// Published into CullUBO.hizParams[view].z (mipCount when on, 0 when off) by updateCullingBuffers. Default off.
void ano_render_set_view_hiz_enable(uint32_t view, bool enable) {
    if (view >= ANO_VIEW_COUNT) return;
    rendererState.hizEnable[view] = enable ? 1u : 0u;
}

bool ano_render_get_view_hiz_enable(uint32_t view) {
    if (view >= ANO_VIEW_COUNT) return false;
    return rendererState.hizEnable[view] != 0u;
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

AnoResourceError ano_render_resources_publish_reload(AnoResourceReload *reload)
{
    if (reload == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    AnoResourceError result = ANO_RESOURCE_OK;
    if (!ano_resource_reload_has_changes(reload))
        return ano_resource_reload_commit(reload);

    AnoRenderResidency *candidate = nullptr;
    result = ano_vk_resource_residency_create_from_epoch(
        ano_resource_reload_epoch(reload), g_resourceResidency, &candidate);
    if (result != ANO_RESOURCE_OK
        || !render_resource_epoch_compatible(&rendererState, candidate)) {
        ano_vk_resource_residency_destroy(candidate);
        ano_resource_reload_abort(reload);
        return result == ANO_RESOURCE_OK
            ? ANO_RESOURCE_OWNER_REJECTED : result;
    }
    if (vkDeviceWaitIdle(ctx.device) != VK_SUCCESS) {
        ano_vk_resource_residency_destroy(candidate);
        ano_resource_reload_abort(reload);
        return ANO_RESOURCE_OWNER_REJECTED;
    }
    result = ano_resource_reload_commit(reload);
    if (result != ANO_RESOURCE_OK) {
        ano_vk_resource_residency_destroy(candidate);
        return result;
    }

    render_apply_resource_epoch(
        &rendererState, candidate, rendererState.frameIndex);
    AnoRenderResidency *retired = g_resourceResidency;
    g_resourceResidency = candidate;
    ano_vk_resource_residency_destroy(retired);
    return ANO_RESOURCE_OK;
}

void ano_render_unload_scene_assets(void)
{
	ano_vk_resource_residency_destroy(g_resourceResidency);
	g_resourceResidency = nullptr;
}
