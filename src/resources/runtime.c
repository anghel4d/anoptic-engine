/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_atomic.h>

using namespace ano;
#include <anoptic_memory_typed.h>
#include <anoptic_resources_runtime.h>
#include <anoptic_threads.h>

#include "cooker_internal.h"

#include <float.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace {

struct ResidencyBinding final {
    AnoResourceTypeId type;
    AnoContentId content;
    uint64_t dependencyFirst;
    uint64_t dependencyCount;
    ano::MemoryVolume *volume;
    ano::MemoryReservation artifact;
    bool resident;
};

struct EpochPlan final {
    ano::MemorySegment<ResidencyBinding> bindings;
    ano::MemorySegment<AnoAssetId> changed;
    ano::MemorySegment<ano::MemoryVolume *> retainedVolumes;
};

} // namespace

struct ano::AnoResidencyEpoch {
    ANO_ATOMIC(uint64_t) references;
    AnoResidencyEpochId id;
    AnoManifestId manifest;
    ano::MemoryVolume *volume;
    ResidencyBinding *bindings;
    uint64_t bindingCount;
    AnoAssetId *changed;
    uint64_t changedCount;
    const AnoResourceDependency *dependencies;
    uint64_t dependencyCount;
    ano::MemoryVolume **retainedVolumes;
    uint64_t retainedVolumeCount;
};

struct ano::AnoResourceManager {
    anothread_mutex_t mutex;
    const AnoCookedRevision *revision;
    AnoResourceGoal *goals;
    uint64_t goalCount;
    uint64_t goalCapacity;
    AnoResidencyEpoch *current;
    uint64_t nextEpoch;
    uint64_t goalRevision;
};

struct ano::AnoResourceReload {
    AnoResourceManager *manager;
    const AnoCookedRevision *revision;
    AnoResidencyEpoch *epoch;
    AnoResidencyEpoch *base;
    uint64_t goalRevision;
    bool hasChanges;
};

