#include "gpu_alloc.h"
#include <anoptic_memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <anoptic_log.h>

#define DEFAULT_BLOCK_SIZE (256 * 1024 * 1024) // 256 MiB

static bool reserve_free_spans(GpuBlock* block, uint32_t required)
{
    if (required <= block->freeCapacity) return true;
    uint32_t capacity = block->freeCapacity == 0 ? 8u : block->freeCapacity * 2u;
    while (capacity < required) capacity *= 2u;
    GpuFreeSpan* grown = mi_reallocn_tp(
        GpuFreeSpan, block->freeSpans, capacity);
    if (!grown) return false;
    block->freeSpans = grown;
    block->freeCapacity = capacity;
    return true;
}

static GpuAllocation allocate_free_span(GpuBlock* block, VkMemoryRequirements reqs)
{
    for (uint32_t i = 0; i < block->freeCount; ++i) {
        const VkDeviceSize begin = block->freeSpans[i].offset;
        const VkDeviceSize end = begin + block->freeSpans[i].size;
        const VkDeviceSize aligned = (begin + reqs.alignment - 1u)
            & ~(reqs.alignment - 1u);
        if (aligned < begin || reqs.size > end - aligned) continue;
        const VkDeviceSize prefix = aligned - begin;
        const VkDeviceSize suffix = end - aligned - reqs.size;
        if (prefix != 0 && suffix != 0) {
            if (!reserve_free_spans(block, block->freeCount + 1u)) continue;
            block->freeSpans[i].size = prefix;
            block->freeSpans[block->freeCount++] = {
                .offset = aligned + reqs.size, .size = suffix};
        } else if (prefix != 0) {
            block->freeSpans[i].size = prefix;
        } else if (suffix != 0) {
            block->freeSpans[i] = {.offset = aligned + reqs.size, .size = suffix};
        } else {
            block->freeSpans[i] = block->freeSpans[--block->freeCount];
        }
        return {
            .memory = block->memory,
            .offset = aligned,
            .size = reqs.size,
            .mapped = block->mapped
                ? static_cast<void*>(static_cast<char*>(block->mapped) + aligned)
                : NULL,
        };
    }
    return {};
}

static uint32_t findMemoryType(VkPhysicalDeviceMemoryProperties memProps, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    // 1u: the domain runs to VK_MAX_MEMORY_TYPES (32), and signed 1 << 31 is UB.
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    ano_log(ANO_ERROR, "Failed to find suitable memory type!");
    return UINT32_MAX;
}

