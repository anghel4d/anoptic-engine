/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Reflected source import and deterministic pack cooking.

#ifndef ANOPTICENGINE_ANOPTIC_RESOURCES_COOK_H
#define ANOPTICENGINE_ANOPTIC_RESOURCES_COOK_H

#include "anoptic_resources_pack.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AnoResourceCooker AnoResourceCooker;

typedef struct AnoResourceCookerConfig {
    AnoAssetId firstDerivedAsset;
    uint32_t workerCount;
} AnoResourceCookerConfig;

typedef struct AnoResourceImportRequest {
    AnoResourceSourceId source;
    AnoAssetId rootAsset;
    AnoResourceCommitGroupId commitGroup;
} AnoResourceImportRequest;

typedef AnoResourceError (*AnoResourceEncodeFunction)(
    void *context, AnoResourceMutableBytes destination);

typedef struct AnoResourceCookArtifact {
    AnoAssetId asset;
    AnoResourceTypeId type;
    AnoResourceCommitGroupId commitGroup;
    uint64_t encodedSize;
    AnoContentId inputIdentity;
    void *context;
    AnoResourceEncodeFunction encode;
} AnoResourceCookArtifact;

AnoResourceError ano_resource_cooker_create(
    AnoResourceCookerConfig config, AnoResourceCooker **cooker);
void ano_resource_cooker_destroy(AnoResourceCooker *cooker);
// Begins a replacement transaction while preserving the persistent instance
// graph, source snapshots, current action keys, executor, and current revision.
AnoResourceError ano_resource_cooker_begin(AnoResourceCooker *cooker);

// Source IDs are stable import handles. Binding the same ID again replaces its
// provider path; import requests and semantic asset IDs remain unchanged.
AnoResourceError ano_resource_source_bind(AnoResourceCooker *cooker,
                                          AnoResourceSourceId source,
                                          const char *path);

// Import dispatch is generated from reflected Importer annotations.
AnoResourceError ano_resource_import(AnoResourceCooker *cooker,
                                     const AnoResourceImportRequest *request);

// Adds canonical bytes produced by a reflected typed encoder or transform.
// The cooker copies the bytes; callers retain ownership of the input span.
AnoResourceError ano_resource_cooker_add(
    AnoResourceCooker *cooker, AnoAssetId asset, AnoResourceTypeId type,
    AnoResourceCommitGroupId commitGroup, AnoResourceBytes artifact);
// Measures first, then encodes directly into disjoint reservations in bounded
// immutable generation volumes. Callbacks complete before this function returns.
AnoResourceError ano_resource_cooker_encode_batch(
    AnoResourceCooker *cooker, AnoResourceCookArtifact *artifacts,
    uint64_t count);

// Cooking publishes an immutable in-memory revision. Unchanged action results
// retain their existing volumes; changed artifacts occupy newly sealed volumes.
AnoResourceError ano_resource_cook(AnoResourceCooker *cooker,
                                   const AnoCookedRevision **revision);
AnoResourceError ano_resource_revision_retain(
    const AnoCookedRevision *revision);
void ano_resource_revision_release(const AnoCookedRevision *revision);
AnoResourceError ano_resource_revision_resolve(
    const AnoCookedRevision *revision, AnoAssetId asset,
    AnoResourceTypeId requiredType, AnoResourceBytes *bytes);
void ano_resource_cooker_cancel(AnoResourceCooker *cooker);

#ifdef __cplusplus
}

namespace ano {
namespace detail {

template<class ArtifactType>
struct CookArtifactContext final {
    ArtifactSource<ArtifactType> source;
};

template<class ArtifactType>
AnoResourceError encode_cooked_artifact(
    void *opaque, AnoResourceMutableBytes destination)
{
    const auto& context =
        *static_cast<const CookArtifactContext<ArtifactType> *>(opaque);
    const EncodeResult encoded = encode(context.source, destination);
    return encoded.error != ANO_RESOURCE_OK
        ? encoded.error
        : encoded.size == destination.size
            ? ANO_RESOURCE_OK : ANO_RESOURCE_NON_CANONICAL;
}

} // namespace detail

template<class ArtifactType>
AnoResourceError cook_artifact(
    AnoResourceCooker *cooker, AssetRef<ArtifactType> asset,
    AnoResourceCommitGroupId group, ArtifactSource<ArtifactType> source)
{
    const EncodeResult measured = encoded_size(source);
    if (measured.error != ANO_RESOURCE_OK)
        return measured.error;
    detail::CookArtifactContext<ArtifactType> context{source};
    AnoResourceCookArtifact artifact{
        .asset = asset.id,
        .type = resource_type_id<ArtifactType>(),
        .commitGroup = group,
        .encodedSize = measured.size,
        .context = &context,
        .encode = detail::encode_cooked_artifact<ArtifactType>,
    };
    return ano_resource_cooker_encode_batch(cooker, &artifact, 1);
}

} // namespace ano
#endif

#endif // ANOPTICENGINE_ANOPTIC_RESOURCES_COOK_H
