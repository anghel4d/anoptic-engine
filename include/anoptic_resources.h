/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
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

typedef struct AnoResourceTypeId {
    uint64_t value;
} AnoResourceTypeId;

typedef struct AnoResourceBytes {
    const uint8_t *data;
    uint64_t size;
} AnoResourceBytes;

typedef struct AnoResourceMutableBytes {
    uint8_t *data;
    uint64_t size;
} AnoResourceMutableBytes;

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
