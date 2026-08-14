/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include "vulkan_backend/geometry.h"
#include <anoptic_memory_typed.h>

using namespace ano;
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

static bool geometry_pool_create_buffer(
    GpuAllocator* allocator, VkDevice device,
    const VkBufferCreateInfo* info, VkBuffer* buffer,
    GpuAllocation* allocation)
{
    *buffer = VK_NULL_HANDLE;
    *allocation = {};
    if (vkCreateBuffer(device, info, NULL, buffer) != VK_SUCCESS)
        return false;
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, *buffer, &requirements);
    *allocation = gpu_alloc(
        allocator, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocation->memory != VK_NULL_HANDLE
        && vkBindBufferMemory(device, *buffer, allocation->memory,
                              allocation->offset) == VK_SUCCESS)
        return true;
    vkDestroyBuffer(device, *buffer, NULL);
    gpu_free(allocator, *allocation);
    *buffer = VK_NULL_HANDLE;
    *allocation = {};
    return false;
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

    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = vertexPoolSize,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = concurrent ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = concurrent ? 2u : 0u,
        .pQueueFamilyIndices = concurrent ? queueFamilyIndices : NULL
    };
    if (!geometry_pool_create_buffer(
            alloc, device, &info, &pool->vertexBuffer,
            &pool->vertexAlloc)) {
        geometry_pool_release_mesh_registry(pool);
        return false;
    }
    info.size = indexPoolSize;
    info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (geometry_pool_create_buffer(
            alloc, device, &info, &pool->indexBuffer,
            &pool->indexAlloc))
        return true;

    vkDestroyBuffer(device, pool->vertexBuffer, NULL);
    gpu_free(alloc, pool->vertexAlloc);
    pool->vertexBuffer = VK_NULL_HANDLE;
    pool->vertexAlloc = {};
    geometry_pool_release_mesh_registry(pool);
    return false;
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

struct GeometryScratch final {
    ano_meshlet_t* meshlets;
    uint32_t* meshletVertices;
    uint8_t* triangles;
    ano_meshlet_bounds_gpu_t* bounds;
};

struct GeometryUploadLayout final {
    size_t vertexBytes;
    size_t meshletBytes;
    size_t uniqueVertexBytes;
    size_t triangleBytes;
    size_t boundsBytes;
    size_t classicIndexBytes;
    size_t metadataBytes;
    size_t totalBytes;
    bool valid;
};

constexpr bool geometry_size_product(size_t left, size_t right, size_t* value)
{
    if (left != 0 && right > SIZE_MAX / left) return false;
    *value = left * right;
    return true;
}

constexpr bool geometry_size_sum(size_t left, size_t right, size_t* value)
{
    if (right > SIZE_MAX - left) return false;
    *value = left + right;
    return true;
}

constexpr GeometryUploadLayout geometry_upload_layout(
    size_t vertexCount, size_t indexCount, size_t meshletCount,
    size_t uniqueVertexCount, size_t localIndexCount)
{
    GeometryUploadLayout result = {};
    if (!geometry_size_product(vertexCount, sizeof(Vertex), &result.vertexBytes)
        || !geometry_size_product(meshletCount, sizeof(ano_meshlet_t),
                                  &result.meshletBytes)
        || !geometry_size_product(uniqueVertexCount, sizeof(uint32_t),
                                  &result.uniqueVertexBytes)
        || localIndexCount > SIZE_MAX - 3u
        || !geometry_size_product(meshletCount,
                                  sizeof(ano_meshlet_bounds_gpu_t),
                                  &result.boundsBytes)
        || !geometry_size_product(indexCount, sizeof(uint32_t),
                                  &result.classicIndexBytes))
        return result;
    result.triangleBytes = (localIndexCount + 3u) & ~size_t{3};
    size_t metadata = 0;
    if (!geometry_size_sum(result.meshletBytes, result.uniqueVertexBytes,
                           &metadata)
        || !geometry_size_sum(metadata, result.triangleBytes, &metadata)
        || !geometry_size_sum(metadata, result.boundsBytes, &metadata)
        || !geometry_size_sum(metadata, result.classicIndexBytes, &metadata)
        || !geometry_size_sum(result.vertexBytes, metadata,
                              &result.totalBytes)
        || metadata > UINT32_MAX || result.vertexBytes > UINT32_MAX)
        return result;
    result.metadataBytes = metadata;
    result.valid = true;
    return result;
}

