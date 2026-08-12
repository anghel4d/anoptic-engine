/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include <anoptic_log.h>
#include <anoptic_memory.h>

#include <stdio.h>
#include <string.h>

#include "vulkan_backend/backend.h"
#include "vulkan_backend/frame/frame.h"
#include "vulkan_backend/instance/instanceInit.h"

static bool capture_format_supported(VkFormat format)
{
    return format == VK_FORMAT_B8G8R8A8_UNORM
        || format == VK_FORMAT_B8G8R8A8_SRGB
        || format == VK_FORMAT_R8G8B8A8_UNORM
        || format == VK_FORMAT_R8G8B8A8_SRGB;
}

bool ano_frame_capture_request(VulkanContext *context, RendererState *state,
                               const char *path)
{
    if (context == nullptr || state == nullptr || path == nullptr)
        return false;
    FrameCapture *capture = &state->frameCapture;
    if (path[0] == '\0' || !capture->supported
        || !capture_format_supported(state->imageFormat)
        || capture->status != FRAME_CAPTURE_IDLE)
        return false;
    const size_t pathLength = strlen(path);
    if (pathLength >= sizeof(capture->path))
        return false;

    const VkDeviceSize required =
        static_cast<VkDeviceSize>(state->imageExtent.width)
        * state->imageExtent.height * 4u;
    if (required == 0)
        return false;
    if (capture->capacity < required) {
        vkDestroyBuffer(context->device, capture->buffer, nullptr);
        capture->buffer = VK_NULL_HANDLE;
        capture->capacity = 0;
        if (!createDataBuffer(
                context, &gpuAllocator, required,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                    | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &capture->buffer, &capture->allocation))
            return false;
        capture->capacity = required;
    }
    memcpy(capture->path, path, pathLength + 1);
    capture->status = FRAME_CAPTURE_REQUESTED;
    return true;
}

void ano_frame_capture_record(RendererState *state, VkCommandBuffer command,
                              uint32_t imageIndex)
{
    const FrameCapture *capture = &state->frameCapture;
    const bool requested = capture->status == FRAME_CAPTURE_REQUESTED
        && capture->buffer != VK_NULL_HANDLE
        && imageIndex < state->imageCount;

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = requested ? VK_ACCESS_TRANSFER_READ_BIT : 0u,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = requested ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                               : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = state->images[imageIndex],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(
        command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        requested ? VK_PIPELINE_STAGE_TRANSFER_BIT
                  : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    if (!requested)
        return;

    const VkBufferImageCopy copy = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {
            state->imageExtent.width, state->imageExtent.height, 1,
        },
    };
    vkCmdCopyImageToBuffer(command, state->images[imageIndex],
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           capture->buffer, 1, &copy);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = 0;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
}

void ano_frame_capture_submitted(RendererState *state, uint32_t frameIndex)
{
    FrameCapture *capture = &state->frameCapture;
    if (capture->status != FRAME_CAPTURE_REQUESTED)
        return;
    capture->status = FRAME_CAPTURE_SUBMITTED;
    capture->frame = frameIndex;
}

static bool write_capture(const RendererState *state)
{
    const FrameCapture *capture = &state->frameCapture;
    FILE *file = fopen(capture->path, "wb");
    if (file == nullptr)
        return false;
    const uint32_t width = state->imageExtent.width;
    const uint32_t height = state->imageExtent.height;
    const size_t pixels = static_cast<size_t>(width) * height;
    uint8_t *rgb = pixels > SIZE_MAX / 3u ? nullptr
        : static_cast<uint8_t *>(mi_malloc(pixels * 3u));
    const bool bgra = state->imageFormat == VK_FORMAT_B8G8R8A8_UNORM
        || state->imageFormat == VK_FORMAT_B8G8R8A8_SRGB;
    const uint8_t *mapped = static_cast<const uint8_t *>(
        capture->allocation.mapped);
    for (size_t i = 0; rgb != nullptr && i < pixels; ++i) {
        rgb[i * 3u] = mapped[i * 4u + (bgra ? 2u : 0u)];
        rgb[i * 3u + 1u] = mapped[i * 4u + 1u];
        rgb[i * 3u + 2u] = mapped[i * 4u + (bgra ? 0u : 2u)];
    }
    bool ok = rgb != nullptr
        && fprintf(file, "P6\n%u %u\n255\n", width, height) > 0
        && fwrite(rgb, 3u, pixels, file) == pixels;
    mi_free(rgb);
    ok = fclose(file) == 0 && ok;
    return ok;
}

void ano_frame_capture_collect(RendererState *state, uint32_t frameIndex)
{
    FrameCapture *capture = &state->frameCapture;
    if (capture->status != FRAME_CAPTURE_SUBMITTED
        || capture->frame != frameIndex)
        return;
    capture->status = FRAME_CAPTURE_IDLE;
    if (write_capture(state))
        ano_log(ANO_INFO, "Frame captured to %s", capture->path);
    else
        ano_log(ANO_ERROR, "Frame capture failed for %s", capture->path);
}

void ano_frame_capture_destroy(VulkanContext *context, RendererState *state)
{
    FrameCapture *capture = &state->frameCapture;
    if (context != nullptr && context->device != VK_NULL_HANDLE
        && capture->buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(context->device, capture->buffer, nullptr);
    *capture = {};
}
