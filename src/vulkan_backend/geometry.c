/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include "vulkan_backend/geometry.h"
#include <anoptic_memory_typed.h>
#include <string.h>
#include <stdio.h>
#include <anoptic_log.h>

static void geometry_pool_release_mesh_registry(GeometryPool* pool)
{
    free(pool->meshes);
    free(pool->references);
    free(pool->meshGpuPublication.pendingFrames);
    free(pool->meshGpuPublication.dirtyRows);
    pool->meshes = NULL;
    pool->references = NULL;
    pool->meshCount = 0;
    pool->meshCapacity = 0;
    pool->meshGpuPublication = {};
}

static void geometry_pool_mark_gpu_dirty(GeometryPool* pool, uint32_t meshIndex)
{
    ano::assume(meshIndex < ANO_MAX_MESHES);
    MeshGpuPublication* publication = &pool->meshGpuPublication;
    if (publication->pendingFrames[meshIndex] == 0u) {
        ano::assume(publication->dirtyCount < ANO_MAX_MESHES);
        publication->dirtyRows[publication->dirtyCount++] = meshIndex;
    }
    publication->pendingFrames[meshIndex] = publication->allFrames;
}

bool ano_vk_init_geometry_pool(GeometryPool* pool, GpuAllocator* alloc, VkDevice device,
                               uint32_t graphicsFamily, uint32_t transferFamily,
                               uint32_t framesInFlight)
{
    if (framesInFlight == 0u || framesInFlight > 8u) return false;
    pool->meshCount = 0;
    pool->meshCapacity = 100;
    pool->meshes = ano::allocate_zero<MeshRegion>(pool->meshCapacity);
    pool->references = ano::allocate_zero<uint32_t>(pool->meshCapacity);
    pool->meshGpuPublication = {};
    pool->meshGpuPublication.pendingFrames =
        ano::allocate_zero<uint8_t>(ANO_MAX_MESHES);
    pool->meshGpuPublication.dirtyRows =
        ano::allocate<uint32_t>(ANO_MAX_MESHES);
    pool->meshGpuPublication.allFrames =
        static_cast<uint8_t>((1u << framesInFlight) - 1u);
    if (!pool->meshes || !pool->references || !pool->meshGpuPublication.pendingFrames
        || !pool->meshGpuPublication.dirtyRows) {
        geometry_pool_release_mesh_registry(pool);
        return false;
    }
    pool->vertexWriteOffset = 0;
    pool->indexWriteOffset = 0;
    
    pool->vertexFreeBlocks = NULL;
    pool->vertexFreeCount = 0;
    pool->vertexFreeCapacity = 0;
    
    pool->indexFreeBlocks = NULL;
    pool->indexFreeCount = 0;
    pool->indexFreeCapacity = 0;
    
    VkDeviceSize vertexPoolSize = 64 * 1024 * 1024; // 64 MB
    VkDeviceSize indexPoolSize = 32 * 1024 * 1024;  // two residency generations may overlap during publication
    
    pool->vertexCapacity = vertexPoolSize;
    pool->indexCapacity = indexPoolSize;

    uint32_t queueFamilyIndices[] = {graphicsFamily, transferFamily};
    bool concurrent = (graphicsFamily != transferFamily);

    VkBufferCreateInfo vInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = vertexPoolSize,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = concurrent ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = concurrent ? 2u : 0u,
        .pQueueFamilyIndices = concurrent ? queueFamilyIndices : NULL
    };
    if (vkCreateBuffer(device, &vInfo, NULL, &pool->vertexBuffer) != VK_SUCCESS) {
        pool->vertexBuffer = VK_NULL_HANDLE;
        geometry_pool_release_mesh_registry(pool);
        return false;
    }
    VkMemoryRequirements vReqs;
    vkGetBufferMemoryRequirements(device, pool->vertexBuffer, &vReqs);
    pool->vertexAlloc = gpu_alloc(alloc, vReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (pool->vertexAlloc.memory == VK_NULL_HANDLE) {
        vkDestroyBuffer(device, pool->vertexBuffer, NULL);
        pool->vertexBuffer = VK_NULL_HANDLE; // atomic rollback: null handle (cleanup guards on it) + free meshes
        geometry_pool_release_mesh_registry(pool);
        return false;
    }
    if (vkBindBufferMemory(device, pool->vertexBuffer, pool->vertexAlloc.memory,
                           pool->vertexAlloc.offset) != VK_SUCCESS) {
        vkDestroyBuffer(device, pool->vertexBuffer, NULL);
        pool->vertexBuffer = VK_NULL_HANDLE;
        geometry_pool_release_mesh_registry(pool);
        return false;
    }

    VkBufferCreateInfo iInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = indexPoolSize,
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = concurrent ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = concurrent ? 2u : 0u,
        .pQueueFamilyIndices = concurrent ? queueFamilyIndices : NULL
    };
    if (vkCreateBuffer(device, &iInfo, NULL, &pool->indexBuffer) != VK_SUCCESS) {
        vkDestroyBuffer(device, pool->vertexBuffer, NULL);
        pool->vertexBuffer = VK_NULL_HANDLE;
        pool->indexBuffer = VK_NULL_HANDLE;
        geometry_pool_release_mesh_registry(pool);
        return false;
    }
    VkMemoryRequirements iReqs;
    vkGetBufferMemoryRequirements(device, pool->indexBuffer, &iReqs);
    pool->indexAlloc = gpu_alloc(alloc, iReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (pool->indexAlloc.memory == VK_NULL_HANDLE) {
        vkDestroyBuffer(device, pool->vertexBuffer, NULL);
        vkDestroyBuffer(device, pool->indexBuffer, NULL);
        pool->vertexBuffer = VK_NULL_HANDLE; // atomic rollback: null both handles + free meshes
        pool->indexBuffer = VK_NULL_HANDLE;
        geometry_pool_release_mesh_registry(pool);
        return false;
    }
    if (vkBindBufferMemory(device, pool->indexBuffer, pool->indexAlloc.memory,
                           pool->indexAlloc.offset) != VK_SUCCESS) {
        vkDestroyBuffer(device, pool->vertexBuffer, NULL);
        vkDestroyBuffer(device, pool->indexBuffer, NULL);
        pool->vertexBuffer = VK_NULL_HANDLE;
        pool->indexBuffer = VK_NULL_HANDLE;
        geometry_pool_release_mesh_registry(pool);
        return false;
    }

    return true;
}

