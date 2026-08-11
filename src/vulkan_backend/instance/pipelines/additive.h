/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. Compiler/library incompleteness disqualifies the toolchain; it does not constrain the architecture. */

#ifndef PIPELINE_ADDITIVE_H
#define PIPELINE_ADDITIVE_H

#include <stdbool.h>
#include "vulkan_backend/structs.h"

// Initialize the additive pipeline: one variant, ONE/ONE commutative blend (order-independent),
// depth-tested against opaque depth but depth-write OFF. Used for emissive glows / FX.
bool ano_pipeline_additive_init(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto);

#endif
