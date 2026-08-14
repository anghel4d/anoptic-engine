/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Residency goals, immutable epochs, and transactional manifest publication.

#pragma once

#include "anoptic_resources_pack.h"

#include <span>

namespace ano {

struct AnoResourceManager;
struct AnoResidencyEpoch;
struct AnoResourceReload;

struct AnoResourceAssetState final {
    AnoResourceTypeId type;
    bool resident;
};

enum {
    ANO_RESOURCE_QUALITY_WHOLE = 0,
};

struct AnoResourceGoal {
    AnoResourceGoalId goal;
    AnoAssetId asset;
    AnoResourceTypeId type;
    AnoResourceCommitGroupId commitGroup;
    AnoResourceQuality quality;
    float importance;
};

// Retains the cooked revision. The first epoch has no resident assets.
[[nodiscard]] ResourceResult<AnoResourceManager *> ano_resource_manager_create(
    const AnoCookedRevision *revision);
void ano_resource_manager_destroy(AnoResourceManager *manager);

// Goal IDs are independent demand contributions. Setting an existing ID
// replaces it; removing an absent ID reports not_found.
[[nodiscard]] ResourceResult<> ano_resource_goal_set(
    AnoResourceManager *manager, AnoResourceGoal goal);
[[nodiscard]] ResourceResult<> ano_resource_goal_remove(
    AnoResourceManager *manager, AnoResourceGoalId goal);

// Publishes a successor only after every demanded commit-group floor and typed
// dependency is present in one immutable revision.
[[nodiscard]] ResourceResult<> ano_resource_reconcile(
    AnoResourceManager *manager);

// Retains a private candidate revision and epoch. Owners may realize
// reload_epoch before commit. Abort or a failed commit preserves the published
// generation; commit consumes the reload.
[[nodiscard]] ResourceResult<AnoResourceReload *> ano_resource_reload_prepare(
    AnoResourceManager *manager, const AnoCookedRevision *revision);
const AnoResidencyEpoch *ano_resource_reload_epoch(
    const AnoResourceReload *reload);
bool ano_resource_reload_has_changes(const AnoResourceReload *reload);
[[nodiscard]] ResourceResult<> ano_resource_reload_commit(
    AnoResourceReload *reload);
void ano_resource_reload_abort(AnoResourceReload *reload);

// Acquired epochs remain immutable and valid until their matching release.
[[nodiscard]] ResourceResult<const AnoResidencyEpoch *>
ano_resource_epoch_acquire(AnoResourceManager *manager);
[[nodiscard]] ResourceResult<> ano_resource_epoch_retain(
    const AnoResidencyEpoch *epoch);
void ano_resource_epoch_release(const AnoResidencyEpoch *epoch);
AnoResidencyEpochId ano_resource_epoch_id(const AnoResidencyEpoch *epoch);
[[nodiscard]] ResourceResult<AnoManifestId> ano_resource_epoch_manifest_id(
    const AnoResidencyEpoch *epoch);

// Returns immutable canonical bytes borrowed from the acquired epoch.
[[nodiscard]] ResourceResult<AnoResourceBytes> ano_resource_epoch_resolve(
    const AnoResidencyEpoch *epoch, AnoAssetId asset,
    AnoResourceTypeId requiredType);
// Returns the immutable dependency row borrowed from the live epoch.
[[nodiscard]] ResourceResult<std::span<const AnoResourceDependency>>
ano_resource_epoch_dependencies(const AnoResidencyEpoch *epoch,
                                AnoAssetId asset);
uint64_t ano_resource_epoch_asset_count(const AnoResidencyEpoch *epoch);
[[nodiscard]] ResourceResult<AnoResourceAssetState> ano_resource_epoch_asset(
    const AnoResidencyEpoch *epoch, AnoAssetId asset);
uint64_t ano_resource_epoch_changed_count(const AnoResidencyEpoch *epoch);
[[nodiscard]] ResourceResult<AnoAssetId> ano_resource_epoch_changed(
    const AnoResidencyEpoch *epoch, uint64_t index);

} // namespace ano
