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
    if (context == nullptr || state == nullptr || path == nullptr
        || path[0] == '\0' || !state->frameCaptureSupported
        || !capture_format_supported(state->imageFormat)
        || state->frameCaptureRequested || state->frameCaptureSubmitted)
        return false;
    size_t pathLength = 0;
    while (pathLength < sizeof(state->frameCapturePath)
           && path[pathLength] != '\0')
        ++pathLength;
    if (pathLength == 0 || pathLength == sizeof(state->frameCapturePath))
        return false;

    const VkDeviceSize required =
        static_cast<VkDeviceSize>(state->imageExtent.width)
        * state->imageExtent.height * 4u;
    if (required == 0)
        return false;
    if (state->frameCaptureCapacity < required) {
        if (state->frameCaptureBuffer != VK_NULL_HANDLE)
            vkDestroyBuffer(context->device, state->frameCaptureBuffer,
                            nullptr);
        state->frameCaptureBuffer = VK_NULL_HANDLE;
        state->frameCaptureCapacity = 0;
        state->frameCaptureMapped = nullptr;
        if (!createDataBuffer(
                context, &gpuAllocator, required,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                    | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &state->frameCaptureBuffer,
                &state->frameCaptureAlloc))
            return false;
        state->frameCaptureCapacity = required;
        state->frameCaptureMapped = static_cast<uint8_t *>(
            state->frameCaptureAlloc.mapped);
    }
    memcpy(state->frameCapturePath, path, pathLength + 1);
    state->frameCaptureRequested = true;
    return true;
}

void ano_frame_capture_record(RendererState *state, VkCommandBuffer command,
                              uint32_t imageIndex)
{
    PerFrameResources *frame = &state->frames[state->frameIndex];
    const bool capture = state->frameCaptureRequested
        && state->frameCaptureBuffer != VK_NULL_HANDLE
        && imageIndex < state->imageCount;
    frame->frameCaptureRecorded = capture;

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = capture ? VK_ACCESS_TRANSFER_READ_BIT : 0u,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = capture ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
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
        capture ? VK_PIPELINE_STAGE_TRANSFER_BIT
                : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    if (!capture)
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
                           state->frameCaptureBuffer, 1, &copy);

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
    PerFrameResources *frame = &state->frames[frameIndex];
    if (!frame->frameCaptureRecorded)
        return;
    state->frameCaptureRequested = false;
    state->frameCaptureSubmitted = true;
    state->frameCaptureFrame = frameIndex;
    state->frameCaptureExtent = state->imageExtent;
    state->frameCaptureFormat = state->imageFormat;
}

static bool write_capture(const RendererState *state)
{
    FILE *file = fopen(state->frameCapturePath, "wb");
    if (file == nullptr)
        return false;
    const uint32_t width = state->frameCaptureExtent.width;
    const uint32_t height = state->frameCaptureExtent.height;
    bool ok = fprintf(file, "P6\n%u %u\n255\n", width, height) > 0;
    uint8_t *row = static_cast<uint8_t *>(
        mi_malloc(static_cast<size_t>(width) * 3u));
    if (row == nullptr)
        ok = false;
    const bool bgra = state->frameCaptureFormat == VK_FORMAT_B8G8R8A8_UNORM
        || state->frameCaptureFormat == VK_FORMAT_B8G8R8A8_SRGB;
    for (uint32_t y = 0; y < height && ok; ++y) {
        const uint8_t *source = state->frameCaptureMapped
            + static_cast<size_t>(y) * width * 4u;
        for (uint32_t x = 0; x < width; ++x) {
            row[x * 3u + 0u] = source[x * 4u + (bgra ? 2u : 0u)];
            row[x * 3u + 1u] = source[x * 4u + 1u];
            row[x * 3u + 2u] = source[x * 4u + (bgra ? 0u : 2u)];
        }
        ok = fwrite(row, 1, static_cast<size_t>(width) * 3u, file)
            == static_cast<size_t>(width) * 3u;
    }
    mi_free(row);
    ok = fclose(file) == 0 && ok;
    return ok;
}

void ano_frame_capture_collect(RendererState *state, uint32_t frameIndex)
{
    if (!state->frameCaptureSubmitted
        || state->frameCaptureFrame != frameIndex)
        return;
    state->frameCaptureSubmitted = false;
    if (write_capture(state))
        ano_log(ANO_INFO, "Frame captured to %s", state->frameCapturePath);
    else
        ano_log(ANO_ERROR, "Frame capture failed for %s",
                state->frameCapturePath);
}

void ano_frame_capture_destroy(VulkanContext *context, RendererState *state)
{
    if (context != nullptr && context->device != VK_NULL_HANDLE
        && state->frameCaptureBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(context->device, state->frameCaptureBuffer, nullptr);
    state->frameCaptureBuffer = VK_NULL_HANDLE;
    state->frameCaptureCapacity = 0;
    state->frameCaptureMapped = nullptr;
    state->frameCaptureRequested = false;
    state->frameCaptureSubmitted = false;
}
