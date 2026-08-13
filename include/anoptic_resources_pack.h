/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Canonical manifests and vendor-neutral authenticated shipping packs.

#ifndef ANOPTICENGINE_ANOPTIC_RESOURCES_PACK_H
#define ANOPTICENGINE_ANOPTIC_RESOURCES_PACK_H

#include "anoptic_resources.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AnoResourceManifest AnoResourceManifest;
typedef struct AnoResourcePack AnoResourcePack;
typedef struct AnoCookedRevision AnoCookedRevision;

typedef struct AnoResourceManifestEntry {
    AnoAssetId asset;
    AnoResourceTypeId type;
    uint64_t producer;
    AnoContentId inputIdentity;
    AnoSchemaFingerprint schema;
    AnoContentId content;
    AnoResourceCommitGroupId commitGroup;
    uint64_t byteSize;
    uint64_t dependencyFirst;
    uint64_t dependencyCount;
} AnoResourceManifestEntry;

// Copies and fully validates canonical manifest bytes.
AnoResourceError ano_resource_manifest_open(AnoResourceBytes bytes,
                                            AnoResourceManifest **manifest);
void ano_resource_manifest_close(AnoResourceManifest *manifest);
AnoResourceError ano_resource_manifest_id(const AnoResourceManifest *manifest,
                                          AnoManifestId *id);
uint64_t ano_resource_manifest_entry_count(
    const AnoResourceManifest *manifest);
AnoResourceError ano_resource_manifest_find(
    const AnoResourceManifest *manifest, AnoAssetId asset,
    AnoResourceManifestEntry *entry);
AnoResourceError ano_resource_manifest_dependency(
    const AnoResourceManifest *manifest, AnoAssetId asset, uint64_t index,
    AnoResourceDependency *dependency);

// Copies the pack and authenticates its manifest and every artifact.
AnoResourceError ano_resource_pack_open(AnoResourceBytes bytes,
                                        AnoResourcePack **pack);
void ano_resource_pack_close(AnoResourcePack *pack);
const AnoResourceManifest *ano_resource_pack_manifest(
    const AnoResourcePack *pack);

// Copies an authenticated artifact. packSize receives the artifact size on
// success or insufficient capacity.
AnoResourceError ano_resource_pack_read(const AnoResourcePack *pack,
                                        AnoAssetId asset,
                                        AnoResourceMutableBytes output,
                                        uint64_t *packSize);
// Returns an authenticated immutable range borrowed from the opened pack.
AnoResourceError ano_resource_pack_view(const AnoResourcePack *pack,
                                        AnoAssetId asset,
                                        AnoResourceBytes *bytes);
// Returns a retained cooked revision over the authenticated pack volume.
AnoResourceError ano_resource_pack_revision(
    const AnoResourcePack *pack, const AnoCookedRevision **revision);

// Shipping serialization is explicit; cook does not emit a pack.
AnoResourceError ano_resource_revision_export_pack(
    const AnoCookedRevision *revision, AnoResourceMutableBytes *pack);
void ano_resource_exported_pack_release(AnoResourceMutableBytes pack);

#ifdef __cplusplus
}

#include "anoptic_resources_typed.h"

namespace ano::asset_schema {

struct [[=Artifact{}]] Manifest final {
    RelativeSpan<AnoResourceManifestEntry> entries;
    RelativeSpan<AnoResourceDependency> dependencies;
};

} // namespace ano::asset_schema
#endif

#endif // ANOPTICENGINE_ANOPTIC_RESOURCES_PACK_H
