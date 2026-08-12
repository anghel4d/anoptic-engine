/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#ifndef ANOPTICENGINE_VULKAN_RESOURCE_RESIDENCY_H
#define ANOPTICENGINE_VULKAN_RESOURCE_RESIDENCY_H

#include <anoptic_render.h>

typedef struct AnoRenderResidency AnoRenderResidency;

typedef enum AnoRenderResidencyUploadStatus {
    ANO_RENDER_RESIDENCY_UPLOAD_PENDING,
    ANO_RENDER_RESIDENCY_UPLOAD_READY,
    ANO_RENDER_RESIDENCY_UPLOAD_REJECTED,
} AnoRenderResidencyUploadStatus;

AnoResourceError ano_vk_resource_residency_create(
    AnoResourceManager *manager, AnoRenderResidency **residency);
AnoResourceError ano_vk_resource_residency_prepare_from_epoch(
    const AnoResidencyEpoch *epoch, const AnoRenderResidency *previous,
    AnoRenderResidency **residency);
AnoResourceError ano_vk_resource_residency_submit(
    AnoRenderResidency *residency);
AnoRenderResidencyUploadStatus ano_vk_resource_residency_upload_status(
    const AnoRenderResidency *residency);
AnoResourceError ano_vk_resource_residency_finish_upload(
    AnoRenderResidency *residency);
AnoResourceError ano_vk_resource_residency_wait_upload(
    AnoRenderResidency *residency);
void ano_vk_resource_residency_destroy(AnoRenderResidency *residency);
uint32_t ano_vk_resource_scene_primitives(
    const AnoRenderResidency *residency, AnoAssetId asset, const mat4 root,
    AnoRenderableDesc *output, uint32_t capacity);
bool ano_vk_resource_scene_primitive(
    const AnoRenderResidency *residency, AnoAssetId asset,
    uint32_t primitive, const mat4 root, AnoRenderableDesc *output);
uint32_t ano_vk_resource_scene_lights(
    const AnoRenderResidency *residency, AnoAssetId asset, const mat4 root,
    AnoSceneLightDesc *output, uint32_t capacity);
uint32_t ano_vk_resource_default_material(
    const AnoRenderResidency *residency);

#endif
