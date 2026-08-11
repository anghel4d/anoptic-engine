/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_atomic.h>
#include <anoptic_memory.h>
#include <anoptic_resources_runtime.h>
#include <anoptic_threads.h>

#include <float.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace {

struct ResidencyBinding final {
    AnoResourceTypeId type;
    AnoContentId content;
    uint64_t offset;
    uint64_t size;
    bool resident;
};

constexpr bool digest_equal(const uint8_t *lhs, const uint8_t *rhs)
{
    for (uint32_t i = 0; i < 32; ++i)
        if (lhs[i] != rhs[i])
            return false;
    return true;
}

bool allocation_size(uint64_t count, uint64_t width, size_t *bytes)
{
    uint64_t total = 0;
    if (bytes == nullptr || !ano::detail::checked_multiply(count, width, &total)
        || total > SIZE_MAX)
        return false;
    *bytes = static_cast<size_t>(total);
    return true;
}

} // namespace

struct AnoResidencyEpoch {
    ANO_ATOMIC(uint64_t) references;
    AnoResidencyEpochId id;
    AnoManifestId manifest;
    ResidencyBinding *bindings;
    uint64_t bindingCount;
    uint8_t *arena;
    uint64_t arenaSize;
    AnoAssetId *changed;
    uint64_t changedCount;
};

struct AnoResourceManager {
    anothread_mutex_t mutex;
    AnoResourcePack *pack;
    AnoResourceGoal *goals;
    uint64_t goalCount;
    uint64_t goalCapacity;
    AnoResidencyEpoch *current;
    uint64_t nextEpoch;
    uint64_t goalRevision;
};

struct AnoResourceReload {
    AnoResourceManager *manager;
    AnoResourcePack *pack;
    AnoResidencyEpoch *epoch;
    AnoResidencyEpoch *base;
    uint64_t goalRevision;
    bool hasChanges;
};

