/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */


#ifndef TEXTURE_H
#define TEXTURE_H

#include <vulkan/vulkan.h>
#include <stdbool.h>

#include <anoptic_results.h>

#include "vulkan_backend/components.h"
#include "vulkan_backend/instance/instanceInit.h"

// Texture construction outcomes.
// SOURCE rejects texels; DEVICE rejects Vulkan construction; INVALID rejects the call contract.
ANO_RESULT_TYPE(AnoTextureResult,
    ANO_TEXTURE_BUILT = 0,   // *pkg complete; caller owns every handle
    ANO_TEXTURE_SOURCE,      // decode refused or outside domain
    ANO_TEXTURE_DEVICE,      // device or texture arena refused
    ANO_TEXTURE_INVALID      // no usage bits, or null destination
);

// Sample interpretations for one image. Both bits -> one mutable-format image, one allocation.
typedef enum TextureUsageBits {
	TEXTURE_USE_NONE  = 0,
	TEXTURE_USE_COLOR = 1u << 0,   // sRGB view
	TEXTURE_USE_DATA  = 1u << 1,   // UNORM view
} TextureUsageBits;
typedef uint32_t TextureUsageFlags;   // PbrFeatureFlags idiom, components.h:157

// One constructed texture.
// srgbView non-null iff COLOR built; unormView iff DATA; BUILT carries >= 1 view.
typedef struct TexturePackage {
	VkImage       image;
	GpuAllocation alloc;
	VkImageView   srgbView;    // iff COLOR built
	VkImageView   unormView;   // iff DATA built
	uint32_t      mipLevels, width, height;
} TexturePackage;

// in: BUILT package. out: registry teardown record over the same handles.
// inv: sole converter; field orders cannot drift.
static inline TextureData ano_texture_record(const TexturePackage* pkg)
{
	return (TextureData){
		.textureImage = pkg->image,
		.textureImageAlloc = pkg->alloc,
		.srgbView = pkg->srgbView,
		.unormView = pkg->unormView,
	};
}

// createTextureImageFromStaging records into a non-null borrowed command
// buffer. transitionImageLayout also accepts VK_NULL_HANDLE and then performs
// its own synchronous transient submission.

// Creates an image and records an upload from a caller-owned staging slice.
AnoTextureResult createTextureImageFromStaging(
    VulkanContext* ctx, VkCommandBuffer cmd, TexturePackage* pkg,
    VkBuffer staging, VkDeviceSize stagingOffset, uint32_t width,
    uint32_t height, TextureUsageFlags usage);

// Synchronous convenience for immutable RGBA8 pixels.
AnoTextureResult createTextureImageFromPixels(
    VulkanContext* ctx, TexturePackage* pkg, const unsigned char* pixels,
    uint32_t width, uint32_t height, TextureUsageFlags usage);
void destroyTexturePackage(VulkanContext* ctx, TexturePackage* pkg);

bool createTextureSampler(VulkanContext* ctx, RendererState* state);


// Absent bindless slot. Same sentinel MaterialData / flat.frag already read as "no texture".
#define ANO_BINDLESS_NONE 0xFFFFFFFFu

// out: slot for (view, sampler), or ANO_BINDLESS_NONE if full or either handle absent.
// inv: granted slot is never ANO_BINDLESS_NONE (refusal outside the index domain).
uint32_t bindless_register_texture(VulkanContext* ctx, BindlessTextureArray* bta, VkImageView view, VkSampler sampler);
void bindless_release_texture(BindlessTextureArray* bta, uint32_t slot);

bool createImage(VulkanContext* ctx, GpuAllocator* allocator, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
				VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, GpuAllocation* imageAlloc, bool flag16);
// createImage with a queue-family share list, >= 2 distinct families selects CONCURRENT sharing.
// viewFormats mirrors it: >= 2 listed formats selects MUTABLE_FORMAT with that explicit list.
[[nodiscard]] bool createImageShared(VulkanContext* ctx, GpuAllocator* allocator, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
				VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, GpuAllocation* imageAlloc, bool flag16,
				const uint32_t* shareFamilies, uint32_t shareFamilyCount,
				const VkFormat* viewFormats, uint32_t viewFormatCount);
// Transition image layout. Borrowed-cmd contract above.
bool transitionImageLayout(VulkanContext* ctx, VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);

#endif