static_assert(geometry_upload_layout(1, 3, 1, 3, 3).valid);

static bool geometry_prepare_level(
    mi_heap_t* heap, AnoPreparedGeometryLevel* level,
    const Vertex* vertices, uint32_t vertexCount,
    const uint32_t* indices, uint32_t indexCount,
    const GeometryScratch& scratch)
{
    const size_t meshletCount = ano_build_meshlets(
        scratch.meshlets, scratch.meshletVertices, scratch.triangles,
        indices, indexCount, 64, 126);
    if (meshletCount == 0) return false;
    const size_t uniqueVertexCount =
        scratch.meshlets[meshletCount - 1].vertex_offset
        + scratch.meshlets[meshletCount - 1].vertex_count;
    const size_t localIndexCount =
        scratch.meshlets[meshletCount - 1].triangle_offset
        + scratch.meshlets[meshletCount - 1].triangle_count * 3u;
    for (size_t i = 0; i < meshletCount; ++i)
        scratch.bounds[i] = ano_compute_meshlet_bounds(
            scratch.meshletVertices + scratch.meshlets[i].vertex_offset,
            scratch.triangles + scratch.meshlets[i].triangle_offset,
            scratch.meshlets[i].triangle_count,
            reinterpret_cast<const float*>(vertices), vertexCount,
            sizeof(Vertex));

    const GeometryUploadLayout layout = geometry_upload_layout(
        vertexCount, indexCount, meshletCount, uniqueVertexCount,
        localIndexCount);
    if (!layout.valid) return false;
    level->upload = static_cast<uint8_t*>(
        mi_heap_malloc(heap, layout.totalBytes));
    if (!level->upload) return false;
    memcpy(level->upload, vertices, layout.vertexBytes);
    uint8_t* metadata = level->upload + layout.vertexBytes;
    memcpy(metadata, scratch.meshlets, layout.meshletBytes);
    memcpy(metadata + layout.meshletBytes, scratch.meshletVertices,
           layout.uniqueVertexBytes);
    memcpy(metadata + layout.meshletBytes + layout.uniqueVertexBytes,
           scratch.triangles, localIndexCount);
    if (layout.triangleBytes > localIndexCount)
        memset(metadata + layout.meshletBytes + layout.uniqueVertexBytes
                   + localIndexCount,
               0, layout.triangleBytes - localIndexCount);
    memcpy(metadata + layout.meshletBytes + layout.uniqueVertexBytes
               + layout.triangleBytes,
           scratch.bounds, layout.boundsBytes);
    memcpy(metadata + layout.meshletBytes + layout.uniqueVertexBytes
               + layout.triangleBytes + layout.boundsBytes,
           indices, layout.classicIndexBytes);

    level->vertexBytes = static_cast<uint32_t>(layout.vertexBytes);
    level->metadataBytes = static_cast<uint32_t>(layout.metadataBytes);
    level->region = {
        .vertexCount = vertexCount,
        .metadataBytes = static_cast<uint32_t>(layout.metadataBytes),
        .meshletCount = static_cast<uint32_t>(meshletCount),
        .uniqueVerticesOffset = static_cast<uint32_t>(layout.meshletBytes),
        .trianglesOffset = static_cast<uint32_t>(
            layout.meshletBytes + layout.uniqueVertexBytes),
        .boundsOffset = static_cast<uint32_t>(
            layout.meshletBytes + layout.uniqueVertexBytes
            + layout.triangleBytes),
        .classicIndexOffset = static_cast<uint32_t>(
            layout.meshletBytes + layout.uniqueVertexBytes
            + layout.triangleBytes + layout.boundsBytes),
        .classicIndexCount = indexCount,
        .lodCount = 1,
    };
    Vector3 minimum = vertices[0].position;
    Vector3 maximum = minimum;
    for (uint32_t i = 1; i < vertexCount; ++i)
        for (uint32_t axis = 0; axis < 3; ++axis) {
            const float value = vertices[i].position.v[axis];
            if (value < minimum.v[axis]) minimum.v[axis] = value;
            if (value > maximum.v[axis]) maximum.v[axis] = value;
        }
    for (uint32_t axis = 0; axis < 3; ++axis)
        level->region.boundingSphereCenter[axis] =
            (minimum.v[axis] + maximum.v[axis]) * 0.5f;
    float maximumDistance = 0.0f;
    for (uint32_t i = 0; i < vertexCount; ++i) {
        float distance = 0.0f;
        for (uint32_t axis = 0; axis < 3; ++axis) {
            const float delta = vertices[i].position.v[axis]
                - level->region.boundingSphereCenter[axis];
            distance += delta * delta;
        }
        if (distance > maximumDistance) maximumDistance = distance;
    }
    level->region.boundingSphereRadius = sqrtf(maximumDistance);
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

// Compact referenced vertices into outVerts and rewrite indices in place.
static uint32_t geometry_compact_level(
    const Vertex* srcVerts, uint32_t srcVertexCount,
    uint32_t* indices, uint32_t indexCount, Vertex* outVerts,
    uint32_t* remap)
{
    memset(remap, 0xFF, (size_t)srcVertexCount * sizeof(uint32_t)); // 0xFFFFFFFF == unassigned

    uint32_t next = 0;
    for (uint32_t i = 0; i < indexCount; ++i) {
        uint32_t old = indices[i];
        if (old >= srcVertexCount) return 0;
        if (remap[old] == 0xFFFFFFFFu) {
            remap[old] = next;
            outVerts[next] = srcVerts[old];
            next++;
        }
        indices[i] = remap[old];
    }
    return next;
}

constexpr VkDeviceSize geometry_upload_align(VkDeviceSize offset)
{
    return (offset + 3u) & ~VkDeviceSize{3};
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

    const size_t meshletCapacity = ano_build_meshlets_bound(
        indexCount, 64, 126);
    if (meshletCapacity == 0
        || meshletCapacity > SIZE_MAX / sizeof(ano_meshlet_t)
        || meshletCapacity > SIZE_MAX / (64u * sizeof(uint32_t))
        || meshletCapacity > SIZE_MAX / (126u * 3u)
        || meshletCapacity > SIZE_MAX / sizeof(ano_meshlet_bounds_gpu_t))
        return false;
    const GeometryScratch scratch = {
        .meshlets = mi_heap_mallocn_tp(
            ano_meshlet_t, heap, meshletCapacity),
        .meshletVertices = static_cast<uint32_t*>(mi_heap_mallocn(
            heap, meshletCapacity, 64u * sizeof(uint32_t))),
        .triangles = static_cast<uint8_t*>(mi_heap_mallocn(
            heap, meshletCapacity, 126u * 3u)),
        .bounds = mi_heap_mallocn_tp(
            ano_meshlet_bounds_gpu_t, heap, meshletCapacity),
    };
    if (!scratch.meshlets || !scratch.meshletVertices
        || !scratch.triangles || !scratch.bounds)
        return false;

    uint32_t* simplified = want > 1u
        ? mi_heap_mallocn_tp(uint32_t, heap, (size_t)indexCount)
        : NULL;
    Vertex* compacted = want > 1u
        ? mi_heap_mallocn_tp(Vertex, heap, (size_t)vertexCount)
        : NULL;
    uint32_t* remap = want > 1u
        ? mi_heap_mallocn_tp(uint32_t, heap, (size_t)vertexCount)
        : NULL;
    if (want > 1u && (!simplified || !compacted || !remap)) want = 1u;

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
                vertices, vertexCount, simplified, levelIndexCount,
                compacted, remap);
            if (levelVertexCount == 0) break;
            levelVertices = compacted;
            levelIndices = simplified;
        }
        AnoPreparedGeometryLevel& output = prepared->levels[prepared->lodCount];
        if (!geometry_prepare_level(
                heap, &output, levelVertices, levelVertexCount,
                levelIndices, levelIndexCount, scratch))
            return false;
        const VkDeviceSize offset = geometry_upload_align(
            prepared->uploadBytes);
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(
            output.vertexBytes) + output.metadataBytes;
        if (offset < prepared->uploadBytes || bytes > UINT64_MAX - offset)
            return false;
        output.uploadOffset = offset;
        prepared->uploadBytes = offset + bytes;
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

