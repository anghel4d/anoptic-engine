/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Module-private header for the bridge/ domain.

#ifndef ANO_BRIDGE_H
#define ANO_BRIDGE_H

#include <stdint.h>

#include "vulkan_backend/structs.h"
#include "vulkan_backend/resources/resources.h"

// Drain the command ring and apply changed per-slot fields into frameIndex delta staging.
void render_apply_commands(RendererState* state, uint32_t frameIndex);
bool render_resource_epoch_compatible(
    const RendererState *state, const AnoRenderResidency *residency);
void render_apply_resource_epoch(
    RendererState *state, const AnoRenderResidency *residency,
    uint32_t frameIndex);

#endif // ANO_BRIDGE_H