GpuAllocation gpu_alloc(GpuAllocator* alloc, VkMemoryRequirements reqs, VkMemoryPropertyFlags props)
{
    uint32_t memoryType = findMemoryType(alloc->memProps, reqs.memoryTypeBits, props);
    if (memoryType == UINT32_MAX) {
        GpuAllocation empty = {0};
        return empty;
    }
    
    // Find an existing block with enough space and matching type
    for (uint32_t i = 0; i < alloc->blockCount; i++)
    {
        GpuBlock* block = &alloc->blocks[i];
        if (block->memoryType == memoryType)
        {
            GpuAllocation recycled = allocate_free_span(block, reqs);
            if (recycled.memory != VK_NULL_HANDLE) return recycled;
            // Align offset
            VkDeviceSize alignedOffset = (block->offset + reqs.alignment - 1) & ~(reqs.alignment - 1);
            if (alignedOffset + reqs.size <= block->size)
            {
                block->offset = alignedOffset + reqs.size;
                
                GpuAllocation allocation = {
                    .memory = block->memory,
                    .offset = alignedOffset,
                    .size = reqs.size,
                    .mapped = block->mapped ? (void*)((char*)block->mapped + alignedOffset) : NULL
                };
                return allocation;
            }
        }
    }

    // Need a new block
    VkDeviceSize blockSize = reqs.size > DEFAULT_BLOCK_SIZE ? reqs.size : DEFAULT_BLOCK_SIZE;
    
    // Expand blocks array
    GpuBlock* temp = mi_reallocn_tp(
        GpuBlock, alloc->blocks, alloc->blockCount + 1u);
    if (!temp) {
        ano_log(ANO_ERROR, "Host OOM: Failed to allocate memory for GPU block tracking array!");
        return (GpuAllocation){0};
    }
    alloc->blocks = temp;
    GpuBlock* newBlock = &alloc->blocks[alloc->blockCount];
    alloc->blockCount++;

    newBlock->size = blockSize;
    newBlock->offset = 0;
    newBlock->memoryType = memoryType;
    newBlock->mapped = NULL;
    newBlock->freeSpans = NULL;
    newBlock->freeCount = 0;
    newBlock->freeCapacity = 0;

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = blockSize,
        .memoryTypeIndex = memoryType
    };

    if (vkAllocateMemory(alloc->device, &allocInfo, NULL, &newBlock->memory) != VK_SUCCESS)
    {
        ano_log(ANO_ERROR, "Failed to allocate GPU block memory!");
        // Revert block count expansion
        alloc->blockCount--;
        GpuAllocation empty = {0};
        return empty;
    }

    if (alloc->memProps.memoryTypes[memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        if (vkMapMemory(
                alloc->device, newBlock->memory, 0, blockSize, 0,
                &newBlock->mapped) != VK_SUCCESS) {
            vkFreeMemory(alloc->device, newBlock->memory, NULL);
            alloc->blockCount--;
            return (GpuAllocation){0};
        }
    }

    VkDeviceSize alignedOffset = (newBlock->offset + reqs.alignment - 1) & ~(reqs.alignment - 1);
    newBlock->offset = alignedOffset + reqs.size;

    GpuAllocation allocation = {
        .memory = newBlock->memory,
        .offset = alignedOffset,
        .size = reqs.size,
        .mapped = newBlock->mapped ? (void*)((char*)newBlock->mapped + alignedOffset) : NULL
    };
    return allocation;
}

void gpu_free(GpuAllocator* alloc, GpuAllocation allocation)
{
    if (allocation.memory == VK_NULL_HANDLE || allocation.size == 0) return;
    for (uint32_t i = 0; i < alloc->blockCount; ++i) {
        GpuBlock* block = &alloc->blocks[i];
        if (block->memory != allocation.memory) continue;
        VkDeviceSize begin = allocation.offset;
        VkDeviceSize end = begin + allocation.size;
        for (uint32_t j = 0; j < block->freeCount;) {
            const VkDeviceSize otherBegin = block->freeSpans[j].offset;
            const VkDeviceSize otherEnd = otherBegin + block->freeSpans[j].size;
            if (otherEnd < begin || end < otherBegin) { ++j; continue; }
            if (otherBegin < begin) begin = otherBegin;
            if (otherEnd > end) end = otherEnd;
            block->freeSpans[j] = block->freeSpans[--block->freeCount];
        }
        if (!reserve_free_spans(block, block->freeCount + 1u)) return;
        block->freeSpans[block->freeCount++] = {
            .offset = begin, .size = end - begin};
        return;
    }
}

void gpu_alloc_reset(GpuAllocator* alloc)
{
    for (uint32_t i = 0; i < alloc->blockCount; i++)
    {
        alloc->blocks[i].offset = 0;
        alloc->blocks[i].freeCount = 0;
    }
}

void gpu_alloc_destroy(GpuAllocator* alloc)
{
    for (uint32_t i = 0; i < alloc->blockCount; i++)
    {
        if (alloc->blocks[i].mapped)
        {
            vkUnmapMemory(alloc->device, alloc->blocks[i].memory);
        }
        vkFreeMemory(alloc->device, alloc->blocks[i].memory, NULL);
        free(alloc->blocks[i].freeSpans);
    }
    free(alloc->blocks);
    alloc->blocks = NULL;
    alloc->blockCount = 0;
}
