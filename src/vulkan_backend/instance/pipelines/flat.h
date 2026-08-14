/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#ifndef PIPELINE_FLAT_H
#define PIPELINE_FLAT_H

#include "vulkan_backend/structs.h"

bool ano_pipeline_flat_init(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto);

// Two-sided lane (cullMode NONE) for opaque glTF doubleSided materials.
bool ano_pipeline_flat_twosided_init(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto);

// Alpha-tested cutout lane (glTF alphaMode MASK), flat_masked frag, LESS + depth write, cullMode NONE.
bool ano_pipeline_flat_masked_init(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto);

void ano_pipeline_flat_cleanup(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto);

#endif