namespace {

void destroy_epoch(AnoResidencyEpoch *epoch)
{
    if (epoch == nullptr)
        return;
    mi_free(epoch->changed);
    mi_free(epoch->arena);
    mi_free(epoch->bindings);
    mi_free(epoch);
}

bool retain_epoch(AnoResidencyEpoch *epoch)
{
    if (epoch == nullptr)
        return false;
    uint64_t references = atomic_load_explicit(
        &epoch->references, memory_order_relaxed);
    for (;;) {
        if (references == UINT64_MAX)
            return false;
        if (atomic_compare_exchange_weak_explicit(
                &epoch->references, &references, references + 1,
                memory_order_relaxed, memory_order_relaxed))
            return true;
    }
}

AnoResourceError load_manifest_entries(
    const AnoResourceManifest *manifest, AnoResourceManifestEntry *entries,
    uint64_t count)
{
    for (uint64_t i = 0; i < count; ++i) {
        const AnoResourceError found =
            ano_resource_manifest_find(manifest, {i + 1}, &entries[i]);
        if (found != ANO_RESOURCE_OK)
            return found;
    }
    return ANO_RESOURCE_OK;
}

AnoResourceError build_epoch(
    const AnoResourcePack *pack, const AnoResidencyEpoch *previous,
    const AnoResourceGoal *goals, uint64_t goalCount,
    AnoResidencyEpochId epochId, bool recordChanges,
    AnoResidencyEpoch **epoch)
{
    if (pack == nullptr || epoch == nullptr
        || (goals == nullptr && goalCount != 0))
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *epoch = nullptr;
    const AnoResourceManifest *manifest = ano_resource_pack_manifest(pack);
    const uint64_t assetCount = ano_resource_manifest_entry_count(manifest);

    size_t entryBytes = 0;
    size_t demandBytes = 0;
    size_t stackBytes = 0;
    size_t bindingBytes = 0;
    if (!allocation_size(assetCount, sizeof(AnoResourceManifestEntry),
                         &entryBytes)
        || !allocation_size(assetCount, sizeof(uint8_t), &demandBytes)
        || !allocation_size(assetCount, sizeof(uint64_t), &stackBytes)
        || !allocation_size(assetCount, sizeof(ResidencyBinding),
                            &bindingBytes))
        return ANO_RESOURCE_OVERFLOW;

    AnoResourceManifestEntry *entries = entryBytes == 0 ? nullptr
        : static_cast<AnoResourceManifestEntry *>(mi_malloc(entryBytes));
    uint8_t *demanded = demandBytes == 0 ? nullptr
        : static_cast<uint8_t *>(mi_calloc(1, demandBytes));
    uint64_t *stack = stackBytes == 0 ? nullptr
        : static_cast<uint64_t *>(mi_malloc(stackBytes));
    if ((entryBytes != 0 && entries == nullptr)
        || (demandBytes != 0 && demanded == nullptr)
        || (stackBytes != 0 && stack == nullptr)) {
        mi_free(stack);
        mi_free(demanded);
        mi_free(entries);
        return ANO_RESOURCE_OUT_OF_MEMORY;
    }

    AnoResourceError result =
        load_manifest_entries(manifest, entries, assetCount);
    uint64_t stackCount = 0;
    for (uint64_t i = 0; i < goalCount && result == ANO_RESOURCE_OK; ++i) {
        const AnoResourceGoal& goal = goals[i];
        if (goal.asset.value == 0 || goal.asset.value > assetCount) {
            result = ANO_RESOURCE_NOT_FOUND;
            break;
        }
        const AnoResourceManifestEntry& root = entries[goal.asset.value - 1];
        if (root.type.value != goal.type.value) {
            result = ANO_RESOURCE_TYPE_MISMATCH;
            break;
        }
        if (root.commitGroup.value != goal.commitGroup.value) {
            result = ANO_RESOURCE_BAD_MANIFEST;
            break;
        }
        for (uint64_t j = 0; j < assetCount; ++j) {
            if (entries[j].commitGroup.value == goal.commitGroup.value
                && demanded[j] == 0) {
                demanded[j] = 1;
                stack[stackCount++] = j;
            }
        }
    }

    while (stackCount != 0 && result == ANO_RESOURCE_OK) {
        const uint64_t current = stack[--stackCount];
        const AnoResourceManifestEntry& entry = entries[current];
        for (uint64_t i = 0; i < entry.dependencyCount; ++i) {
            AnoResourceDependency dependency = {};
            result = ano_resource_manifest_dependency(
                manifest, entry.asset, i, &dependency);
            if (result != ANO_RESOURCE_OK)
                break;
            const uint64_t target = dependency.asset.value - 1;
            if (demanded[target] == 0) {
                demanded[target] = 1;
                stack[stackCount++] = target;
            }
        }
    }

    AnoResidencyEpoch *candidate = nullptr;
    if (result == ANO_RESOURCE_OK) {
        candidate = static_cast<AnoResidencyEpoch *>(
            mi_calloc(1, sizeof(AnoResidencyEpoch)));
        if (candidate == nullptr)
            result = ANO_RESOURCE_OUT_OF_MEMORY;
    }
    if (result == ANO_RESOURCE_OK && bindingBytes != 0) {
        candidate->bindings = static_cast<ResidencyBinding *>(
            mi_calloc(1, bindingBytes));
        if (candidate->bindings == nullptr)
            result = ANO_RESOURCE_OUT_OF_MEMORY;
    }

    uint64_t arenaSize = 0;
    for (uint64_t i = 0; i < assetCount && result == ANO_RESOURCE_OK; ++i) {
        ResidencyBinding& binding = candidate->bindings[i];
        binding.type = entries[i].type;
        binding.content = entries[i].content;
        if (demanded[i] == 0)
            continue;
        uint64_t artifactSize = 0;
        const AnoResourceError measured = ano_resource_pack_read(
            pack, entries[i].asset, {nullptr, 0}, &artifactSize);
        if (measured != ANO_RESOURCE_BUFFER_TOO_SMALL)
            result = measured;
        else if (!ano::detail::checked_add(arenaSize, artifactSize,
                                           &arenaSize)
                 || arenaSize > SIZE_MAX)
            result = ANO_RESOURCE_OVERFLOW;
    }
    if (result == ANO_RESOURCE_OK && arenaSize != 0) {
        candidate->arena =
            static_cast<uint8_t *>(mi_malloc(static_cast<size_t>(arenaSize)));
        if (candidate->arena == nullptr)
            result = ANO_RESOURCE_OUT_OF_MEMORY;
    }

    uint64_t arenaCursor = 0;
    for (uint64_t i = 0; i < assetCount && result == ANO_RESOURCE_OK; ++i) {
        if (demanded[i] == 0)
            continue;
        ResidencyBinding& binding = candidate->bindings[i];
        uint64_t artifactSize = arenaSize - arenaCursor;
        result = ano_resource_pack_read(
            pack, entries[i].asset,
            {.data = candidate->arena + arenaCursor,
             .size = artifactSize},
            &artifactSize);
        if (result == ANO_RESOURCE_OK) {
            binding.offset = arenaCursor;
            binding.size = artifactSize;
            binding.resident = true;
            arenaCursor += artifactSize;
        }
    }
    if (result == ANO_RESOURCE_OK && arenaCursor != arenaSize)
        result = ANO_RESOURCE_BAD_PACK;

    uint64_t compareCount = assetCount;
    if (previous != nullptr && previous->bindingCount > compareCount)
        compareCount = previous->bindingCount;
    size_t changedBytes = 0;
    if (result == ANO_RESOURCE_OK && recordChanges
        && !allocation_size(compareCount, sizeof(AnoAssetId), &changedBytes))
        result = ANO_RESOURCE_OVERFLOW;
    if (result == ANO_RESOURCE_OK && changedBytes != 0) {
        candidate->changed =
            static_cast<AnoAssetId *>(mi_malloc(changedBytes));
        if (candidate->changed == nullptr)
            result = ANO_RESOURCE_OUT_OF_MEMORY;
    }

    for (uint64_t i = 0;
         i < compareCount && result == ANO_RESOURCE_OK && recordChanges; ++i) {
        const bool hasOld = previous != nullptr && i < previous->bindingCount;
        const bool hasNew = i < assetCount;
        if (hasOld && hasNew
            && previous->bindings[i].type.value
                != candidate->bindings[i].type.value) {
            result = ANO_RESOURCE_BAD_MANIFEST;
            break;
        }
        const bool changed = hasOld != hasNew
            || (hasOld && hasNew
                && (!digest_equal(previous->bindings[i].content.bytes,
                                  candidate->bindings[i].content.bytes)
                    || previous->bindings[i].resident
                        != candidate->bindings[i].resident));
        if (changed)
            candidate->changed[candidate->changedCount++] = {i + 1};
    }

    if (result == ANO_RESOURCE_OK) {
        candidate->id = epochId;
        candidate->bindingCount = assetCount;
        candidate->arenaSize = arenaSize;
        atomic_init(&candidate->references, UINT64_C(1));
        result = ano_resource_manifest_id(manifest, &candidate->manifest);
    }

    mi_free(stack);
    mi_free(demanded);
    mi_free(entries);
    if (result != ANO_RESOURCE_OK) {
        destroy_epoch(candidate);
        return result;
    }
    *epoch = candidate;
    return ANO_RESOURCE_OK;
}

bool epoch_has_changes(const AnoResidencyEpoch *candidate,
                       const AnoResidencyEpoch *current)
{
    return candidate->changedCount != 0
        || !digest_equal(candidate->manifest.bytes, current->manifest.bytes);
}

AnoResourceError lock_manager(AnoResourceManager *manager)
{
    return manager != nullptr && ano_mutex_lock(&manager->mutex) == 0
        ? ANO_RESOURCE_OK
        : ANO_RESOURCE_INVALID_ARGUMENT;
}

} // namespace