void ano_vk_cleanup_geometry_pool(GeometryPool* pool, VkDevice device)
{
    if (pool->vertexBuffer) vkDestroyBuffer(device, pool->vertexBuffer, NULL);
    if (pool->indexBuffer) vkDestroyBuffer(device, pool->indexBuffer, NULL);
    geometry_pool_release_mesh_registry(pool);
    
    if (pool->vertexFreeBlocks) free(pool->vertexFreeBlocks);
    pool->vertexFreeBlocks = NULL;
    
    if (pool->indexFreeBlocks) free(pool->indexFreeBlocks);
    pool->indexFreeBlocks = NULL;
    
}

static bool geometry_prepare_level(mi_heap_t* heap,
                                   AnoPreparedGeometryLevel* level)
{
    const size_t maximum = ano_build_meshlets_bound(level->indexCount, 64, 126);
    if (maximum == 0) return false;
    ano_meshlet_t* meshlets = static_cast<ano_meshlet_t*>(mi_heap_malloc(
        heap, maximum * sizeof(ano_meshlet_t)));
    uint32_t* vertices = static_cast<uint32_t*>(mi_heap_malloc(
        heap, maximum * 64u * sizeof(uint32_t)));
    uint8_t* triangles = static_cast<uint8_t*>(mi_heap_malloc(
        heap, maximum * 126u * 3u));
    ano_meshlet_bounds_gpu_t* bounds =
        static_cast<ano_meshlet_bounds_gpu_t*>(mi_heap_malloc(
            heap, maximum * sizeof(ano_meshlet_bounds_gpu_t)));
    if (!meshlets || !vertices || !triangles || !bounds) return false;
    const size_t meshletCount = ano_build_meshlets(
        meshlets, vertices, triangles, level->indices, level->indexCount,
        64, 126);
    if (meshletCount == 0) return false;
    const size_t uniqueVertexCount =
        meshlets[meshletCount - 1].vertex_offset
        + meshlets[meshletCount - 1].vertex_count;
    const size_t localIndexCount =
        meshlets[meshletCount - 1].triangle_offset
        + meshlets[meshletCount - 1].triangle_count * 3u;
    for (size_t i = 0; i < meshletCount; ++i)
        bounds[i] = ano_compute_meshlet_bounds(
            vertices + meshlets[i].vertex_offset,
            triangles + meshlets[i].triangle_offset,
            meshlets[i].triangle_count, (const float*)level->vertices,
            level->vertexCount, sizeof(Vertex));

    const size_t meshletBytes = meshletCount * sizeof(ano_meshlet_t);
    const size_t uniqueVertexBytes = uniqueVertexCount * sizeof(uint32_t);
    const size_t triangleBytes = (localIndexCount + 3u) & ~size_t{3};
    const size_t boundsBytes = meshletCount * sizeof(ano_meshlet_bounds_gpu_t);
    const size_t classicIndexBytes =
        (size_t)level->indexCount * sizeof(uint32_t);
    const size_t metadataBytes = meshletBytes + uniqueVertexBytes
        + triangleBytes + boundsBytes + classicIndexBytes;
    const size_t vertexBytes = (size_t)level->vertexCount * sizeof(Vertex);
    if (metadataBytes > UINT32_MAX || vertexBytes > UINT32_MAX
        || metadataBytes > SIZE_MAX - vertexBytes)
        return false;
    level->upload = static_cast<uint8_t*>(mi_heap_malloc(
        heap, vertexBytes + metadataBytes));
    if (!level->upload) return false;
    memcpy(level->upload, level->vertices, vertexBytes);
    uint8_t* metadata = level->upload + vertexBytes;
    memcpy(metadata, meshlets, meshletBytes);
    memcpy(metadata + meshletBytes, vertices, uniqueVertexBytes);
    memcpy(metadata + meshletBytes + uniqueVertexBytes,
           triangles, localIndexCount);
    if (triangleBytes > localIndexCount)
        memset(metadata + meshletBytes + uniqueVertexBytes + localIndexCount,
               0, triangleBytes - localIndexCount);
    memcpy(metadata + meshletBytes + uniqueVertexBytes + triangleBytes,
           bounds, boundsBytes);
    memcpy(metadata + meshletBytes + uniqueVertexBytes + triangleBytes
               + boundsBytes,
           level->indices, classicIndexBytes);

    level->vertexBytes = (uint32_t)vertexBytes;
    level->metadataBytes = (uint32_t)metadataBytes;
    level->region = {
        .vertexCount = level->vertexCount,
        .indexCount = (uint32_t)metadataBytes,
        .meshletCount = (uint32_t)meshletCount,
        .uniqueVerticesOffset = (uint32_t)meshletBytes,
        .trianglesOffset = (uint32_t)(meshletBytes + uniqueVertexBytes),
        .boundsOffset = (uint32_t)(meshletBytes + uniqueVertexBytes
                                   + triangleBytes),
        .classicIndexOffset = (uint32_t)(meshletBytes + uniqueVertexBytes
                                         + triangleBytes + boundsBytes),
        .classicIndexCount = level->indexCount,
        .lodCount = 1,
    };
    Vector3 minimum = level->vertices[0].position;
    Vector3 maximumBounds = minimum;
    for (uint32_t i = 1; i < level->vertexCount; ++i)
        for (uint32_t axis = 0; axis < 3; ++axis) {
            const float value = level->vertices[i].position.v[axis];
            if (value < minimum.v[axis]) minimum.v[axis] = value;
            if (value > maximumBounds.v[axis]) maximumBounds.v[axis] = value;
        }
    float maximumDistance = 0.0f;
    for (uint32_t axis = 0; axis < 3; ++axis)
        level->region.boundingSphereCenter[axis] =
            (minimum.v[axis] + maximumBounds.v[axis]) * 0.5f;
    for (uint32_t i = 0; i < level->vertexCount; ++i) {
        float distance = 0.0f;
        for (uint32_t axis = 0; axis < 3; ++axis) {
            const float delta = level->vertices[i].position.v[axis]
                - level->region.boundingSphereCenter[axis];
            distance += delta * delta;
        }
        if (distance > maximumDistance) maximumDistance = distance;
    }
    level->region.boundingSphereRadius = sqrtf(maximumDistance);
    return true;
}

