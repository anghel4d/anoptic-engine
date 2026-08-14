/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef VULKANMASTER_H
#define VULKANMASTER_H


#include <vulkan/vulkan.h>

#include <anoptic_render.h> // lifecycle + command protocol; defined in this translation unit

using namespace ano;

#include "vulkan_backend/structs.h"

#include "vulkan_backend/instance/instanceInit.h"

#include "vulkan_backend/instance/pipeline.h"

#include "vulkan_backend/vulkanConfig.h"

#include "vulkan_backend/texture/texture.h"

/* Function Interfaces */

extern RendererState rendererState;

extern uint32_t g_ValidationErrors;

/* Render World / Logic Producer */

// Render+GLFW on the main thread; logic talks through the lock-free bridge.

void deferred_delete_resource(RendererState* state, DeletionResourceType type, uint32_t handle);
void flush_deletion_queue(VulkanContext* ctx, RendererState* state, uint32_t frameIndex);

extern PFN_vkCmdDrawMeshTasksEXT pfnVkCmdDrawMeshTasksEXT;
extern PFN_vkCmdDrawMeshTasksIndirectEXT pfnVkCmdDrawMeshTasksIndirectEXT;
extern PFN_vkCmdDrawMeshTasksIndirectCountEXT pfnVkCmdDrawMeshTasksIndirectCountEXT;

#endif