extern "C" AnoResourceError ano_resource_manager_create(
    AnoResourceBytes packBytes, AnoResourceManager **manager)
{
    if (manager == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *manager = nullptr;
    AnoResourcePack *pack = nullptr;
    AnoResourceError result = ano_resource_pack_open(packBytes, &pack);
    if (result != ANO_RESOURCE_OK)
        return result;

    AnoResourceManager *created = static_cast<AnoResourceManager *>(
        mi_calloc(1, sizeof(AnoResourceManager)));
    if (created == nullptr) {
        ano_resource_pack_close(pack);
        return ANO_RESOURCE_OUT_OF_MEMORY;
    }
    if (ano_mutex_init(&created->mutex, nullptr) != 0) {
        mi_free(created);
        ano_resource_pack_close(pack);
        return ANO_RESOURCE_IO_ERROR;
    }
    created->pack = pack;
    created->nextEpoch = 2;
    created->goalRevision = 1;
    result = build_epoch(pack, nullptr, nullptr, 0, {1}, false,
                         &created->current);
    if (result != ANO_RESOURCE_OK) {
        ano_mutex_destroy(&created->mutex);
        ano_resource_pack_close(pack);
        mi_free(created);
        return result;
    }
    *manager = created;
    return ANO_RESOURCE_OK;
}

extern "C" void ano_resource_manager_destroy(AnoResourceManager *manager)
{
    if (manager == nullptr)
        return;
    AnoResidencyEpoch *current = nullptr;
    AnoResourcePack *pack = nullptr;
    if (ano_mutex_lock(&manager->mutex) == 0) {
        current = manager->current;
        pack = manager->pack;
        manager->current = nullptr;
        manager->pack = nullptr;
        ano_mutex_unlock(&manager->mutex);
    }
    ano_mutex_destroy(&manager->mutex);
    mi_free(manager->goals);
    ano_resource_pack_close(pack);
    ano_resource_epoch_release(current);
    mi_free(manager);
}

extern "C" AnoResourceError ano_resource_goal_set(
    AnoResourceManager *manager, AnoResourceGoal goal)
{
    if (manager == nullptr || goal.goal.value == 0 || goal.asset.value == 0
        || goal.type.value == 0 || goal.commitGroup.value == 0
        || goal.quality.level != ANO_RESOURCE_QUALITY_WHOLE
        || goal.importance != goal.importance
        || goal.importance < 0.0f || goal.importance > FLT_MAX)
        return goal.quality.level != ANO_RESOURCE_QUALITY_WHOLE
            ? ANO_RESOURCE_UNSUPPORTED
            : ANO_RESOURCE_INVALID_ARGUMENT;
    AnoResourceError result = lock_manager(manager);
    if (result != ANO_RESOURCE_OK)
        return result;

    AnoResourceManifestEntry entry = {};
    result = ano_resource_manifest_find(ano_resource_pack_manifest(manager->pack),
                                        goal.asset, &entry);
    if (result == ANO_RESOURCE_OK && entry.type.value != goal.type.value)
        result = ANO_RESOURCE_TYPE_MISMATCH;
    if (result == ANO_RESOURCE_OK
        && entry.commitGroup.value != goal.commitGroup.value)
        result = ANO_RESOURCE_BAD_MANIFEST;

    uint64_t existing = manager->goalCount;
    for (uint64_t i = 0; i < manager->goalCount; ++i)
        if (manager->goals[i].goal.value == goal.goal.value) {
            existing = i;
            break;
        }
    if (result == ANO_RESOURCE_OK && existing == manager->goalCount) {
        if (manager->goalCount == manager->goalCapacity) {
            const uint64_t capacity = manager->goalCapacity == 0
                ? 4
                : manager->goalCapacity * 2;
            size_t bytes = 0;
            if (capacity < manager->goalCapacity
                || !allocation_size(capacity, sizeof(AnoResourceGoal), &bytes)) {
                result = ANO_RESOURCE_OVERFLOW;
            } else {
                void *grown = mi_realloc(manager->goals, bytes);
                if (grown == nullptr)
                    result = ANO_RESOURCE_OUT_OF_MEMORY;
                else {
                    manager->goals = static_cast<AnoResourceGoal *>(grown);
                    manager->goalCapacity = capacity;
                }
            }
        }
        if (result == ANO_RESOURCE_OK)
            ++manager->goalCount;
    }
    if (result == ANO_RESOURCE_OK)
        manager->goals[existing] = goal;
    if (result == ANO_RESOURCE_OK)
        ++manager->goalRevision;
    ano_mutex_unlock(&manager->mutex);
    return result;
}

extern "C" AnoResourceError ano_resource_goal_remove(
    AnoResourceManager *manager, AnoResourceGoalId goal)
{
    if (manager == nullptr || goal.value == 0)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    AnoResourceError result = lock_manager(manager);
    if (result != ANO_RESOURCE_OK)
        return result;
    uint64_t found = manager->goalCount;
    for (uint64_t i = 0; i < manager->goalCount; ++i)
        if (manager->goals[i].goal.value == goal.value) {
            found = i;
            break;
        }
    if (found == manager->goalCount) {
        result = ANO_RESOURCE_NOT_FOUND;
    } else {
        --manager->goalCount;
        if (found != manager->goalCount)
            memmove(manager->goals + found, manager->goals + found + 1,
                    static_cast<size_t>((manager->goalCount - found)
                                        * sizeof(AnoResourceGoal)));
        ++manager->goalRevision;
    }
    ano_mutex_unlock(&manager->mutex);
    return result;
}

extern "C" AnoResourceError ano_resource_reconcile(
    AnoResourceManager *manager)
{
    AnoResourceError result = lock_manager(manager);
    if (result != ANO_RESOURCE_OK)
        return result;
    AnoResidencyEpoch *candidate = nullptr;
    result = build_epoch(manager->pack, manager->current, manager->goals,
                         manager->goalCount, {manager->nextEpoch}, true,
                         &candidate);
    if (result == ANO_RESOURCE_OK
        && epoch_has_changes(candidate, manager->current)) {
        AnoResidencyEpoch *retired = manager->current;
        manager->current = candidate;
        ++manager->nextEpoch;
        candidate = nullptr;
        ano_resource_epoch_release(retired);
    }
    ano_resource_epoch_release(candidate);
    ano_mutex_unlock(&manager->mutex);
    return result;
}

extern "C" AnoResourceError ano_resource_reload_prepare(
    AnoResourceManager *manager, AnoResourceBytes packBytes,
    AnoResourceReload **reload)
{
    if (manager == nullptr || reload == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *reload = nullptr;
    AnoResourcePack *candidatePack = nullptr;
    AnoResourceError result = ano_resource_pack_open(packBytes, &candidatePack);
    if (result != ANO_RESOURCE_OK)
        return result;
    AnoResourceReload *prepared = static_cast<AnoResourceReload *>(
        mi_calloc(1, sizeof(AnoResourceReload)));
    if (prepared == nullptr) {
        ano_resource_pack_close(candidatePack);
        return ANO_RESOURCE_OUT_OF_MEMORY;
    }
    result = lock_manager(manager);
    if (result != ANO_RESOURCE_OK) {
        ano_resource_pack_close(candidatePack);
        mi_free(prepared);
        return result;
    }

    result = build_epoch(candidatePack, manager->current, manager->goals,
                         manager->goalCount, {manager->nextEpoch}, true,
                         &prepared->epoch);
    if (result == ANO_RESOURCE_OK && !retain_epoch(manager->current))
        result = ANO_RESOURCE_OVERFLOW;
    if (result == ANO_RESOURCE_OK) {
        prepared->manager = manager;
        prepared->pack = candidatePack;
        prepared->base = manager->current;
        prepared->goalRevision = manager->goalRevision;
        prepared->hasChanges = epoch_has_changes(
            prepared->epoch, manager->current);
        candidatePack = nullptr;
    }
    ano_mutex_unlock(&manager->mutex);
    ano_resource_pack_close(candidatePack);
    if (result != ANO_RESOURCE_OK) {
        ano_resource_epoch_release(prepared->epoch);
        mi_free(prepared);
        return result;
    }
    *reload = prepared;
    return ANO_RESOURCE_OK;
}

extern "C" const AnoResidencyEpoch *ano_resource_reload_epoch(
    const AnoResourceReload *reload)
{
    return reload == nullptr ? nullptr : reload->epoch;
}

extern "C" bool ano_resource_reload_has_changes(
    const AnoResourceReload *reload)
{
    return reload != nullptr && reload->hasChanges;
}

extern "C" void ano_resource_reload_abort(AnoResourceReload *reload)
{
    if (reload == nullptr)
        return;
    ano_resource_epoch_release(reload->base);
    ano_resource_epoch_release(reload->epoch);
    ano_resource_pack_close(reload->pack);
    mi_free(reload);
}

extern "C" AnoResourceError ano_resource_reload_commit(
    AnoResourceReload *reload)
{
    if (reload == nullptr || reload->manager == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    AnoResourceManager *manager = reload->manager;
    AnoResourcePack *retiredPack = nullptr;
    AnoResidencyEpoch *retiredEpoch = nullptr;
    AnoResourceError result = lock_manager(manager);
    if (result == ANO_RESOURCE_OK) {
        if (manager->current != reload->base
            || manager->goalRevision != reload->goalRevision) {
            result = ANO_RESOURCE_CANCELLED;
        } else {
            retiredPack = manager->pack;
            manager->pack = reload->pack;
            reload->pack = nullptr;
            if (reload->hasChanges) {
                retiredEpoch = manager->current;
                manager->current = reload->epoch;
                reload->epoch = nullptr;
                ++manager->nextEpoch;
            }
        }
        ano_mutex_unlock(&manager->mutex);
    }
    ano_resource_pack_close(retiredPack);
    ano_resource_epoch_release(retiredEpoch);
    ano_resource_epoch_release(reload->base);
    ano_resource_epoch_release(reload->epoch);
    ano_resource_pack_close(reload->pack);
    mi_free(reload);
    return result;
}

extern "C" AnoResourceError ano_resource_reload(
    AnoResourceManager *manager, AnoResourceBytes packBytes)
{
    AnoResourceReload *reload = nullptr;
    const AnoResourceError prepared = ano_resource_reload_prepare(
        manager, packBytes, &reload);
    return prepared == ANO_RESOURCE_OK
        ? ano_resource_reload_commit(reload) : prepared;
}

extern "C" AnoResourceError ano_resource_epoch_acquire(
    AnoResourceManager *manager, const AnoResidencyEpoch **epoch)
{
    if (manager == nullptr || epoch == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *epoch = nullptr;
    AnoResourceError result = lock_manager(manager);
    if (result != ANO_RESOURCE_OK)
        return result;
    const uint64_t references = atomic_load_explicit(
        &manager->current->references, memory_order_relaxed);
    if (references == UINT64_MAX) {
        result = ANO_RESOURCE_OVERFLOW;
    } else {
        atomic_fetch_add_explicit(&manager->current->references, UINT64_C(1),
                                  memory_order_relaxed);
        *epoch = manager->current;
    }
    ano_mutex_unlock(&manager->mutex);
    return result;
}

extern "C" void ano_resource_epoch_release(const AnoResidencyEpoch *epoch)
{
    if (epoch == nullptr)
        return;
    AnoResidencyEpoch *mutableEpoch = const_cast<AnoResidencyEpoch *>(epoch);
    if (atomic_fetch_sub_explicit(&mutableEpoch->references, UINT64_C(1),
                                  memory_order_acq_rel) == 1)
        destroy_epoch(mutableEpoch);
}

extern "C" AnoResourceError ano_resource_epoch_retain(
    const AnoResidencyEpoch *epoch)
{
    if (epoch == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    return retain_epoch(const_cast<AnoResidencyEpoch *>(epoch))
        ? ANO_RESOURCE_OK : ANO_RESOURCE_OVERFLOW;
}

extern "C" AnoResidencyEpochId ano_resource_epoch_id(
    const AnoResidencyEpoch *epoch)
{
    return epoch == nullptr ? AnoResidencyEpochId{0} : epoch->id;
}

extern "C" AnoResourceError ano_resource_epoch_manifest_id(
    const AnoResidencyEpoch *epoch, AnoManifestId *manifest)
{
    if (epoch == nullptr || manifest == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *manifest = epoch->manifest;
    return ANO_RESOURCE_OK;
}

extern "C" AnoResourceError ano_resource_epoch_resolve(
    const AnoResidencyEpoch *epoch, AnoAssetId asset,
    AnoResourceTypeId requiredType, AnoResourceBytes *bytes)
{
    if (epoch == nullptr || bytes == nullptr || requiredType.value == 0)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *bytes = {.data = nullptr, .size = 0};
    if (asset.value == 0 || asset.value > epoch->bindingCount)
        return ANO_RESOURCE_NOT_FOUND;
    const ResidencyBinding& binding = epoch->bindings[asset.value - 1];
    if (binding.type.value != requiredType.value)
        return ANO_RESOURCE_TYPE_MISMATCH;
    if (!binding.resident)
        return ANO_RESOURCE_NOT_FOUND;
    if (!ano::detail::byte_range(epoch->arenaSize, binding.offset,
                                 binding.size))
        return ANO_RESOURCE_BAD_PACK;
    *bytes = {
        .data = epoch->arena + binding.offset,
        .size = binding.size,
    };
    return ANO_RESOURCE_OK;
}

extern "C" uint64_t ano_resource_epoch_asset_count(
    const AnoResidencyEpoch *epoch)
{
    return epoch == nullptr ? 0 : epoch->bindingCount;
}

extern "C" AnoResourceError ano_resource_epoch_asset(
    const AnoResidencyEpoch *epoch, AnoAssetId asset,
    AnoResourceTypeId *type, bool *resident)
{
    if (epoch == nullptr || type == nullptr || resident == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    if (asset.value == 0 || asset.value > epoch->bindingCount)
        return ANO_RESOURCE_NOT_FOUND;
    const ResidencyBinding& binding = epoch->bindings[asset.value - 1];
    *type = binding.type;
    *resident = binding.resident;
    return ANO_RESOURCE_OK;
}

extern "C" uint64_t ano_resource_epoch_changed_count(
    const AnoResidencyEpoch *epoch)
{
    return epoch == nullptr ? 0 : epoch->changedCount;
}

extern "C" AnoResourceError ano_resource_epoch_changed(
    const AnoResidencyEpoch *epoch, uint64_t index, AnoAssetId *asset)
{
    if (epoch == nullptr || asset == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    if (index >= epoch->changedCount)
        return ANO_RESOURCE_OUT_OF_BOUNDS;
    *asset = epoch->changed[index];
    return ANO_RESOURCE_OK;
}
