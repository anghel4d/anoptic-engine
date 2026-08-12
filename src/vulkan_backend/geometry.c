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
    free(pool->meshGpuPublication.pendingFrames);
    free(pool->meshGpuPublication.dirtyRows);
    pool->meshes = NULL;
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
    pool->meshGpuPublication = {};
    pool->meshGpuPublication.pendingFrames =
        ano::allocate_zero<uint8_t>(ANO_MAX_MESHES);
    pool->meshGpuPublication.dirtyRows =
        ano::allocate<uint32_t>(ANO_MAX_MESHES);
    pool->meshGpuPublication.allFrames =
        static_cast<uint8_t>((1u << framesInFlight) - 1u);
    if (!pool->meshes || !pool->meshGpuPublication.pendingFrames
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
    
    pool->freeMeshIndices = NULL;
    pool->freeMeshIndexCount = 0;
    pool->freeMeshIndexCapacity = 0;

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
    
    if (pool->freeMeshIndices) free(pool->freeMeshIndices);
    pool->freeMeshIndices = NULL;
}

typedef struct GeometryScratch {
    ano_meshlet_t* meshlets;
    uint32_t* vertices;
    uint8_t* triangles;
    ano_meshlet_bounds_gpu_t* bounds;
    VkBuffer staging;
} GeometryScratch;

static void geometry_scratch_discard(VkDevice device, GeometryScratch* scratch)
{
    if (scratch->staging != VK_NULL_HANDLE)
        vkDestroyBuffer(device, scratch->staging, NULL);
    free(scratch->meshlets);
    free(scratch->vertices);
    free(scratch->triangles);
    free(scratch->bounds);
}

