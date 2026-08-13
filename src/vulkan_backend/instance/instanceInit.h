/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef INSTANCEINIT_H
#define INSTANCEINIT_H

#include <vulkan/vulkan.h>

#include "vulkan_backend/structs.h"
#include "vulkan_backend/vertex/vertex.h"
#include "vulkan_backend/vulkanConfig.h"
#include "vulkan_backend/texture/texture.h"
#include "anoptic_time.h"

/* Function Interfaces */

VkResult createInstance(VulkanContext* ctx);

void cleanupVulkan(VulkanContext* ctx);

// NULL on failure.
GLFWwindow* initWindow(VulkanContext* ctx, Monitors* monitors);

void enumerateMonitors(Monitors* monitors);

VkResult createSurface(VkInstance instance, GLFWwindow *window, VkSurfaceKHR *surface);

bool pickPhysicalDevice(VulkanContext* ctx, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices, const char* preferredDevice);

struct DeviceCapabilities populateCapabilities(VkPhysicalDevice device);

VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice* device, VkQueue* graphicsQueue, VkQueue* computeQueue, VkQueue* transferQueue, VkQueue* presentQueue, struct QueueFamilyIndices* indices);

bool initSwapChain(VulkanContext* ctx, GLFWwindow* window, VkPresentModeKHR preferredMode, VkSwapchainKHR oldSwapChain, RendererState* state);

void recreateSwapChain(VulkanContext* ctx, GLFWwindow* window);

void cleanupSwapChain(VulkanContext* ctx, RendererState* state);

// 2D image view. VK_NULL_HANDLE on failure; callers check before storing.
VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);

// Swapchain colour views, one per presentable image. false leaves views NULL and viewCount 0.
[[nodiscard]] bool createImageViews(VulkanContext* ctx, RendererState* state);

bool createCommandPool(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkCommandPool* commandPool);

// Total out-params: false leaves *buffer VK_NULL_HANDLE and *allocation zeroed, never indeterminate.
[[nodiscard]] bool createDataBuffer(VulkanContext* ctx, GpuAllocator* allocator, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, GpuAllocation* allocation);

// createDataBuffer with gfx+compute CONCURRENT sharing when computeShared, EXCLUSIVE otherwise.
// computeShared demands two distinct queue families, i.e. the asyncLc gate.
[[nodiscard]] bool createDataBufferShared(VulkanContext* ctx, GpuAllocator* allocator, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, bool computeShared, VkBuffer* buffer, GpuAllocation* allocation);

bool createUniformBuffers(VulkanContext* ctx, RendererState* state);

// updateUniformBuffer declared in vulkan_backend/frame/frame.h

bool updateMeshTransforms(VulkanContext* ctx, RenderEntity* entity, float move);

// MSAA colour/picking-id draw targets plus per-frame resolve targets.
// false at the first refused image, view or transition; no dead handle is published.
[[nodiscard]] bool createColorResourcesChecked(VulkanContext* ctx);

// createColorResourcesChecked with the status dropped.
void createColorResources(VulkanContext* ctx);

// Depth image + view for the current swapchain.
// Status only failure channel; frame/view 0 fill first.
[[nodiscard]] bool createDepthResources(VulkanContext* ctx, RendererState* state);

// Per-view half-res R32F depth pyramids, recreated with the swapchain.
bool createHiZResources(VulkanContext* ctx, RendererState* state);
void updateHiZDescriptorSets(VulkanContext* ctx, RendererState* state);

bool createDescriptorPool(VulkanContext* ctx, RendererState* state);

bool createBindlessTextureArray(VulkanContext* ctx, RendererState* state);

bool createDescriptorSets(VulkanContext* ctx, RendererState* state);

// Re-points every set at the current scene buffers.
// Sole TransformSSBO re-point path (global/view, cull, update, scatter, lightsetup, shadowsetup binding 1).
void updateUboDescriptorSets(VulkanContext* ctx, RendererState* state);

// (Re)binds each frame's tonemap set to its HDR resolve view; rerun after a swapchain resize.
void updateTonemapDescriptorSets(VulkanContext* ctx, RendererState* state);

// Binds the clustered-forward froxel buffers (global set 10/11 + light-cull set); init-only.
void updateClusterDescriptorSets(VulkanContext* ctx, RendererState* state);

// Binds the dynamic shadow sets (shadowsetup compute set + shadow geom/sampling set 2); init-only.
// Must run after updateUboDescriptorSets, which owns re-pointing setupSet binding 1 on entity growth.
void updateShadowDescriptorSets(VulkanContext* ctx, RendererState* state);

uint32_t findMemoryType(VulkanContext* ctx, uint32_t typeFilter, VkMemoryPropertyFlags properties);

// Begins a transient one-shot command buffer.
// VK_NULL_HANDLE on failure: nothing allocated; do not record into it.
VkCommandBuffer beginSingleTimeCommands(VulkanContext* ctx);

bool hasStencilComponent(VkFormat format);

// End + submit transient CB started by beginSingleTimeCommands(); false if any step failed.
// The CB and its fence are discharged on every arm.
[[nodiscard]] bool endSingleTimeCommandsChecked(VulkanContext* ctx, VkCommandBuffer commandBuffer);

// endSingleTimeCommandsChecked with the status dropped.
void endSingleTimeCommands(VulkanContext* ctx, VkCommandBuffer commandBuffer);

// Copies data from one GPU buffer to another. False if the one-shot submit never completed.
[[nodiscard]] bool copyBuffer(VulkanContext* ctx, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

// One-shot staged upload: host data -> transient staging buffer -> dstBuffer (device-local).
// False means the copy never reached the destination; the staging buffer is discharged either way.
[[nodiscard]] bool stagingTransfer(VulkanContext* ctx, const void* data, VkBuffer dstBuffer, VkDeviceSize bufferSize);

bool createCommandBuffer(VulkanContext* ctx, RendererState* state);

bool createSyncObjects(VulkanContext* ctx, RendererState* state);

void cleanupMonitors(Monitors* monitors);

/* More Function Prototypes */

bool checkValidationLayerSupport(const char* validationLayers[], size_t validationCount);
const char** getRequiredExtensions(uint32_t* extensionsCount);
void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT* createInfo);
void setupDebugMessenger(VkInstance* instance, VkDebugUtilsMessengerEXT* debugMessenger);

/* Cross-File Helpers */

// Exposed by the instance/ split.
struct QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR *surface);
void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);

#endif
