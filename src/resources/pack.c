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

struct OpenVolume final { ano::MemoryVolume *volume; OpenVolumePlan plan; };

AnoResourceError validate_manifest_graph(
    const AnoResourceManifestEntry *entries, uint64_t entryCount,
    const AnoResourceDependency *dependencies, uint64_t dependencyCount)
{
    if ((entries == nullptr && entryCount != 0)
        || (dependencies == nullptr && dependencyCount != 0))
        return ANO_RESOURCE_BAD_MANIFEST;
    for (uint64_t i = 0; i < entryCount; ++i) {
        const AnoResourceManifestEntry& entry = entries[i];
        const auto schema = resource_artifact_schema(entry.type);
        if (entry.byteSize == 0
            || !schema
            || !ano::detail::fingerprint_equal(
                entry.schema, schema->fingerprint))
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
    if (!decoded)
        return ANO_RESOURCE_BAD_MANIFEST;
    if (root != nullptr)
        *root = decoded->value;
    if (!ano::resolve_span(*decoded, decoded->value.entries,
                           entries, decoded->value.entries.count)
        || !ano::resolve_span(*decoded, decoded->value.dependencies,
                             dependencies,
                             decoded->value.dependencies.count))
        return ANO_RESOURCE_BAD_MANIFEST;
    return validate_manifest_graph(
        entries, decoded->value.entries.count,
        dependencies, decoded->value.dependencies.count);
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

ResourceResult<OpenVolume> allocate_open_volume(
    AnoResourceBytes bytes, uint64_t entryCount, uint64_t dependencyCount,
    bool withRevision)
{
    if (bytes.size > SIZE_MAX || entryCount > SIZE_MAX
        || dependencyCount > SIZE_MAX)
        return failure(ANO_RESOURCE_OVERFLOW);
    OpenVolumePlan plan{
        .bytes = {.count = static_cast<size_t>(bytes.size)},
        .entries = {.count = static_cast<size_t>(entryCount)},
        .dependencies = {.count = static_cast<size_t>(dependencyCount)},
        .revisionStorage = {.count = withRevision
            ? static_cast<size_t>(entryCount) : 0},
        .revisionVolumes = {.count = withRevision ? 1u : 0u},
    };
    const auto layout = ano::memory_layout(plan);
    if (!layout)
        return failure(ANO_RESOURCE_OVERFLOW);
    const auto volume = ano::memory_volume_create(*layout);
    return result_if(volume.has_value(), OpenVolume{volume.value_or(nullptr), plan},
                     ANO_RESOURCE_OUT_OF_MEMORY);
}

ResourceResult<AnoResourceManifest *> make_manifest(
    ano::MemoryVolume *volume, OpenVolumePlan plan, AnoResourceBytes bytes)
{
    if (volume == nullptr)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    auto entries = ano::memory_volume_write(volume, plan.entries);
    auto dependencies = ano::memory_volume_write(volume, plan.dependencies);
    if (!entries || !dependencies)
        return failure(ANO_RESOURCE_BAD_MANIFEST);
    ResourceManifest root{};
    const AnoResourceError decoded = decode_manifest(
        bytes, entries->data(), dependencies->data(), &root);
    if (decoded != ANO_RESOURCE_OK)
        return failure(decoded);
    const auto content = resource_content_id(bytes);
    if (!content)
        return failure(content.error());
    AnoResourceManifest *opened = mi_zalloc_tp(AnoResourceManifest);
    if (opened == nullptr)
        return failure(ANO_RESOURCE_OUT_OF_MEMORY);
    const auto retained = ano::memory_volume_retain(volume);
    if (!retained) {
        mi_free(opened);
        return failure(ANO_RESOURCE_OVERFLOW);
    }
    opened->volume = volume;
    opened->root = root;
    opened->entries = entries->data();
    opened->dependencies = dependencies->data();
    memcpy(opened->id.bytes, content->bytes, sizeof(opened->id.bytes));
    return opened;
}

} // namespace

ResourceResult<AnoResourceManifest *> ano::resource_manifest_open(
    AnoResourceBytes bytes)
{
    if (bytes.data == nullptr && bytes.size != 0)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    const ano::DecodeResult<ResourceManifest> decoded =
        ano::decode<ResourceManifest>(bytes);
    if (!decoded)
        return failure(ANO_RESOURCE_BAD_MANIFEST);
    const auto openedVolume = allocate_open_volume(
        bytes, decoded->value.entries.count,
        decoded->value.dependencies.count, false);
    if (!openedVolume)
        return failure(openedVolume.error());
    auto [volume, plan] = *openedVolume;
    AnoResourceError result = ANO_RESOURCE_OK;
    AnoResourceManifest *manifest = nullptr;
    if (result == ANO_RESOURCE_OK) {
        auto destination = ano::memory_volume_write(volume, plan.bytes);
        if (!destination)
            result = ANO_RESOURCE_BAD_MANIFEST;
        else if (bytes.size != 0)
            memcpy(destination->data(), bytes.data,
                   static_cast<size_t>(bytes.size));
        if (destination) {
            const AnoResourceBytes owned{
                destination->data(), destination->size()};
            const auto opened = make_manifest(volume, plan, owned);
            if (opened)
                manifest = *opened;
            else
                result = opened.error();
        }
    }
    if (result == ANO_RESOURCE_OK && !ano::memory_volume_seal(volume))
        result = ANO_RESOURCE_BAD_MANIFEST;
    if (result != ANO_RESOURCE_OK) {
        resource_manifest_close(manifest);
        manifest = nullptr;
    }
    ano::memory_volume_release(volume);
    if (result != ANO_RESOURCE_OK)
        return failure(result);
    return manifest;
}

void ano::resource_manifest_close(AnoResourceManifest *manifest)
{
    if (manifest == nullptr)
        return;
    ano::memory_volume_release(manifest->volume);
    mi_free(manifest);
}

ResourceResult<AnoManifestId> ano::resource_manifest_id(
    const AnoResourceManifest *manifest)
{
    if (manifest == nullptr)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    return manifest->id;
}

uint64_t ano::resource_manifest_entry_count(
    const AnoResourceManifest *manifest)
{
    return manifest == nullptr ? 0 : manifest->root.entries.count;
}

ResourceResult<AnoResourceManifestEntry> ano::resource_manifest_find(
    const AnoResourceManifest *manifest, AnoAssetId asset)
{
    if (manifest == nullptr)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    if (asset.value == 0 || asset.value > manifest->root.entries.count)
        return failure(ANO_RESOURCE_NOT_FOUND);
    const AnoResourceManifestEntry& found = manifest->entries[asset.value - 1];
    if (found.asset.value != asset.value)
        return failure(ANO_RESOURCE_BAD_MANIFEST);
    return found;
}

ResourceResult<AnoResourceDependency> ano::resource_manifest_dependency(
    const AnoResourceManifest *manifest, AnoAssetId asset, uint64_t index)
{
    const auto entry = resource_manifest_find(manifest, asset);
    if (!entry)
        return failure(entry.error());
    if (index >= entry->dependencyCount)
        return failure(ANO_RESOURCE_OUT_OF_BOUNDS);
    return manifest->dependencies[entry->dependencyFirst + index];
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
            if (resource_content_id_equal(
                    entry.content, revision->entries[j].content)) {
                const auto lhs = resource_revision_resolve(
                    revision, entry.asset, entry.type);
                const auto rhs = resource_revision_resolve(
                        revision, revision->entries[j].asset,
                        revision->entries[j].type);
                if (!lhs || !rhs || lhs->size != rhs->size
                    || memcmp(lhs->data, rhs->data,
                              static_cast<size_t>(lhs->size)) != 0) {
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
            if (!ano::checked_accumulate(
                    payloadSize, storage.artifact.size))
                result = ANO_RESOURCE_OVERFLOW;
        } else {
            placements[i] = placements[canonical];
        }
    }

    const auto entryBytes = ano::checked_allocation_size(
        itemCount, sizeof(AnoResourceManifestEntry));
    const auto dependencyBytes = ano::checked_allocation_size(
        revision->dependencyCount, sizeof(AnoResourceDependency));
    const auto extentSize = entryBytes && dependencyBytes
        ? ano::checked_add(*entryBytes, *dependencyBytes)
        : ano::ArithmeticResult<size_t>(failure(ano::ArithmeticError::overflow));
    if (result == ANO_RESOURCE_OK && !extentSize)
        result = ANO_RESOURCE_OVERFLOW;
    uint8_t *extent = result == ANO_RESOURCE_OK && *extentSize != 0
        ? static_cast<uint8_t *>(mi_malloc(*extentSize))
        : nullptr;
    if (result == ANO_RESOURCE_OK && *extentSize != 0 && extent == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    if (result == ANO_RESOURCE_OK && *entryBytes != 0)
        memcpy(extent, revision->entries, *entryBytes);
    if (result == ANO_RESOURCE_OK && *dependencyBytes != 0)
        memcpy(extent + *entryBytes, revision->dependencies, *dependencyBytes);
    const ResourceManifest root = {
        .entries = itemCount == 0
            ? ano::RelativeSpan<AnoResourceManifestEntry>{0, 0}
            : ano::RelativeSpan<AnoResourceManifestEntry>{0, itemCount},
        .dependencies = revision->dependencyCount == 0
            ? ano::RelativeSpan<AnoResourceDependency>{0, 0}
            : ano::RelativeSpan<AnoResourceDependency>{
                  *entryBytes, revision->dependencyCount},
    };
    ano::EncodeResult manifestSize = ano::failure(result);
    if (result == ANO_RESOURCE_OK)
        manifestSize = ano::encoded_size(
            ano::ArtifactSource<ResourceManifest>{
                .value = &root,
                .extent = {.data = extent, .size = *extentSize},
            });
    if (!manifestSize)
        result = manifestSize.error();

    const auto placementBytes = ano::checked_multiply(itemCount, placementSize);
    const auto manifestEnd = manifestSize
        ? ano::checked_add(packHeaderSize, *manifestSize)
        : ano::ArithmeticResult<uint64_t>(failure(ano::ArithmeticError::overflow));
    const auto payloadOffset = placementBytes && manifestEnd
        ? ano::checked_add(*manifestEnd, *placementBytes)
        : ano::ArithmeticResult<uint64_t>(failure(ano::ArithmeticError::overflow));
    const auto totalSize = payloadOffset
        ? ano::checked_add(*payloadOffset, payloadSize)
        : ano::ArithmeticResult<uint64_t>(failure(ano::ArithmeticError::overflow));
    if (result == ANO_RESOURCE_OK
        && (!totalSize || *totalSize > SIZE_MAX))
        result = ANO_RESOURCE_OVERFLOW;
    uint8_t *built = result == ANO_RESOURCE_OK
        ? static_cast<uint8_t *>(mi_zalloc(static_cast<size_t>(*totalSize)))
        : nullptr;
    if (result == ANO_RESOURCE_OK && built == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    if (result == ANO_RESOURCE_OK) {
        memcpy(built, packMagic, sizeof(packMagic));
        ano::detail::write_unsigned(built + 8, *manifestSize, 8);
        ano::detail::write_unsigned(built + 16, *placementBytes, 8);
        ano::detail::write_unsigned(built + 24, *payloadOffset, 8);
        const ano::EncodeResult encoded = ano::encode(
            ano::ArtifactSource<ResourceManifest>{
                .value = &root,
                .extent = {.data = extent, .size = *extentSize},
            },
            {.data = built + packHeaderSize,
             .size = *manifestSize});
        if (!encoded || *encoded != *manifestSize)
            result = encoded ? ANO_RESOURCE_BAD_MANIFEST : encoded.error();
    }
    if (result == ANO_RESOURCE_OK) {
        const AnoResourceBytes manifestBytes{
            built + packHeaderSize, *manifestSize};
        const auto id = resource_content_id(manifestBytes);
        if (id)
            memcpy(built + 32, id->bytes, sizeof(id->bytes));
        else
            result = id.error();
    }
    const uint64_t placementOffset = packHeaderSize
        + (manifestSize ? *manifestSize : 0);
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
            const auto artifact = resource_revision_resolve(
                revision, revision->entries[i].asset,
                revision->entries[i].type);
            if (artifact)
                memcpy(built + *payloadOffset + placements[i].offset,
                       artifact->data, static_cast<size_t>(artifact->size));
            else
                result = artifact.error();
        }
    }
    if (result == ANO_RESOURCE_OK) {
        *pack = {built, *totalSize};
        built = nullptr;
    }
    mi_free(built);
    mi_free(extent);
    mi_free(placements);
    return result;
}

ResourceResult<AnoResourcePack *> ano::resource_pack_open(
    AnoResourceBytes bytes)
{
    if (bytes.data == nullptr || bytes.size < packHeaderSize)
        return failure(ANO_RESOURCE_BAD_PACK);
    if (memcmp(bytes.data, packMagic, sizeof(packMagic)) != 0)
        return failure(ANO_RESOURCE_BAD_MAGIC);
    const uint64_t manifestSize = ano::detail::read_unsigned(
        bytes.data + 8, 8);
    const uint64_t placementBytes = ano::detail::read_unsigned(
        bytes.data + 16, 8);
    const uint64_t payloadOffset = ano::detail::read_unsigned(
        bytes.data + 24, 8);
    const auto manifestEnd = ano::checked_add(packHeaderSize, manifestSize);
    const auto expectedPayload = manifestEnd
        ? ano::checked_add(*manifestEnd, placementBytes)
        : ano::ArithmeticResult<uint64_t>(
              ano::failure(manifestEnd.error()));
    if (!expectedPayload || *expectedPayload != payloadOffset
        || payloadOffset > bytes.size)
        return failure(ANO_RESOURCE_BAD_PACK);
    const AnoResourceBytes manifestBytes{
        bytes.data + packHeaderSize, manifestSize};
    const auto manifestId = resource_content_id(manifestBytes);
    if (!manifestId
        || !ano::detail::bytes_equal(
            manifestId->bytes, bytes.data + 32,
            sizeof(manifestId->bytes)))
        return failure(ANO_RESOURCE_BAD_MANIFEST);
    const ano::DecodeResult<ResourceManifest> decoded =
        ano::decode<ResourceManifest>(manifestBytes);
    const auto expectedPlacementBytes = decoded
        ? ano::checked_multiply(
              decoded->value.entries.count, placementSize)
        : ano::ArithmeticResult<uint64_t>(
              ano::failure(ano::ArithmeticError::overflow));
    if (!decoded || decoded->value.entries.count > SIZE_MAX
        || decoded->value.dependencies.count > SIZE_MAX
        || !expectedPlacementBytes || placementBytes != *expectedPlacementBytes)
        return failure(ANO_RESOURCE_BAD_MANIFEST);

    const auto openedVolume = allocate_open_volume(
        bytes, decoded->value.entries.count,
        decoded->value.dependencies.count, true);
    ano::MemoryVolume *volume = openedVolume
        ? openedVolume->volume : nullptr;
    OpenVolumePlan plan = openedVolume
        ? openedVolume->plan : OpenVolumePlan{};
    AnoResourceError result = openedVolume
        ? ANO_RESOURCE_OK : openedVolume.error();
    auto ownedBytes = result == ANO_RESOURCE_OK
        ? ano::memory_volume_write(volume, plan.bytes).value_or(
              std::span<uint8_t>{})
        : std::span<uint8_t>{};
    if (result == ANO_RESOURCE_OK)
        memcpy(ownedBytes.data(), bytes.data, static_cast<size_t>(bytes.size));
    AnoResourceManifest *manifest = nullptr;
    if (result == ANO_RESOURCE_OK) {
        const AnoResourceBytes ownedManifest{
            ownedBytes.data() + packHeaderSize, manifestSize};
        const auto opened = make_manifest(volume, plan, ownedManifest);
        if (opened)
            manifest = *opened;
        else
            result = opened.error();
    }
    auto revisionStorage = result == ANO_RESOURCE_OK
        ? ano::memory_volume_write(volume, plan.revisionStorage).value_or(
              std::span<AnoResourceRevisionStorage>{})
        : std::span<AnoResourceRevisionStorage>{};
    auto revisionVolumes = result == ANO_RESOURCE_OK
        ? ano::memory_volume_write(volume, plan.revisionVolumes).value_or(
              std::span<ano::MemoryVolume *>{})
        : std::span<ano::MemoryVolume *>{};
    uint64_t expectedOffset = 0;
    for (uint64_t i = 0;
         i < decoded->value.entries.count && result == ANO_RESOURCE_OK;
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
        const auto payloadBase = ano::checked_add(
            plan.bytes.reservation.offset,
            static_cast<size_t>(payloadOffset));
        const auto artifactOffset = payloadBase
            ? ano::checked_add(
                  *payloadBase, static_cast<size_t>(placement.offset))
            : ano::ArithmeticResult<size_t>(
                  ano::failure(payloadBase.error()));
        if (!artifactOffset) {
            result = ANO_RESOURCE_OVERFLOW;
            break;
        }
        bool canonical = true;
        for (uint64_t j = 0; j < i; ++j) {
            const bool samePlacement =
                revisionStorage[j].artifact.offset == *artifactOffset
                && revisionStorage[j].artifact.size == placement.size;
            const bool sameContent = resource_content_id_equal(
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
                || !ano::checked_accumulate(expectedOffset, placement.size)) {
                result = ANO_RESOURCE_BAD_PACK;
                break;
            }
        }
        const AnoResourceBytes artifact{
            ownedBytes.data() + payloadOffset + placement.offset,
            placement.size};
        const auto content = result == ANO_RESOURCE_OK
            ? resource_content_id(artifact)
            : ResourceResult<AnoContentId>(failure(result));
        if (result == ANO_RESOURCE_OK
            && (!content
                || !resource_content_id_equal(*content, entry.content)
                || !resource_validate_artifact(entry.type, artifact)))
            result = ANO_RESOURCE_BAD_PACK;
        revisionStorage[i] = {
            .source = {},
            .volume = volume,
            .artifact = {
                .offset = *artifactOffset,
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
        revisionVolumeRetained = !!ano::memory_volume_retain(volume);
        if (!revisionVolumeRetained)
            result = ANO_RESOURCE_OVERFLOW;
    }
    if (result == ANO_RESOURCE_OK) {
        __atomic_store_n(&revision->references, size_t{1}, __ATOMIC_RELAXED);
        revision->entries = manifest->entries;
        revision->storage = revisionStorage.data();
        revision->itemCount = decoded->value.entries.count;
        revision->dependencies = manifest->dependencies;
        revision->dependencyCount = decoded->value.dependencies.count;
        revision->volumes = revisionVolumes.data();
        revision->volumeCount = 1;
    }
    if (result == ANO_RESOURCE_OK && !ano::memory_volume_seal(volume))
        result = ANO_RESOURCE_BAD_MANIFEST;
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
        ano::memory_volume_release(volume);
        return opened;
    }
    mi_free(opened);
    if (revisionVolumeRetained)
        resource_revision_release(revision);
    else
        mi_free(revision);
    resource_manifest_close(manifest);
    ano::memory_volume_release(volume);
    return failure(result);
}

void ano::resource_pack_close(AnoResourcePack *pack)
{
    if (pack == nullptr)
        return;
    resource_revision_release(pack->revision);
    resource_manifest_close(pack->manifest);
    mi_free(pack);
}

ResourceResult<const AnoCookedRevision *> ano::resource_pack_revision(
    const AnoResourcePack *pack)
{
    if (pack == nullptr)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    const auto retained = resource_revision_retain(pack->revision);
    if (!retained)
        return failure(retained.error());
    return pack->revision;
}

const AnoResourceManifest *ano::resource_pack_manifest(
    const AnoResourcePack *pack)
{
    return pack == nullptr ? nullptr : pack->manifest;
}

ResourceResult<AnoResourceBytes> ano::resource_pack_view(
    const AnoResourcePack *pack, AnoAssetId asset)
{
    if (pack == nullptr)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    const AnoResourceManifestEntry *item = ano_resource_revision_entry(
        pack->revision, asset);
    if (item == nullptr)
        return failure(ANO_RESOURCE_NOT_FOUND);
    return resource_revision_resolve(
        pack->revision, asset, item->type);
}