// Emit into a caller-reserved meshes[] slot and record its copies.
static bool geometry_pool_emit_level(GeometryPool* pool, GpuAllocator* alloc, VkDevice device,
                                     const Vertex* vertices, uint32_t vertexCount,
                                     const uint32_t* indices, uint32_t indexCount,
                                     uint32_t meshIndex, VkCommandBuffer command,
                                     VkBuffer* retainedStaging)
{
    // Build meshlets and calculate bounds on the host
    size_t max_meshlets = ano_build_meshlets_bound(indexCount, 64, 126);
    if (max_meshlets == 0) return false;

    GeometryScratch scratch = {
        .meshlets = ano::allocate<ano_meshlet_t>(max_meshlets),
        .vertices = ano::allocate<uint32_t>(max_meshlets * 64u),
        .triangles = ano::allocate<uint8_t>(max_meshlets * 126u * 3u),
        .bounds = ano::allocate<ano_meshlet_bounds_gpu_t>(max_meshlets),
    };
    if (!scratch.meshlets || !scratch.vertices || !scratch.triangles
        || !scratch.bounds) {
        geometry_scratch_discard(device, &scratch);
        return false;
    }

    size_t meshlet_count = ano_build_meshlets(
        scratch.meshlets, scratch.vertices, scratch.triangles,
        indices,
        indexCount,
        64,
        126
    );

    if (meshlet_count == 0) {
        geometry_scratch_discard(device, &scratch);
        return false;
    }

    size_t unique_vertex_count = scratch.meshlets[meshlet_count - 1].vertex_offset
        + scratch.meshlets[meshlet_count - 1].vertex_count;
    size_t local_indices_count = scratch.meshlets[meshlet_count - 1].triangle_offset
        + scratch.meshlets[meshlet_count - 1].triangle_count * 3;

    for (size_t p = 0; p < meshlet_count; ++p) {
        scratch.bounds[p] = ano_compute_meshlet_bounds(
            scratch.vertices + scratch.meshlets[p].vertex_offset,
            scratch.triangles + scratch.meshlets[p].triangle_offset,
            scratch.meshlets[p].triangle_count,
            (const float*)vertices,
            vertexCount,
            sizeof(Vertex)
        );
    }

    VkDeviceSize meshlets_size = meshlet_count * sizeof(ano_meshlet_t);
    VkDeviceSize unique_vertices_size = unique_vertex_count * sizeof(uint32_t);
    VkDeviceSize local_triangles_size = (local_indices_count * sizeof(uint8_t) + 3) & ~3;
    VkDeviceSize bounds_size = meshlet_count * sizeof(ano_meshlet_bounds_gpu_t);
    // u32 triangle-list indices for the VS fallback, 4-byte aligned after the bounds block.
    VkDeviceSize classic_indices_size = indexCount * sizeof(uint32_t);

    VkDeviceSize total_metadata_size = meshlets_size + unique_vertices_size + local_triangles_size + bounds_size + classic_indices_size;

    VkDeviceSize vertexSize = sizeof(Vertex) * vertexCount;
    VkDeviceSize totalSize = vertexSize + total_metadata_size;

    VkBufferCreateInfo stagingInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = totalSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(device, &stagingInfo, NULL, &scratch.staging)
        != VK_SUCCESS) {
        scratch.staging = VK_NULL_HANDLE;
        geometry_scratch_discard(device, &scratch);
        return false;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, scratch.staging, &memReqs);

    GpuAllocation stagingAlloc = gpu_alloc(alloc, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (stagingAlloc.memory == VK_NULL_HANDLE
        || vkBindBufferMemory(device, scratch.staging, stagingAlloc.memory,
                              stagingAlloc.offset) != VK_SUCCESS) {
        geometry_scratch_discard(device, &scratch);
        return false;
    }

    // Copy vertex and metadata data into the staging buffer
    char* mapped = (char*)stagingAlloc.mapped;
    memcpy(mapped, vertices, vertexSize);
    
    char* meta_ptr = mapped + vertexSize;
    memcpy(meta_ptr, scratch.meshlets, meshlets_size);
    memcpy(meta_ptr + meshlets_size, scratch.vertices, unique_vertices_size);
    memcpy(meta_ptr + meshlets_size + unique_vertices_size, scratch.triangles, local_indices_count);
    if (local_triangles_size > local_indices_count) {
        memset(meta_ptr + meshlets_size + unique_vertices_size + local_indices_count, 0, local_triangles_size - local_indices_count);
    }
    memcpy(meta_ptr + meshlets_size + unique_vertices_size + local_triangles_size, scratch.bounds, bounds_size);
    memcpy(meta_ptr + meshlets_size + unique_vertices_size + local_triangles_size + bounds_size, indices, classic_indices_size);

    // Plan allocations
    uint32_t finalVertexOffset = (uint32_t)-1;
    int vertexFreeIdx = -1;
    for (uint32_t i = 0; i < pool->vertexFreeCount; i++) {
        if (pool->vertexFreeBlocks[i].size >= vertexSize) {
            finalVertexOffset = pool->vertexFreeBlocks[i].offset;
            vertexFreeIdx = (int)i;
            break;
        }
    }
    if (finalVertexOffset == (uint32_t)-1) {
        if ((VkDeviceSize)pool->vertexWriteOffset + vertexSize > pool->vertexCapacity) {
            ano_log(ANO_ERROR, "Error: Geometry mega-buffer vertex pool exhausted! Requested %llu, Capacity %llu",
                   (unsigned long long)(pool->vertexWriteOffset + vertexSize), (unsigned long long)pool->vertexCapacity);
            geometry_scratch_discard(device, &scratch);
            return false; // pool exhausted
        }
        finalVertexOffset = pool->vertexWriteOffset;
    }

    uint32_t finalIndexOffset = (uint32_t)-1;
    int indexFreeIdx = -1;
    for (uint32_t i = 0; i < pool->indexFreeCount; i++) {
        if (pool->indexFreeBlocks[i].size >= total_metadata_size) {
            finalIndexOffset = pool->indexFreeBlocks[i].offset;
            indexFreeIdx = (int)i;
            break;
        }
    }
    if (finalIndexOffset == (uint32_t)-1) {
        if ((VkDeviceSize)pool->indexWriteOffset + total_metadata_size > pool->indexCapacity) {
            ano_log(ANO_ERROR, "Error: Geometry mega-buffer metadata pool exhausted! Requested %llu, Capacity %llu",
                   (unsigned long long)(pool->indexWriteOffset + total_metadata_size), (unsigned long long)pool->indexCapacity);
            geometry_scratch_discard(device, &scratch);
            return false; // pool exhausted
        }
        finalIndexOffset = pool->indexWriteOffset;
    }

    // Commit reservations
    if (vertexFreeIdx >= 0) {
        pool->vertexFreeBlocks[vertexFreeIdx].offset += vertexSize;
        pool->vertexFreeBlocks[vertexFreeIdx].size -= vertexSize;
        if (pool->vertexFreeBlocks[vertexFreeIdx].size == 0) {
            pool->vertexFreeBlocks[vertexFreeIdx] = pool->vertexFreeBlocks[--pool->vertexFreeCount];
        }
    } else {
        pool->vertexWriteOffset += vertexSize;
    }
    if (indexFreeIdx >= 0) {
        pool->indexFreeBlocks[indexFreeIdx].offset += total_metadata_size;
        pool->indexFreeBlocks[indexFreeIdx].size -= total_metadata_size;
        if (pool->indexFreeBlocks[indexFreeIdx].size == 0) {
            pool->indexFreeBlocks[indexFreeIdx] = pool->indexFreeBlocks[--pool->indexFreeCount];
        }
    } else {
        pool->indexWriteOffset += total_metadata_size;
    }

    VkBufferCopy copyRegion = {
        .srcOffset = 0,
        .dstOffset = finalVertexOffset,
        .size = vertexSize
    };
    vkCmdCopyBuffer(command, scratch.staging, pool->vertexBuffer, 1, &copyRegion);

    VkBufferCopy indexCopyRegion = {
        .srcOffset = vertexSize,
        .dstOffset = finalIndexOffset,
        .size = total_metadata_size
    };
    vkCmdCopyBuffer(command, scratch.staging, pool->indexBuffer, 1,
                    &indexCopyRegion);
    *retainedStaging = scratch.staging;
    scratch.staging = VK_NULL_HANDLE;
    geometry_scratch_discard(device, &scratch);

    // Register the mesh into the caller-owned slot.
    MeshRegion* mesh = &pool->meshes[meshIndex];
    mesh->vertexOffset = finalVertexOffset;
    mesh->vertexCount = vertexCount;
    mesh->indexOffset = finalIndexOffset;
    mesh->indexCount = (uint32_t)total_metadata_size;
    mesh->meshletOffset = finalIndexOffset;
    mesh->meshletCount = (uint32_t)meshlet_count;
    mesh->uniqueVerticesOffset = finalIndexOffset + (uint32_t)meshlets_size;
    mesh->trianglesOffset = finalIndexOffset + (uint32_t)(meshlets_size + unique_vertices_size);
    mesh->boundsOffset = finalIndexOffset + (uint32_t)(meshlets_size + unique_vertices_size + local_triangles_size);
    mesh->classicIndexOffset = finalIndexOffset + (uint32_t)(meshlets_size + unique_vertices_size + local_triangles_size + bounds_size);
    mesh->classicIndexCount = indexCount;
    mesh->lodCount = 1u; // the chain recorder overrides its base

    // Calculate bounding sphere
    Vector3 minBounds = vertices[0].position;
    Vector3 maxBounds = vertices[0].position;
    for (uint32_t i = 1; i < vertexCount; i++) {
        if (vertices[i].position.v[0] < minBounds.v[0]) minBounds.v[0] = vertices[i].position.v[0];
        if (vertices[i].position.v[1] < minBounds.v[1]) minBounds.v[1] = vertices[i].position.v[1];
        if (vertices[i].position.v[2] < minBounds.v[2]) minBounds.v[2] = vertices[i].position.v[2];
        if (vertices[i].position.v[0] > maxBounds.v[0]) maxBounds.v[0] = vertices[i].position.v[0];
        if (vertices[i].position.v[1] > maxBounds.v[1]) maxBounds.v[1] = vertices[i].position.v[1];
        if (vertices[i].position.v[2] > maxBounds.v[2]) maxBounds.v[2] = vertices[i].position.v[2];
    }
    
    mesh->boundingSphereCenter[0] = (minBounds.v[0] + maxBounds.v[0]) * 0.5f;
    mesh->boundingSphereCenter[1] = (minBounds.v[1] + maxBounds.v[1]) * 0.5f;
    mesh->boundingSphereCenter[2] = (minBounds.v[2] + maxBounds.v[2]) * 0.5f;

    float maxDistSq = 0.0f;
    for (uint32_t i = 0; i < vertexCount; i++) {
        float dx = vertices[i].position.v[0] - mesh->boundingSphereCenter[0];
        float dy = vertices[i].position.v[1] - mesh->boundingSphereCenter[1];
        float dz = vertices[i].position.v[2] - mesh->boundingSphereCenter[2];
        float distSq = dx*dx + dy*dy + dz*dz;
        if (distSq > maxDistSq) maxDistSq = distSq;
    }
    mesh->boundingSphereRadius = sqrtf(maxDistSq);

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
static uint32_t geometry_compact_level(const Vertex* srcVerts, uint32_t srcVertexCount,
                                       uint32_t* indices, uint32_t indexCount, Vertex* outVerts)
{
    uint32_t* remap = ano::allocate<uint32_t>(srcVertexCount);
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
    free(remap);
    return next;
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
    if (command == VK_NULL_HANDLE || outStaging == NULL
        || out_lodCount == NULL)
        return ANO_MESH_NONE;
    *out_lodCount = 0;
    uint32_t want = config ? config->lodCount : 1u;
    if (want < 1u) want = 1u;
    if (want > ANO_MAX_LOD) want = ANO_MAX_LOD;
    float targetError = config ? config->targetError : 0.0f;

    // Clamp chain to remaining ANO_MAX_MESHES slots; no room -> refuse.
    if (pool->meshCount >= ANO_MAX_MESHES)
        return ANO_MESH_NONE;
    if ((uint64_t)pool->meshCount + want > ANO_MAX_MESHES)
        want = (uint32_t)(ANO_MAX_MESHES - pool->meshCount);

    // Reserve `want` contiguous slots (cull addresses lodBase+level; free-list gaps break that).
    if ((uint64_t)pool->meshCount + want > pool->meshCapacity) {
        uint32_t capacity = pool->meshCapacity;
        while ((uint64_t)pool->meshCount + want > capacity)
            capacity = capacity == 0 ? 100u : capacity * 2u;
        MeshRegion* meshes = ano::reallocate(pool->meshes, capacity);
        if (!meshes)
            return ANO_MESH_NONE;
        pool->meshes = meshes;
        pool->meshCapacity = capacity;
    }

    // Per-level scratch (simplified + compacted), reused; sized to full source count.
    uint32_t* simplified = want > 1u ? ano::allocate<uint32_t>(indexCount) : NULL;
    Vertex* compacted = want > 1u ? ano::allocate<Vertex>(vertexCount) : NULL;
    if (want > 1u && (!simplified || !compacted)) {
        free(simplified);
        free(compacted);
        simplified = NULL;
        compacted = NULL;
        want = 1u;
    }

    uint32_t lodBase = pool->meshCount;
    pool->meshCount += want;  // reserve; rolled back to the count actually produced below

    uint32_t produced = 0;
    for (uint32_t lvl = 0; lvl < want; ++lvl) {
        const uint32_t* lvlIndices = indices;
        uint32_t lvlCount = indexCount;
        const Vertex*   lvlVertices = vertices;
        uint32_t lvlVertexCount = vertexCount;
        if (lvl > 0 && simplified) {
            float ratio = config->ratios[lvl];
            if (ratio <= 0.0f || ratio > 1.0f) ratio = 1.0f;
            uint32_t targetIdx = (uint32_t)((float)indexCount * ratio);
            targetIdx -= targetIdx % 3u;
            if (targetIdx < 3u) targetIdx = 3u;
            size_t got = ano_simplify_ex(simplified, indices, indexCount,
                                         (const float*)vertices, vertexCount, sizeof(Vertex),
                                         targetIdx, targetError, config->edgeLenFactor, NULL);
            if (got < 3u) break;  // simplifier produced nothing usable: truncate the chain here
            ano_optimize_vertex_cache(simplified, simplified, got, vertexCount);
            lvlIndices = simplified;
            lvlCount = (uint32_t)got;
            // Compact referenced verts (remaps simplified in place); cc==0 keeps full array
            if (compacted) {
                uint32_t cc = geometry_compact_level(vertices, vertexCount, simplified, lvlCount, compacted);
                if (cc > 0u) { lvlVertices = compacted; lvlVertexCount = cc; }
            }
        }
        VkBuffer staging = VK_NULL_HANDLE;
        if (!geometry_pool_emit_level(
                pool, alloc, device, lvlVertices, lvlVertexCount,
                lvlIndices, lvlCount, lodBase + lvl, command, &staging))
            break;  // pool exhausted: truncate
        outStaging[produced] = staging;
        produced++;
    }

    free(simplified);
    free(compacted);

    // Release unused reserved slots
    pool->meshCount = lodBase + produced;

    // Base lodCount = chain length; members stay at 1
    if (produced) {
        pool->meshes[lodBase].lodCount = produced;
        for (uint32_t i = 0; i < produced; ++i)
            geometry_pool_mark_gpu_dirty(pool, lodBase + i);
    }

    *out_lodCount = produced;
    return produced ? lodBase : ANO_MESH_NONE;
}

void geometry_pool_free(GeometryPool* pool, uint32_t meshIndex)
{
    if (meshIndex >= pool->meshCount || meshIndex == 0) return; // Don't free fallback or out of bounds

    MeshRegion* mesh = &pool->meshes[meshIndex];
    if (mesh->vertexCount == 0) return; // Already freed

    // Add to vertex free list
    if (pool->vertexFreeCount >= pool->vertexFreeCapacity) {
        uint32_t capacity = pool->vertexFreeCapacity == 0 ? 32u : pool->vertexFreeCapacity * 2u;
        GeoFreeBlock* blocks = ano::reallocate(pool->vertexFreeBlocks, capacity);
        if (blocks) {
            pool->vertexFreeBlocks = blocks;
            pool->vertexFreeCapacity = capacity;
        }
    }
    if (pool->vertexFreeCount < pool->vertexFreeCapacity)
        pool->vertexFreeBlocks[pool->vertexFreeCount++] = (GeoFreeBlock){
            .offset = mesh->vertexOffset,
            .size = static_cast<uint32_t>(mesh->vertexCount * sizeof(Vertex))
        };

    // Add to index free list
    if (mesh->indexCount > 0) {
        if (pool->indexFreeCount >= pool->indexFreeCapacity) {
            uint32_t capacity = pool->indexFreeCapacity == 0 ? 32u : pool->indexFreeCapacity * 2u;
            GeoFreeBlock* blocks = ano::reallocate(pool->indexFreeBlocks, capacity);
            if (blocks) {
                pool->indexFreeBlocks = blocks;
                pool->indexFreeCapacity = capacity;
            }
        }
        if (pool->indexFreeCount < pool->indexFreeCapacity)
            pool->indexFreeBlocks[pool->indexFreeCount++] = (GeoFreeBlock){
                .offset = mesh->indexOffset,
                .size = mesh->indexCount
            };
    }

    // Add mesh index to free list
    if (pool->freeMeshIndexCount >= pool->freeMeshIndexCapacity) {
        uint32_t capacity = pool->freeMeshIndexCapacity == 0 ? 32u : pool->freeMeshIndexCapacity * 2u;
        uint32_t* indices = ano::reallocate(pool->freeMeshIndices, capacity);
        if (indices) {
            pool->freeMeshIndices = indices;
            pool->freeMeshIndexCapacity = capacity;
        }
    }
    if (pool->freeMeshIndexCount < pool->freeMeshIndexCapacity)
        pool->freeMeshIndices[pool->freeMeshIndexCount++] = meshIndex;

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
