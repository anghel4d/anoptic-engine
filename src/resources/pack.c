/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include "cooker_internal.h"

#include <anoptic_memory_typed.h>

using namespace ano;
#include <anoptic_resources_pack.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

using ResourceManifest = ano::asset_schema::Manifest;

namespace {

inline constexpr uint64_t packHeaderSize = 64;
inline constexpr uint64_t placementSize = 16;
inline constexpr uint8_t packMagic[8] = {
    'A', 'N', 'O', 'P', 'A', 'C', 'K', 2,
};

struct PackPlacement final {
    uint64_t offset;
    uint64_t size;
};

struct OpenVolumePlan final {
    ano::MemorySegment<uint8_t, ANO_CACHE_LINE> bytes;
    ano::MemorySegment<AnoResourceManifestEntry> entries;
    ano::MemorySegment<AnoResourceDependency> dependencies;
    ano::MemorySegment<AnoResourceRevisionStorage> revisionStorage;
    ano::MemorySegment<ano::MemoryVolume *> revisionVolumes;
};

AnoResourceError validate_manifest_graph(
    const AnoResourceManifestEntry *entries, uint64_t entryCount,
    const AnoResourceDependency *dependencies, uint64_t dependencyCount)
{
    if ((entries == nullptr && entryCount != 0)
        || (dependencies == nullptr && dependencyCount != 0))
        return ANO_RESOURCE_BAD_MANIFEST;
    for (uint64_t i = 0; i < entryCount; ++i) {
        const AnoResourceManifestEntry& entry = entries[i];
        AnoResourceSchema schema{};
        if (entry.byteSize == 0
            || ano_resource_artifact_schema(entry.type, &schema)
                != ANO_RESOURCE_OK
            || !ano::detail::fingerprint_equal(
                entry.schema, schema.fingerprint))
            return ANO_RESOURCE_BAD_MANIFEST;
    }
    return ano::resource_detail::validate_dependency_graph(
        entries, entryCount, dependencies, dependencyCount);
}

AnoResourceError decode_manifest(
    AnoResourceBytes bytes, AnoResourceManifestEntry *entries,
    AnoResourceDependency *dependencies, ResourceManifest *root)
{
    const ano::DecodeResult<ResourceManifest> decoded =
        ano::decode<ResourceManifest>(bytes);
    if (decoded.error != ANO_RESOURCE_OK)
        return ANO_RESOURCE_BAD_MANIFEST;
    if (root != nullptr)
        *root = decoded.view.value;
    if (ano::resolve_span(decoded.view, decoded.view.value.entries,
                          entries, decoded.view.value.entries.count)
            != ANO_RESOURCE_OK
        || ano::resolve_span(decoded.view, decoded.view.value.dependencies,
                             dependencies,
                             decoded.view.value.dependencies.count)
            != ANO_RESOURCE_OK)
        return ANO_RESOURCE_BAD_MANIFEST;
    return validate_manifest_graph(
        entries, decoded.view.value.entries.count,
        dependencies, decoded.view.value.dependencies.count);
}

} // namespace

struct ano::AnoResourceManifest {
    ano::MemoryVolume *volume;
    ResourceManifest root;
    AnoResourceManifestEntry *entries;
    AnoResourceDependency *dependencies;
    AnoManifestId id;
};

struct ano::AnoResourcePack {
    AnoResourceManifest *manifest;
    AnoCookedRevision *revision;
};