// Emit worker-prepared bytes into a caller-reserved meshes[] slot.
static bool geometry_pool_emit_level(GeometryPool* pool, GpuAllocator* alloc,
                                     VkDevice device,
                                     const AnoPreparedGeometryLevel* level,
                                     uint32_t meshIndex,
                                     VkCommandBuffer command,
                                     VkBuffer* retainedStaging)
{
    const VkDeviceSize totalSize =
        (VkDeviceSize)level->vertexBytes + level->metadataBytes;
    VkBufferCreateInfo stagingInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = totalSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VkBuffer staging = VK_NULL_HANDLE;
    if (vkCreateBuffer(device, &stagingInfo, NULL, &staging)
        != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, staging, &memReqs);

    GpuAllocation stagingAlloc = gpu_alloc(alloc, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (stagingAlloc.memory == VK_NULL_HANDLE
        || vkBindBufferMemory(device, staging, stagingAlloc.memory,
                              stagingAlloc.offset) != VK_SUCCESS) {
        vkDestroyBuffer(device, staging, NULL);
        return false;
    }
    memcpy(stagingAlloc.mapped, level->upload, (size_t)totalSize);

    // Plan allocations
    uint32_t finalVertexOffset = (uint32_t)-1;
    int vertexFreeIdx = -1;
    for (uint32_t i = 0; i < pool->vertexFreeCount; i++) {
        if (pool->vertexFreeBlocks[i].size >= level->vertexBytes) {
            finalVertexOffset = pool->vertexFreeBlocks[i].offset;
            vertexFreeIdx = (int)i;
            break;
        }
    }
    if (finalVertexOffset == (uint32_t)-1) {
        if ((VkDeviceSize)pool->vertexWriteOffset + level->vertexBytes > pool->vertexCapacity) {
            ano_log(ANO_ERROR, "Error: Geometry mega-buffer vertex pool exhausted! Requested %llu, Capacity %llu",
                   (unsigned long long)(pool->vertexWriteOffset + level->vertexBytes), (unsigned long long)pool->vertexCapacity);
            vkDestroyBuffer(device, staging, NULL);
            return false; // pool exhausted
        }
        finalVertexOffset = pool->vertexWriteOffset;
    }

    uint32_t finalIndexOffset = (uint32_t)-1;
    int indexFreeIdx = -1;
    for (uint32_t i = 0; i < pool->indexFreeCount; i++) {
        if (pool->indexFreeBlocks[i].size >= level->metadataBytes) {
            finalIndexOffset = pool->indexFreeBlocks[i].offset;
            indexFreeIdx = (int)i;
            break;
        }
    }
    if (finalIndexOffset == (uint32_t)-1) {
        if ((VkDeviceSize)pool->indexWriteOffset + level->metadataBytes > pool->indexCapacity) {
            ano_log(ANO_ERROR, "Error: Geometry mega-buffer metadata pool exhausted! Requested %llu, Capacity %llu",
                   (unsigned long long)(pool->indexWriteOffset + level->metadataBytes), (unsigned long long)pool->indexCapacity);
            vkDestroyBuffer(device, staging, NULL);
            return false; // pool exhausted
        }
        finalIndexOffset = pool->indexWriteOffset;
    }

    // Commit reservations
    if (vertexFreeIdx >= 0) {
        pool->vertexFreeBlocks[vertexFreeIdx].offset += level->vertexBytes;
        pool->vertexFreeBlocks[vertexFreeIdx].size -= level->vertexBytes;
        if (pool->vertexFreeBlocks[vertexFreeIdx].size == 0) {
            pool->vertexFreeBlocks[vertexFreeIdx] = pool->vertexFreeBlocks[--pool->vertexFreeCount];
        }
    } else {
        pool->vertexWriteOffset += level->vertexBytes;
    }
    if (indexFreeIdx >= 0) {
        pool->indexFreeBlocks[indexFreeIdx].offset += level->metadataBytes;
        pool->indexFreeBlocks[indexFreeIdx].size -= level->metadataBytes;
        if (pool->indexFreeBlocks[indexFreeIdx].size == 0) {
            pool->indexFreeBlocks[indexFreeIdx] = pool->indexFreeBlocks[--pool->indexFreeCount];
        }
    } else {
        pool->indexWriteOffset += level->metadataBytes;
    }

    VkBufferCopy copyRegion = {
        .srcOffset = 0,
        .dstOffset = finalVertexOffset,
        .size = level->vertexBytes
    };
    vkCmdCopyBuffer(command, staging, pool->vertexBuffer, 1, &copyRegion);

    VkBufferCopy indexCopyRegion = {
        .srcOffset = level->vertexBytes,
        .dstOffset = finalIndexOffset,
        .size = level->metadataBytes
    };
    vkCmdCopyBuffer(command, staging, pool->indexBuffer, 1,
                    &indexCopyRegion);
    *retainedStaging = staging;

    MeshRegion* mesh = &pool->meshes[meshIndex];
    *mesh = level->region;
    mesh->vertexOffset = finalVertexOffset;
    mesh->indexOffset = finalIndexOffset;
    mesh->meshletOffset += finalIndexOffset;
    mesh->uniqueVerticesOffset += finalIndexOffset;
    mesh->trianglesOffset += finalIndexOffset;
    mesh->boundsOffset += finalIndexOffset;
    mesh->classicIndexOffset += finalIndexOffset;

    return true;
}

// Default LOD: ratios 1, 1/2, 1/4, ...; 5% extent error. lodCount clamped to [1, ANO_MAX_LOD].
AnoLodConfig ano_lod_config_default(uint32_t lodCount)
{
    AnoLodConfig c;
    memset(&c, 0, sizeof c);
    if (lodCount < 1u) lodCount = 1u;
    if (lodCount > ANO_MAX_LOD) lodCount = ANO_MAX_LOD;
    c.lodCount = lodCount;
    c.targetError = 0.05f;
    c.edgeLenFactor = ANO_SIMPLIFY_EDGE_FACTOR_DEFAULT;  // guard the in-plane courtyard-bridge case
    float ratio = 1.0f;
    for (uint32_t i = 0; i < ANO_MAX_LOD; ++i) {
        c.ratios[i] = ratio;  // level 0 == 1.0 (full mesh)
        ratio *= 0.5f;
    }
    return c;
}

// Compact referenced verts into outVerts; rewrite indices in place. Returns compacted count, or 0 on OOM.
static uint32_t geometry_compact_level(mi_heap_t* heap,
                                       const Vertex* srcVerts, uint32_t srcVertexCount,
                                       uint32_t* indices, uint32_t indexCount, Vertex* outVerts)
{
    uint32_t* remap = static_cast<uint32_t*>(mi_heap_malloc(
        heap, (size_t)srcVertexCount * sizeof(uint32_t)));
    if (!remap) return 0;
    memset(remap, 0xFF, (size_t)srcVertexCount * sizeof(uint32_t)); // 0xFFFFFFFF == unassigned

    uint32_t next = 0;
    for (uint32_t i = 0; i < indexCount; ++i) {
        uint32_t old = indices[i];
        if (remap[old] == 0xFFFFFFFFu) {
            remap[old] = next;
            outVerts[next] = srcVerts[old];
            next++;
        }
        indices[i] = remap[old];
    }
    return next;
}

bool geometry_prepare_chain(
    mi_heap_t* heap, const Vertex* vertices, uint32_t vertexCount,
    const uint32_t* indices, uint32_t indexCount,
    const AnoLodConfig* config, AnoPreparedGeometry* prepared)
{
    if (!heap || !vertices || !indices || !prepared || vertexCount == 0
        || indexCount < 3)
        return false;
    *prepared = {};
    uint32_t want = config ? config->lodCount : 1u;
    if (want < 1u) want = 1u;
    if (want > ANO_MAX_LOD) want = ANO_MAX_LOD;
    const float targetError = config ? config->targetError : 0.0f;

    uint32_t* simplified = want > 1u
        ? static_cast<uint32_t*>(mi_heap_malloc(
            heap, (size_t)indexCount * sizeof(uint32_t)))
        : NULL;
    Vertex* compacted = want > 1u
        ? static_cast<Vertex*>(mi_heap_malloc(
            heap, (size_t)vertexCount * sizeof(Vertex)))
        : NULL;
    if (want > 1u && (!simplified || !compacted)) want = 1u;

    for (uint32_t level = 0; level < want; ++level) {
        const Vertex* levelVertices = vertices;
        uint32_t levelVertexCount = vertexCount;
        const uint32_t* levelIndices = indices;
        uint32_t levelIndexCount = indexCount;
        if (level > 0 && simplified && compacted) {
            float ratio = config->ratios[level];
            if (ratio <= 0.0f || ratio > 1.0f) ratio = 1.0f;
            uint32_t target = (uint32_t)((float)indexCount * ratio);
            target -= target % 3u;
            if (target < 3u) target = 3u;
            const size_t got = ano_simplify_ex(
                simplified, indices, indexCount, (const float*)vertices,
                vertexCount, sizeof(Vertex), target, targetError,
                config->edgeLenFactor, NULL);
            if (got < 3u) break;
            ano_optimize_vertex_cache(simplified, simplified, got, vertexCount);
            levelIndexCount = (uint32_t)got;
            levelVertexCount = geometry_compact_level(
                heap, vertices, vertexCount, simplified, levelIndexCount,
                compacted);
            if (levelVertexCount == 0) break;
            levelVertices = compacted;
            levelIndices = simplified;
        }
        AnoPreparedGeometryLevel& output = prepared->levels[prepared->lodCount];
        output.vertices = static_cast<Vertex*>(mi_heap_malloc(
            heap, (size_t)levelVertexCount * sizeof(Vertex)));
        output.indices = static_cast<uint32_t*>(mi_heap_malloc(
            heap, (size_t)levelIndexCount * sizeof(uint32_t)));
        if (!output.vertices || !output.indices) return false;
        memcpy(output.vertices, levelVertices,
               (size_t)levelVertexCount * sizeof(Vertex));
        memcpy(output.indices, levelIndices,
               (size_t)levelIndexCount * sizeof(uint32_t));
        output.vertexCount = levelVertexCount;
        output.indexCount = levelIndexCount;
        if (!geometry_prepare_level(heap, &output)) return false;
        ++prepared->lodCount;
    }
    return prepared->lodCount != 0;
}

static bool geometry_pool_grow_meshes(GeometryPool* pool, uint32_t required)
{
    if (required <= pool->meshCapacity) return true;
    uint32_t capacity = pool->meshCapacity == 0 ? 100u : pool->meshCapacity;
    while (capacity < required) capacity *= 2u;
    if (capacity > ANO_MAX_MESHES) capacity = ANO_MAX_MESHES;
    if (capacity < required) return false;
    MeshRegion* meshes = ano::allocate_zero<MeshRegion>(capacity);
    uint32_t* references = ano::allocate_zero<uint32_t>(capacity);
    if (!meshes || !references) {
        free(references);
        free(meshes);
        return false;
    }
    if (pool->meshCapacity != 0) {
        memcpy(meshes, pool->meshes,
               (size_t)pool->meshCapacity * sizeof(MeshRegion));
        memcpy(references, pool->references,
               (size_t)pool->meshCapacity * sizeof(uint32_t));
    }
    free(pool->meshes);
    free(pool->references);
    pool->meshes = meshes;
    pool->references = references;
    pool->meshCapacity = capacity;
    return true;
}

static uint32_t geometry_pool_reserve_meshes(GeometryPool* pool,
                                              uint32_t count,
                                              bool* appended)
{
    for (uint32_t base = 1; base + count <= pool->meshCount; ++base) {
        uint32_t available = 0;
        while (available < count
               && pool->meshes[base + available].vertexCount == 0
               && pool->references[base + available] == 0)
            ++available;
        if (available == count) {
            *appended = false;
            return base;
        }
        base += available;
    }
    if (count > ANO_MAX_MESHES - pool->meshCount
        || !geometry_pool_grow_meshes(pool, pool->meshCount + count))
        return ANO_MESH_NONE;
    const uint32_t base = pool->meshCount;
    pool->meshCount += count;
    *appended = true;
    return base;
}

uint32_t geometry_pool_record_prepared_chain(
    GeometryPool* pool, GpuAllocator* alloc, VkDevice device,
    const AnoPreparedGeometry* prepared, VkCommandBuffer command,
    VkBuffer outStaging[ANO_MAX_LOD], uint32_t* out_lodCount)
{
    if (!prepared || prepared->lodCount == 0 || !outStaging || !out_lodCount
        || command == VK_NULL_HANDLE)
        return ANO_MESH_NONE;
    *out_lodCount = 0;
    bool appended = false;
    const uint32_t base = geometry_pool_reserve_meshes(
        pool, prepared->lodCount, &appended);
    if (base == ANO_MESH_NONE) return base;
    uint32_t produced = 0;
    for (; produced < prepared->lodCount; ++produced) {
        const AnoPreparedGeometryLevel& level = prepared->levels[produced];
        VkBuffer staging = VK_NULL_HANDLE;
        if (!geometry_pool_emit_level(
                pool, alloc, device, &level, base + produced, command,
                &staging))
            break;
        outStaging[produced] = staging;
    }
    if (appended) pool->meshCount = base + produced;
    if (produced != 0) {
        pool->meshes[base].lodCount = produced;
        for (uint32_t level = 0; level < produced; ++level)
            geometry_pool_mark_gpu_dirty(pool, base + level);
    }
    *out_lodCount = produced;
    return produced ? base : ANO_MESH_NONE;
}

// Upload contiguous LOD chain. Level 0 = full mesh; level i = ano_simplify(source, ratios[i]) then compact+emit.
// Cull bound from level 0 only. Truncates on stall/exhaust; releases unused reserved slots.
// in: source mesh, config (NULL => one full level)
// out: base / *out_lodCount; total failure => ANO_MESH_NONE + 0 (never the fallback slot)
uint32_t geometry_pool_record_chain(
    GeometryPool* pool, GpuAllocator* alloc, VkDevice device,
    const Vertex* vertices, uint32_t vertexCount,
    const uint32_t* indices, uint32_t indexCount,
    const AnoLodConfig* config, VkCommandBuffer command,
    VkBuffer outStaging[ANO_MAX_LOD], uint32_t* out_lodCount)
{
    mi_heap_t* heap = mi_heap_new();
    if (!heap) return ANO_MESH_NONE;
    AnoPreparedGeometry prepared = {};
    const bool ready = geometry_prepare_chain(
        heap, vertices, vertexCount, indices, indexCount, config, &prepared);
    const uint32_t base = ready
        ? geometry_pool_record_prepared_chain(
            pool, alloc, device, &prepared, command, outStaging, out_lodCount)
        : ANO_MESH_NONE;
    mi_heap_destroy(heap);
    return base;
}

static void geometry_pool_free_span(GeoFreeBlock** spans, uint32_t* count,
                                    uint32_t* capacity, uint32_t offset,
                                    uint32_t size)
{
    uint32_t begin = offset;
    uint32_t end = offset + size;
    for (uint32_t i = 0; i < *count;) {
        const uint32_t otherBegin = (*spans)[i].offset;
        const uint32_t otherEnd = otherBegin + (*spans)[i].size;
        if (otherEnd < begin || end < otherBegin) { ++i; continue; }
        if (otherBegin < begin) begin = otherBegin;
        if (otherEnd > end) end = otherEnd;
        (*spans)[i] = (*spans)[--*count];
    }
    if (*count == *capacity) {
        const uint32_t grown = *capacity == 0 ? 32u : *capacity * 2u;
        GeoFreeBlock* blocks = ano::reallocate(*spans, grown);
        if (!blocks) return;
        *spans = blocks;
        *capacity = grown;
    }
    (*spans)[(*count)++] = {.offset = begin, .size = end - begin};
}

void geometry_pool_free(GeometryPool* pool, uint32_t meshIndex)
{
    if (meshIndex >= pool->meshCount || meshIndex == 0) return; // Don't free fallback or out of bounds

    MeshRegion* mesh = &pool->meshes[meshIndex];
    if (mesh->vertexCount == 0) return; // Already freed

    geometry_pool_free_span(
        &pool->vertexFreeBlocks, &pool->vertexFreeCount,
        &pool->vertexFreeCapacity, mesh->vertexOffset,
        static_cast<uint32_t>(mesh->vertexCount * sizeof(Vertex)));
    if (mesh->indexCount != 0)
        geometry_pool_free_span(
            &pool->indexFreeBlocks, &pool->indexFreeCount,
            &pool->indexFreeCapacity, mesh->indexOffset, mesh->indexCount);

    // Clear mesh
    memset(mesh, 0, sizeof(MeshRegion));
    geometry_pool_mark_gpu_dirty(pool, meshIndex);
}

// Free a contiguous LOD chain.
void geometry_pool_free_chain(GeometryPool* pool, uint32_t lodBase, uint32_t lodCount)
{
    for (uint32_t i = 0; i < lodCount; ++i)
        geometry_pool_free(pool, lodBase + i);
}

void geometry_pool_retain_chain(GeometryPool* pool, uint32_t lodBase)
{
    if (lodBase < pool->meshCount && pool->meshes[lodBase].vertexCount != 0)
        ++pool->references[lodBase];
}

void geometry_pool_release_chain(GeometryPool* pool, uint32_t lodBase)
{
    if (lodBase >= pool->meshCount || pool->references[lodBase] == 0)
        return;
    if (--pool->references[lodBase] == 0)
        geometry_pool_free_chain(pool, lodBase, pool->meshes[lodBase].lodCount);
}
