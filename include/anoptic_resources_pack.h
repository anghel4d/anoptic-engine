/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Canonical manifests and vendor-neutral authenticated shipping packs.

#pragma once

#include "anoptic_resources.h"

namespace ano {

struct AnoResourceManifest;
struct AnoResourcePack;
struct AnoCookedRevision;

struct AnoResourceManifestEntry {
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
};

// Copies and fully validates canonical manifest bytes.
[[nodiscard]] ResourceResult<AnoResourceManifest *> resource_manifest_open(
    AnoResourceBytes bytes);
void resource_manifest_close(AnoResourceManifest *manifest);
[[nodiscard]] ResourceResult<AnoManifestId> resource_manifest_id(
    const AnoResourceManifest *manifest);
uint64_t resource_manifest_entry_count(
    const AnoResourceManifest *manifest);
[[nodiscard]] ResourceResult<AnoResourceManifestEntry> resource_manifest_find(
    const AnoResourceManifest *manifest, AnoAssetId asset);
[[nodiscard]] ResourceResult<AnoResourceDependency>
resource_manifest_dependency(const AnoResourceManifest *manifest,
                                 AnoAssetId asset, uint64_t index);

// Copies the pack and authenticates its manifest and every artifact.
[[nodiscard]] ResourceResult<AnoResourcePack *> resource_pack_open(
    AnoResourceBytes bytes);
void resource_pack_close(AnoResourcePack *pack);
const AnoResourceManifest *resource_pack_manifest(
    const AnoResourcePack *pack);

// Returns an authenticated immutable range borrowed from the opened pack.
[[nodiscard]] ResourceResult<AnoResourceBytes> resource_pack_view(
    const AnoResourcePack *pack, AnoAssetId asset);
// Returns a retained cooked revision over the authenticated pack volume.
[[nodiscard]] ResourceResult<const AnoCookedRevision *>
resource_pack_revision(const AnoResourcePack *pack);

// Shipping serialization is explicit; cook does not emit a pack.
[[nodiscard]] ResourceResult<AnoResourceMutableBytes>
resource_revision_export_pack(const AnoCookedRevision *revision);
void resource_exported_pack_release(AnoResourceMutableBytes pack);

} // namespace ano

#include "anoptic_resources_typed.h"

namespace ano::asset_schema {

struct [[=Artifact{}]] Manifest final {
    RelativeSpan<AnoResourceManifestEntry> entries;
    RelativeSpan<AnoResourceDependency> dependencies;
};

} // namespace ano::asset_schema