static void geometry_pool_free_span(GeoFreeBlock** spans, uint32_t* count,
                                    uint32_t* capacity, uint32_t offset,
                                    uint32_t size);

static bool geometry_pool_reserve_free_capacity(
    GeoFreeBlock** blocks, uint32_t count, uint32_t* capacity,
    uint32_t additional)
{
    if (additional > UINT32_MAX - count) return false;
    const uint32_t required = count + additional;
    if (required <= *capacity) return true;
    uint32_t grown = *capacity == 0 ? 32u : *capacity;
    while (grown < required) {
        if (grown > UINT32_MAX / 2u) return false;
        grown *= 2u;
    }
    GeoFreeBlock* replacement = ano::reallocate(*blocks, grown);
    if (!replacement) return false;
    *blocks = replacement;
    *capacity = grown;
    return true;
}

static bool geometry_pool_take_span(
    GeoFreeBlock* blocks, uint32_t* freeCount, uint32_t* writeOffset,
    VkDeviceSize capacity, uint32_t bytes, uint32_t* offset)
{
    for (uint32_t i = 0; i < *freeCount; ++i) {
        if (blocks[i].size < bytes) continue;
        *offset = blocks[i].offset;
        blocks[i].offset += bytes;
        blocks[i].size -= bytes;
        if (blocks[i].size == 0)
            blocks[i] = blocks[--*freeCount];
        return true;
    }
    if (static_cast<VkDeviceSize>(*writeOffset) + bytes > capacity)
        return false;
    *offset = *writeOffset;
    *writeOffset += bytes;
    return true;
}

