#ifndef GPU_ALLOC_H
#define GPU_ALLOC_H

#include <vulkan/vulkan.h>

typedef struct GpuFreeSpan
{
    VkDeviceSize offset;
    VkDeviceSize size;
} GpuFreeSpan;

typedef struct GpuBlock
{
    VkDeviceMemory  memory;
    VkDeviceSize    size;
    VkDeviceSize    offset;     // next free offset (bump allocator)
    uint32_t        memoryType;
    void*           mapped;     // persistently mapped if HOST_VISIBLE, else NULL
    GpuFreeSpan*    freeSpans;
    uint32_t        freeCount;
    uint32_t        freeCapacity;
} GpuBlock;

typedef struct GpuAllocator
{
    VkDevice        device;
    GpuBlock*       blocks;
    uint32_t        blockCount;
    VkPhysicalDeviceMemoryProperties memProps;
} GpuAllocator;

typedef struct GpuAllocation
{
    VkDeviceMemory  memory;
    VkDeviceSize    offset;
    VkDeviceSize    size;
    void*           mapped;     // mapped pointer + offset, or NULL
} GpuAllocation;

// Alignment is applied internally. Creates a new block if needed.
GpuAllocation gpu_alloc(GpuAllocator* alloc, VkMemoryRequirements reqs,
                        VkMemoryPropertyFlags props);
void gpu_free(GpuAllocator* alloc, GpuAllocation allocation);

// Reset/teardown remain available for generation-local arenas.
void gpu_alloc_reset(GpuAllocator* alloc);   // all blocks back to offset 0
void gpu_alloc_destroy(GpuAllocator* alloc);  // free all VkDeviceMemory

extern GpuAllocator gpuAllocator;
extern GpuAllocator swapchainAllocator;
extern GpuAllocator stagingAllocator;
extern GpuAllocator textureAllocator;

#endif
