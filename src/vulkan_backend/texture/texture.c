/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include "vulkan_backend/texture/texture.h"

#include <anoptic_log.h>

#include "vulkan_backend/vulkanMaster.h"

#include <string.h>

extern GpuAllocator textureAllocator;
extern GpuAllocator stagingAllocator;

uint32_t bindless_register_texture(
    VulkanContext* context, BindlessTextureArray* textures,
    VkImageView view, VkSampler sampler)
{
    if (view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
        ano_log(ANO_ERROR,
                "Bindless registration refused an absent view or sampler.");
        return ANO_BINDLESS_NONE;
    }
    if (textures->textureCount >= textures->maxTextures
        && textures->freeCount == 0) {
        ano_log(ANO_ERROR, "Bindless texture array is full.");
        return ANO_BINDLESS_NONE;
    }
    const uint32_t index = textures->freeCount != 0
        ? textures->freeSlots[--textures->freeCount]
        : textures->textureCount++;
    const VkDescriptorImageInfo image = {
        .sampler = sampler,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    const VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = textures->set,
        .dstBinding = 0,
        .dstArrayElement = index,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image,
    };
    vkUpdateDescriptorSets(context->device, 1, &write, 0, nullptr);
    return index;
}

void bindless_release_texture(BindlessTextureArray* textures, uint32_t slot)
{
    if (slot == ANO_BINDLESS_NONE || slot >= textures->textureCount
        || textures->freeCount >= textures->maxTextures)
        return;
    textures->freeSlots[textures->freeCount++] = slot;
}

bool transitionImageLayout(
    VulkanContext* context, VkCommandBuffer borrowed, VkImage image,
    VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout,
    uint32_t mipLevels)
{
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = mipLevels,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    VkPipelineStageFlags sourceStage = 0;
    VkPipelineStageFlags destinationStage = 0;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
        && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
               && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
               && newLayout
                    == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
            | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
               && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
            | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
               && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        ano_log(ANO_ERROR, "Unsupported image layout transition.");
        return false;
    }
    if (format == VK_FORMAT_D32_SFLOAT || hasStencilComponent(format)) {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencilComponent(format))
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    VkCommandBuffer command = borrowed;
    if (command == VK_NULL_HANDLE)
        command = beginSingleTimeCommands(context);
    if (command == VK_NULL_HANDLE) return false;
    vkCmdPipelineBarrier(command, sourceStage, destinationStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    return borrowed != VK_NULL_HANDLE
        || endSingleTimeCommandsChecked(context, command);
}

static void copy_buffer_to_image(
    VkCommandBuffer command, VkBuffer buffer, VkDeviceSize offset,
    VkImage image, uint32_t width, uint32_t height)
{
    const VkBufferImageCopy region = {
        .bufferOffset = offset,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1},
    };
    vkCmdCopyBufferToImage(
        command, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region);
}

bool createImageShared(
    VulkanContext* context, GpuAllocator* allocator, uint32_t width,
    uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits samples,
    VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties, VkImage* image,
    GpuAllocation* allocation, bool flag16,
    const uint32_t* shareFamilies, uint32_t shareFamilyCount,
    const VkFormat* viewFormats, uint32_t viewFormatCount)
{
    (void)flag16;
    *image = VK_NULL_HANDLE;
    *allocation = {};
    VkImageFormatListCreateInfo formatList = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
    };
    VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .samples = samples,
        .tiling = tiling,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (shareFamilies && shareFamilyCount >= 2) {
        info.sharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = shareFamilyCount;
        info.pQueueFamilyIndices = shareFamilies;
    }
    if (viewFormats && viewFormatCount >= 2) {
        formatList.viewFormatCount = viewFormatCount;
        formatList.pViewFormats = viewFormats;
        info.pNext = &formatList;
        info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    }
    if (vkCreateImage(context->device, &info, nullptr, image) != VK_SUCCESS)
        return false;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(context->device, *image, &requirements);
    *allocation = gpu_alloc(allocator, requirements, properties);
    if (allocation->memory != VK_NULL_HANDLE
        && vkBindImageMemory(context->device, *image, allocation->memory,
                             allocation->offset) == VK_SUCCESS)
        return true;
    vkDestroyImage(context->device, *image, nullptr);
    gpu_free(allocator, *allocation);
    *image = VK_NULL_HANDLE;
    *allocation = {};
    return false;
}

bool createImage(
    VulkanContext* context, GpuAllocator* allocator, uint32_t width,
    uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits samples,
    VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties, VkImage* image,
    GpuAllocation* allocation, bool flag16)
{
    return createImageShared(
        context, allocator, width, height, mipLevels, samples, format,
        tiling, usage, properties, image, allocation, flag16,
        nullptr, 0, nullptr, 0);
}

static constexpr VkFormat textureViewFormats[] = {
    VK_FORMAT_R8G8B8A8_SRGB,
    VK_FORMAT_R8G8B8A8_UNORM,
};

static constexpr bool texture_usage_valid(TextureUsageFlags usage)
{
    constexpr TextureUsageFlags known =
        TEXTURE_USE_COLOR | TEXTURE_USE_DATA;
    return (usage & known) != 0 && (usage & ~known) == 0;
}

