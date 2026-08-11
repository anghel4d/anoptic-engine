/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Residency goals, immutable epochs, and transactional manifest publication.

#ifndef ANOPTICENGINE_ANOPTIC_RESOURCES_RUNTIME_H
#define ANOPTICENGINE_ANOPTIC_RESOURCES_RUNTIME_H

#include "anoptic_resources_pack.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AnoResourceManager AnoResourceManager;
typedef struct AnoResidencyEpoch AnoResidencyEpoch;
typedef struct AnoResourceReload AnoResourceReload;

enum {
    ANO_RESOURCE_QUALITY_WHOLE = 0,
};

typedef struct AnoResourceGoal {
    AnoResourceGoalId goal;
    AnoAssetId asset;
    AnoResourceTypeId type;
    AnoResourceCommitGroupId commitGroup;
    AnoResourceQuality quality;
    float importance;
} AnoResourceGoal;

// The manager owns a validated copy of the pack and begins with an empty epoch.
AnoResourceError ano_resource_manager_create(AnoResourceBytes packBytes,
                                             AnoResourceManager **manager);
void ano_resource_manager_destroy(AnoResourceManager *manager);

// Goal IDs identify independent demand contributions. Setting an existing ID
// replaces that contribution; removing an absent ID reports not_found.
AnoResourceError ano_resource_goal_set(AnoResourceManager *manager,
                                       AnoResourceGoal goal);
AnoResourceError ano_resource_goal_remove(AnoResourceManager *manager,
                                          AnoResourceGoalId goal);

// Reconciliation publishes a successor only after every demanded commit-group
// floor and typed dependency is copied and validated.
AnoResourceError ano_resource_reconcile(AnoResourceManager *manager);

// A prepared reload owns a private candidate pack and epoch. Owner modules may
// realize reload_epoch before commit; abort or any failed commit preserves the
// published generation. Commit consumes the reload object.
AnoResourceError ano_resource_reload_prepare(AnoResourceManager *manager,
                                             AnoResourceBytes packBytes,
                                             AnoResourceReload **reload);
const AnoResidencyEpoch *ano_resource_reload_epoch(
    const AnoResourceReload *reload);
bool ano_resource_reload_has_changes(const AnoResourceReload *reload);
AnoResourceError ano_resource_reload_commit(AnoResourceReload *reload);
void ano_resource_reload_abort(AnoResourceReload *reload);

// Acquired epochs remain immutable and valid until their matching release.
AnoResourceError ano_resource_epoch_acquire(
    AnoResourceManager *manager, const AnoResidencyEpoch **epoch);
AnoResourceError ano_resource_epoch_retain(const AnoResidencyEpoch *epoch);
void ano_resource_epoch_release(const AnoResidencyEpoch *epoch);
AnoResidencyEpochId ano_resource_epoch_id(const AnoResidencyEpoch *epoch);
AnoResourceError ano_resource_epoch_manifest_id(
    const AnoResidencyEpoch *epoch, AnoManifestId *manifest);

// Resolve returns immutable canonical bytes borrowed from the acquired epoch.
AnoResourceError ano_resource_epoch_resolve(
    const AnoResidencyEpoch *epoch, AnoAssetId asset,
    AnoResourceTypeId requiredType, AnoResourceBytes *bytes);
uint64_t ano_resource_epoch_asset_count(const AnoResidencyEpoch *epoch);
AnoResourceError ano_resource_epoch_asset(
    const AnoResidencyEpoch *epoch, AnoAssetId asset,
    AnoResourceTypeId *type, bool *resident);
uint64_t ano_resource_epoch_changed_count(const AnoResidencyEpoch *epoch);
AnoResourceError ano_resource_epoch_changed(const AnoResidencyEpoch *epoch,
                                             uint64_t index,
                                             AnoAssetId *asset);

#ifdef __cplusplus
}
#endif

#endif // ANOPTICENGINE_ANOPTIC_RESOURCES_RUNTIME_H