namespace {

AnoManifestId revision_manifest_id(const AnoCookedRevision *revision)
{
    ano::detail::Sha256 hash;
    ano::detail::hash_u64(hash, revision->itemCount);
    ano::detail::hash_u64(hash, revision->dependencyCount);
    for (uint64_t i = 0; i < revision->itemCount; ++i) {
        const AnoResourceManifestEntry& entry = revision->entries[i];
        ano::detail::hash_u64(hash, entry.asset.value);
        ano::detail::hash_u64(hash, entry.type.value);
        ano::detail::hash_u64(hash, entry.producer);
        hash.append(entry.inputIdentity.bytes,
                    sizeof(entry.inputIdentity.bytes));
        ano::detail::hash_u64(hash, entry.commitGroup.value);
        hash.append(entry.schema.bytes, sizeof(entry.schema.bytes));
        hash.append(entry.content.bytes, sizeof(entry.content.bytes));
        ano::detail::hash_u64(hash, entry.dependencyCount);
    }
    for (uint64_t i = 0; i < revision->dependencyCount; ++i) {
        ano::detail::hash_u64(hash, revision->dependencies[i].asset.value);
        ano::detail::hash_u64(hash, revision->dependencies[i].type.value);
    }
    const AnoContentId digest = hash.finish();
    AnoManifestId result{};
    memcpy(result.bytes, digest.bytes, sizeof(result.bytes));
    return result;
}

bool retain_epoch(AnoResidencyEpoch *epoch)
{
    if (epoch == nullptr)
        return false;
    uint64_t references = atomic_load_explicit(
        &epoch->references, memory_order_relaxed);
    do {
        if (references == 0 || references == UINT64_MAX)
            return false;
    } while (!atomic_compare_exchange_weak_explicit(
        &epoch->references, &references, references + 1,
        memory_order_relaxed, memory_order_relaxed));
    return true;
}

void destroy_epoch(AnoResidencyEpoch *epoch)
{
    if (epoch == nullptr)
        return;
    for (uint64_t i = epoch->retainedVolumeCount; i != 0; --i)
        ano::memory_volume_release(epoch->retainedVolumes[i - 1]);
    ano::memory_volume_release(epoch->volume);
    mi_free(epoch);
}

void destroy_reload(AnoResourceReload *reload)
{
    if (reload == nullptr)
        return;
    resource_revision_release(reload->revision);
    resource_epoch_release(reload->base);
    resource_epoch_release(reload->epoch);
    mi_free(reload);
}

AnoResourceError build_demand(
    const AnoCookedRevision *revision, const AnoResourceGoal *goals,
    uint64_t goalCount, uint8_t *demanded, uint64_t *stack)
{
    const uint64_t assetCount = revision->itemCount;
    uint64_t stackCount = 0;
    for (uint64_t i = 0; i < goalCount; ++i) {
        const AnoResourceGoal& goal = goals[i];
        const AnoResourceManifestEntry *root = ano_resource_revision_entry(
            revision, goal.asset);
        if (root == nullptr)
            return ANO_RESOURCE_NOT_FOUND;
        if (root->type.value != goal.type.value)
            return ANO_RESOURCE_TYPE_MISMATCH;
        if (root->commitGroup.value != goal.commitGroup.value)
            return ANO_RESOURCE_BAD_MANIFEST;
        for (uint64_t j = 0; j < assetCount; ++j)
            if (revision->entries[j].commitGroup.value
                    == goal.commitGroup.value
                && demanded[j] == 0) {
                demanded[j] = 1;
                stack[stackCount++] = j;
            }
    }
    while (stackCount != 0) {
        const AnoResourceManifestEntry& entry =
            revision->entries[stack[--stackCount]];
        for (uint64_t i = 0; i < entry.dependencyCount; ++i) {
            const AnoResourceDependency& dependency =
                revision->dependencies[entry.dependencyFirst + i];
            const uint64_t target = dependency.asset.value - 1;
            if (target >= assetCount)
                return ANO_RESOURCE_BAD_MANIFEST;
            if (demanded[target] == 0) {
                demanded[target] = 1;
                stack[stackCount++] = target;
            }
        }
    }
    return ANO_RESOURCE_OK;
}

AnoResourceError build_epoch(
    const AnoCookedRevision *revision, const AnoResidencyEpoch *previous,
    const AnoResourceGoal *goals, uint64_t goalCount,
    AnoResidencyEpochId id, bool recordChanges,
    AnoResidencyEpoch **output)
{
    if (revision == nullptr || output == nullptr
        || (goals == nullptr && goalCount != 0))
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *output = nullptr;
    AnoResourceError result = ANO_RESOURCE_OK;
    const uint64_t assetCount = revision->itemCount;
    if (assetCount >= SIZE_MAX)
        return ANO_RESOURCE_OVERFLOW;

    ano::MemoryRegion *scratch = result == ANO_RESOURCE_OK
        ? ano::memory_region_create().value_or(nullptr) : nullptr;
    if (result == ANO_RESOURCE_OK && scratch == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    uint8_t *demanded = result == ANO_RESOURCE_OK && assetCount != 0
        ? ano::memory_region_allocate_zero<uint8_t>(
              scratch, static_cast<size_t>(assetCount)).value_or(nullptr)
        : nullptr;
    uint64_t *stack = result == ANO_RESOURCE_OK && assetCount != 0
        ? ano::memory_region_allocate<uint64_t>(
              scratch, static_cast<size_t>(assetCount)).value_or(nullptr)
        : nullptr;
    if (result == ANO_RESOURCE_OK && assetCount != 0
        && (demanded == nullptr || stack == nullptr))
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    if (result == ANO_RESOURCE_OK)
        result = build_demand(
            revision, goals, goalCount, demanded, stack);
    ano::MemoryVolume **ownerCandidates = result == ANO_RESOURCE_OK
        ? ano::memory_region_allocate<ano::MemoryVolume *>(
              scratch,
              static_cast<size_t>(assetCount + 1)).value_or(nullptr)
        : nullptr;
    if (result == ANO_RESOURCE_OK && ownerCandidates == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    uint64_t ownerCount = 0;
    if (result == ANO_RESOURCE_OK)
        ownerCandidates[ownerCount++] = revision->volumes[0];
    for (uint64_t i = 0; i < assetCount && result == ANO_RESOURCE_OK; ++i) {
        if (demanded[i] == 0)
            continue;
        ano::MemoryVolume *owner = revision->storage[i].volume;
        bool known = false;
        for (uint64_t j = 0; j < ownerCount; ++j)
            known |= ownerCandidates[j] == owner;
        if (!known)
            ownerCandidates[ownerCount++] = owner;
    }

    uint64_t compareCount = assetCount;
    if (previous != nullptr && previous->bindingCount > compareCount)
        compareCount = previous->bindingCount;
    if (compareCount > SIZE_MAX)
        result = ANO_RESOURCE_OVERFLOW;
    EpochPlan plan{
        .bindings = {.count = static_cast<size_t>(assetCount)},
        .changed = {.count = recordChanges
            ? static_cast<size_t>(compareCount) : 0},
        .retainedVolumes = {.count = static_cast<size_t>(ownerCount)},
    };
    const auto layout = ano::memory_layout(plan);
    if (result == ANO_RESOURCE_OK && !layout)
        result = ANO_RESOURCE_OVERFLOW;
    ano::MemoryVolume *volume = result == ANO_RESOURCE_OK
        ? ano::memory_volume_create(*layout).value_or(nullptr) : nullptr;
    AnoResidencyEpoch *candidate = result == ANO_RESOURCE_OK
        ? mi_zalloc_tp(AnoResidencyEpoch) : nullptr;
    if (result == ANO_RESOURCE_OK
        && (volume == nullptr || candidate == nullptr))
        result = ANO_RESOURCE_OUT_OF_MEMORY;

    auto bindings = result == ANO_RESOURCE_OK
        ? ano::memory_volume_write(volume, plan.bindings).value_or(
              std::span<ResidencyBinding>{})
        : std::span<ResidencyBinding>{};
    auto changed = result == ANO_RESOURCE_OK
        ? ano::memory_volume_write(volume, plan.changed).value_or(
              std::span<AnoAssetId>{})
        : std::span<AnoAssetId>{};
    auto retainedVolumes = result == ANO_RESOURCE_OK
        ? ano::memory_volume_write(volume, plan.retainedVolumes).value_or(
              std::span<ano::MemoryVolume *>{})
        : std::span<ano::MemoryVolume *>{};
    if (result == ANO_RESOURCE_OK && assetCount != 0
        && bindings.data() == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;

    for (uint64_t i = 0; i < assetCount && result == ANO_RESOURCE_OK; ++i) {
        bindings[i] = {
            .type = revision->entries[i].type,
            .content = revision->entries[i].content,
            .dependencyFirst = revision->entries[i].dependencyFirst,
            .dependencyCount = revision->entries[i].dependencyCount,
            .volume = demanded[i] != 0 ? revision->storage[i].volume : nullptr,
            .artifact = demanded[i] != 0
                ? revision->storage[i].artifact : ano::MemoryReservation{},
            .resident = demanded[i] != 0,
        };
    }
    uint64_t changedCount = 0;
    for (uint64_t i = 0;
         i < compareCount && result == ANO_RESOURCE_OK && recordChanges;
         ++i) {
        const bool hasOld = previous != nullptr
            && i < previous->bindingCount;
        const bool hasNew = i < assetCount;
        if (hasOld && hasNew
            && previous->bindings[i].type.value != bindings[i].type.value) {
            result = ANO_RESOURCE_BAD_MANIFEST;
            break;
        }
        if (hasOld != hasNew
            || (hasOld && (!resource_content_id_equal(
                    previous->bindings[i].content, bindings[i].content)
                || previous->bindings[i].resident
                    != bindings[i].resident)))
            changed[changedCount++] = {i + 1};
    }

    uint64_t retainedCount = 0;
    for (; retainedCount < ownerCount && result == ANO_RESOURCE_OK;
         ++retainedCount) {
        if (!ano::memory_volume_retain(ownerCandidates[retainedCount])) {
            result = ANO_RESOURCE_OVERFLOW;
            break;
        }
        retainedVolumes[retainedCount] = ownerCandidates[retainedCount];
    }
    if (result == ANO_RESOURCE_OK && !ano::memory_volume_seal(volume))
        result = ANO_RESOURCE_BAD_MANIFEST;
    if (result == ANO_RESOURCE_OK) {
        atomic_init(&candidate->references, UINT64_C(1));
        candidate->id = id;
        candidate->manifest = revision_manifest_id(revision);
        candidate->volume = volume;
        candidate->bindings = bindings.data();
        candidate->bindingCount = assetCount;
        candidate->changed = changed.data();
        candidate->changedCount = changedCount;
        candidate->dependencies = revision->dependencies;
        candidate->dependencyCount = revision->dependencyCount;
        candidate->retainedVolumes = retainedVolumes.data();
        candidate->retainedVolumeCount = retainedCount;
        *output = candidate;
        candidate = nullptr;
        volume = nullptr;
        retainedCount = 0;
    }
    for (uint64_t i = retainedCount; i != 0; --i)
        ano::memory_volume_release(retainedVolumes[i - 1]);
    mi_free(candidate);
    ano::memory_volume_release(volume);
    ano::memory_region_destroy(scratch);
    return result;
}

bool epoch_has_changes(const AnoResidencyEpoch *candidate,
                       const AnoResidencyEpoch *current)
{
    return candidate->changedCount != 0
        || !ano::detail::bytes_equal(
            candidate->manifest.bytes, current->manifest.bytes,
            sizeof(candidate->manifest.bytes));
}

AnoResourceError lock_manager(AnoResourceManager *manager)
{
    return manager != nullptr && mutex_lock(&manager->mutex)
        ? ANO_RESOURCE_OK : ANO_RESOURCE_INVALID_ARGUMENT;
}

} // namespace

ResourceResult<AnoResourceManager *> ano::resource_manager_create(
    const AnoCookedRevision *revision)
{
    if (revision == nullptr)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    AnoResourceError result = ano_resource_revision_validate(revision);
    AnoResourceManager *created = result == ANO_RESOURCE_OK
        ? mi_zalloc_tp(AnoResourceManager) : nullptr;
    if (result == ANO_RESOURCE_OK && created == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    bool mutexReady = false;
    if (result == ANO_RESOURCE_OK) {
        mutexReady = mutex_init(&created->mutex, nullptr).has_value();
        if (!mutexReady)
            result = ANO_RESOURCE_IO_ERROR;
    }
    if (result == ANO_RESOURCE_OK) {
        const auto retained = resource_revision_retain(revision);
        result = retained ? ANO_RESOURCE_OK : retained.error();
    }
    if (result == ANO_RESOURCE_OK) {
        created->revision = revision;
        created->nextEpoch = 2;
        created->goalRevision = 1;
        result = build_epoch(
            revision, nullptr, nullptr, 0, {1}, false,
            &created->current);
    }
    if (result != ANO_RESOURCE_OK) {
        if (created != nullptr) {
            resource_revision_release(created->revision);
            if (mutexReady)
                (void)mutex_destroy(&created->mutex);
            mi_free(created);
        }
        return failure(result);
    }
    return created;
}

void ano::resource_manager_destroy(AnoResourceManager *manager)
{
    if (manager == nullptr)
        return;
    const AnoCookedRevision *revision = nullptr;
    AnoResidencyEpoch *current = nullptr;
    if (mutex_lock(&manager->mutex)) {
        revision = manager->revision;
        current = manager->current;
        manager->revision = nullptr;
        manager->current = nullptr;
        (void)mutex_unlock(&manager->mutex);
    }
    (void)mutex_destroy(&manager->mutex);
    mi_free(manager->goals);
    resource_epoch_release(current);
    resource_revision_release(revision);
    mi_free(manager);
}

ResourceResult<> ano::resource_goal_set(
    AnoResourceManager *manager, AnoResourceGoal goal)
{
    if (manager == nullptr || goal.goal.value == 0 || goal.asset.value == 0
        || goal.type.value == 0 || goal.commitGroup.value == 0
        || goal.quality.level != ANO_RESOURCE_QUALITY_WHOLE
        || goal.importance != goal.importance
        || goal.importance < 0.0f || goal.importance > FLT_MAX)
        return failure(goal.quality.level != ANO_RESOURCE_QUALITY_WHOLE
            ? ANO_RESOURCE_UNSUPPORTED : ANO_RESOURCE_INVALID_ARGUMENT);
    AnoResourceError result = lock_manager(manager);
    if (result != ANO_RESOURCE_OK)
        return failure(result);
    const AnoResourceManifestEntry *item = ano_resource_revision_entry(
        manager->revision, goal.asset);
    if (item == nullptr)
        result = ANO_RESOURCE_NOT_FOUND;
    if (result == ANO_RESOURCE_OK && item->type.value != goal.type.value)
        result = ANO_RESOURCE_TYPE_MISMATCH;
    if (result == ANO_RESOURCE_OK
        && item->commitGroup.value != goal.commitGroup.value)
        result = ANO_RESOURCE_BAD_MANIFEST;

    uint64_t index = manager->goalCount;
    for (uint64_t i = 0; i < manager->goalCount; ++i)
        if (manager->goals[i].goal.value == goal.goal.value) {
            index = i;
            break;
        }
    if (result == ANO_RESOURCE_OK && index == manager->goalCount) {
        if (manager->goalCount == manager->goalCapacity) {
            const auto required = ano::checked_add(
                manager->goalCount, UINT64_C(1));
            if (!required
                || *required > SIZE_MAX / sizeof(AnoResourceGoal)) {
                result = ANO_RESOURCE_OVERFLOW;
            } else if (!ano::reserve_array(
                           manager->goals, manager->goalCapacity,
                           *required, 4)) {
                result = ANO_RESOURCE_OUT_OF_MEMORY;
            }
        }
        if (result == ANO_RESOURCE_OK)
            ++manager->goalCount;
    }
    if (result == ANO_RESOURCE_OK) {
        manager->goals[index] = goal;
        ++manager->goalRevision;
    }
    (void)mutex_unlock(&manager->mutex);
    return resource_status(result);
}

ResourceResult<> ano::resource_goal_remove(
    AnoResourceManager *manager, AnoResourceGoalId goal)
{
    if (manager == nullptr || goal.value == 0)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    AnoResourceError result = lock_manager(manager);
    if (result != ANO_RESOURCE_OK)
        return failure(result);
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
    (void)mutex_unlock(&manager->mutex);
    return resource_status(result);
}

ResourceResult<> ano::resource_reconcile(
    AnoResourceManager *manager)
{
    AnoResourceError result = lock_manager(manager);
    if (result != ANO_RESOURCE_OK)
        return failure(result);
    AnoResidencyEpoch *candidate = nullptr;
    result = build_epoch(
        manager->revision, manager->current, manager->goals,
        manager->goalCount, {manager->nextEpoch}, true, &candidate);
    AnoResidencyEpoch *retired = nullptr;
    if (result == ANO_RESOURCE_OK
        && epoch_has_changes(candidate, manager->current)) {
        retired = manager->current;
        manager->current = candidate;
        candidate = nullptr;
        ++manager->nextEpoch;
    }
    (void)mutex_unlock(&manager->mutex);
    resource_epoch_release(retired);
    resource_epoch_release(candidate);
    return resource_status(result);
}

ResourceResult<AnoResourceReload *> ano::resource_reload_prepare(
    AnoResourceManager *manager, const AnoCookedRevision *revision)
{
    if (manager == nullptr || revision == nullptr)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    AnoResourceError result = ano_resource_revision_validate(revision);
    AnoResourceReload *prepared = result == ANO_RESOURCE_OK
        ? mi_zalloc_tp(AnoResourceReload) : nullptr;
    if (result == ANO_RESOURCE_OK && prepared == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    if (result == ANO_RESOURCE_OK)
        result = lock_manager(manager);
    if (result != ANO_RESOURCE_OK) {
        mi_free(prepared);
        return failure(result);
    }
    result = build_epoch(
        revision, manager->current, manager->goals, manager->goalCount,
        {manager->nextEpoch}, true, &prepared->epoch);
    if (result == ANO_RESOURCE_OK) {
        const auto retained = resource_revision_retain(revision);
        result = retained ? ANO_RESOURCE_OK : retained.error();
    }
    if (result == ANO_RESOURCE_OK)
        prepared->revision = revision;
    if (result == ANO_RESOURCE_OK && !retain_epoch(manager->current))
        result = ANO_RESOURCE_OVERFLOW;
    if (result == ANO_RESOURCE_OK)
        prepared->base = manager->current;
    if (result == ANO_RESOURCE_OK) {
        prepared->manager = manager;
        prepared->goalRevision = manager->goalRevision;
        prepared->hasChanges = epoch_has_changes(
            prepared->epoch, manager->current);
    }
    (void)mutex_unlock(&manager->mutex);
    if (result != ANO_RESOURCE_OK) {
        destroy_reload(prepared);
        return failure(result);
    }
    return prepared;
}

const AnoResidencyEpoch *ano::resource_reload_epoch(
    const AnoResourceReload *reload)
{
    return reload == nullptr ? nullptr : reload->epoch;
}

bool ano::resource_reload_has_changes(
    const AnoResourceReload *reload)
{
    return reload != nullptr && reload->hasChanges;
}

void ano::resource_reload_abort(AnoResourceReload *reload)
{
    destroy_reload(reload);
}

ResourceResult<> ano::resource_reload_commit(
    AnoResourceReload *reload)
{
    if (reload == nullptr || reload->manager == nullptr)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    AnoResourceManager *manager = reload->manager;
    const AnoCookedRevision *retiredRevision = nullptr;
    AnoResidencyEpoch *retiredEpoch = nullptr;
    AnoResourceError result = lock_manager(manager);
    if (result == ANO_RESOURCE_OK) {
        if (manager->current != reload->base
            || manager->goalRevision != reload->goalRevision) {
            result = ANO_RESOURCE_CANCELLED;
        } else {
            retiredRevision = manager->revision;
            manager->revision = reload->revision;
            reload->revision = nullptr;
            if (reload->hasChanges) {
                retiredEpoch = manager->current;
                manager->current = reload->epoch;
                reload->epoch = nullptr;
                ++manager->nextEpoch;
            }
        }
        (void)mutex_unlock(&manager->mutex);
    }
    resource_revision_release(retiredRevision);
    resource_epoch_release(retiredEpoch);
    destroy_reload(reload);
    return resource_status(result);
}

ResourceResult<const AnoResidencyEpoch *> ano::resource_epoch_acquire(
    AnoResourceManager *manager)
{
    if (manager == nullptr)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    AnoResourceError result = lock_manager(manager);
    if (result != ANO_RESOURCE_OK)
        return failure(result);
    const AnoResidencyEpoch *epoch = nullptr;
    if (!retain_epoch(manager->current))
        result = ANO_RESOURCE_OVERFLOW;
    else
        epoch = manager->current;
    (void)mutex_unlock(&manager->mutex);
    if (result != ANO_RESOURCE_OK)
        return failure(result);
    return epoch;
}

ResourceResult<> ano::resource_epoch_retain(
    const AnoResidencyEpoch *epoch)
{
    if (epoch == nullptr)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    if (!retain_epoch(const_cast<AnoResidencyEpoch *>(epoch)))
        return failure(ANO_RESOURCE_OVERFLOW);
    return {};
}

void ano::resource_epoch_release(
    const AnoResidencyEpoch *epoch)
{
    if (epoch == nullptr)
        return;
    AnoResidencyEpoch *mutableEpoch = const_cast<AnoResidencyEpoch *>(epoch);
    if (atomic_fetch_sub_explicit(
            &mutableEpoch->references, UINT64_C(1), memory_order_acq_rel) == 1)
        destroy_epoch(mutableEpoch);
}

AnoResidencyEpochId ano::resource_epoch_id(
    const AnoResidencyEpoch *epoch)
{
    return epoch == nullptr ? AnoResidencyEpochId{0} : epoch->id;
}

ResourceResult<AnoManifestId> ano::resource_epoch_manifest_id(
    const AnoResidencyEpoch *epoch)
{
    if (epoch == nullptr)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    return epoch->manifest;
}

ResourceResult<AnoResourceBytes> ano::resource_epoch_resolve(
    const AnoResidencyEpoch *epoch, AnoAssetId asset,
    AnoResourceTypeId requiredType)
{
    if (epoch == nullptr || requiredType.value == 0)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    if (asset.value == 0 || asset.value > epoch->bindingCount)
        return failure(ANO_RESOURCE_NOT_FOUND);
    const ResidencyBinding& binding = epoch->bindings[asset.value - 1];
    if (binding.type.value != requiredType.value)
        return failure(ANO_RESOURCE_TYPE_MISMATCH);
    if (!binding.resident)
        return failure(ANO_RESOURCE_NOT_FOUND);
    const auto viewed = ano::memory_volume_view(
        binding.volume, binding.artifact);
    if (!viewed || viewed->size() != binding.artifact.size)
        return failure(ANO_RESOURCE_BAD_MANIFEST);
    const std::span<const uint8_t> view = *viewed;
    return AnoResourceBytes{
        .data = view.data(),
        .size = view.size(),
    };
}

ResourceResult<std::span<const AnoResourceDependency>>
ano::resource_epoch_dependencies(const AnoResidencyEpoch *epoch,
                                     AnoAssetId asset)
{
    if (epoch == nullptr)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    if (asset.value == 0 || asset.value > epoch->bindingCount)
        return failure(ANO_RESOURCE_NOT_FOUND);
    const ResidencyBinding& binding = epoch->bindings[asset.value - 1];
    if (binding.dependencyFirst > epoch->dependencyCount
        || binding.dependencyCount
            > epoch->dependencyCount - binding.dependencyFirst)
        return failure(ANO_RESOURCE_BAD_MANIFEST);
    return std::span<const AnoResourceDependency>{
        binding.dependencyCount == 0 ? nullptr
            : epoch->dependencies + binding.dependencyFirst,
        static_cast<size_t>(binding.dependencyCount)};
}

uint64_t ano::resource_epoch_asset_count(
    const AnoResidencyEpoch *epoch)
{
    return epoch == nullptr ? 0 : epoch->bindingCount;
}

ResourceResult<AnoResourceAssetState> ano::resource_epoch_asset(
    const AnoResidencyEpoch *epoch, AnoAssetId asset)
{
    if (epoch == nullptr)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    if (asset.value == 0 || asset.value > epoch->bindingCount)
        return failure(ANO_RESOURCE_NOT_FOUND);
    const ResidencyBinding& binding = epoch->bindings[asset.value - 1];
    return AnoResourceAssetState{binding.type, binding.resident};
}

uint64_t ano::resource_epoch_changed_count(
    const AnoResidencyEpoch *epoch)
{
    return epoch == nullptr ? 0 : epoch->changedCount;
}

ResourceResult<AnoAssetId> ano::resource_epoch_changed(
    const AnoResidencyEpoch *epoch, uint64_t index)
{
    if (epoch == nullptr)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    if (index >= epoch->changedCount)
        return failure(ANO_RESOURCE_OUT_OF_BOUNDS);
    return epoch->changed[index];
}
