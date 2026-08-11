/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "cooker_internal.h"

#include <anoptic_memory.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

using ResourceManifest = ano::asset_schema::Manifest;

struct AnoResourceManifest {
    uint8_t *bytes;
    uint64_t byteCount;
    ResourceManifest root;
    AnoResourceManifestEntry *entries;
    AnoResourceDependency *dependencies;
    AnoManifestId id;
};

struct AnoResourcePack {
    uint8_t *bytes;
    uint64_t byteCount;
    uint64_t payloadOffset;
    AnoResourceManifest *manifest;
};

namespace {

inline constexpr uint64_t packHeaderSize = 64;
inline constexpr uint8_t packMagic[8] = {
    'A', 'N', 'O', 'P', 'A', 'C', 'K', 1,
};

struct WorkItem final {
    AnoResourcePackItem source;
    AnoResourceSchema schema;
    AnoContentId content;
    AnoResourceDependency *dependencies;
    uint64_t dependencyCount;
    uint64_t canonicalIndex;
    uint64_t packOffset;
};

struct ContentOrder final {
    AnoContentId content;
    uint64_t index;
};

struct GraphFrame final {
    uint64_t entry;
    uint64_t nextDependency;
};

int compare_items(const void *lhs, const void *rhs)
{
    const WorkItem& first = *static_cast<const WorkItem *>(lhs);
    const WorkItem& second = *static_cast<const WorkItem *>(rhs);
    if (first.source.asset.value < second.source.asset.value)
        return -1;
    if (first.source.asset.value > second.source.asset.value)
        return 1;
    return 0;
}

int compare_dependencies(const void *lhs, const void *rhs)
{
    const AnoResourceDependency& first =
        *static_cast<const AnoResourceDependency *>(lhs);
    const AnoResourceDependency& second =
        *static_cast<const AnoResourceDependency *>(rhs);
    if (first.asset.value < second.asset.value)
        return -1;
    if (first.asset.value > second.asset.value)
        return 1;
    if (first.type.value < second.type.value)
        return -1;
    if (first.type.value > second.type.value)
        return 1;
    return 0;
}

int compare_content_order(const void *lhs, const void *rhs)
{
    const ContentOrder& first = *static_cast<const ContentOrder *>(lhs);
    const ContentOrder& second = *static_cast<const ContentOrder *>(rhs);
    const int digestOrder = memcmp(first.content.bytes, second.content.bytes,
                                   sizeof(first.content.bytes));
    if (digestOrder != 0)
        return digestOrder;
    if (first.index < second.index)
        return -1;
    if (first.index > second.index)
        return 1;
    return 0;
}

AnoResourceError collect_dependencies(AnoResourceTypeId type,
                                      AnoResourceBytes artifact,
                                      AnoResourceDependency **dependencies,
                                      uint64_t *dependencyCount)
{
    if (dependencies == nullptr || dependencyCount == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *dependencies = nullptr;
    *dependencyCount = 0;

    uint64_t required = 0;
    const AnoResourceError measured = ano_resource_artifact_dependencies(
        type, artifact, nullptr, 0, &required);
    if (required == 0)
        return measured;
    if (measured != ANO_RESOURCE_DEPENDENCY_CAPACITY)
        return measured;

    size_t bytes = 0;
    if (!ano::detail::checked_allocation_size(
            required, sizeof(AnoResourceDependency), &bytes))
        return ANO_RESOURCE_OVERFLOW;
    AnoResourceDependency *values =
        static_cast<AnoResourceDependency *>(mi_malloc(bytes));
    if (values == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;

    uint64_t actual = 0;
    const AnoResourceError extracted = ano_resource_artifact_dependencies(
        type, artifact, values, required, &actual);
    if (extracted != ANO_RESOURCE_OK || actual != required) {
        mi_free(values);
        return extracted == ANO_RESOURCE_OK ? ANO_RESOURCE_BAD_MANIFEST
                                            : extracted;
    }

    qsort(values, static_cast<size_t>(actual), sizeof(*values),
          compare_dependencies);
    uint64_t unique = 0;
    for (uint64_t i = 0; i < actual; ++i) {
        if (unique != 0
            && compare_dependencies(&values[unique - 1], &values[i]) == 0)
            continue;
        values[unique++] = values[i];
    }
    *dependencies = values;
    *dependencyCount = unique;
    return ANO_RESOURCE_OK;
}

void release_work(WorkItem *work, uint64_t count)
{
    if (work == nullptr)
        return;
    for (uint64_t i = 0; i < count; ++i)
        mi_free(work[i].dependencies);
    mi_free(work);
}

AnoResourceError validate_dependency_graph(
    const AnoResourceManifestEntry *entries, uint64_t entryCount,
    const AnoResourceDependency *dependencies, uint64_t dependencyCount)
{
    if ((entries == nullptr && entryCount != 0)
        || (dependencies == nullptr && dependencyCount != 0))
        return ANO_RESOURCE_BAD_MANIFEST;

    uint64_t expectedDependency = 0;
    for (uint64_t i = 0; i < entryCount; ++i) {
        const AnoResourceManifestEntry& entry = entries[i];
        if (entry.asset.value != i + 1 || entry.type.value == 0
            || entry.commitGroup.value == 0 || entry.packedSize == 0
            || entry.packedSize != entry.unpackedSize
            || entry.dependencyFirst != expectedDependency)
            return ANO_RESOURCE_BAD_MANIFEST;

        AnoResourceSchema expectedSchema = {};
        if (ano_resource_artifact_schema(entry.type, &expectedSchema)
                != ANO_RESOURCE_OK
            || !ano::detail::fingerprint_equal(
                entry.schema, expectedSchema.fingerprint))
            return ANO_RESOURCE_BAD_MANIFEST;

        if (!ano::detail::checked_add(expectedDependency,
                                      entry.dependencyCount,
                                      &expectedDependency)
            || expectedDependency > dependencyCount)
            return ANO_RESOURCE_BAD_MANIFEST;

        const AnoResourceDependency *previous = nullptr;
        for (uint64_t j = 0; j < entry.dependencyCount; ++j) {
            const AnoResourceDependency& dependency =
                dependencies[entry.dependencyFirst + j];
            if (dependency.asset.value == 0
                || dependency.asset.value > entryCount
                || dependency.type.value == 0
                || entries[dependency.asset.value - 1].type.value
                    != dependency.type.value
                || (previous != nullptr
                    && compare_dependencies(previous, &dependency) >= 0))
                return ANO_RESOURCE_BAD_MANIFEST;
            previous = &dependency;
        }
    }
    if (expectedDependency != dependencyCount)
        return ANO_RESOURCE_BAD_MANIFEST;
    if (entryCount == 0)
        return ANO_RESOURCE_OK;

    size_t colorBytes = 0;
    size_t stackBytes = 0;
    if (!ano::detail::checked_allocation_size(
            entryCount, sizeof(uint8_t), &colorBytes)
        || !ano::detail::checked_allocation_size(
            entryCount, sizeof(GraphFrame), &stackBytes))
        return ANO_RESOURCE_OVERFLOW;
    uint8_t *colors = static_cast<uint8_t *>(mi_calloc(1, colorBytes));
    GraphFrame *stack = static_cast<GraphFrame *>(mi_malloc(stackBytes));
    if (colors == nullptr || stack == nullptr) {
        mi_free(colors);
        mi_free(stack);
        return ANO_RESOURCE_OUT_OF_MEMORY;
    }

    AnoResourceError result = ANO_RESOURCE_OK;
    for (uint64_t root = 0; root < entryCount && result == ANO_RESOURCE_OK;
         ++root) {
        if (colors[root] != 0)
            continue;
        uint64_t depth = 1;
        colors[root] = 1;
        stack[0] = {.entry = root, .nextDependency = 0};
        while (depth != 0) {
            GraphFrame& frame = stack[depth - 1];
            const AnoResourceManifestEntry& entry = entries[frame.entry];
            if (frame.nextDependency == entry.dependencyCount) {
                colors[frame.entry] = 2;
                --depth;
                continue;
            }
            const AnoResourceDependency& dependency = dependencies[
                entry.dependencyFirst + frame.nextDependency++];
            const uint64_t target = dependency.asset.value - 1;
            if (colors[target] == 1) {
                result = ANO_RESOURCE_BAD_MANIFEST;
                break;
            }
            if (colors[target] == 0) {
                colors[target] = 1;
                stack[depth++] = {.entry = target, .nextDependency = 0};
            }
        }
    }

    mi_free(stack);
    mi_free(colors);
    return result;
}

AnoResourceError canonicalize_work_payloads(WorkItem *work, uint64_t count,
                                            uint64_t *payloadSize)
{
    if (payloadSize == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *payloadSize = 0;
    if (count == 0)
        return ANO_RESOURCE_OK;

    size_t orderBytes = 0;
    if (!ano::detail::checked_allocation_size(
            count, sizeof(ContentOrder), &orderBytes))
        return ANO_RESOURCE_OVERFLOW;
    ContentOrder *order = static_cast<ContentOrder *>(mi_malloc(orderBytes));
    if (order == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    for (uint64_t i = 0; i < count; ++i)
        order[i] = {.content = work[i].content, .index = i};
    qsort(order, static_cast<size_t>(count), sizeof(*order),
          compare_content_order);

    AnoResourceError result = ANO_RESOURCE_OK;
    for (uint64_t first = 0; first < count && result == ANO_RESOURCE_OK;) {
        uint64_t last = first + 1;
        while (last < count
               && ano::detail::bytes_equal(
                   order[first].content.bytes, order[last].content.bytes, 32))
            ++last;
        const uint64_t canonical = order[first].index;
        for (uint64_t i = first; i < last; ++i) {
            WorkItem& candidate = work[order[i].index];
            const WorkItem& root = work[canonical];
            if (candidate.source.artifact.size != root.source.artifact.size
                || memcmp(candidate.source.artifact.data,
                          root.source.artifact.data,
                          static_cast<size_t>(root.source.artifact.size)) != 0) {
                result = ANO_RESOURCE_BAD_PACK;
                break;
            }
            candidate.canonicalIndex = canonical;
        }
        first = last;
    }

    uint64_t cursor = 0;
    for (uint64_t i = 0; i < count && result == ANO_RESOURCE_OK; ++i) {
        if (work[i].canonicalIndex == i) {
            work[i].packOffset = cursor;
            if (!ano::detail::checked_add(cursor,
                                          work[i].source.artifact.size,
                                          &cursor))
                result = ANO_RESOURCE_OVERFLOW;
        } else {
            work[i].packOffset = work[work[i].canonicalIndex].packOffset;
        }
    }
    mi_free(order);
    *payloadSize = cursor;
    return result;
}

AnoResourceError validate_payload(
    AnoResourceBytes packBytes, uint64_t payloadOffset,
    const AnoResourceManifest *manifest,
    const AnoResourceManifestEntry& entry)
{
    uint64_t absolute = 0;
    if (!ano::detail::checked_add(payloadOffset, entry.packOffset, &absolute)
        || !ano::detail::byte_range(packBytes.size, absolute,
                                    entry.packedSize))
        return ANO_RESOURCE_BAD_PACK;
    const AnoResourceBytes artifact = {
        .data = packBytes.data + absolute,
        .size = entry.packedSize,
    };

    AnoContentId actualContent = {};
    if (ano_resource_content_id(artifact, &actualContent) != ANO_RESOURCE_OK
        || !ano::detail::bytes_equal(
            entry.content.bytes, actualContent.bytes, 32)
        || ano_resource_validate_artifact(entry.type, artifact)
            != ANO_RESOURCE_OK)
        return ANO_RESOURCE_BAD_PACK;
    AnoResourceSchema schema = {};
    if (ano_resource_artifact_schema(entry.type, &schema) != ANO_RESOURCE_OK
        || !ano::detail::fingerprint_equal(
            entry.schema, schema.fingerprint))
        return ANO_RESOURCE_BAD_PACK;

    AnoResourceDependency *actualDependencies = nullptr;
    uint64_t actualCount = 0;
    const AnoResourceError extracted = collect_dependencies(
        entry.type, artifact, &actualDependencies, &actualCount);
    if (extracted != ANO_RESOURCE_OK)
        return extracted == ANO_RESOURCE_OUT_OF_MEMORY ? extracted
                                                       : ANO_RESOURCE_BAD_PACK;
    bool equal = actualCount == entry.dependencyCount;
    for (uint64_t i = 0; i < actualCount && equal; ++i) {
        const AnoResourceDependency& expected = manifest->dependencies[
            entry.dependencyFirst + i];
        equal = actualDependencies[i].asset.value == expected.asset.value
            && actualDependencies[i].type.value == expected.type.value;
    }
    mi_free(actualDependencies);
    return equal ? ANO_RESOURCE_OK : ANO_RESOURCE_BAD_PACK;
}

AnoResourceError validate_pack_layout(
    AnoResourceBytes bytes, uint64_t payloadOffset,
    const AnoResourceManifest *manifest)
{
    const uint64_t count = manifest->root.entries.count;
    const uint64_t payloadSize = bytes.size - payloadOffset;
    if (count == 0)
        return payloadSize == 0 ? ANO_RESOURCE_OK : ANO_RESOURCE_BAD_PACK;

    size_t orderBytes = 0;
    size_t canonicalBytes = 0;
    if (!ano::detail::checked_allocation_size(
            count, sizeof(ContentOrder), &orderBytes)
        || !ano::detail::checked_allocation_size(
            count, sizeof(uint64_t), &canonicalBytes))
        return ANO_RESOURCE_OVERFLOW;
    ContentOrder *order = static_cast<ContentOrder *>(mi_malloc(orderBytes));
    uint64_t *canonical = static_cast<uint64_t *>(mi_malloc(canonicalBytes));
    if (order == nullptr || canonical == nullptr) {
        mi_free(order);
        mi_free(canonical);
        return ANO_RESOURCE_OUT_OF_MEMORY;
    }
    for (uint64_t i = 0; i < count; ++i)
        order[i] = {.content = manifest->entries[i].content, .index = i};
    qsort(order, static_cast<size_t>(count), sizeof(*order),
          compare_content_order);

    AnoResourceError result = ANO_RESOURCE_OK;
    for (uint64_t first = 0; first < count && result == ANO_RESOURCE_OK;) {
        uint64_t last = first + 1;
        while (last < count
               && ano::detail::bytes_equal(
                   order[first].content.bytes, order[last].content.bytes, 32))
            ++last;
        const uint64_t rootIndex = order[first].index;
        const AnoResourceManifestEntry& root = manifest->entries[rootIndex];
        const uint8_t *rootBytes = bytes.data + payloadOffset + root.packOffset;
        for (uint64_t i = first; i < last; ++i) {
            const uint64_t index = order[i].index;
            const AnoResourceManifestEntry& candidate = manifest->entries[index];
            if (candidate.packedSize != root.packedSize
                || memcmp(bytes.data + payloadOffset + candidate.packOffset,
                          rootBytes, static_cast<size_t>(root.packedSize)) != 0) {
                result = ANO_RESOURCE_BAD_PACK;
                break;
            }
            canonical[index] = rootIndex;
        }
        first = last;
    }

    uint64_t expectedOffset = 0;
    for (uint64_t i = 0; i < count && result == ANO_RESOURCE_OK; ++i) {
        const AnoResourceManifestEntry& entry = manifest->entries[i];
        if (canonical[i] == i) {
            if (entry.packOffset != expectedOffset
                || !ano::detail::checked_add(expectedOffset, entry.packedSize,
                                              &expectedOffset))
                result = ANO_RESOURCE_BAD_PACK;
        } else if (entry.packOffset
                   != manifest->entries[canonical[i]].packOffset) {
            result = ANO_RESOURCE_BAD_PACK;
        }
    }
    if (result == ANO_RESOURCE_OK && expectedOffset != payloadSize)
        result = ANO_RESOURCE_BAD_PACK;

    mi_free(canonical);
    mi_free(order);
    return result;
}

} // namespace

extern "C" AnoResourceError ano_resource_manifest_open(
    AnoResourceBytes bytes, AnoResourceManifest **manifest)
{
    if (manifest == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *manifest = nullptr;
    if (bytes.data == nullptr && bytes.size != 0)
        return ANO_RESOURCE_INVALID_ARGUMENT;

    const ano::DecodeResult<ResourceManifest> decoded =
        ano::decode<ResourceManifest>(bytes);
    if (decoded.error != ANO_RESOURCE_OK)
        return ANO_RESOURCE_BAD_MANIFEST;

    size_t entryBytes = 0;
    size_t dependencyBytes = 0;
    if (!ano::detail::checked_allocation_size(
            decoded.view.value.entries.count,
            sizeof(AnoResourceManifestEntry), &entryBytes)
        || !ano::detail::checked_allocation_size(
            decoded.view.value.dependencies.count,
            sizeof(AnoResourceDependency), &dependencyBytes)
        || bytes.size > SIZE_MAX)
        return ANO_RESOURCE_OVERFLOW;

    AnoResourceManifest *opened = static_cast<AnoResourceManifest *>(
        mi_calloc(1, sizeof(AnoResourceManifest)));
    if (opened == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    opened->bytes = static_cast<uint8_t *>(mi_malloc(static_cast<size_t>(bytes.size)));
    opened->entries = entryBytes == 0 ? nullptr
        : static_cast<AnoResourceManifestEntry *>(mi_malloc(entryBytes));
    opened->dependencies = dependencyBytes == 0 ? nullptr
        : static_cast<AnoResourceDependency *>(mi_malloc(dependencyBytes));
    if ((bytes.size != 0 && opened->bytes == nullptr)
        || (entryBytes != 0 && opened->entries == nullptr)
        || (dependencyBytes != 0 && opened->dependencies == nullptr)) {
        ano_resource_manifest_close(opened);
        return ANO_RESOURCE_OUT_OF_MEMORY;
    }
    if (bytes.size != 0)
        memcpy(opened->bytes, bytes.data, static_cast<size_t>(bytes.size));
    opened->byteCount = bytes.size;
    opened->root = decoded.view.value;

    const ano::ArtifactView<ResourceManifest> view = {
        .value = opened->root,
        .bytes = {.data = opened->bytes, .size = opened->byteCount},
    };
    if (ano::resolve_span(view, opened->root.entries, opened->entries,
                          opened->root.entries.count) != ANO_RESOURCE_OK
        || ano::resolve_span(view, opened->root.dependencies,
                             opened->dependencies,
                             opened->root.dependencies.count)
            != ANO_RESOURCE_OK) {
        ano_resource_manifest_close(opened);
        return ANO_RESOURCE_BAD_MANIFEST;
    }

    const AnoResourceError graph = validate_dependency_graph(
        opened->entries, opened->root.entries.count, opened->dependencies,
        opened->root.dependencies.count);
    if (graph != ANO_RESOURCE_OK) {
        ano_resource_manifest_close(opened);
        return graph;
    }
    AnoContentId content = {};
    if (ano_resource_content_id(bytes, &content) != ANO_RESOURCE_OK) {
        ano_resource_manifest_close(opened);
        return ANO_RESOURCE_BAD_MANIFEST;
    }
    memcpy(opened->id.bytes, content.bytes, sizeof(opened->id.bytes));
    *manifest = opened;
    return ANO_RESOURCE_OK;
}

extern "C" void ano_resource_manifest_close(AnoResourceManifest *manifest)
{
    if (manifest == nullptr)
        return;
    mi_free(manifest->dependencies);
    mi_free(manifest->entries);
    mi_free(manifest->bytes);
    mi_free(manifest);
}

extern "C" AnoResourceError ano_resource_manifest_id(
    const AnoResourceManifest *manifest, AnoManifestId *id)
{
    if (manifest == nullptr || id == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *id = manifest->id;
    return ANO_RESOURCE_OK;
}

extern "C" uint64_t ano_resource_manifest_entry_count(
    const AnoResourceManifest *manifest)
{
    return manifest == nullptr ? 0 : manifest->root.entries.count;
}

extern "C" AnoResourceError ano_resource_manifest_find(
    const AnoResourceManifest *manifest, AnoAssetId asset,
    AnoResourceManifestEntry *entry)
{
    if (manifest == nullptr || entry == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    if (asset.value == 0 || asset.value > manifest->root.entries.count)
        return ANO_RESOURCE_NOT_FOUND;
    const AnoResourceManifestEntry& found = manifest->entries[asset.value - 1];
    if (found.asset.value != asset.value)
        return ANO_RESOURCE_BAD_MANIFEST;
    *entry = found;
    return ANO_RESOURCE_OK;
}

extern "C" AnoResourceError ano_resource_manifest_dependency(
    const AnoResourceManifest *manifest, AnoAssetId asset, uint64_t index,
    AnoResourceDependency *dependency)
{
    if (dependency == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    AnoResourceManifestEntry entry = {};
    const AnoResourceError found =
        ano_resource_manifest_find(manifest, asset, &entry);
    if (found != ANO_RESOURCE_OK)
        return found;
    if (index >= entry.dependencyCount)
        return ANO_RESOURCE_OUT_OF_BOUNDS;
    *dependency = manifest->dependencies[entry.dependencyFirst + index];
    return ANO_RESOURCE_OK;
}

AnoResourceError ano_resource_pack_build(
    const AnoResourcePackItem *items, uint64_t itemCount,
    AnoResourceMutableBytes *pack)
{
    if (pack == nullptr || (items == nullptr && itemCount != 0))
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *pack = {};

    size_t workBytes = 0;
    if (!ano::detail::checked_allocation_size(
            itemCount, sizeof(WorkItem), &workBytes))
        return ANO_RESOURCE_OVERFLOW;
    WorkItem *work = workBytes == 0 ? nullptr
        : static_cast<WorkItem *>(mi_calloc(1, workBytes));
    if (workBytes != 0 && work == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    for (uint64_t i = 0; i < itemCount; ++i)
        work[i].source = items[i];
    if (itemCount != 0)
        qsort(work, static_cast<size_t>(itemCount), sizeof(*work), compare_items);

    AnoResourceError result = ANO_RESOURCE_OK;
    uint64_t dependencyCount = 0;
    for (uint64_t i = 0; i < itemCount && result == ANO_RESOURCE_OK; ++i) {
        WorkItem& item = work[i];
        if (item.source.asset.value != i + 1)
            result = i != 0
                    && item.source.asset.value == work[i - 1].source.asset.value
                ? ANO_RESOURCE_DUPLICATE_ASSET
                : ANO_RESOURCE_BAD_MANIFEST;
        else if (item.source.commitGroup.value == 0
                 || item.source.artifact.data == nullptr
                 || item.source.artifact.size == 0)
            result = ANO_RESOURCE_INVALID_ARGUMENT;
        else if (ano_resource_artifact_schema(item.source.type, &item.schema)
                     != ANO_RESOURCE_OK
                 || ano_resource_validate_artifact(item.source.type,
                                                   item.source.artifact)
                     != ANO_RESOURCE_OK)
            result = ANO_RESOURCE_TYPE_MISMATCH;
        else if (ano_resource_content_id(item.source.artifact, &item.content)
                 != ANO_RESOURCE_OK)
            result = ANO_RESOURCE_BAD_PACK;
        else
            result = collect_dependencies(
                item.source.type, item.source.artifact, &item.dependencies,
                &item.dependencyCount);
        if (result == ANO_RESOURCE_OK
            && !ano::detail::checked_add(dependencyCount,
                                         item.dependencyCount,
                                         &dependencyCount))
            result = ANO_RESOURCE_OVERFLOW;
    }

    uint64_t payloadSize = 0;
    if (result == ANO_RESOURCE_OK)
        result = canonicalize_work_payloads(work, itemCount, &payloadSize);

    size_t entryBytes = 0;
    size_t dependencyBytes = 0;
    uint64_t extentSize64 = 0;
    if (result == ANO_RESOURCE_OK
        && (!ano::detail::checked_allocation_size(
                itemCount, sizeof(AnoResourceManifestEntry), &entryBytes)
            || !ano::detail::checked_allocation_size(
                dependencyCount, sizeof(AnoResourceDependency),
                &dependencyBytes)
            || !ano::detail::checked_add(entryBytes, dependencyBytes,
                                         &extentSize64)
            || extentSize64 > SIZE_MAX))
        result = ANO_RESOURCE_OVERFLOW;

    uint8_t *extent = nullptr;
    if (result == ANO_RESOURCE_OK && extentSize64 != 0) {
        extent = static_cast<uint8_t *>(
            mi_malloc(static_cast<size_t>(extentSize64)));
        if (extent == nullptr)
            result = ANO_RESOURCE_OUT_OF_MEMORY;
    }
    AnoResourceManifestEntry *entries =
        reinterpret_cast<AnoResourceManifestEntry *>(extent);
    AnoResourceDependency *dependencies = extent == nullptr ? nullptr
        : reinterpret_cast<AnoResourceDependency *>(extent + entryBytes);

    uint64_t dependencyCursor = 0;
    for (uint64_t i = 0; i < itemCount && result == ANO_RESOURCE_OK; ++i) {
        const WorkItem& item = work[i];
        entries[i] = {
            .asset = item.source.asset,
            .type = item.source.type,
            .schema = item.schema.fingerprint,
            .content = item.content,
            .commitGroup = item.source.commitGroup,
            .packOffset = item.packOffset,
            .packedSize = item.source.artifact.size,
            .unpackedSize = item.source.artifact.size,
            .dependencyFirst = dependencyCursor,
            .dependencyCount = item.dependencyCount,
        };
        if (item.dependencyCount != 0)
            memcpy(dependencies + dependencyCursor, item.dependencies,
                   static_cast<size_t>(item.dependencyCount
                                       * sizeof(AnoResourceDependency)));
        dependencyCursor += item.dependencyCount;
    }
    if (result == ANO_RESOURCE_OK)
        result = validate_dependency_graph(entries, itemCount, dependencies,
                                           dependencyCount);

    const ResourceManifest root = {
        .entries = itemCount == 0
            ? ano::RelativeSpan<AnoResourceManifestEntry>{0, 0}
            : ano::RelativeSpan<AnoResourceManifestEntry>{0, itemCount},
        .dependencies = dependencyCount == 0
            ? ano::RelativeSpan<AnoResourceDependency>{0, 0}
            : ano::RelativeSpan<AnoResourceDependency>{entryBytes,
                                                       dependencyCount},
    };
    ano::EncodeResult manifestSize = {result, 0};
    if (result == ANO_RESOURCE_OK)
        manifestSize = ano::encoded_size(ano::ArtifactSource<ResourceManifest>{
            .value = &root,
            .extent = {.data = extent, .size = extentSize64},
        });
    if (result == ANO_RESOURCE_OK)
        result = manifestSize.error;

    uint64_t payloadOffset = 0;
    uint64_t totalSize = 0;
    if (result == ANO_RESOURCE_OK
        && (!ano::detail::checked_add(packHeaderSize, manifestSize.size,
                                      &payloadOffset)
            || !ano::detail::checked_add(payloadOffset, payloadSize,
                                         &totalSize)
            || totalSize > SIZE_MAX))
        result = ANO_RESOURCE_OVERFLOW;
    uint8_t *built = nullptr;
    if (result == ANO_RESOURCE_OK) {
        built = static_cast<uint8_t *>(mi_calloc(1, static_cast<size_t>(totalSize)));
        if (built == nullptr)
            result = ANO_RESOURCE_OUT_OF_MEMORY;
    }
    if (result == ANO_RESOURCE_OK) {
        memcpy(built, packMagic, sizeof(packMagic));
        ano::detail::write_unsigned(built + 8, manifestSize.size, 8);
        ano::detail::write_unsigned(built + 16, totalSize, 8);
        ano::detail::write_unsigned(built + 24, payloadOffset, 8);
        const ano::EncodeResult encoded = ano::encode(
            ano::ArtifactSource<ResourceManifest>{
                .value = &root,
                .extent = {.data = extent, .size = extentSize64},
            },
            {.data = built + packHeaderSize, .size = manifestSize.size});
        if (encoded.error != ANO_RESOURCE_OK
            || encoded.size != manifestSize.size)
            result = encoded.error == ANO_RESOURCE_OK
                ? ANO_RESOURCE_BAD_MANIFEST
                : encoded.error;
    }
    if (result == ANO_RESOURCE_OK) {
        AnoContentId manifestContent = {};
        const AnoResourceBytes manifestBytes = {
            .data = built + packHeaderSize,
            .size = manifestSize.size,
        };
        if (ano_resource_content_id(manifestBytes, &manifestContent)
                != ANO_RESOURCE_OK)
            result = ANO_RESOURCE_BAD_MANIFEST;
        else
            memcpy(built + 32, manifestContent.bytes,
                   sizeof(manifestContent.bytes));
    }
    for (uint64_t i = 0; i < itemCount && result == ANO_RESOURCE_OK; ++i)
        memmove(built + payloadOffset + work[i].packOffset,
                work[i].source.artifact.data,
                static_cast<size_t>(work[i].source.artifact.size));

    if (result == ANO_RESOURCE_OK) {
        AnoResourcePack *verified = nullptr;
        result = ano_resource_pack_open(
            {.data = built, .size = totalSize}, &verified);
        ano_resource_pack_close(verified);
    }
    if (result == ANO_RESOURCE_OK) {
        *pack = {.data = built, .size = totalSize};
        built = nullptr;
    }

    mi_free(built);
    mi_free(extent);
    release_work(work, itemCount);
    return result;
}

extern "C" AnoResourceError ano_resource_pack_open(
    AnoResourceBytes bytes, AnoResourcePack **pack)
{
    if (pack == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *pack = nullptr;
    if (bytes.data == nullptr || bytes.size < packHeaderSize)
        return ANO_RESOURCE_BAD_PACK;
    if (memcmp(bytes.data, packMagic, sizeof(packMagic)) != 0)
        return ANO_RESOURCE_BAD_MAGIC;

    const uint64_t manifestSize = ano::detail::read_unsigned(bytes.data + 8, 8);
    const uint64_t totalSize = ano::detail::read_unsigned(bytes.data + 16, 8);
    const uint64_t payloadOffset = ano::detail::read_unsigned(bytes.data + 24, 8);
    uint64_t expectedPayloadOffset = 0;
    if (totalSize != bytes.size
        || !ano::detail::checked_add(packHeaderSize, manifestSize,
                                     &expectedPayloadOffset)
        || payloadOffset != expectedPayloadOffset
        || payloadOffset > bytes.size)
        return ANO_RESOURCE_BAD_PACK;

    const AnoResourceBytes manifestBytes = {
        .data = bytes.data + packHeaderSize,
        .size = manifestSize,
    };
    AnoContentId manifestContent = {};
    if (ano_resource_content_id(manifestBytes, &manifestContent)
            != ANO_RESOURCE_OK
        || !ano::detail::bytes_equal(
            manifestContent.bytes, bytes.data + 32, 32))
        return ANO_RESOURCE_BAD_MANIFEST;

    AnoResourceManifest *manifest = nullptr;
    AnoResourceError result =
        ano_resource_manifest_open(manifestBytes, &manifest);
    if (result != ANO_RESOURCE_OK)
        return result;
    for (uint64_t i = 0;
         i < manifest->root.entries.count && result == ANO_RESOURCE_OK; ++i)
        result = validate_payload(bytes, payloadOffset, manifest,
                                  manifest->entries[i]);
    if (result == ANO_RESOURCE_OK)
        result = validate_pack_layout(bytes, payloadOffset, manifest);
    if (result != ANO_RESOURCE_OK) {
        ano_resource_manifest_close(manifest);
        return result;
    }

    AnoResourcePack *opened =
        static_cast<AnoResourcePack *>(mi_calloc(1, sizeof(AnoResourcePack)));
    uint8_t *copy = bytes.size > SIZE_MAX ? nullptr
        : static_cast<uint8_t *>(mi_malloc(static_cast<size_t>(bytes.size)));
    if (opened == nullptr || copy == nullptr) {
        mi_free(opened);
        mi_free(copy);
        ano_resource_manifest_close(manifest);
        return bytes.size > SIZE_MAX ? ANO_RESOURCE_OVERFLOW
                                     : ANO_RESOURCE_OUT_OF_MEMORY;
    }
    memcpy(copy, bytes.data, static_cast<size_t>(bytes.size));
    opened->bytes = copy;
    opened->byteCount = bytes.size;
    opened->payloadOffset = payloadOffset;
    opened->manifest = manifest;
    *pack = opened;
    return ANO_RESOURCE_OK;
}

extern "C" void ano_resource_pack_close(AnoResourcePack *pack)
{
    if (pack == nullptr)
        return;
    ano_resource_manifest_close(pack->manifest);
    mi_free(pack->bytes);
    mi_free(pack);
}

extern "C" const AnoResourceManifest *ano_resource_pack_manifest(
    const AnoResourcePack *pack)
{
    return pack == nullptr ? nullptr : pack->manifest;
}

extern "C" AnoResourceError ano_resource_pack_read(
    const AnoResourcePack *pack, AnoAssetId asset,
    AnoResourceMutableBytes output, uint64_t *packSize)
{
    if (pack == nullptr || packSize == nullptr
        || (output.data == nullptr && output.size != 0))
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *packSize = 0;
    AnoResourceManifestEntry entry = {};
    const AnoResourceError found =
        ano_resource_manifest_find(pack->manifest, asset, &entry);
    if (found != ANO_RESOURCE_OK)
        return found;
    *packSize = entry.unpackedSize;
    const AnoResourceError validated = validate_payload(
        {.data = pack->bytes, .size = pack->byteCount}, pack->payloadOffset,
        pack->manifest, entry);
    if (validated != ANO_RESOURCE_OK)
        return validated;
    if (output.data == nullptr || output.size < entry.unpackedSize)
        return ANO_RESOURCE_BUFFER_TOO_SMALL;
    memmove(output.data,
            pack->bytes + pack->payloadOffset + entry.packOffset,
            static_cast<size_t>(entry.unpackedSize));
    return ANO_RESOURCE_OK;
}
