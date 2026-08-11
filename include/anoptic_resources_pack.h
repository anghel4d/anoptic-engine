/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
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

typedef struct AnoResourceManifestEntry {
    AnoAssetId asset;
    AnoResourceTypeId type;
    AnoSchemaFingerprint schema;
    AnoContentId content;
    AnoResourceCommitGroupId commitGroup;
    uint64_t packOffset;
    uint64_t packedSize;
    uint64_t unpackedSize;
    uint64_t dependencyFirst;
    uint64_t dependencyCount;
} AnoResourceManifestEntry;

typedef struct AnoResourcePackItem {
    AnoAssetId asset;
    AnoResourceTypeId type;
    AnoResourceCommitGroupId commitGroup;
    AnoResourceBytes artifact;
} AnoResourcePackItem;

// Builds deterministic raw-atom pack bytes. Dependencies and schemas are
// compiled from each reflected artifact declaration and canonical payload.
AnoResourceError ano_resource_pack_build(const AnoResourcePackItem *items,
                                         uint64_t itemCount,
                                         AnoResourceMutableBytes output,
                                         uint64_t *packSize);

// Opening copies and completely validates canonical manifest bytes.
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

// Opening copies the pack and authenticates its manifest and every artifact.
AnoResourceError ano_resource_pack_open(AnoResourceBytes bytes,
                                        AnoResourcePack **pack);
void ano_resource_pack_close(AnoResourcePack *pack);
const AnoResourceManifest *ano_resource_pack_manifest(
    const AnoResourcePack *pack);

// A read rechecks the selected extent, content identity, schema, and artifact.
// packSize receives the unpacked size on success or insufficient capacity.
AnoResourceError ano_resource_pack_read(const AnoResourcePack *pack,
                                        AnoAssetId asset,
                                        AnoResourceMutableBytes output,
                                        uint64_t *packSize);

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