static constexpr VkFormat texture_base_format(TextureUsageFlags usage)
{
    return (usage & TEXTURE_USE_COLOR)
        ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
}

static constexpr uint32_t texture_view_format_count(
    TextureUsageFlags usage)
{
    return (usage & TEXTURE_USE_COLOR) && (usage & TEXTURE_USE_DATA)
        ? 2u : 0u;
}

static bool create_texture_view(
    VulkanContext* context, VkImage image, VkImageView* view,
    VkFormat format)
{
    *view = createImageView(
        context->device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    return *view != VK_NULL_HANDLE;
}

void destroyTexturePackage(VulkanContext* context, TexturePackage* package)
{
    if (!package) return;
    vkDestroyImageView(context->device, package->unormView, nullptr);
    vkDestroyImageView(context->device, package->srgbView, nullptr);
    vkDestroyImage(context->device, package->image, nullptr);
    gpu_free(&textureAllocator, package->alloc);
    *package = {};
}

AnoTextureResult createTextureImageFromStaging(
    VulkanContext* context, VkCommandBuffer command, TexturePackage* package,
    VkBuffer staging, VkDeviceSize stagingOffset, uint32_t width,
    uint32_t height, TextureUsageFlags usage)
{
    if (!package)
        return ANO_TEXTURE_INVALID;
    *package = {};
    if (command == VK_NULL_HANDLE || staging == VK_NULL_HANDLE
        || (stagingOffset & 3u) != 0 || !texture_usage_valid(usage))
        return ANO_TEXTURE_INVALID;
    if (width == 0 || height == 0)
        return ANO_TEXTURE_SOURCE;

    const VkFormat format = texture_base_format(usage);
    if (!createImageShared(
            context, &textureAllocator, width, height, 1,
            VK_SAMPLE_COUNT_1_BIT, format, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &package->image, &package->alloc, false, nullptr, 0,
            textureViewFormats, texture_view_format_count(usage)))
        return ANO_TEXTURE_DEVICE;
    const bool views = (!(usage & TEXTURE_USE_COLOR)
                        || create_texture_view(
                            context, package->image, &package->srgbView,
                            VK_FORMAT_R8G8B8A8_SRGB))
        && (!(usage & TEXTURE_USE_DATA)
            || create_texture_view(
                context, package->image, &package->unormView,
                VK_FORMAT_R8G8B8A8_UNORM));
    if (!views
        || !transitionImageLayout(
            context, command, package->image, format,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1)) {
        destroyTexturePackage(context, package);
        return ANO_TEXTURE_DEVICE;
    }
    copy_buffer_to_image(
        command, staging, stagingOffset, package->image, width, height);
    if (!transitionImageLayout(
            context, command, package->image, format,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)) {
        destroyTexturePackage(context, package);
        return ANO_TEXTURE_DEVICE;
    }
    package->mipLevels = 1;
    package->width = width;
    package->height = height;
    return ANO_TEXTURE_BUILT;
}

AnoTextureResult createTextureImageFromPixels(
    VulkanContext* context, TexturePackage* package,
    const unsigned char* pixels, uint32_t width, uint32_t height,
    TextureUsageFlags usage)
{
    if (!package)
        return ANO_TEXTURE_INVALID;
    *package = {};
    uint64_t pixelCount = 0;
    if (!pixels || width == 0 || height == 0
        || width > UINT64_MAX / height
        || (pixelCount = static_cast<uint64_t>(width) * height)
            > UINT64_MAX / 4u)
        return ANO_TEXTURE_SOURCE;
    if (!texture_usage_valid(usage))
        return ANO_TEXTURE_INVALID;

    const VkDeviceSize bytes = pixelCount * 4u;
    VkBuffer staging = VK_NULL_HANDLE;
    GpuAllocation allocation = {};
    if (!createDataBuffer(
            context, &stagingAllocator, bytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &staging, &allocation)
        || !allocation.mapped) {
        vkDestroyBuffer(context->device, staging, nullptr);
        gpu_free(&stagingAllocator, allocation);
        return ANO_TEXTURE_DEVICE;
    }
    memcpy(allocation.mapped, pixels, static_cast<size_t>(bytes));

    VkCommandBuffer command = beginSingleTimeCommands(context);
    AnoTextureResult result = command == VK_NULL_HANDLE
        ? ANO_TEXTURE_DEVICE
        : createTextureImageFromStaging(
            context, command, package, staging, 0, width, height, usage);
    if (command != VK_NULL_HANDLE) {
        if (result == ANO_TEXTURE_BUILT) {
            if (!endSingleTimeCommandsChecked(context, command)) {
                destroyTexturePackage(context, package);
                result = ANO_TEXTURE_DEVICE;
            }
        } else {
            vkFreeCommandBuffers(
                context->device, rendererState.commandPool, 1, &command);
        }
    }
    vkDestroyBuffer(context->device, staging, nullptr);
    gpu_free(&stagingAllocator, allocation);
    return result;
}

bool createTextureSampler(VulkanContext* context, RendererState* state)
{
    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(context->physicalDevice, &properties);
    const VkSamplerCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = 20.0f,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };
    if (vkCreateSampler(
            context->device, &info, nullptr,
            &state->textureSampler) == VK_SUCCESS)
        return true;
    ano_log(ANO_FATAL, "Failed to create texture sampler.");
    return false;
}
