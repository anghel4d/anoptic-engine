/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#ifndef ANOPTICENGINE_VULKAN_RESOURCE_RESIDENCY_H
#define ANOPTICENGINE_VULKAN_RESOURCE_RESIDENCY_H

#include <anoptic_render.h>

using namespace ano;
#include <anoptic_memory.h>
#include <vulkan/vulkan.h>

typedef struct AnoRenderResidency AnoRenderResidency;

AnoResourceError ano_vk_resource_residency_create(
    AnoResourceManager *manager, AnoRenderResidency **residency);
AnoResourceError ano_vk_resource_residency_prepare_from_epoch(
    const AnoResidencyEpoch *epoch, const AnoRenderResidency *previous,
    mi_heap_t *preparationHeap, AnoRenderResidency **residency);
AnoResourceError ano_vk_resource_residency_realize_step(
    AnoRenderResidency *residency, uint32_t assetBudget, bool *complete);
VkResult ano_vk_resource_residency_poll_upload(
    AnoRenderResidency *residency);
AnoResourceError ano_vk_resource_residency_wait_upload(
    AnoRenderResidency *residency);
void ano_vk_resource_residency_destroy(AnoRenderResidency *residency);
bool ano_vk_resource_scene_affected(
    const AnoRenderResidency *residency, AnoAssetId asset);
RenderResult<uint32_t> ano_vk_resource_scene_primitives(
    const AnoRenderResidency *residency, AnoAssetId asset, const mat4 root,
    AnoRenderableDesc *output, uint32_t capacity);
RenderResult<AnoRenderableDesc> ano_vk_resource_scene_primitive(
    const AnoRenderResidency *residency, AnoAssetId asset,
    uint32_t primitive, const mat4 root);
RenderResult<uint32_t> ano_vk_resource_scene_lights(
    const AnoRenderResidency *residency, AnoAssetId asset, const mat4 root,
    AnoSceneLightDesc *output, uint32_t capacity);
uint32_t ano_vk_resource_default_material(
    const AnoRenderResidency *residency);

#endif
