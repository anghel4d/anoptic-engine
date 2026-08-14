/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Reflected source import and deterministic pack cooking.

#pragma once

#include "anoptic_resources_pack.h"

namespace ano {

struct AnoResourceCooker;

struct AnoResourceCookerConfig {
    AnoAssetId firstDerivedAsset;
    uint32_t workerCount;
};

struct AnoResourceImportRequest {
    AnoResourceSourceId source;
    AnoAssetId rootAsset;
    AnoResourceCommitGroupId commitGroup;
};

using AnoResourceEncodeFunction = ResourceResult<> (*)(
    void *context, AnoResourceMutableBytes destination);

struct AnoResourceCookArtifact {
    AnoAssetId asset;
    AnoResourceTypeId type;
    AnoResourceCommitGroupId commitGroup;
    uint64_t encodedSize;
    AnoContentId inputIdentity;
    void *context;
    AnoResourceEncodeFunction encode;
};

[[nodiscard]] ResourceResult<AnoResourceCooker *> ano_resource_cooker_create(
    AnoResourceCookerConfig config);
void ano_resource_cooker_destroy(AnoResourceCooker *cooker);
// Discards the pending cook. Bindings, snapshots, action keys, executor, and
// the current revision stay.
[[nodiscard]] ResourceResult<> ano_resource_cooker_begin(
    AnoResourceCooker *cooker);

// Source IDs are stable. Rebinding replaces the path; import requests and
// semantic asset IDs stay.
[[nodiscard]] ResourceResult<> ano_resource_source_bind(
    AnoResourceCooker *cooker, AnoResourceSourceId source, const char *path);

// Import dispatch is generated from reflected Importer annotations.
[[nodiscard]] ResourceResult<> ano_resource_import(
    AnoResourceCooker *cooker, const AnoResourceImportRequest& request);

// Copies canonical artifact bytes. Callers keep the input span.
[[nodiscard]] ResourceResult<> ano_resource_cooker_add(
    AnoResourceCooker *cooker, AnoAssetId asset, AnoResourceTypeId type,
    AnoResourceCommitGroupId commitGroup, AnoResourceBytes artifact);
// Caller supplies encodedSize. Encoding writes disjoint reservations in sealed
// volumes. Callbacks complete before return.
[[nodiscard]] ResourceResult<> ano_resource_cooker_encode_batch(
    AnoResourceCooker *cooker, AnoResourceCookArtifact *artifacts,
    uint64_t count);

// Publishes an immutable revision. Unchanged artifacts keep their volumes;
// changed ones occupy newly sealed volumes.
[[nodiscard]] ResourceResult<const AnoCookedRevision *> ano_resource_cook(
    AnoResourceCooker *cooker);
[[nodiscard]] ResourceResult<> ano_resource_revision_retain(
    const AnoCookedRevision *revision);
void ano_resource_revision_release(const AnoCookedRevision *revision);
[[nodiscard]] ResourceResult<AnoResourceBytes> ano_resource_revision_resolve(
    const AnoCookedRevision *revision, AnoAssetId asset,
    AnoResourceTypeId requiredType);
void ano_resource_cooker_cancel(AnoResourceCooker *cooker);

namespace detail {

template<class ArtifactType>
struct CookArtifactContext final {
    ArtifactSource<ArtifactType> source;
};

template<class ArtifactType>
ResourceResult<> encode_cooked_artifact(
    void *opaque, AnoResourceMutableBytes destination)
{
    const auto& context =
        *static_cast<const CookArtifactContext<ArtifactType> *>(opaque);
    const EncodeResult encoded = encode(context.source, destination);
    if (!encoded)
        return failure(encoded.error());
    if (*encoded != destination.size)
        return failure(ANO_RESOURCE_NON_CANONICAL);
    return {};
}

} // namespace detail

template<class ArtifactType>
ResourceResult<> cook_artifact(
    AnoResourceCooker *cooker, AssetRef<ArtifactType> asset,
    AnoResourceCommitGroupId group, ArtifactSource<ArtifactType> source)
{
    const EncodeResult measured = encoded_size(source);
    if (!measured)
        return failure(measured.error());
    detail::CookArtifactContext<ArtifactType> context{source};
    AnoResourceCookArtifact artifact{
        .asset = asset.id,
        .type = resource_type_id<ArtifactType>(),
        .commitGroup = group,
        .encodedSize = *measured,
        .context = &context,
        .encode = detail::encode_cooked_artifact<ArtifactType>,
    };
    return ano_resource_cooker_encode_batch(cooker, &artifact, 1);
}

} // namespace ano