namespace {

AnoResourceError allocate_open_volume(
    AnoResourceBytes bytes, uint64_t entryCount, uint64_t dependencyCount,
    bool withRevision, ano::MemoryVolume **volume,
    OpenVolumePlan *plan)
{
    if (volume == nullptr || plan == nullptr || bytes.size > SIZE_MAX
        || entryCount > SIZE_MAX || dependencyCount > SIZE_MAX)
        return ANO_RESOURCE_OVERFLOW;
    *volume = nullptr;
    *plan = {
        .bytes = {.count = static_cast<size_t>(bytes.size)},
        .entries = {.count = static_cast<size_t>(entryCount)},
        .dependencies = {.count = static_cast<size_t>(dependencyCount)},
        .revisionStorage = {.count = withRevision
            ? static_cast<size_t>(entryCount) : 0},
        .revisionVolumes = {.count = withRevision ? 1u : 0u},
    };
    const ano::MemoryLayoutCursor layout = ano::memory_layout(*plan);
    *volume = ano::memory_volume_create(layout);
    return *volume == nullptr ? ANO_RESOURCE_OUT_OF_MEMORY : ANO_RESOURCE_OK;
}

AnoResourceError make_manifest(
    ano::MemoryVolume *volume, OpenVolumePlan plan, AnoResourceBytes bytes,
    AnoResourceManifest **manifest)
{
    if (volume == nullptr || manifest == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *manifest = nullptr;
    auto entries = ano::memory_volume_write(volume, plan.entries);
    auto dependencies = ano::memory_volume_write(volume, plan.dependencies);
    ResourceManifest root{};
    AnoResourceError result = decode_manifest(
        bytes, entries.data(), dependencies.data(), &root);
    AnoContentId content{};
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_content_id(bytes, &content);
    AnoResourceManifest *opened = nullptr;
    if (result == ANO_RESOURCE_OK) {
        opened = mi_zalloc_tp(AnoResourceManifest);
        if (opened == nullptr || !ano::memory_volume_retain(volume))
            result = ANO_RESOURCE_OUT_OF_MEMORY;
    }
    if (result != ANO_RESOURCE_OK) {
        mi_free(opened);
        return result;
    }
    opened->volume = volume;
    opened->root = root;
    opened->entries = entries.data();
    opened->dependencies = dependencies.data();
    memcpy(opened->id.bytes, content.bytes, sizeof(opened->id.bytes));
    *manifest = opened;
    return ANO_RESOURCE_OK;
}

} // namespace

extern "C" AnoResourceError ano::ano_resource_manifest_open(
    AnoResourceBytes bytes, AnoResourceManifest **manifest)
{
    if (manifest == nullptr || (bytes.data == nullptr && bytes.size != 0))
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *manifest = nullptr;
    const ano::DecodeResult<ResourceManifest> decoded =
        ano::decode<ResourceManifest>(bytes);
    if (decoded.error != ANO_RESOURCE_OK)
        return ANO_RESOURCE_BAD_MANIFEST;
    ano::MemoryVolume *volume = nullptr;
    OpenVolumePlan plan{};
    AnoResourceError result = allocate_open_volume(
        bytes, decoded.view.value.entries.count,
        decoded.view.value.dependencies.count, false, &volume, &plan);
    if (result == ANO_RESOURCE_OK) {
        auto destination = ano::memory_volume_write(volume, plan.bytes);
        if (bytes.size != 0)
            memcpy(destination.data(), bytes.data,
                   static_cast<size_t>(bytes.size));
        const AnoResourceBytes owned{
            destination.data(), destination.size()};
        result = make_manifest(volume, plan, owned, manifest);
    }
    if (result == ANO_RESOURCE_OK)
        ano::memory_volume_seal(volume);
    if (result != ANO_RESOURCE_OK) {
        ano_resource_manifest_close(*manifest);
        *manifest = nullptr;
    }
    ano::memory_volume_release(volume);
    return result;
}

extern "C" void ano::ano_resource_manifest_close(AnoResourceManifest *manifest)
{
    if (manifest == nullptr)
        return;
    ano::memory_volume_release(manifest->volume);
    mi_free(manifest);
}

extern "C" AnoResourceError ano::ano_resource_manifest_id(
    const AnoResourceManifest *manifest, AnoManifestId *id)
{
    if (manifest == nullptr || id == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *id = manifest->id;
    return ANO_RESOURCE_OK;
}

extern "C" uint64_t ano::ano_resource_manifest_entry_count(
    const AnoResourceManifest *manifest)
{
    return manifest == nullptr ? 0 : manifest->root.entries.count;
}

extern "C" AnoResourceError ano::ano_resource_manifest_find(
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

extern "C" AnoResourceError ano::ano_resource_manifest_dependency(
    const AnoResourceManifest *manifest, AnoAssetId asset, uint64_t index,
    AnoResourceDependency *dependency)
{
    if (dependency == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    AnoResourceManifestEntry entry{};
    const AnoResourceError found = ano_resource_manifest_find(
        manifest, asset, &entry);
    if (found != ANO_RESOURCE_OK)
        return found;
    if (index >= entry.dependencyCount)
        return ANO_RESOURCE_OUT_OF_BOUNDS;
    *dependency = manifest->dependencies[entry.dependencyFirst + index];
    return ANO_RESOURCE_OK;
}

AnoResourceError ano_resource_pack_build(
    const AnoCookedRevision *revision, AnoResourceMutableBytes *pack)
{
    if (revision == nullptr || pack == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *pack = {};
    AnoResourceError result = ano_resource_revision_validate(revision);
    const uint64_t itemCount = revision->itemCount;
    PackPlacement *placements = itemCount == 0 ? nullptr
        : mi_calloc_tp(PackPlacement, static_cast<size_t>(itemCount));
    if (result == ANO_RESOURCE_OK && itemCount != 0
        && placements == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;

    uint64_t payloadSize = 0;
    for (uint64_t i = 0; i < itemCount && result == ANO_RESOURCE_OK; ++i) {
        const AnoResourceManifestEntry& entry = revision->entries[i];
        const AnoResourceRevisionStorage& storage = revision->storage[i];
        uint64_t canonical = i;
        for (uint64_t j = 0; j < i; ++j)
            if (ano_resource_content_id_equal(
                    entry.content, revision->entries[j].content)) {
                AnoResourceBytes lhs{};
                AnoResourceBytes rhs{};
                if (ano_resource_revision_resolve(
                        revision, entry.asset, entry.type, &lhs)
                        != ANO_RESOURCE_OK
                    || ano_resource_revision_resolve(
                        revision, revision->entries[j].asset,
                        revision->entries[j].type, &rhs) != ANO_RESOURCE_OK
                    || lhs.size != rhs.size
                    || memcmp(lhs.data, rhs.data,
                              static_cast<size_t>(lhs.size)) != 0) {
                    result = ANO_RESOURCE_BAD_MANIFEST;
                    break;
                }
                canonical = j;
                break;
            }
        if (result != ANO_RESOURCE_OK)
            break;
        if (canonical == i) {
            placements[i] = {payloadSize, storage.artifact.size};
            if (!ano::checked_add(
                    payloadSize, storage.artifact.size, &payloadSize))
                result = ANO_RESOURCE_OVERFLOW;
        } else {
            placements[i] = placements[canonical];
        }
    }

    size_t entryBytes = 0;
    size_t dependencyBytes = 0;
    uint64_t extentSize = 0;
    if (result == ANO_RESOURCE_OK
        && (!ano::checked_allocation_size(
                itemCount, sizeof(AnoResourceManifestEntry), &entryBytes)
            || !ano::checked_allocation_size(
                revision->dependencyCount, sizeof(AnoResourceDependency),
                &dependencyBytes)
            || !ano::checked_add(
                entryBytes, dependencyBytes, &extentSize)
            || extentSize > SIZE_MAX))
        result = ANO_RESOURCE_OVERFLOW;
    uint8_t *extent = result == ANO_RESOURCE_OK && extentSize != 0
        ? static_cast<uint8_t *>(mi_malloc(static_cast<size_t>(extentSize)))
        : nullptr;
    if (result == ANO_RESOURCE_OK && extentSize != 0 && extent == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    if (result == ANO_RESOURCE_OK && entryBytes != 0)
        memcpy(extent, revision->entries, entryBytes);
    if (result == ANO_RESOURCE_OK && dependencyBytes != 0)
        memcpy(extent + entryBytes, revision->dependencies, dependencyBytes);
    const ResourceManifest root = {
        .entries = itemCount == 0
            ? ano::RelativeSpan<AnoResourceManifestEntry>{0, 0}
            : ano::RelativeSpan<AnoResourceManifestEntry>{0, itemCount},
        .dependencies = revision->dependencyCount == 0
            ? ano::RelativeSpan<AnoResourceDependency>{0, 0}
            : ano::RelativeSpan<AnoResourceDependency>{
                  entryBytes, revision->dependencyCount},
    };
    ano::EncodeResult manifestSize{result, 0};
    if (result == ANO_RESOURCE_OK)
        manifestSize = ano::encoded_size(
            ano::ArtifactSource<ResourceManifest>{
                .value = &root,
                .extent = {.data = extent, .size = extentSize},
            });
    result = manifestSize.error;

    uint64_t placementBytes = 0;
    uint64_t payloadOffset = 0;
    uint64_t totalSize = 0;
    if (result == ANO_RESOURCE_OK
        && (!ano::checked_multiply(
                itemCount, placementSize, &placementBytes)
            || !ano::checked_add(
                packHeaderSize, manifestSize.size, &payloadOffset)
            || !ano::checked_add(
                payloadOffset, placementBytes, &payloadOffset)
            || !ano::checked_add(
                payloadOffset, payloadSize, &totalSize)
            || totalSize > SIZE_MAX))
        result = ANO_RESOURCE_OVERFLOW;
    uint8_t *built = result == ANO_RESOURCE_OK
        ? static_cast<uint8_t *>(mi_zalloc(static_cast<size_t>(totalSize)))
        : nullptr;
    if (result == ANO_RESOURCE_OK && built == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    if (result == ANO_RESOURCE_OK) {
        memcpy(built, packMagic, sizeof(packMagic));
        ano::detail::write_unsigned(built + 8, manifestSize.size, 8);
        ano::detail::write_unsigned(built + 16, placementBytes, 8);
        ano::detail::write_unsigned(built + 24, payloadOffset, 8);
        const ano::EncodeResult encoded = ano::encode(
            ano::ArtifactSource<ResourceManifest>{
                .value = &root,
                .extent = {.data = extent, .size = extentSize},
            },
            {.data = built + packHeaderSize,
             .size = manifestSize.size});
        if (encoded.error != ANO_RESOURCE_OK
            || encoded.size != manifestSize.size)
            result = encoded.error == ANO_RESOURCE_OK
                ? ANO_RESOURCE_BAD_MANIFEST : encoded.error;
    }
    if (result == ANO_RESOURCE_OK) {
        AnoContentId id{};
        const AnoResourceBytes manifestBytes{
            built + packHeaderSize, manifestSize.size};
        result = ano_resource_content_id(manifestBytes, &id);
        if (result == ANO_RESOURCE_OK)
            memcpy(built + 32, id.bytes, sizeof(id.bytes));
    }
    const uint64_t placementOffset = packHeaderSize + manifestSize.size;
    for (uint64_t i = 0; i < itemCount && result == ANO_RESOURCE_OK; ++i) {
        ano::detail::write_unsigned(
            built + placementOffset + i * placementSize,
            placements[i].offset, 8);
        ano::detail::write_unsigned(
            built + placementOffset + i * placementSize + 8,
            placements[i].size, 8);
        bool canonical = true;
        for (uint64_t j = 0; j < i; ++j)
            if (placements[j].offset == placements[i].offset) {
                canonical = false;
                break;
            }
        if (canonical) {
            AnoResourceBytes artifact{};
            result = ano_resource_revision_resolve(
                revision, revision->entries[i].asset,
                revision->entries[i].type, &artifact);
            if (result == ANO_RESOURCE_OK)
                memcpy(built + payloadOffset + placements[i].offset,
                       artifact.data, static_cast<size_t>(artifact.size));
        }
    }
    if (result == ANO_RESOURCE_OK) {
        *pack = {built, totalSize};
        built = nullptr;
    }
    mi_free(built);
    mi_free(extent);
    mi_free(placements);
    return result;
}

extern "C" AnoResourceError ano::ano_resource_pack_open(
    AnoResourceBytes bytes, AnoResourcePack **pack)
{
    if (pack == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *pack = nullptr;
    if (bytes.data == nullptr || bytes.size < packHeaderSize)
        return ANO_RESOURCE_BAD_PACK;
    if (memcmp(bytes.data, packMagic, sizeof(packMagic)) != 0)
        return ANO_RESOURCE_BAD_MAGIC;
    const uint64_t manifestSize = ano::detail::read_unsigned(
        bytes.data + 8, 8);
    const uint64_t placementBytes = ano::detail::read_unsigned(
        bytes.data + 16, 8);
    const uint64_t payloadOffset = ano::detail::read_unsigned(
        bytes.data + 24, 8);
    uint64_t expectedPayload = 0;
    if (!ano::checked_add(
            packHeaderSize, manifestSize, &expectedPayload)
        || !ano::checked_add(
            expectedPayload, placementBytes, &expectedPayload)
        || expectedPayload != payloadOffset || payloadOffset > bytes.size)
        return ANO_RESOURCE_BAD_PACK;
    const AnoResourceBytes manifestBytes{
        bytes.data + packHeaderSize, manifestSize};
    AnoContentId manifestId{};
    if (ano_resource_content_id(manifestBytes, &manifestId)
            != ANO_RESOURCE_OK
        || !ano::detail::bytes_equal(
            manifestId.bytes, bytes.data + 32, sizeof(manifestId.bytes)))
        return ANO_RESOURCE_BAD_MANIFEST;
    const ano::DecodeResult<ResourceManifest> decoded =
        ano::decode<ResourceManifest>(manifestBytes);
    uint64_t expectedPlacementBytes = 0;
    if (decoded.error != ANO_RESOURCE_OK
        || decoded.view.value.entries.count > SIZE_MAX
        || decoded.view.value.dependencies.count > SIZE_MAX
        || !ano::checked_multiply(
            decoded.view.value.entries.count, placementSize,
            &expectedPlacementBytes)
        || placementBytes != expectedPlacementBytes)
        return ANO_RESOURCE_BAD_MANIFEST;

    ano::MemoryVolume *volume = nullptr;
    OpenVolumePlan plan{};
    AnoResourceError result = allocate_open_volume(
        bytes, decoded.view.value.entries.count,
        decoded.view.value.dependencies.count, true, &volume, &plan);
    auto ownedBytes = result == ANO_RESOURCE_OK
        ? ano::memory_volume_write(volume, plan.bytes)
        : std::span<uint8_t>{};
    if (result == ANO_RESOURCE_OK)
        memcpy(ownedBytes.data(), bytes.data, static_cast<size_t>(bytes.size));
    AnoResourceManifest *manifest = nullptr;
    if (result == ANO_RESOURCE_OK) {
        const AnoResourceBytes ownedManifest{
            ownedBytes.data() + packHeaderSize, manifestSize};
        result = make_manifest(volume, plan, ownedManifest, &manifest);
    }
    auto revisionStorage = result == ANO_RESOURCE_OK
        ? ano::memory_volume_write(volume, plan.revisionStorage)
        : std::span<AnoResourceRevisionStorage>{};
    auto revisionVolumes = result == ANO_RESOURCE_OK
        ? ano::memory_volume_write(volume, plan.revisionVolumes)
        : std::span<ano::MemoryVolume *>{};
    uint64_t expectedOffset = 0;
    for (uint64_t i = 0;
         i < decoded.view.value.entries.count && result == ANO_RESOURCE_OK;
         ++i) {
        const uint8_t *wire = ownedBytes.data()
            + packHeaderSize + manifestSize + i * placementSize;
        const PackPlacement placement = {
            ano::detail::read_unsigned(wire, 8),
            ano::detail::read_unsigned(wire + 8, 8),
        };
        const AnoResourceManifestEntry& entry = manifest->entries[i];
        if (placement.size != entry.byteSize
            || placement.offset > bytes.size - payloadOffset
            || placement.size
                > bytes.size - payloadOffset - placement.offset) {
            result = ANO_RESOURCE_BAD_PACK;
            break;
        }
        size_t artifactOffset = 0;
        if (!ano::checked_add(plan.bytes.reservation.offset,
                              static_cast<size_t>(payloadOffset),
                              &artifactOffset)
            || !ano::checked_add(
                artifactOffset, static_cast<size_t>(placement.offset),
                &artifactOffset)) {
            result = ANO_RESOURCE_OVERFLOW;
            break;
        }
        bool canonical = true;
        for (uint64_t j = 0; j < i; ++j) {
            const bool samePlacement =
                revisionStorage[j].artifact.offset == artifactOffset
                && revisionStorage[j].artifact.size == placement.size;
            const bool sameContent = ano_resource_content_id_equal(
                manifest->entries[j].content, entry.content);
            if (samePlacement != sameContent) {
                result = ANO_RESOURCE_BAD_PACK;
                break;
            }
            if (samePlacement) {
                canonical = false;
                break;
            }
        }
        if (canonical) {
            if (placement.offset != expectedOffset
                || !ano::checked_add(
                    expectedOffset, placement.size, &expectedOffset)) {
                result = ANO_RESOURCE_BAD_PACK;
                break;
            }
        }
        const AnoResourceBytes artifact{
            ownedBytes.data() + payloadOffset + placement.offset,
            placement.size};
        AnoContentId content{};
        if (result == ANO_RESOURCE_OK
            && (ano_resource_content_id(artifact, &content)
                    != ANO_RESOURCE_OK
                || !ano_resource_content_id_equal(content, entry.content)
                || ano_resource_validate_artifact(entry.type, artifact)
                    != ANO_RESOURCE_OK))
            result = ANO_RESOURCE_BAD_PACK;
        revisionStorage[i] = {
            .source = {},
            .volume = volume,
            .artifact = {
                .offset = artifactOffset,
                .size = static_cast<size_t>(placement.size),
            },
        };
    }
    if (result == ANO_RESOURCE_OK
        && expectedOffset != bytes.size - payloadOffset)
        result = ANO_RESOURCE_BAD_PACK;
    AnoCookedRevision *revision = result == ANO_RESOURCE_OK
        ? mi_zalloc_tp(AnoCookedRevision) : nullptr;
    if (result == ANO_RESOURCE_OK && revision == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    bool revisionVolumeRetained = false;
    if (result == ANO_RESOURCE_OK) {
        revisionVolumes[0] = volume;
        revisionVolumeRetained = ano::memory_volume_retain(volume);
        if (!revisionVolumeRetained)
            result = ANO_RESOURCE_OVERFLOW;
    }
    if (result == ANO_RESOURCE_OK) {
        __atomic_store_n(&revision->references, size_t{1}, __ATOMIC_RELAXED);
        revision->entries = manifest->entries;
        revision->storage = revisionStorage.data();
        revision->itemCount = decoded.view.value.entries.count;
        revision->dependencies = manifest->dependencies;
        revision->dependencyCount = decoded.view.value.dependencies.count;
        revision->volumes = revisionVolumes.data();
        revision->volumeCount = 1;
    }
    if (result == ANO_RESOURCE_OK)
        ano::memory_volume_seal(volume);
    if (result == ANO_RESOURCE_OK)
        revision->validated = true;
    AnoResourcePack *opened = nullptr;
    if (result == ANO_RESOURCE_OK) {
        opened = mi_zalloc_tp(AnoResourcePack);
        if (opened == nullptr)
            result = ANO_RESOURCE_OUT_OF_MEMORY;
    }
    if (result == ANO_RESOURCE_OK) {
        opened->manifest = manifest;
        opened->revision = revision;
        *pack = opened;
        ano::memory_volume_release(volume);
        return ANO_RESOURCE_OK;
    }
    mi_free(opened);
    if (revisionVolumeRetained)
        ano_resource_revision_release(revision);
    else
        mi_free(revision);
    ano_resource_manifest_close(manifest);
    ano::memory_volume_release(volume);
    return result;
}

extern "C" void ano::ano_resource_pack_close(AnoResourcePack *pack)
{
    if (pack == nullptr)
        return;
    ano_resource_revision_release(pack->revision);
    ano_resource_manifest_close(pack->manifest);
    mi_free(pack);
}

extern "C" AnoResourceError ano::ano_resource_pack_revision(
    const AnoResourcePack *pack, const AnoCookedRevision **revision)
{
    if (pack == nullptr || revision == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *revision = nullptr;
    const AnoResourceError retained = ano_resource_revision_retain(
        pack->revision);
    if (retained == ANO_RESOURCE_OK)
        *revision = pack->revision;
    return retained;
}

extern "C" const AnoResourceManifest *ano::ano_resource_pack_manifest(
    const AnoResourcePack *pack)
{
    return pack == nullptr ? nullptr : pack->manifest;
}

extern "C" AnoResourceError ano::ano_resource_pack_view(
    const AnoResourcePack *pack, AnoAssetId asset, AnoResourceBytes *bytes)
{
    if (pack == nullptr || bytes == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *bytes = {};
    const AnoResourceManifestEntry *item = ano_resource_revision_entry(
        pack->revision, asset);
    if (item == nullptr)
        return ANO_RESOURCE_NOT_FOUND;
    return ano_resource_revision_resolve(
        pack->revision, asset, item->type, bytes);
}

extern "C" AnoResourceError ano::ano_resource_pack_read(
    const AnoResourcePack *pack, AnoAssetId asset,
    AnoResourceMutableBytes output, uint64_t *packSize)
{
    if (packSize == nullptr
        || (output.data == nullptr && output.size != 0))
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *packSize = 0;
    AnoResourceBytes view{};
    const AnoResourceError result = ano_resource_pack_view(
        pack, asset, &view);
    if (result != ANO_RESOURCE_OK)
        return result;
    *packSize = view.size;
    if (output.data == nullptr || output.size < view.size)
        return ANO_RESOURCE_BUFFER_TOO_SMALL;
    memmove(output.data, view.data, static_cast<size_t>(view.size));
    return ANO_RESOURCE_OK;
}
