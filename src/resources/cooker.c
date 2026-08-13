/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include "cooker_internal.h"

#include <anoptic_atomic.h>
#include <anoptic_hive.h>
#include <anoptic_memory_typed.h>
#include <anoptic_resources_typed.h>

#include <new>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

namespace {

struct SourceBinding final {
    AnoResourceSourceId source;
    char *path;
    struct SourceFileState *files;
    uint64_t fileCount;
    AnoAssetId *assets;
    uint64_t assetCount;
    AnoContentId snapshotId;
    AnoContentId rootId;
    bool snapshotValid;
    bool rootValid;
    bool pathChanged;
    struct SourceFileState *candidateFiles;
    uint64_t candidateFileCount;
    AnoAssetId *candidateAssets;
    uint64_t candidateAssetCount;
    AnoContentId candidateSnapshotId;
    AnoContentId candidateRootId;
    bool candidateValid;
    bool candidateReplaceAll;
};

struct SourceFileState final {
    char *path;
    AnoContentId content;
    ano::MemoryVolume *volume;
    ano::MemoryReservation bytes;
};

struct AcquiredFile final {
    char *path;
    uint8_t *bytes;
    size_t size;
    AnoContentId content;
    ano::MemoryVolume *volume;
    ano::MemoryReservation reservation;
};

struct CookNode final {
    AnoAssetId asset;
    AnoResourceSourceId source;
    AnoResourceTypeId type;
    uint64_t producer;
    AnoContentId action;
    AnoContentId result;
    bool present;
    bool dirty;
};

struct WorkItem final {
    AnoResourcePackItem source;
    const AnoResourceRevisionItem *previous;
    const AnoResourceDependency *dependencies;
    uint64_t dependencyCount;
    AnoResourceSchema schema;
    AnoContentId content;
    AnoContentId action;
    ano::MemoryReservation artifact;
    ano::MemoryVolume *volume;
    bool pending;
    bool changed;
    bool artifactChanged;
    bool affected;
};

struct RevisionPlan final {
    ano::MemorySegment<AnoResourceRevisionItem> items;
    ano::MemorySegment<AnoResourceDependency> dependencies;
    ano::MemorySegment<ano::MemoryVolume *> volumes;
};

struct EncodeWork final {
    AnoResourceCookArtifact *artifact;
    ano::MemoryVolume *volume;
    uint64_t volumeIndex;
    ano::MemoryReservation reservation;
    AnoResourceBytes bytes;
    AnoResourceSchema schema;
    AnoContentId content;
    const AnoResourceDependency *dependencies;
    uint64_t dependencyCount;
    ano::MemoryRegion *metadata;
    AnoResourceError result;
};

struct EncodeVolume final {
    ano::MemoryLayoutCursor layout;
    ano::MemoryVolume *volume;
    AnoResourceTypeId type;
    AnoResourceCommitGroupId commitGroup;
};

inline constexpr size_t artifactVolumeTarget = 8 * 1024 * 1024;

struct CopyEncodeContext final {
    AnoResourceBytes source;
};

int compare_pack_items(const void *left, const void *right)
{
    const auto& lhs = *static_cast<const WorkItem *>(left);
    const auto& rhs = *static_cast<const WorkItem *>(right);
    if (lhs.source.asset.value < rhs.source.asset.value)
        return -1;
    if (lhs.source.asset.value > rhs.source.asset.value)
        return 1;
    return 0;
}

int compare_acquired_files(const void *left, const void *right)
{
    const auto& lhs = *static_cast<const AcquiredFile *>(left);
    const auto& rhs = *static_cast<const AcquiredFile *>(right);
    return strcmp(lhs.path, rhs.path);
}

int compare_asset_ids(const void *left, const void *right)
{
    const auto& lhs = *static_cast<const AnoAssetId *>(left);
    const auto& rhs = *static_cast<const AnoAssetId *>(right);
    return lhs.value < rhs.value ? -1 : lhs.value > rhs.value ? 1 : 0;
}

bool content_equal(const AnoContentId& lhs, const AnoContentId& rhs)
{
    return ano::detail::bytes_equal(lhs.bytes, rhs.bytes, sizeof(lhs.bytes));
}

char *copy_string(const char *value)
{
    if (value == nullptr)
        return nullptr;
    const size_t length = strlen(value);
    if (length == SIZE_MAX)
        return nullptr;
    char *copy = static_cast<char *>(mi_malloc(length + 1));
    if (copy != nullptr)
        memcpy(copy, value, length + 1);
    return copy;
}

void release_source_files(SourceFileState *files, uint64_t count)
{
    if (files == nullptr)
        return;
    for (uint64_t i = 0; i < count; ++i) {
        ano::memory_volume_release(files[i].volume);
        mi_free(files[i].path);
    }
    mi_free(files);
}

void discard_source_candidate(SourceBinding& source)
{
    release_source_files(source.candidateFiles, source.candidateFileCount);
    mi_free(source.candidateAssets);
    source.candidateFiles = nullptr;
    source.candidateFileCount = 0;
    source.candidateAssets = nullptr;
    source.candidateAssetCount = 0;
    source.candidateSnapshotId = {};
    source.candidateRootId = {};
    source.candidateValid = false;
    source.candidateReplaceAll = false;
}

SourceBinding *find_source(struct AnoResourceCooker *cooker,
                           AnoResourceSourceId source);
const SourceBinding *find_source(const struct AnoResourceCooker *cooker,
                                 AnoResourceSourceId source);

AnoResourceError extract_dependencies(
    AnoResourceTypeId type, AnoResourceBytes artifact,
    ano::MemoryRegion *region, const AnoResourceDependency **dependencies,
    uint64_t *dependencyCount)
{
    *dependencies = nullptr;
    *dependencyCount = 0;
    uint64_t required = 0;
    const AnoResourceError measured = ano_resource_artifact_dependencies(
        type, artifact, nullptr, 0, &required);
    if (required == 0)
        return measured;
    if (measured != ANO_RESOURCE_DEPENDENCY_CAPACITY
        || required > SIZE_MAX / sizeof(AnoResourceDependency))
        return measured == ANO_RESOURCE_DEPENDENCY_CAPACITY
            ? ANO_RESOURCE_OVERFLOW : measured;
    auto *values = ano::memory_region_allocate<AnoResourceDependency>(
        region, static_cast<size_t>(required));
    if (values == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    uint64_t actual = 0;
    const AnoResourceError extracted = ano_resource_artifact_dependencies(
        type, artifact, values, required, &actual);
    if (extracted != ANO_RESOURCE_OK || actual != required)
        return extracted == ANO_RESOURCE_OK ? ANO_RESOURCE_BAD_MANIFEST
                                            : extracted;
    qsort(values, static_cast<size_t>(actual), sizeof(*values),
          ano::resource_detail::compare_dependency);
    uint64_t unique = 0;
    for (uint64_t i = 0; i < actual; ++i)
        if (unique == 0
            || ano::resource_detail::compare_dependency(
                   values + unique - 1, values + i) != 0)
            values[unique++] = values[i];
    *dependencies = values;
    *dependencyCount = unique;
    return ANO_RESOURCE_OK;
}

void encode_artifact(void *context, uint64_t index, ano::MemoryRegion *)
{
    EncodeWork& work = static_cast<EncodeWork *>(context)[index];
    ano::MemoryMutableView view{};
    if (!ano::memory_volume_write(work.volume, work.reservation, view)) {
        work.result = ANO_RESOURCE_BAD_MANIFEST;
        return;
    }
    work.bytes = {
        .data = static_cast<const uint8_t *>(view.data),
        .size = view.size,
    };
    work.result = work.artifact->encode(
        work.artifact->context,
        {.data = static_cast<uint8_t *>(view.data), .size = view.size});
    if (work.result == ANO_RESOURCE_OK)
        work.result = ano_resource_validate_artifact(
            work.artifact->type, work.bytes);
    if (work.result == ANO_RESOURCE_OK)
        work.result = ano_resource_artifact_schema(
            work.artifact->type, &work.schema);
    if (work.result == ANO_RESOURCE_OK)
        work.result = ano_resource_content_id(work.bytes, &work.content);
    if (work.result == ANO_RESOURCE_OK)
        work.result = extract_dependencies(
            work.artifact->type, work.bytes, work.metadata,
            &work.dependencies, &work.dependencyCount);
}

AnoResourceError copy_encode(void *context,
                             AnoResourceMutableBytes destination)
{
    const auto& copy = *static_cast<const CopyEncodeContext *>(context);
    if (copy.source.data == nullptr
        || copy.source.size != destination.size)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    memcpy(destination.data, copy.source.data,
           static_cast<size_t>(destination.size));
    return ANO_RESOURCE_OK;
}

} // namespace

struct AnoResourceCooker {
    AnoResourcePackItem *items = nullptr;
    uint64_t itemCount = 0;
    uint64_t itemCapacity = 0;
    ano::MemoryVolume **pendingVolumes = nullptr;
    uint64_t pendingVolumeCount = 0;
    uint64_t pendingVolumeCapacity = 0;
    uint64_t *pendingIndex = nullptr;
    uint64_t pendingIndexCapacity = 0;
    ano::hive<SourceBinding, ano::MimallocAllocator<SourceBinding>> sources;
    ano::MemoryRegion *activeRegion = nullptr;
    ano::MemoryRegion *pendingMetadata = nullptr;
    AcquiredFile *activeFiles = nullptr;
    uint64_t activeFileCount = 0;
    uint64_t activeFileCapacity = 0;
    AnoAssetId *activeAssets = nullptr;
    uint64_t activeAssetCount = 0;
    uint64_t activeAssetCapacity = 0;
    AnoResourceSourceId activeSource{};
    uint64_t activeProducer = 0;
    AnoAssetId firstDerivedAsset{};
    AnoAssetId nextDerivedAsset{};
    ANO_ATOMIC(bool) cancelled;
    ano::resource_detail::Executor *executor = nullptr;
    ano::hive<CookNode, ano::MimallocAllocator<CookNode>> nodes;
    CookNode **nodeIndex = nullptr;
    uint64_t nodeIndexCapacity = 0;
    const AnoCookedRevision *current = nullptr;
    uint64_t executedActions = 0;
    uint64_t allocatedArtifacts = 0;
    bool rebuildTail = false;
    bool activeRootChanged = false;
    bool activeFullRebuild = false;
};

namespace {

SourceBinding *find_source(AnoResourceCooker *cooker,
                           AnoResourceSourceId source)
{
    if (cooker != nullptr)
        for (SourceBinding& binding : cooker->sources)
            if (binding.source.value == source.value)
                return &binding;
    return nullptr;
}

const SourceBinding *find_source(const AnoResourceCooker *cooker,
                                 AnoResourceSourceId source)
{
    return find_source(const_cast<AnoResourceCooker *>(cooker), source);
}

struct FileStamp final {
    uint64_t size;
    int64_t modified;
    int64_t changed;
    int64_t subsecond;
};

bool file_stamp(const char *path, FileStamp *stamp)
{
    if (path == nullptr || stamp == nullptr)
        return false;
#if defined(_WIN32)
    struct _stat64 state{};
    if (_stat64(path, &state) != 0 || state.st_size < 0)
        return false;
    *stamp = {
        static_cast<uint64_t>(state.st_size),
        static_cast<int64_t>(state.st_mtime),
        static_cast<int64_t>(state.st_ctime),
        0,
    };
#else
    struct stat state{};
    if (stat(path, &state) != 0 || state.st_size < 0)
        return false;
#if defined(__APPLE__)
    const int64_t subsecond = state.st_mtimespec.tv_nsec;
#else
    const int64_t subsecond = state.st_mtim.tv_nsec;
#endif
    *stamp = {
        static_cast<uint64_t>(state.st_size),
        static_cast<int64_t>(state.st_mtime),
        static_cast<int64_t>(state.st_ctime),
        subsecond,
    };
#endif
    return true;
}

bool stamp_equal(const FileStamp& lhs, const FileStamp& rhs)
{
    return lhs.size == rhs.size && lhs.modified == rhs.modified
        && lhs.changed == rhs.changed && lhs.subsecond == rhs.subsecond;
}

AcquiredFile *find_active_file(AnoResourceCooker& cooker, const char *path)
{
    for (uint64_t i = 0; i < cooker.activeFileCount; ++i)
        if (strcmp(cooker.activeFiles[i].path, path) == 0)
            return &cooker.activeFiles[i];
    return nullptr;
}

AnoResourceError reserve_active_files(AnoResourceCooker& cooker,
                                      uint64_t additional)
{
    uint64_t required = 0;
    if (!ano::detail::checked_add(
            cooker.activeFileCount, additional, &required)
        || required > SIZE_MAX / sizeof(AcquiredFile))
        return ANO_RESOURCE_OVERFLOW;
    if (required <= cooker.activeFileCapacity)
        return ANO_RESOURCE_OK;
    uint64_t capacity = cooker.activeFileCapacity == 0
        ? 8 : cooker.activeFileCapacity;
    while (capacity < required) {
        if (capacity > UINT64_MAX / 2)
            return ANO_RESOURCE_OVERFLOW;
        capacity *= 2;
    }
    AcquiredFile *grown = ano::memory_region_allocate_zero<AcquiredFile>(
        cooker.activeRegion, static_cast<size_t>(capacity));
    if (grown == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    if (cooker.activeFileCount != 0)
        memcpy(grown, cooker.activeFiles,
               static_cast<size_t>(cooker.activeFileCount)
                   * sizeof(AcquiredFile));
    cooker.activeFiles = grown;
    cooker.activeFileCapacity = capacity;
    return ANO_RESOURCE_OK;
}

AnoResourceError read_file(AnoResourceCooker& cooker, AcquiredFile& acquired)
{
    char *path = acquired.path;
    FileStamp before{};
    if (!file_stamp(path, &before) || before.size > SIZE_MAX)
        return ANO_RESOURCE_IO_ERROR;
    ano::MemoryLayoutCursor layout{};
    ano::MemoryReservation reservation{};
    if (!layout.reserve(static_cast<size_t>(before.size), ANO_CACHE_LINE,
                        reservation))
        return ANO_RESOURCE_OVERFLOW;
    ano::MemoryVolume *volume = ano::memory_volume_create(layout);
    if (volume == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    ano::MemoryMutableView destination{};
    if (!ano::memory_volume_write(volume, reservation, destination)) {
        ano::memory_volume_release(volume);
        return ANO_RESOURCE_OUT_OF_MEMORY;
    }
    FILE *file = fopen(path, "rb");
    if (file == nullptr) {
        ano::memory_volume_release(volume);
        return ANO_RESOURCE_IO_ERROR;
    }
    auto *bytes = static_cast<uint8_t *>(destination.data);
    AnoResourceError result = ANO_RESOURCE_OK;
    if (result == ANO_RESOURCE_OK
        && before.size != 0
        && fread(bytes, 1, static_cast<size_t>(before.size), file)
            != before.size)
        result = ANO_RESOURCE_IO_ERROR;
    if (result == ANO_RESOURCE_OK
        && (fgetc(file) != EOF || ferror(file)))
        result = ANO_RESOURCE_IO_ERROR;
    if (fclose(file) != 0 && result == ANO_RESOURCE_OK)
        result = ANO_RESOURCE_IO_ERROR;
    if (result == ANO_RESOURCE_OK
        && ano_resource_cooker_cancelled(&cooker))
        result = ANO_RESOURCE_CANCELLED;
    FileStamp after{};
    if (result == ANO_RESOURCE_OK
        && (!file_stamp(path, &after) || !stamp_equal(before, after)))
        result = ANO_RESOURCE_CANCELLED;
    if (result == ANO_RESOURCE_OK && !ano::memory_volume_seal(volume))
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    if (result != ANO_RESOURCE_OK) {
        ano::memory_volume_release(volume);
        return result;
    }
    acquired = {
        .path = path,
        .bytes = bytes,
        .size = static_cast<size_t>(before.size),
        .content = ano::detail::sha256(bytes, static_cast<size_t>(before.size)),
        .volume = volume,
        .reservation = reservation,
    };
    return ANO_RESOURCE_OK;
}

struct ReadBatch final {
    AnoResourceCooker *cooker;
    AcquiredFile *files;
    AnoResourceError *results;
};

void read_source_file(void *context, uint64_t index, ano::MemoryRegion *)
{
    ReadBatch& batch = *static_cast<ReadBatch *>(context);
    batch.results[index] = read_file(*batch.cooker, batch.files[index]);
}

AnoResourceError acquire_file(AnoResourceCooker& cooker, const char *path,
                              AcquiredFile **output)
{
    if (output == nullptr || path == nullptr || path[0] == '\0'
        || cooker.activeRegion == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    if (ano_resource_cooker_cancelled(&cooker))
        return ANO_RESOURCE_CANCELLED;
    if (AcquiredFile *existing = find_active_file(cooker, path)) {
        *output = existing;
        return ANO_RESOURCE_OK;
    }
    *output = nullptr;
    AnoResourceError result = reserve_active_files(cooker, 1);
    const size_t pathSize = strlen(path) + 1;
    char *pathCopy = result == ANO_RESOURCE_OK
        ? ano::memory_region_allocate<char>(cooker.activeRegion, pathSize)
        : nullptr;
    if (result == ANO_RESOURCE_OK && pathCopy == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    if (result != ANO_RESOURCE_OK)
        return result;
    memcpy(pathCopy, path, pathSize);
    AcquiredFile& acquired = cooker.activeFiles[cooker.activeFileCount];
    acquired.path = pathCopy;
    result = read_file(cooker, acquired);
    if (result == ANO_RESOURCE_OK) {
        ++cooker.activeFileCount;
        *output = &acquired;
    }
    return result;
}

AnoContentId snapshot_identity(const AcquiredFile *files, uint64_t count)
{
    ano::detail::Sha256 hash;
    ano::detail::hash_u64(hash, count);
    for (uint64_t i = 0; i < count; ++i) {
        const size_t pathLength = strlen(files[i].path);
        ano::detail::hash_u64(hash, pathLength);
        hash.append(reinterpret_cast<const uint8_t *>(files[i].path),
                    pathLength);
        ano::detail::hash_u64(hash, files[i].size);
        hash.append(files[i].content.bytes, sizeof(files[i].content.bytes));
    }
    return hash.finish();
}

void clear_active_import(AnoResourceCooker& cooker)
{
    for (uint64_t i = 0; i < cooker.activeFileCount; ++i)
        ano::memory_volume_release(cooker.activeFiles[i].volume);
    ano::memory_region_destroy(cooker.activeRegion);
    cooker.activeRegion = nullptr;
    cooker.activeFiles = nullptr;
    cooker.activeFileCount = 0;
    cooker.activeFileCapacity = 0;
    cooker.activeAssets = nullptr;
    cooker.activeAssetCount = 0;
    cooker.activeAssetCapacity = 0;
    cooker.activeSource = {};
    cooker.activeProducer = 0;
    cooker.activeRootChanged = false;
    cooker.activeFullRebuild = false;
}

bool pending_asset(const AnoResourceCooker& cooker, AnoAssetId asset)
{
    return asset.value < cooker.pendingIndexCapacity
        && cooker.pendingIndex[asset.value] != 0;
}

void release_pending(AnoResourceCooker& cooker)
{
    for (uint64_t i = 0; i < cooker.itemCount; ++i)
        cooker.pendingIndex[cooker.items[i].asset.value] = 0;
    for (uint64_t i = 0; i < cooker.pendingVolumeCount; ++i)
        ano::memory_volume_release(cooker.pendingVolumes[i]);
    cooker.itemCount = 0;
    cooker.pendingVolumeCount = 0;
}

AnoResourceError reserve_pending_index(AnoResourceCooker& cooker,
                                       AnoAssetId asset)
{
    if (asset.value == UINT64_MAX)
        return ANO_RESOURCE_OVERFLOW;
    const uint64_t required = asset.value + 1;
    if (required <= cooker.pendingIndexCapacity)
        return ANO_RESOURCE_OK;
    uint64_t capacity = cooker.pendingIndexCapacity == 0
        ? 16 : cooker.pendingIndexCapacity;
    while (capacity < required) {
        if (capacity > UINT64_MAX / 2)
            return ANO_RESOURCE_OVERFLOW;
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(uint64_t))
        return ANO_RESOURCE_OVERFLOW;
    void *grown = mi_reallocn(
        cooker.pendingIndex, static_cast<size_t>(capacity),
        sizeof(uint64_t));
    if (grown == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    cooker.pendingIndex = static_cast<uint64_t *>(grown);
    memset(cooker.pendingIndex + cooker.pendingIndexCapacity, 0,
           static_cast<size_t>(capacity - cooker.pendingIndexCapacity)
               * sizeof(uint64_t));
    cooker.pendingIndexCapacity = capacity;
    return ANO_RESOURCE_OK;
}

AnoResourceError reserve_pending_items(AnoResourceCooker& cooker,
                                       uint64_t additional)
{
    uint64_t required = 0;
    if (!ano::detail::checked_add(cooker.itemCount, additional, &required)
        || required > SIZE_MAX / sizeof(AnoResourcePackItem))
        return ANO_RESOURCE_OVERFLOW;
    if (required <= cooker.itemCapacity)
        return ANO_RESOURCE_OK;
    uint64_t capacity = cooker.itemCapacity == 0 ? 16 : cooker.itemCapacity;
    while (capacity < required) {
        if (capacity > UINT64_MAX / 2)
            return ANO_RESOURCE_OVERFLOW;
        capacity *= 2;
    }
    void *grown = mi_reallocn(cooker.items, static_cast<size_t>(capacity),
                              sizeof(AnoResourcePackItem));
    if (grown == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    cooker.items = static_cast<AnoResourcePackItem *>(grown);
    cooker.itemCapacity = capacity;
    return ANO_RESOURCE_OK;
}

AnoResourceError reserve_pending_volumes(AnoResourceCooker& cooker,
                                         uint64_t additional)
{
    uint64_t required = 0;
    if (!ano::detail::checked_add(
            cooker.pendingVolumeCount, additional, &required)
        || required > SIZE_MAX / sizeof(ano::MemoryVolume *))
        return ANO_RESOURCE_OVERFLOW;
    if (required <= cooker.pendingVolumeCapacity)
        return ANO_RESOURCE_OK;
    uint64_t capacity = cooker.pendingVolumeCapacity == 0
        ? 8 : cooker.pendingVolumeCapacity;
    while (capacity < required) {
        if (capacity > UINT64_MAX / 2)
            return ANO_RESOURCE_OVERFLOW;
        capacity *= 2;
    }
    void *grown = mi_reallocn(
        cooker.pendingVolumes, static_cast<size_t>(capacity),
        sizeof(ano::MemoryVolume *));
    if (grown == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    cooker.pendingVolumes = static_cast<ano::MemoryVolume **>(grown);
    cooker.pendingVolumeCapacity = capacity;
    return ANO_RESOURCE_OK;
}

CookNode *find_node(AnoResourceCooker& cooker, AnoAssetId asset)
{
    return asset.value < cooker.nodeIndexCapacity
        ? cooker.nodeIndex[asset.value] : nullptr;
}

AnoResourceError reserve_node_index(AnoResourceCooker& cooker,
                                    uint64_t assetCount)
{
    if (assetCount == UINT64_MAX)
        return ANO_RESOURCE_OVERFLOW;
    const uint64_t required = assetCount + 1;
    if (required <= cooker.nodeIndexCapacity)
        return ANO_RESOURCE_OK;
    uint64_t capacity = cooker.nodeIndexCapacity == 0
        ? 16 : cooker.nodeIndexCapacity;
    while (capacity < required) {
        if (capacity > UINT64_MAX / 2)
            return ANO_RESOURCE_OVERFLOW;
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(CookNode *))
        return ANO_RESOURCE_OVERFLOW;
    void *grown = mi_reallocn(
        cooker.nodeIndex, static_cast<size_t>(capacity), sizeof(CookNode *));
    if (grown == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    cooker.nodeIndex = static_cast<CookNode **>(grown);
    memset(cooker.nodeIndex + cooker.nodeIndexCapacity, 0,
           static_cast<size_t>(capacity - cooker.nodeIndexCapacity)
               * sizeof(CookNode *));
    cooker.nodeIndexCapacity = capacity;
    return ANO_RESOURCE_OK;
}

const AnoResourceRevisionItem *previous_item(
    const AnoCookedRevision *revision, AnoAssetId asset)
{
    const AnoResourceRevisionItem *item = nullptr;
    return ano_resource_revision_item(revision, asset, &item)
            == ANO_RESOURCE_OK
        ? item : nullptr;
}

bool source_replaced(const AnoResourceCooker& cooker,
                     AnoResourceSourceId source)
{
    const SourceBinding *binding = find_source(&cooker, source);
    return binding != nullptr && binding->candidateReplaceAll;
}

AnoContentId action_key(uint64_t producer, AnoResourceTypeId type,
                        AnoResourceCommitGroupId commitGroup,
                        const AnoSchemaFingerprint& schema,
                        const AnoContentId& input)
{
    ano::detail::Sha256 hash;
    hash.append("anoptic.resource.action.v2");
    ano::detail::hash_u64(hash, producer);
    ano::detail::hash_u64(hash, type.value);
    ano::detail::hash_u64(hash, commitGroup.value);
    hash.append(schema.bytes, sizeof(schema.bytes));
    hash.append(input.bytes, sizeof(input.bytes));
    return hash.finish();
}

bool dependency_arrays_equal(const AnoCookedRevision *base,
                             const WorkItem& work)
{
    if (work.previous == nullptr
        || work.previous->dependencyCount != work.dependencyCount)
        return false;
    if (work.dependencyCount == 0)
        return true;
    const AnoResourceDependency *old = base->dependencies
        + work.previous->dependencyFirst;
    return memcmp(old, work.dependencies,
                  static_cast<size_t>(work.dependencyCount
                                      * sizeof(AnoResourceDependency))) == 0;
}

void finish_transaction(AnoResourceCooker& cooker, bool publishSources = false)
{
    release_pending(cooker);
    for (SourceBinding& source : cooker.sources) {
        if (publishSources && source.candidateValid) {
            release_source_files(source.files, source.fileCount);
            mi_free(source.assets);
            source.files = source.candidateFiles;
            source.fileCount = source.candidateFileCount;
            source.assets = source.candidateAssets;
            source.assetCount = source.candidateAssetCount;
            source.snapshotId = source.candidateSnapshotId;
            source.rootId = source.candidateRootId;
            source.snapshotValid = true;
            source.rootValid = true;
            source.pathChanged = false;
            source.candidateFiles = nullptr;
            source.candidateFileCount = 0;
            source.candidateAssets = nullptr;
            source.candidateAssetCount = 0;
            source.candidateValid = false;
            source.candidateReplaceAll = false;
        } else {
            discard_source_candidate(source);
        }
    }
    clear_active_import(cooker);
}

} // namespace

extern "C" AnoResourceError ano_resource_cooker_create(
    AnoResourceCookerConfig config, AnoResourceCooker **output)
{
    if (output == nullptr || config.firstDerivedAsset.value < 2)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *output = nullptr;
    void *storage = mi_zalloc(sizeof(AnoResourceCooker));
    if (storage == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    AnoResourceCooker *cooker = ::new (storage) AnoResourceCooker{};
    cooker->firstDerivedAsset = config.firstDerivedAsset;
    cooker->nextDerivedAsset = config.firstDerivedAsset;
    atomic_init(&cooker->cancelled, false);
    cooker->pendingMetadata = ano::memory_region_create();
    const AnoResourceError executor = cooker->pendingMetadata == nullptr
        ? ANO_RESOURCE_OUT_OF_MEMORY
        : ano::resource_detail::executor_create(
            config.workerCount, &cooker->executor);
    if (executor != ANO_RESOURCE_OK) {
        ano::memory_region_destroy(cooker->pendingMetadata);
        cooker->~AnoResourceCooker();
        mi_free(cooker);
        return executor;
    }
    *output = cooker;
    return ANO_RESOURCE_OK;
}

extern "C" void ano_resource_cooker_destroy(AnoResourceCooker *cooker)
{
    if (cooker == nullptr)
        return;
    ano::resource_detail::executor_destroy(cooker->executor);
    finish_transaction(*cooker);
    ano::memory_region_destroy(cooker->pendingMetadata);
    for (SourceBinding& source : cooker->sources) {
        release_source_files(source.files, source.fileCount);
        mi_free(source.assets);
        mi_free(source.path);
    }
    mi_free(cooker->items);
    mi_free(cooker->pendingVolumes);
    mi_free(cooker->pendingIndex);
    mi_free(cooker->nodeIndex);
    ano_resource_revision_release(cooker->current);
    cooker->~AnoResourceCooker();
    mi_free(cooker);
}

extern "C" AnoResourceError ano_resource_cooker_begin(
    AnoResourceCooker *cooker)
{
    if (cooker == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    finish_transaction(*cooker);
    if (!ano::memory_region_reset(cooker->pendingMetadata))
        return ANO_RESOURCE_OUT_OF_MEMORY;
    cooker->nextDerivedAsset = cooker->firstDerivedAsset;
    cooker->executedActions = 0;
    cooker->allocatedArtifacts = 0;
    cooker->rebuildTail = false;
    atomic_store_explicit(&cooker->cancelled, false, memory_order_release);
    return ANO_RESOURCE_OK;
}

extern "C" AnoResourceError ano_resource_source_bind(
    AnoResourceCooker *cooker, AnoResourceSourceId source, const char *path)
{
    if (cooker == nullptr || source.value == 0 || path == nullptr
        || path[0] == '\0')
        return ANO_RESOURCE_INVALID_ARGUMENT;
    if (ano_resource_cooker_cancelled(cooker))
        return ANO_RESOURCE_CANCELLED;
    char *copy = copy_string(path);
    if (copy == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    if (SourceBinding *existing = find_source(cooker, source)) {
        if (strcmp(existing->path, path) == 0) {
            mi_free(copy);
            return ANO_RESOURCE_OK;
        }
        discard_source_candidate(*existing);
        mi_free(existing->path);
        existing->path = copy;
        existing->pathChanged = true;
        return ANO_RESOURCE_OK;
    }
    cooker->sources.insert(SourceBinding{
        .source = source,
        .path = copy,
    });
    return ANO_RESOURCE_OK;
}

extern "C" AnoResourceError ano_resource_cook(
    AnoResourceCooker *cooker, const AnoCookedRevision **output)
{
    if (cooker == nullptr || output == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *output = nullptr;
    if (ano_resource_cooker_cancelled(cooker))
        return ANO_RESOURCE_CANCELLED;

    uint64_t baseCount = cooker->current == nullptr
        ? 0 : cooker->current->itemCount;
    uint64_t carryCount = 0;
    for (uint64_t i = 0; i < baseCount; ++i) {
        const AnoResourceRevisionItem& item = cooker->current->items[i];
        if (!source_replaced(*cooker, item.source)
            && !pending_asset(*cooker, item.asset))
            ++carryCount;
    }
    uint64_t workCount = 0;
    if (!ano::detail::checked_add(
            carryCount, cooker->itemCount, &workCount)
        || workCount > SIZE_MAX / sizeof(WorkItem))
        return ANO_RESOURCE_OVERFLOW;
    ano::MemoryRegion *transaction = ano::memory_region_create();
    if (transaction == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    WorkItem *work = workCount == 0 ? nullptr
        : ano::memory_region_allocate_zero<WorkItem>(
            transaction, static_cast<size_t>(workCount));
    if (workCount != 0 && work == nullptr) {
        ano::memory_region_destroy(transaction);
        return ANO_RESOURCE_OUT_OF_MEMORY;
    }

    uint64_t cursor = 0;
    for (uint64_t i = 0; i < baseCount; ++i) {
        const AnoResourceRevisionItem& item = cooker->current->items[i];
        if (source_replaced(*cooker, item.source)
            || pending_asset(*cooker, item.asset))
            continue;
        AnoResourceBytes bytes{};
        if (ano_resource_revision_resolve(
                cooker->current, item.asset, item.type, &bytes)
            != ANO_RESOURCE_OK) {
            ano::memory_region_destroy(transaction);
            return ANO_RESOURCE_BAD_MANIFEST;
        }
        work[cursor++] = {
            .source = {
                .asset = item.asset,
                .source = item.source,
                .type = item.type,
                .producer = item.producer,
                .inputIdentity = item.inputIdentity,
                .commitGroup = item.commitGroup,
                .artifact = bytes,
            },
            .previous = &item,
            .dependencies = item.dependencyCount == 0 ? nullptr
                : cooker->current->dependencies + item.dependencyFirst,
            .dependencyCount = item.dependencyCount,
            .schema = {.type = item.type, .fingerprint = item.schema},
            .content = item.content,
            .volume = item.volume,
        };
    }
    for (uint64_t i = 0; i < cooker->itemCount; ++i) {
        WorkItem& item = work[cursor++];
        item.source = cooker->items[i];
        item.previous = previous_item(cooker->current, item.source.asset);
        item.dependencies = item.source.dependencies;
        item.dependencyCount = item.source.dependencyCount;
        item.schema = item.source.schema;
        item.content = item.source.content;
        item.pending = true;
    }
    if (workCount != 0)
        qsort(work, static_cast<size_t>(workCount), sizeof(*work),
              compare_pack_items);
    for (uint64_t i = 0; i < workCount; ++i)
        if (work[i].source.asset.value != i + 1) {
            const AnoResourceError error = i != 0
                    && work[i].source.asset.value
                        == work[i - 1].source.asset.value
                ? ANO_RESOURCE_DUPLICATE_ASSET : ANO_RESOURCE_BAD_MANIFEST;
            ano::memory_region_destroy(transaction);
            return error;
        }

    AnoResourceError result = reserve_node_index(*cooker, workCount);
    cooker->executedActions = cooker->itemCount;
    uint64_t dependencyCount = 0;
    for (uint64_t i = 0; i < workCount && result == ANO_RESOURCE_OK; ++i) {
        if (!ano::detail::checked_add(
                dependencyCount, work[i].dependencyCount, &dependencyCount))
            result = ANO_RESOURCE_OVERFLOW;
    }
    if (result == ANO_RESOURCE_OK && ano_resource_cooker_cancelled(cooker))
        result = ANO_RESOURCE_CANCELLED;
    if (result != ANO_RESOURCE_OK) {
        ano::memory_region_destroy(transaction);
        return result;
    }

    bool anyChanged = workCount != baseCount;
    for (uint64_t i = 0; i < workCount; ++i) {
        WorkItem& item = work[i];
        const AnoContentId& input = item.source.source.value == 0
            ? item.content : item.source.inputIdentity;
        item.action = action_key(
            item.source.producer, item.source.type, item.source.commitGroup,
            item.schema.fingerprint, input);
        const bool sameArtifact = item.previous != nullptr
            && item.previous->type.value == item.source.type.value
            && content_equal(item.previous->content, item.content);
        const bool same = sameArtifact
            && item.previous->producer == item.source.producer
            && content_equal(item.previous->inputIdentity,
                             item.source.inputIdentity)
            && item.previous->source.value == item.source.source.value
            && item.previous->commitGroup.value
                == item.source.commitGroup.value
            && dependency_arrays_equal(cooker->current, item);
        item.changed = !same;
        item.artifactChanged = !sameArtifact;
        item.affected = item.artifactChanged;
        anyChanged |= item.changed;
    }
    uint64_t *reverseOffsets = ano::memory_region_allocate_zero<uint64_t>(
        transaction, static_cast<size_t>(workCount + 1));
    uint64_t *reverseCursors = workCount == 0 ? nullptr
        : ano::memory_region_allocate_zero<uint64_t>(
            transaction, static_cast<size_t>(workCount));
    uint64_t *reverseEdges = dependencyCount == 0 ? nullptr
        : ano::memory_region_allocate<uint64_t>(
            transaction, static_cast<size_t>(dependencyCount));
    uint64_t *queue = workCount == 0 ? nullptr
        : ano::memory_region_allocate<uint64_t>(
            transaction, static_cast<size_t>(workCount));
    if (reverseOffsets == nullptr
        || (workCount != 0 && (reverseCursors == nullptr || queue == nullptr))
        || (dependencyCount != 0 && reverseEdges == nullptr))
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    for (uint64_t dependent = 0;
         dependent < workCount && result == ANO_RESOURCE_OK; ++dependent)
        for (uint64_t d = 0; d < work[dependent].dependencyCount; ++d) {
            const AnoResourceDependency& dependency =
                work[dependent].dependencies[d];
            if (dependency.asset.value == 0
                || dependency.asset.value > workCount
                || work[dependency.asset.value - 1].source.type.value
                    != dependency.type.value
                || reverseOffsets[dependency.asset.value] == UINT64_MAX) {
                result = ANO_RESOURCE_BAD_MANIFEST;
                break;
            }
            ++reverseOffsets[dependency.asset.value];
        }
    for (uint64_t i = 1; i <= workCount && result == ANO_RESOURCE_OK; ++i)
        if (!ano::detail::checked_add(
                reverseOffsets[i - 1], reverseOffsets[i],
                &reverseOffsets[i]))
            result = ANO_RESOURCE_OVERFLOW;
    for (uint64_t dependent = 0;
         dependent < workCount && result == ANO_RESOURCE_OK; ++dependent)
        for (uint64_t d = 0; d < work[dependent].dependencyCount; ++d) {
            const uint64_t target =
                work[dependent].dependencies[d].asset.value - 1;
            reverseEdges[reverseOffsets[target] + reverseCursors[target]++] =
                dependent;
        }
    uint64_t queueFirst = 0;
    uint64_t queueCount = 0;
    for (uint64_t i = 0; i < workCount; ++i)
        if (work[i].affected)
            queue[queueCount++] = i;
    while (queueFirst < queueCount) {
        const uint64_t changed = queue[queueFirst++];
        for (uint64_t edge = reverseOffsets[changed];
             edge < reverseOffsets[changed + 1]; ++edge) {
            const uint64_t dependent = reverseEdges[edge];
            if (!work[dependent].affected) {
                work[dependent].affected = true;
                queue[queueCount++] = dependent;
            }
        }
    }
    if (result != ANO_RESOURCE_OK) {
        ano::memory_region_destroy(transaction);
        return result;
    }

    if (!anyChanged && cooker->current != nullptr) {
        for (uint64_t i = 0; i < workCount; ++i) {
            CookNode *node = find_node(*cooker, work[i].source.asset);
            if (node != nullptr) {
                node->action = work[i].action;
                node->dirty = false;
            }
        }
        result = ano_resource_revision_retain(cooker->current);
        if (result == ANO_RESOURCE_OK)
            *output = cooker->current;
        finish_transaction(*cooker, true);
        ano::memory_region_destroy(transaction);
        return result;
    }

    ano::MemoryVolume **artifactVolumes = workCount == 0 ? nullptr
        : ano::memory_region_allocate_zero<ano::MemoryVolume *>(
            transaction, static_cast<size_t>(workCount));
    if (workCount != 0 && artifactVolumes == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    uint64_t artifactVolumeCount = 0;
    uint64_t changedCount = 0;
    for (uint64_t i = 0; i < workCount && result == ANO_RESOURCE_OK; ++i) {
        WorkItem& item = work[i];
        if (item.artifactChanged) {
            item.volume = item.source.volume;
            item.artifact = item.source.reservation;
            ++changedCount;
        } else {
            item.volume = item.previous->volume;
            item.artifact = item.previous->artifact;
        }
        bool known = false;
        for (uint64_t v = 0; v < artifactVolumeCount; ++v)
            known |= artifactVolumes[v] == item.volume;
        if (!known)
            artifactVolumes[artifactVolumeCount++] = item.volume;
    }

    RevisionPlan plan{
        .items = {.count = static_cast<size_t>(workCount)},
        .dependencies = {.count = static_cast<size_t>(dependencyCount)},
        .volumes = {.count = static_cast<size_t>(artifactVolumeCount + 1)},
    };
    const ano::MemoryLayoutCursor layout = ano::memory_layout(plan);
    ano::MemoryVolume *metadata = result == ANO_RESOURCE_OK
        ? ano::memory_volume_create(layout) : nullptr;
    if (result == ANO_RESOURCE_OK && metadata == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    auto items = result == ANO_RESOURCE_OK
        ? ano::memory_volume_write(metadata, plan.items)
        : ano::MemorySpan<AnoResourceRevisionItem>{};
    auto dependencies = result == ANO_RESOURCE_OK
        ? ano::memory_volume_write(metadata, plan.dependencies)
        : ano::MemorySpan<AnoResourceDependency>{};
    auto volumes = result == ANO_RESOURCE_OK
        ? ano::memory_volume_write(metadata, plan.volumes)
        : ano::MemorySpan<ano::MemoryVolume *>{};
    uint64_t retainedVolumeCount = 0;
    if (result == ANO_RESOURCE_OK) {
        volumes[0] = metadata;
        for (uint64_t v = 0; v < artifactVolumeCount; ++v) {
            if (!ano::memory_volume_retain(artifactVolumes[v])) {
                result = ANO_RESOURCE_OVERFLOW;
                break;
            }
            volumes[v + 1] = artifactVolumes[v];
            ++retainedVolumeCount;
        }
    }

    uint64_t dependencyCursor = 0;
    for (uint64_t i = 0; i < workCount && result == ANO_RESOURCE_OK; ++i) {
        WorkItem& source = work[i];
        items[i] = {
            .asset = source.source.asset,
            .source = source.source.source,
            .type = source.source.type,
            .producer = source.source.producer,
            .inputIdentity = source.source.inputIdentity,
            .commitGroup = source.source.commitGroup,
            .schema = source.schema.fingerprint,
            .content = source.content,
            .dependencyFirst = dependencyCursor,
            .dependencyCount = source.dependencyCount,
            .volume = source.volume,
            .artifact = source.artifact,
        };
        if (source.dependencyCount != 0)
            memcpy(dependencies.data() + dependencyCursor,
                   source.dependencies,
                   static_cast<size_t>(source.dependencyCount
                                       * sizeof(AnoResourceDependency)));
        dependencyCursor += source.dependencyCount;
    }
    if (result == ANO_RESOURCE_OK && ano_resource_cooker_cancelled(cooker))
        result = ANO_RESOURCE_CANCELLED;
    if (result == ANO_RESOURCE_OK && !ano::memory_volume_seal(metadata))
        result = ANO_RESOURCE_OUT_OF_MEMORY;

    AnoCookedRevision *revision = nullptr;
    if (result == ANO_RESOURCE_OK) {
        revision = mi_zalloc_tp(AnoCookedRevision);
        if (revision == nullptr)
            result = ANO_RESOURCE_OUT_OF_MEMORY;
    }
    if (result == ANO_RESOURCE_OK) {
        __atomic_store_n(&revision->references, size_t{1}, __ATOMIC_RELAXED);
        revision->items = items.data();
        revision->itemCount = workCount;
        revision->dependencies = dependencies.data();
        revision->dependencyCount = dependencyCount;
        revision->volumes = volumes.data();
        revision->volumeCount = artifactVolumeCount + 1;
        result = ano::resource_detail::validate_dependency_graph(
            revision->items, revision->itemCount, revision->dependencies,
            revision->dependencyCount);
        revision->validated = result == ANO_RESOURCE_OK;
    }
    if (result != ANO_RESOURCE_OK) {
        for (uint64_t v = 0; v < retainedVolumeCount; ++v)
            ano::memory_volume_release(volumes[v + 1]);
        ano::memory_volume_release(metadata);
        mi_free(revision);
        ano::memory_region_destroy(transaction);
        return result;
    }

    for (CookNode& node : cooker->nodes)
        node.present = false;
    for (uint64_t i = 0; i < workCount; ++i) {
        CookNode *node = find_node(*cooker, work[i].source.asset);
        if (node == nullptr) {
            const auto inserted = cooker->nodes.insert(CookNode{});
            node = &*inserted;
            node->asset = work[i].source.asset;
            cooker->nodeIndex[node->asset.value] = node;
        }
        node->source = work[i].source.source;
        node->type = work[i].source.type;
        node->producer = work[i].source.producer;
        node->action = work[i].action;
        node->result = work[i].content;
        node->present = true;
        node->dirty = work[i].affected;
    }
    cooker->allocatedArtifacts = changedCount;
    ano_resource_revision_release(cooker->current);
    cooker->current = revision;
    result = ano_resource_revision_retain(revision);
    if (result == ANO_RESOURCE_OK)
        *output = revision;
    finish_transaction(*cooker, true);
    ano::memory_region_destroy(transaction);
    return result;
}

extern "C" AnoResourceError ano_resource_revision_retain(
    const AnoCookedRevision *revision)
{
    if (revision == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    size_t references = __atomic_load_n(
        &revision->references, __ATOMIC_RELAXED);
    do {
        if (references == 0 || references == SIZE_MAX)
            return ANO_RESOURCE_OVERFLOW;
    } while (!__atomic_compare_exchange_n(
        &const_cast<AnoCookedRevision *>(revision)->references,
        &references, references + 1, true,
        __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    return ANO_RESOURCE_OK;
}

extern "C" void ano_resource_revision_release(
    const AnoCookedRevision *constant)
{
    if (constant == nullptr)
        return;
    auto *revision = const_cast<AnoCookedRevision *>(constant);
    if (__atomic_fetch_sub(&revision->references, size_t{1},
                           __ATOMIC_ACQ_REL) != 1)
        return;
    for (uint64_t i = revision->volumeCount; i > 1; --i)
        ano::memory_volume_release(revision->volumes[i - 1]);
    ano::memory_volume_release(revision->volumes[0]);
    mi_free(revision);
}

extern "C" uint64_t ano_resource_revision_asset_count(
    const AnoCookedRevision *revision)
{
    return revision == nullptr ? 0 : revision->itemCount;
}

AnoResourceError ano_resource_revision_item(
    const AnoCookedRevision *revision, AnoAssetId asset,
    const AnoResourceRevisionItem **item)
{
    if (revision == nullptr || item == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *item = nullptr;
    if (asset.value == 0 || asset.value > revision->itemCount)
        return ANO_RESOURCE_NOT_FOUND;
    const AnoResourceRevisionItem& found = revision->items[asset.value - 1];
    if (found.asset.value != asset.value)
        return ANO_RESOURCE_BAD_MANIFEST;
    *item = &found;
    return ANO_RESOURCE_OK;
}

AnoResourceError ano_resource_revision_dependencies(
    const AnoCookedRevision *revision, AnoAssetId asset,
    const AnoResourceDependency **dependencies, uint64_t *count)
{
    if (dependencies == nullptr || count == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *dependencies = nullptr;
    *count = 0;
    const AnoResourceRevisionItem *item = nullptr;
    const AnoResourceError found = ano_resource_revision_item(
        revision, asset, &item);
    if (found != ANO_RESOURCE_OK)
        return found;
    *dependencies = item->dependencyCount == 0 ? nullptr
        : revision->dependencies + item->dependencyFirst;
    *count = item->dependencyCount;
    return ANO_RESOURCE_OK;
}

extern "C" AnoResourceError ano_resource_revision_resolve(
    const AnoCookedRevision *revision, AnoAssetId asset,
    AnoResourceTypeId requiredType, AnoResourceBytes *bytes)
{
    if (bytes == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *bytes = {};
    const AnoResourceRevisionItem *item = nullptr;
    const AnoResourceError found = ano_resource_revision_item(
        revision, asset, &item);
    if (found != ANO_RESOURCE_OK)
        return found;
    if (item->type.value != requiredType.value)
        return ANO_RESOURCE_TYPE_MISMATCH;
    ano::MemoryView view{};
    if (!ano::memory_volume_view(item->volume, item->artifact, view))
        return ANO_RESOURCE_BAD_MANIFEST;
    *bytes = {
        .data = static_cast<const uint8_t *>(view.data),
        .size = view.size,
    };
    return ANO_RESOURCE_OK;
}

AnoResourceError ano_resource_revision_validate(
    const AnoCookedRevision *revision)
{
    return revision != nullptr && revision->validated
        ? ANO_RESOURCE_OK : ANO_RESOURCE_BAD_MANIFEST;
}

extern "C" AnoResourceError ano_resource_revision_export_pack(
    const AnoCookedRevision *revision, AnoResourceMutableBytes *pack)
{
    return ano_resource_pack_build(revision, pack);
}

extern "C" void ano_resource_exported_pack_release(
    AnoResourceMutableBytes pack)
{
    mi_free(pack.data);
}

extern "C" uint64_t ano_resource_cooker_executed_actions(
    const AnoResourceCooker *cooker)
{
    return cooker == nullptr ? 0 : cooker->executedActions;
}

extern "C" uint64_t ano_resource_cooker_allocated_artifacts(
    const AnoResourceCooker *cooker)
{
    return cooker == nullptr ? 0 : cooker->allocatedArtifacts;
}

extern "C" AnoResourceError ano_resource_cooker_current_resolve(
    const AnoResourceCooker *cooker, AnoAssetId asset,
    AnoResourceTypeId requiredType, AnoResourceBytes *bytes)
{
    if (cooker == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    return ano_resource_revision_resolve(
        cooker->current, asset, requiredType, bytes);
}

extern "C" void ano_resource_cooker_cancel(AnoResourceCooker *cooker)
{
    if (cooker != nullptr)
        atomic_store_explicit(&cooker->cancelled, true, memory_order_release);
}

AnoResourceCookCheckpoint ano_resource_cooker_checkpoint(
    const AnoResourceCooker *cooker)
{
    return cooker == nullptr ? AnoResourceCookCheckpoint{}
        : AnoResourceCookCheckpoint{
              .itemCount = cooker->itemCount,
              .volumeCount = cooker->pendingVolumeCount,
              .nextDerivedAsset = cooker->nextDerivedAsset,
          };
}

void ano_resource_cooker_rollback(AnoResourceCooker *cooker,
                                  AnoResourceCookCheckpoint checkpoint)
{
    if (cooker == nullptr || checkpoint.itemCount > cooker->itemCount
        || checkpoint.volumeCount > cooker->pendingVolumeCount)
        return;
    for (uint64_t i = checkpoint.volumeCount;
         i < cooker->pendingVolumeCount; ++i) {
        ano::memory_volume_release(cooker->pendingVolumes[i]);
        cooker->pendingVolumes[i] = nullptr;
    }
    for (uint64_t i = checkpoint.itemCount; i < cooker->itemCount; ++i) {
        cooker->pendingIndex[cooker->items[i].asset.value] = 0;
        cooker->items[i] = {};
    }
    cooker->itemCount = checkpoint.itemCount;
    cooker->pendingVolumeCount = checkpoint.volumeCount;
    cooker->nextDerivedAsset = checkpoint.nextDerivedAsset;
}

bool ano_resource_cooker_root_valid(const AnoResourceCooker *cooker,
                                    AnoAssetId root)
{
    return cooker != nullptr && root.value != 0
        && root.value < cooker->firstDerivedAsset.value
        && !pending_asset(*cooker, root);
}

const char *ano_resource_cooker_source_path(
    const AnoResourceCooker *cooker, AnoResourceSourceId source)
{
    const SourceBinding *binding = find_source(cooker, source);
    return binding == nullptr ? nullptr : binding->path;
}

AnoResourceError ano_resource_cooker_action_required(
    AnoResourceCooker *cooker, AnoAssetId asset, AnoResourceTypeId type,
    AnoResourceCommitGroupId commitGroup, AnoContentId inputIdentity,
    bool *required)
{
    if (cooker == nullptr || required == nullptr || asset.value == 0
        || type.value == 0 || commitGroup.value == 0
        || cooker->activeSource.value == 0 || cooker->activeRegion == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *required = true;
    for (uint64_t i = 0; i < cooker->activeAssetCount; ++i)
        if (cooker->activeAssets[i].value == asset.value)
            return ANO_RESOURCE_DUPLICATE_ASSET;
    if (cooker->activeAssetCount == cooker->activeAssetCapacity) {
        const uint64_t capacity = cooker->activeAssetCapacity == 0
            ? 16 : cooker->activeAssetCapacity * 2;
        if (capacity < cooker->activeAssetCapacity
            || capacity > SIZE_MAX / sizeof(AnoAssetId))
            return ANO_RESOURCE_OVERFLOW;
        AnoAssetId *grown = ano::memory_region_allocate<AnoAssetId>(
            cooker->activeRegion, static_cast<size_t>(capacity));
        if (grown == nullptr)
            return ANO_RESOURCE_OUT_OF_MEMORY;
        if (cooker->activeAssetCount != 0)
            memcpy(grown, cooker->activeAssets,
                   static_cast<size_t>(cooker->activeAssetCount)
                       * sizeof(AnoAssetId));
        cooker->activeAssets = grown;
        cooker->activeAssetCapacity = capacity;
    }
    cooker->activeAssets[cooker->activeAssetCount++] = asset;

    AnoResourceSchema schema{};
    const AnoResourceError described = ano_resource_artifact_schema(
        type, &schema);
    if (described != ANO_RESOURCE_OK)
        return described;
    const uint64_t producer = cooker->activeProducer != 0
        ? cooker->activeProducer : type.value;
    const AnoContentId expected = action_key(
        producer, type, commitGroup, schema.fingerprint, inputIdentity);
    const CookNode *node = find_node(*cooker, asset);
    *required = node == nullptr || !node->present
        || node->source.value != cooker->activeSource.value
        || node->type.value != type.value || node->producer != producer
        || !content_equal(node->action, expected);
    return ANO_RESOURCE_OK;
}

AnoResourceError ano_resource_cooker_allocate_derived(
    AnoResourceCooker *cooker, AnoAssetId *asset)
{
    if (cooker == nullptr || asset == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    if (ano_resource_cooker_cancelled(cooker))
        return ANO_RESOURCE_CANCELLED;
    while (pending_asset(*cooker, cooker->nextDerivedAsset)) {
        if (cooker->nextDerivedAsset.value == UINT64_MAX)
            return ANO_RESOURCE_OVERFLOW;
        ++cooker->nextDerivedAsset.value;
    }
    *asset = cooker->nextDerivedAsset;
    if (cooker->nextDerivedAsset.value == UINT64_MAX)
        return ANO_RESOURCE_OVERFLOW;
    ++cooker->nextDerivedAsset.value;
    return ANO_RESOURCE_OK;
}

AnoResourceError ano_resource_cooker_encode_batch(
    AnoResourceCooker *cooker, AnoResourceCookArtifact *artifacts,
    uint64_t count)
{
    if (cooker == nullptr || (count != 0 && artifacts == nullptr))
        return ANO_RESOURCE_INVALID_ARGUMENT;
    if (count == 0)
        return ANO_RESOURCE_OK;
    if (ano_resource_cooker_cancelled(cooker))
        return ANO_RESOURCE_CANCELLED;
    if (count > SIZE_MAX / sizeof(EncodeWork))
        return ANO_RESOURCE_OVERFLOW;
    AnoAssetId maximumAsset{};
    for (uint64_t i = 0; i < count; ++i) {
        const AnoResourceCookArtifact& artifact = artifacts[i];
        if (artifact.asset.value == 0 || artifact.type.value == 0
            || artifact.commitGroup.value == 0 || artifact.encodedSize == 0
            || artifact.encodedSize > SIZE_MAX || artifact.encode == nullptr)
            return ANO_RESOURCE_INVALID_ARGUMENT;
        if (pending_asset(*cooker, artifact.asset))
            return ANO_RESOURCE_DUPLICATE_ASSET;
        for (uint64_t previous = 0; previous < i; ++previous)
            if (artifacts[previous].asset.value == artifact.asset.value)
                return ANO_RESOURCE_DUPLICATE_ASSET;
        if (artifact.asset.value > maximumAsset.value)
            maximumAsset = artifact.asset;
    }
    ano::MemoryRegion *transaction = ano::memory_region_create();
    if (transaction == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    EncodeWork *work = ano::memory_region_allocate_zero<EncodeWork>(
        transaction, static_cast<size_t>(count));
    EncodeVolume *volumes = ano::memory_region_allocate_zero<EncodeVolume>(
        transaction, static_cast<size_t>(count));
    AnoResourceError result = work == nullptr || volumes == nullptr
        ? ANO_RESOURCE_OUT_OF_MEMORY : ANO_RESOURCE_OK;
    uint64_t volumeCount = 0;
    for (uint64_t i = 0; i < count && result == ANO_RESOURCE_OK; ++i) {
        work[i].artifact = &artifacts[i];
        work[i].metadata = cooker->pendingMetadata;
        const size_t bytes = static_cast<size_t>(artifacts[i].encodedSize);
        bool beginVolume = volumeCount == 0;
        if (!beginVolume) {
            const EncodeVolume& current = volumes[volumeCount - 1];
            beginVolume = current.type.value != artifacts[i].type.value
                || current.commitGroup.value
                    != artifacts[i].commitGroup.value;
            if (!beginVolume) {
                ano::MemoryLayoutCursor candidate = current.layout;
                ano::MemoryReservation reservation{};
                beginVolume = !candidate.reserve(
                                  bytes, ANO_CACHE_LINE, reservation)
                    || (current.layout.size != 0
                        && candidate.size > artifactVolumeTarget);
            }
        }
        if (beginVolume) {
            EncodeVolume& next = volumes[volumeCount++];
            next = {};
            next.type = artifacts[i].type;
            next.commitGroup = artifacts[i].commitGroup;
        }
        EncodeVolume& selected = volumes[volumeCount - 1];
        if (!selected.layout.reserve(
                bytes, ANO_CACHE_LINE, work[i].reservation))
            result = ANO_RESOURCE_OVERFLOW;
        work[i].volumeIndex = volumeCount - 1;
    }
    if (result == ANO_RESOURCE_OK)
        result = reserve_pending_items(*cooker, count);
    if (result == ANO_RESOURCE_OK)
        result = reserve_pending_volumes(*cooker, volumeCount);
    if (result == ANO_RESOURCE_OK)
        result = reserve_pending_index(*cooker, maximumAsset);
    for (uint64_t i = 0; i < volumeCount && result == ANO_RESOURCE_OK; ++i) {
        volumes[i].volume = ano::memory_volume_create(volumes[i].layout);
        if (volumes[i].volume == nullptr)
            result = ANO_RESOURCE_OUT_OF_MEMORY;
    }
    for (uint64_t i = 0; i < count && result == ANO_RESOURCE_OK; ++i) {
        work[i].volume = work[i].volumeIndex < volumeCount
            ? volumes[work[i].volumeIndex].volume : nullptr;
        if (work[i].volume == nullptr)
            result = ANO_RESOURCE_BAD_MANIFEST;
    }
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cooker_parallel(
            cooker, count, work, encode_artifact);
    for (uint64_t i = 0; i < count && result == ANO_RESOURCE_OK; ++i)
        result = work[i].result;
    if (result == ANO_RESOURCE_OK && ano_resource_cooker_cancelled(cooker))
        result = ANO_RESOURCE_CANCELLED;
    for (uint64_t i = 0; i < volumeCount && result == ANO_RESOURCE_OK; ++i)
        if (!ano::memory_volume_seal(volumes[i].volume))
            result = ANO_RESOURCE_OUT_OF_MEMORY;
    if (result == ANO_RESOURCE_OK) {
        for (uint64_t i = 0; i < volumeCount; ++i) {
            cooker->pendingVolumes[cooker->pendingVolumeCount++] =
                volumes[i].volume;
            volumes[i].volume = nullptr;
        }
        for (uint64_t i = 0; i < count; ++i) {
            const AnoResourceCookArtifact& artifact = artifacts[i];
            const uint64_t itemIndex = cooker->itemCount++;
            cooker->items[itemIndex] = {
                .asset = artifact.asset,
                .source = cooker->activeSource,
                .type = artifact.type,
                .producer = cooker->activeProducer != 0
                    ? cooker->activeProducer : artifact.type.value,
                .inputIdentity = artifact.inputIdentity,
                .commitGroup = artifact.commitGroup,
                .artifact = work[i].bytes,
                .schema = work[i].schema,
                .content = work[i].content,
                .dependencies = work[i].dependencies,
                .dependencyCount = work[i].dependencyCount,
                .volume = work[i].volume,
                .reservation = work[i].reservation,
            };
            cooker->pendingIndex[artifact.asset.value] = itemIndex + 1;
        }
    }
    for (uint64_t i = 0; i < volumeCount; ++i)
        ano::memory_volume_release(volumes[i].volume);
    ano::memory_region_destroy(transaction);
    return result;
}

extern "C" AnoResourceError ano_resource_cooker_add(
    AnoResourceCooker *cooker, AnoAssetId asset, AnoResourceTypeId type,
    AnoResourceCommitGroupId commitGroup, AnoResourceBytes artifact)
{
    if (artifact.data == nullptr || artifact.size == 0
        || artifact.size > SIZE_MAX)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    const AnoResourceError canonical = ano_resource_validate_artifact(
        type, artifact);
    if (canonical != ANO_RESOURCE_OK)
        return ANO_RESOURCE_NON_CANONICAL;
    CopyEncodeContext copy{artifact};
    AnoResourceCookArtifact input{
        .asset = asset,
        .type = type,
        .commitGroup = commitGroup,
        .encodedSize = artifact.size,
        .context = &copy,
        .encode = copy_encode,
    };
    const AnoResourceSourceId active = cooker == nullptr
        ? AnoResourceSourceId{} : cooker->activeSource;
    if (cooker != nullptr)
        cooker->activeSource = {};
    const AnoResourceError result = ano_resource_cooker_encode_batch(
        cooker, &input, 1);
    if (cooker != nullptr)
        cooker->activeSource = active;
    return result;
}

bool ano_resource_cooker_cancelled(const AnoResourceCooker *cooker)
{
    return cooker != nullptr
        && atomic_load_explicit(&cooker->cancelled, memory_order_acquire);
}

AnoResourceError ano_resource_cooker_parallel(
    AnoResourceCooker *cooker, uint64_t count, void *context,
    ano::resource_detail::ParallelFunction function)
{
    if (cooker == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    return ano::resource_detail::parallel_for(
        cooker->executor, count, context, function);
}

AnoResourceError ano_resource_cooker_import_begin(
    AnoResourceCooker *cooker, AnoResourceSourceId source, bool *execute)
{
    if (cooker == nullptr || source.value == 0 || execute == nullptr
        || cooker->activeSource.value != 0)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *execute = false;
    SourceBinding *binding = find_source(cooker, source);
    if (binding == nullptr)
        return ANO_RESOURCE_NOT_FOUND;
    cooker->activeRegion = ano::memory_region_create();
    if (cooker->activeRegion == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    cooker->activeSource = source;
    AcquiredFile *root = nullptr;
    AnoResourceError result = acquire_file(*cooker, binding->path, &root);
    cooker->activeRootChanged = result == ANO_RESOURCE_OK
        && (!binding->rootValid
            || !content_equal(binding->rootId, root->content));
    if (result == ANO_RESOURCE_OK && !cooker->activeRootChanged
        && binding->snapshotValid && !binding->pathChanged)
        for (uint64_t i = 1; i < binding->fileCount
                             && result == ANO_RESOURCE_OK; ++i) {
            AcquiredFile *dependency = nullptr;
            result = acquire_file(
                *cooker, binding->files[i].path, &dependency);
        }
    if (result != ANO_RESOURCE_OK) {
        clear_active_import(*cooker);
        return result;
    }

    const AnoContentId identity = snapshot_identity(
        cooker->activeFiles, cooker->activeFileCount);
    if (!cooker->rebuildTail && !cooker->activeRootChanged
        && binding->snapshotValid
        && content_equal(binding->snapshotId, identity)) {
        clear_active_import(*cooker);
        if (cooker->current != nullptr)
            for (uint64_t i = 0; i < cooker->current->itemCount; ++i)
                if (cooker->current->items[i].source.value == source.value
                    && cooker->current->items[i].asset.value
                        >= cooker->nextDerivedAsset.value) {
                    if (cooker->current->items[i].asset.value == UINT64_MAX)
                        return ANO_RESOURCE_OVERFLOW;
                    cooker->nextDerivedAsset.value =
                        cooker->current->items[i].asset.value + 1;
                }
        return ANO_RESOURCE_OK;
    }
    cooker->activeFullRebuild = cooker->rebuildTail
        || cooker->activeRootChanged || !binding->snapshotValid
        || binding->pathChanged;
    if (cooker->activeFullRebuild)
        cooker->rebuildTail = true;
    *execute = true;
    return ANO_RESOURCE_OK;
}

AnoResourceError ano_resource_cooker_import_end(
    AnoResourceCooker *cooker, AnoResourceSourceId source, bool success)
{
    if (cooker == nullptr || source.value == 0
        || cooker->activeSource.value != source.value)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    SourceBinding *binding = find_source(cooker, source);
    AnoResourceError result = binding == nullptr
        ? ANO_RESOURCE_NOT_FOUND : ANO_RESOURCE_OK;
    if (success && result == ANO_RESOURCE_OK
        && cooker->activeFileCount == 0)
        result = ANO_RESOURCE_BAD_MANIFEST;
    if (success && result == ANO_RESOURCE_OK
        && cooker->activeFileCount > 2)
        qsort(cooker->activeFiles + 1,
              static_cast<size_t>(cooker->activeFileCount - 1),
              sizeof(AcquiredFile), compare_acquired_files);
    if (binding != nullptr)
        discard_source_candidate(*binding);

    SourceFileState *files = success && result == ANO_RESOURCE_OK
        ? mi_calloc_tp(SourceFileState,
                       static_cast<size_t>(cooker->activeFileCount))
        : nullptr;
    if (success && result == ANO_RESOURCE_OK
        && files == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    for (uint64_t i = 0; i < cooker->activeFileCount
                         && result == ANO_RESOURCE_OK && success; ++i) {
        AcquiredFile& acquired = cooker->activeFiles[i];
        files[i].path = copy_string(acquired.path);
        if (files[i].path == nullptr) {
            result = ANO_RESOURCE_OUT_OF_MEMORY;
            break;
        }
        files[i].content = acquired.content;
        files[i].volume = acquired.volume;
        files[i].bytes = acquired.reservation;
        acquired.volume = nullptr;
    }
    const uint64_t assetCount = cooker->activeFullRebuild
        ? cooker->activeAssetCount
        : binding == nullptr ? 0 : binding->assetCount;
    AnoAssetId *assets = success && result == ANO_RESOURCE_OK
            && assetCount != 0
        ? mi_mallocn_tp(AnoAssetId, static_cast<size_t>(assetCount))
        : nullptr;
    if (success && result == ANO_RESOURCE_OK
        && assetCount != 0 && assets == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    if (result == ANO_RESOURCE_OK && assetCount != 0) {
        const AnoAssetId *source = cooker->activeFullRebuild
            ? cooker->activeAssets : binding->assets;
        memcpy(assets, source,
               static_cast<size_t>(assetCount) * sizeof(AnoAssetId));
        if (cooker->activeFullRebuild)
            qsort(assets, static_cast<size_t>(assetCount), sizeof(*assets),
                  compare_asset_ids);
    }
    if (success && result == ANO_RESOURCE_OK) {
        bool replaceAll = cooker->activeFullRebuild
            && assetCount != binding->assetCount;
        if (cooker->activeFullRebuild && !replaceAll && assetCount != 0)
            replaceAll = memcmp(
                assets, binding->assets,
                static_cast<size_t>(assetCount) * sizeof(AnoAssetId)) != 0;
        binding->candidateFiles = files;
        binding->candidateFileCount = cooker->activeFileCount;
        binding->candidateAssets = assets;
        binding->candidateAssetCount = assetCount;
        binding->candidateSnapshotId = snapshot_identity(
            cooker->activeFiles, cooker->activeFileCount);
        binding->candidateRootId = cooker->activeFiles[0].content;
        binding->candidateValid = true;
        binding->candidateReplaceAll = replaceAll;
        files = nullptr;
        assets = nullptr;
    }
    release_source_files(files, cooker->activeFileCount);
    mi_free(assets);
    clear_active_import(*cooker);
    return result;
}

AnoResourceError ano_resource_cooker_source_dependencies(
    AnoResourceCooker *cooker, const char *const *paths, uint64_t count)
{
    if (cooker == nullptr || (count != 0 && paths == nullptr)
        || cooker->activeSource.value == 0 || cooker->activeRegion == nullptr
        || count > SIZE_MAX / sizeof(AnoResourceError))
        return ANO_RESOURCE_INVALID_ARGUMENT;
    uint64_t uniqueCount = 0;
    for (uint64_t i = 0; i < count; ++i) {
        if (paths[i] == nullptr || paths[i][0] == '\0')
            return ANO_RESOURCE_INVALID_ARGUMENT;
        bool known = find_active_file(*cooker, paths[i]) != nullptr;
        for (uint64_t j = 0; j < i && !known; ++j)
            known = strcmp(paths[i], paths[j]) == 0;
        uniqueCount += known ? 0u : 1u;
    }
    if (uniqueCount == 0)
        return ANO_RESOURCE_OK;
    AnoResourceError result = reserve_active_files(*cooker, uniqueCount);
    AnoResourceError *results = result == ANO_RESOURCE_OK
        ? ano::memory_region_allocate_zero<AnoResourceError>(
              cooker->activeRegion, static_cast<size_t>(uniqueCount))
        : nullptr;
    if (result == ANO_RESOURCE_OK && results == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    const uint64_t first = cooker->activeFileCount;
    uint64_t cursor = 0;
    for (uint64_t i = 0; i < count && result == ANO_RESOURCE_OK; ++i) {
        bool known = find_active_file(*cooker, paths[i]) != nullptr;
        for (uint64_t j = 0; j < i && !known; ++j)
            known = strcmp(paths[i], paths[j]) == 0;
        if (known)
            continue;
        const size_t pathSize = strlen(paths[i]) + 1;
        char *path = ano::memory_region_allocate<char>(
            cooker->activeRegion, pathSize);
        if (path == nullptr) {
            result = ANO_RESOURCE_OUT_OF_MEMORY;
            break;
        }
        memcpy(path, paths[i], pathSize);
        cooker->activeFiles[first + cursor++].path = path;
    }
    if (result != ANO_RESOURCE_OK)
        return result;
    cooker->activeFileCount += uniqueCount;
    ReadBatch batch{
        .cooker = cooker,
        .files = cooker->activeFiles + first,
        .results = results,
    };
    result = ano_resource_cooker_parallel(
        cooker, uniqueCount, &batch, read_source_file);
    for (uint64_t i = 0; i < uniqueCount && result == ANO_RESOURCE_OK; ++i)
        result = results[i];
    return result;
}

AnoResourceError ano_resource_cooker_source_bytes(
    AnoResourceCooker *cooker, const char *path, AnoResourceBytes *bytes)
{
    if (cooker == nullptr || path == nullptr || bytes == nullptr
        || cooker->activeSource.value == 0)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *bytes = {};
    AcquiredFile *file = nullptr;
    const AnoResourceError result = acquire_file(*cooker, path, &file);
    if (result == ANO_RESOURCE_OK)
        *bytes = {.data = file->bytes, .size = file->size};
    return result;
}

AnoResourceError ano_resource_cooker_source_content(
    AnoResourceCooker *cooker, const char *path, AnoContentId *content)
{
    if (cooker == nullptr || path == nullptr || content == nullptr
        || cooker->activeSource.value == 0)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *content = {};
    AcquiredFile *file = nullptr;
    const AnoResourceError result = acquire_file(*cooker, path, &file);
    if (result == ANO_RESOURCE_OK)
        *content = file->content;
    return result;
}

bool ano_resource_cooker_source_changed(
    const AnoResourceCooker *cooker, const char *path)
{
    if (cooker == nullptr || path == nullptr
        || cooker->activeSource.value == 0)
        return true;
    const AcquiredFile *active = nullptr;
    for (uint64_t i = 0; i < cooker->activeFileCount; ++i)
        if (strcmp(cooker->activeFiles[i].path, path) == 0) {
            active = &cooker->activeFiles[i];
            break;
        }
    const SourceBinding *binding = find_source(
        cooker, cooker->activeSource);
    if (active == nullptr || binding == nullptr)
        return true;
    for (uint64_t i = 0; i < binding->fileCount; ++i)
        if (strcmp(binding->files[i].path, path) == 0)
            return !content_equal(binding->files[i].content, active->content);
    return true;
}

bool ano_resource_cooker_import_all(const AnoResourceCooker *cooker)
{
    return cooker == nullptr || cooker->activeFullRebuild;
}

void ano_resource_cooker_select_producer(
    AnoResourceCooker *cooker, uint64_t producer)
{
    if (cooker != nullptr && cooker->activeSource.value != 0)
        cooker->activeProducer = producer;
}
