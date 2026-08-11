/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. Compiler/library incompleteness disqualifies the toolchain; it does not constrain the architecture. */
/*  == Anoptic Game Engine v0.0000001 == */

// C boundary for resource identities, canonical bytes, and errors.

#ifndef ANOPTICENGINE_ANOPTIC_RESOURCES_H
#define ANOPTICENGINE_ANOPTIC_RESOURCES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AnoAssetId {
    uint64_t value;
} AnoAssetId;

typedef struct AnoResourceSourceId {
    uint64_t value;
} AnoResourceSourceId;

typedef struct AnoContentId {
    uint8_t bytes[32];
} AnoContentId;

typedef struct AnoSchemaFingerprint {
    uint8_t bytes[32];
} AnoSchemaFingerprint;

typedef struct AnoManifestId {
    uint8_t bytes[32];
} AnoManifestId;

typedef struct AnoResidencyEpochId {
    uint64_t value;
} AnoResidencyEpochId;

typedef struct AnoResourceGoalId {
    uint64_t value;
} AnoResourceGoalId;

typedef struct AnoResourceCommitGroupId {
    uint64_t value;
} AnoResourceCommitGroupId;

typedef struct AnoResourceQuality {
    uint32_t level;
} AnoResourceQuality;

typedef struct AnoResourceTypeId {
    uint64_t value;
} AnoResourceTypeId;

typedef struct AnoResourceDependency {
    AnoAssetId asset;
    AnoResourceTypeId type;
} AnoResourceDependency;

typedef struct AnoResourceBytes {
    const uint8_t *data;
    uint64_t size;
} AnoResourceBytes;

typedef struct AnoResourceMutableBytes {
    uint8_t *data;
    uint64_t size;
} AnoResourceMutableBytes;

typedef struct AnoResourceSchema {
    AnoResourceTypeId type;
    AnoSchemaFingerprint fingerprint;
    uint64_t fixedSize;
} AnoResourceSchema;

typedef enum AnoResourceError {
    ANO_RESOURCE_OK = 0,
    ANO_RESOURCE_INVALID_ARGUMENT,
    ANO_RESOURCE_BUFFER_TOO_SMALL,
    ANO_RESOURCE_OVERFLOW,
    ANO_RESOURCE_TRUNCATED,
    ANO_RESOURCE_BAD_MAGIC,
    ANO_RESOURCE_TYPE_MISMATCH,
    ANO_RESOURCE_SCHEMA_MISMATCH,
    ANO_RESOURCE_NON_CANONICAL,
    ANO_RESOURCE_OUT_OF_BOUNDS,
    ANO_RESOURCE_MISALIGNED_SOURCE,
    ANO_RESOURCE_DEPENDENCY_CAPACITY,
    ANO_RESOURCE_OUT_OF_MEMORY,
    ANO_RESOURCE_IO_ERROR,
    ANO_RESOURCE_NOT_FOUND,
    ANO_RESOURCE_DUPLICATE_ASSET,
    ANO_RESOURCE_BAD_MANIFEST,
    ANO_RESOURCE_BAD_PACK,
    ANO_RESOURCE_CANCELLED,
    ANO_RESOURCE_UNSUPPORTED,
    ANO_RESOURCE_OWNER_REJECTED,
    ANO_RESOURCE_ERROR_COUNT
} AnoResourceError;

static inline bool ano_asset_id_equal(AnoAssetId lhs, AnoAssetId rhs)
{
    return lhs.value == rhs.value;
}

static inline bool ano_resource_type_id_equal(AnoResourceTypeId lhs,
                                               AnoResourceTypeId rhs)
{
    return lhs.value == rhs.value;
}

const char *ano_resource_error_string(AnoResourceError error);
AnoResourceError ano_resource_content_id(AnoResourceBytes bytes,
                                         AnoContentId *contentId);
AnoResourceError ano_resource_artifact_schema(AnoResourceTypeId type,
                                              AnoResourceSchema *schema);
AnoResourceError ano_resource_validate_artifact(AnoResourceTypeId type,
                                                AnoResourceBytes bytes);
AnoResourceError ano_resource_artifact_dependencies(
    AnoResourceTypeId type, AnoResourceBytes bytes,
    AnoResourceDependency *dependencies, uint64_t dependencyCapacity,
    uint64_t *dependencyCount);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(sizeof(AnoContentId) == 32);
static_assert(sizeof(AnoSchemaFingerprint) == 32);
#else
_Static_assert(sizeof(AnoContentId) == 32, "AnoContentId must be 256 bits");
_Static_assert(sizeof(AnoSchemaFingerprint) == 32,
               "AnoSchemaFingerprint must be 256 bits");
#endif

#endif // ANOPTICENGINE_ANOPTIC_RESOURCES_H
