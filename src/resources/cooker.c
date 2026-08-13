/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include "cooker_internal.h"

#include <anoptic_atomic.h>
#include <anoptic_memory.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct SourceBinding final {
    AnoResourceSourceId source;
    char *path;
};

struct AnoResourceCooker {
    AnoResourcePackItem *items;
    uint64_t itemCount;
    uint64_t itemCapacity;
    SourceBinding *sources;
    uint64_t sourceCount;
    uint64_t sourceCapacity;
    AnoAssetId firstDerivedAsset;
    AnoAssetId nextDerivedAsset;
    ANO_ATOMIC(bool) cancelled;
};

namespace {

bool contains_asset(const AnoResourceCooker *cooker, AnoAssetId asset)
{
    for (uint64_t i = 0; i < cooker->itemCount; ++i)
        if (cooker->items[i].asset.value == asset.value)
            return true;
    return false;
}

} // namespace

extern "C" AnoResourceError ano_resource_cooker_create(
    AnoResourceCookerConfig config, AnoResourceCooker **cooker)
{
    if (cooker == nullptr || config.firstDerivedAsset.value < 2)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *cooker = nullptr;
    AnoResourceCooker *created = mi_zalloc_tp(AnoResourceCooker);
    if (created == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    created->firstDerivedAsset = config.firstDerivedAsset;
    created->nextDerivedAsset = config.firstDerivedAsset;
    atomic_init(&created->cancelled, false);
    *cooker = created;
    return ANO_RESOURCE_OK;
}

extern "C" void ano_resource_cooker_destroy(AnoResourceCooker *cooker)
{
    if (cooker == nullptr)
        return;
    for (uint64_t i = 0; i < cooker->itemCount; ++i)
        mi_free(const_cast<uint8_t *>(cooker->items[i].artifact.data));
    for (uint64_t i = 0; i < cooker->sourceCount; ++i)
        mi_free(cooker->sources[i].path);
    mi_free(cooker->sources);
    mi_free(cooker->items);
    mi_free(cooker);
}

extern "C" AnoResourceError ano_resource_source_bind(
    AnoResourceCooker *cooker, AnoResourceSourceId source, const char *path)
{
    if (cooker == nullptr || source.value == 0 || path == nullptr
        || path[0] == '\0')
        return ANO_RESOURCE_INVALID_ARGUMENT;
    if (ano_resource_cooker_cancelled(cooker))
        return ANO_RESOURCE_CANCELLED;
    const size_t length = strlen(path);
    if (length == SIZE_MAX)
        return ANO_RESOURCE_OVERFLOW;
    char *copy = static_cast<char *>(mi_malloc(length + 1));
    if (copy == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    memcpy(copy, path, length + 1);

    for (uint64_t i = 0; i < cooker->sourceCount; ++i)
        if (cooker->sources[i].source.value == source.value) {
            mi_free(cooker->sources[i].path);
            cooker->sources[i].path = copy;
            return ANO_RESOURCE_OK;
        }

    if (cooker->sourceCount == cooker->sourceCapacity) {
        const uint64_t capacity = cooker->sourceCapacity == 0
            ? 8 : cooker->sourceCapacity * 2;
        size_t bytes = 0;
        if (capacity < cooker->sourceCapacity
            || !ano::detail::checked_allocation_size(
                capacity, sizeof(SourceBinding), &bytes)) {
            mi_free(copy);
            return ANO_RESOURCE_OVERFLOW;
        }
        void *grown = mi_realloc(cooker->sources, bytes);
        if (grown == nullptr) {
            mi_free(copy);
            return ANO_RESOURCE_OUT_OF_MEMORY;
        }
        cooker->sources = static_cast<SourceBinding *>(grown);
        cooker->sourceCapacity = capacity;
    }
    cooker->sources[cooker->sourceCount++] = {source, copy};
    return ANO_RESOURCE_OK;
}

extern "C" AnoResourceError ano_resource_cook(
    AnoResourceCooker *cooker, AnoResourceMutableBytes *pack)
{
    if (cooker == nullptr || pack == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *pack = {};
    if (ano_resource_cooker_cancelled(cooker))
        return ANO_RESOURCE_CANCELLED;
    return ano_resource_pack_build(cooker->items, cooker->itemCount, pack);
}

extern "C" void ano_resource_cooked_pack_release(
    AnoResourceMutableBytes pack)
{
    mi_free(pack.data);
}

extern "C" void ano_resource_cooker_cancel(AnoResourceCooker *cooker)
{
    if (cooker != nullptr)
        atomic_store_explicit(&cooker->cancelled, true, memory_order_release);
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
    uint8_t *copy = static_cast<uint8_t *>(
        mi_malloc(static_cast<size_t>(artifact.size)));
    if (copy == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    memcpy(copy, artifact.data, static_cast<size_t>(artifact.size));
    const AnoResourceError result = ano_resource_cooker_adopt(
        cooker, asset, type, commitGroup, copy, artifact.size);
    if (result != ANO_RESOURCE_OK)
        mi_free(copy);
    return result;
}

AnoResourceCookCheckpoint ano_resource_cooker_checkpoint(
    const AnoResourceCooker *cooker)
{
    return cooker == nullptr
        ? AnoResourceCookCheckpoint{}
        : AnoResourceCookCheckpoint{
              .itemCount = cooker->itemCount,
              .nextDerivedAsset = cooker->nextDerivedAsset,
          };
}

void ano_resource_cooker_rollback(AnoResourceCooker *cooker,
                                  AnoResourceCookCheckpoint checkpoint)
{
    if (cooker == nullptr || checkpoint.itemCount > cooker->itemCount)
        return;
    for (uint64_t i = checkpoint.itemCount; i < cooker->itemCount; ++i) {
        mi_free(const_cast<uint8_t *>(cooker->items[i].artifact.data));
        cooker->items[i] = {};
    }
    cooker->itemCount = checkpoint.itemCount;
    cooker->nextDerivedAsset = checkpoint.nextDerivedAsset;
}

bool ano_resource_cooker_root_valid(const AnoResourceCooker *cooker,
                                    AnoAssetId root)
{
    return cooker != nullptr && root.value != 0
        && root.value < cooker->firstDerivedAsset.value
        && !contains_asset(cooker, root);
}

const char *ano_resource_cooker_source_path(
    const AnoResourceCooker *cooker, AnoResourceSourceId source)
{
    if (cooker == nullptr || source.value == 0)
        return nullptr;
    for (uint64_t i = 0; i < cooker->sourceCount; ++i)
        if (cooker->sources[i].source.value == source.value)
            return cooker->sources[i].path;
    return nullptr;
}

AnoResourceError ano_resource_cooker_allocate_derived(
    AnoResourceCooker *cooker, AnoAssetId *asset)
{
    if (cooker == nullptr || asset == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    if (ano_resource_cooker_cancelled(cooker))
        return ANO_RESOURCE_CANCELLED;
    while (contains_asset(cooker, cooker->nextDerivedAsset)) {
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

AnoResourceError ano_resource_cooker_adopt(
    AnoResourceCooker *cooker, AnoAssetId asset, AnoResourceTypeId type,
    AnoResourceCommitGroupId commitGroup, uint8_t *artifact,
    uint64_t artifactSize)
{
    if (cooker == nullptr || asset.value == 0 || type.value == 0
        || commitGroup.value == 0 || artifact == nullptr || artifactSize == 0)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    if (ano_resource_cooker_cancelled(cooker))
        return ANO_RESOURCE_CANCELLED;
    if (contains_asset(cooker, asset))
        return ANO_RESOURCE_DUPLICATE_ASSET;

    if (cooker->itemCount == cooker->itemCapacity) {
        const uint64_t capacity = cooker->itemCapacity == 0
            ? 16
            : cooker->itemCapacity * 2;
        size_t bytes = 0;
        if (capacity < cooker->itemCapacity
            || !ano::detail::checked_allocation_size(
                capacity, sizeof(AnoResourcePackItem), &bytes))
            return ANO_RESOURCE_OVERFLOW;
        void *grown = mi_realloc(cooker->items, bytes);
        if (grown == nullptr)
            return ANO_RESOURCE_OUT_OF_MEMORY;
        cooker->items = static_cast<AnoResourcePackItem *>(grown);
        cooker->itemCapacity = capacity;
    }
    cooker->items[cooker->itemCount++] = {
        .asset = asset,
        .type = type,
        .commitGroup = commitGroup,
        .artifact = {.data = artifact, .size = artifactSize},
    };
    return ANO_RESOURCE_OK;
}

bool ano_resource_cooker_cancelled(const AnoResourceCooker *cooker)
{
    return cooker != nullptr
        && atomic_load_explicit(&cooker->cancelled, memory_order_acquire);
}
