/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. Compiler/library incompleteness disqualifies the toolchain; it does not constrain the architecture. */
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
} AnoResourceCookerConfig;

typedef struct AnoResourceImportRequest {
    AnoResourceSourceId source;
    AnoAssetId rootAsset;
    AnoResourceCommitGroupId commitGroup;
} AnoResourceImportRequest;

AnoResourceError ano_resource_cooker_create(
    AnoResourceCookerConfig config, AnoResourceCooker **cooker);
void ano_resource_cooker_destroy(AnoResourceCooker *cooker);

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

// Returns one owned pack allocation. ano_resource_cooked_pack_release releases it.
AnoResourceError ano_resource_cook(AnoResourceCooker *cooker,
                                   AnoResourceMutableBytes *pack);
void ano_resource_cooked_pack_release(AnoResourceMutableBytes pack);
void ano_resource_cooker_cancel(AnoResourceCooker *cooker);

#ifdef __cplusplus
}
#endif

#endif // ANOPTICENGINE_ANOPTIC_RESOURCES_COOK_H