uint32_t geometry_pool_record_prepared_chain(
    GeometryPool* pool, const AnoPreparedGeometry* prepared,
    VkCommandBuffer command, VkBuffer staging, void* stagingMapped,
    VkDeviceSize stagingBase)
{
    if (!pool || !prepared || prepared->lodCount == 0
        || prepared->lodCount > ANO_MAX_LOD
        || command == VK_NULL_HANDLE || staging == VK_NULL_HANDLE
        || !stagingMapped || (stagingBase & 3u) != 0
        || prepared->uploadBytes > UINT64_MAX - stagingBase
        || stagingBase + prepared->uploadBytes > SIZE_MAX)
        return ANO_MESH_NONE;
    bool appended = false;
    const uint32_t base = geometry_pool_reserve_meshes(
        pool, prepared->lodCount, &appended);
    if (base == ANO_MESH_NONE) return base;
    if (!geometry_pool_reserve_free_capacity(
            &pool->vertexFreeBlocks, pool->vertexFreeCount,
            &pool->vertexFreeCapacity, prepared->lodCount)
        || !geometry_pool_reserve_free_capacity(
            &pool->indexFreeBlocks, pool->indexFreeCount,
            &pool->indexFreeCapacity, prepared->lodCount)) {
        if (appended) pool->meshCount = base;
        return ANO_MESH_NONE;
    }

    uint32_t vertexOffsets[ANO_MAX_LOD] = {};
    uint32_t metadataOffsets[ANO_MAX_LOD] = {};
    uint32_t reserved = 0;
    for (; reserved < prepared->lodCount; ++reserved) {
        const AnoPreparedGeometryLevel& level = prepared->levels[reserved];
        if (!geometry_pool_take_span(
                pool->vertexFreeBlocks, &pool->vertexFreeCount,
                &pool->vertexWriteOffset, pool->vertexCapacity,
                level.vertexBytes, &vertexOffsets[reserved]))
            break;
        if (!geometry_pool_take_span(
                pool->indexFreeBlocks, &pool->indexFreeCount,
                &pool->indexWriteOffset, pool->indexCapacity,
                level.metadataBytes, &metadataOffsets[reserved])) {
            geometry_pool_free_span(
                &pool->vertexFreeBlocks, &pool->vertexFreeCount,
                &pool->vertexFreeCapacity, vertexOffsets[reserved],
                level.vertexBytes);
            break;
        }
    }
    if (reserved != prepared->lodCount) {
        for (uint32_t i = 0; i < reserved; ++i) {
            geometry_pool_free_span(
                &pool->vertexFreeBlocks, &pool->vertexFreeCount,
                &pool->vertexFreeCapacity, vertexOffsets[i],
                prepared->levels[i].vertexBytes);
            geometry_pool_free_span(
                &pool->indexFreeBlocks, &pool->indexFreeCount,
                &pool->indexFreeCapacity, metadataOffsets[i],
                prepared->levels[i].metadataBytes);
        }
        if (appended) pool->meshCount = base;
        ano_log(ANO_ERROR, "Geometry mega-buffer exhausted while reserving a LOD chain.");
        return ANO_MESH_NONE;
    }

    uint8_t* mapped = static_cast<uint8_t*>(stagingMapped);
    for (uint32_t i = 0; i < prepared->lodCount; ++i) {
        const AnoPreparedGeometryLevel& level = prepared->levels[i];
        const VkDeviceSize source = stagingBase + level.uploadOffset;
        const VkDeviceSize total = static_cast<VkDeviceSize>(
            level.vertexBytes) + level.metadataBytes;
        memcpy(mapped + static_cast<size_t>(source), level.upload,
               static_cast<size_t>(total));
        const VkBufferCopy vertexCopy = {
            .srcOffset = source,
            .dstOffset = vertexOffsets[i],
            .size = level.vertexBytes,
        };
        const VkBufferCopy metadataCopy = {
            .srcOffset = source + level.vertexBytes,
            .dstOffset = metadataOffsets[i],
            .size = level.metadataBytes,
        };
        vkCmdCopyBuffer(command, staging, pool->vertexBuffer, 1,
                        &vertexCopy);
        vkCmdCopyBuffer(command, staging, pool->indexBuffer, 1,
                        &metadataCopy);

        MeshRegion& mesh = pool->meshes[base + i];
        mesh = level.region;
        mesh.vertexOffset = vertexOffsets[i];
        mesh.indexOffset = metadataOffsets[i];
        mesh.meshletOffset += metadataOffsets[i];
        mesh.uniqueVerticesOffset += metadataOffsets[i];
        mesh.trianglesOffset += metadataOffsets[i];
        mesh.boundsOffset += metadataOffsets[i];
        mesh.classicIndexOffset += metadataOffsets[i];
    }
    pool->meshes[base].lodCount = prepared->lodCount;
    for (uint32_t i = 0; i < prepared->lodCount; ++i)
        geometry_pool_mark_gpu_dirty(pool, base + i);
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
    if (mesh->metadataBytes != 0)
        geometry_pool_free_span(
            &pool->indexFreeBlocks, &pool->indexFreeCount,
            &pool->indexFreeCapacity, mesh->indexOffset,
            mesh->metadataBytes);

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
