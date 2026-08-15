/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// C++26 resource identities, canonical bytes, and domain errors.

#pragma once

#include "anoptic_results.h"

#include <stddef.h>
#include <stdint.h>

namespace ano {

struct AnoAssetId final { uint64_t value; };
struct AnoResourceSourceId final { uint64_t value; };
struct AnoContentId final { uint8_t bytes[32]; };
struct AnoSchemaFingerprint final { uint8_t bytes[32]; };
struct AnoManifestId final { uint8_t bytes[32]; };
struct AnoResidencyEpochId final { uint64_t value; };
struct AnoResourceGoalId final { uint64_t value; };
struct AnoResourceCommitGroupId final { uint64_t value; };
struct AnoResourceQuality final { uint32_t level; };
struct AnoResourceTypeId final { uint64_t value; };
struct AnoResourceDependency final { AnoAssetId asset; AnoResourceTypeId type; };
struct AnoResourceBytes final { const uint8_t *data; uint64_t size; };
struct AnoResourceMutableBytes final { uint8_t *data; uint64_t size; };
struct AnoResourceSchema final {
    AnoResourceTypeId type; AnoSchemaFingerprint fingerprint; uint64_t fixedSize;
};

enum AnoResourceError {
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
};

template<class Value = void>
using ResourceResult = Result<Value, AnoResourceError>;

[[nodiscard]] constexpr ResourceResult<> resource_status(
    AnoResourceError error) noexcept
{
    return result_if(error == ANO_RESOURCE_OK, error);
}

static inline bool asset_id_equal(AnoAssetId lhs, AnoAssetId rhs)
{
    return lhs.value == rhs.value;
}

static inline bool resource_type_id_equal(AnoResourceTypeId lhs,
                                               AnoResourceTypeId rhs)
{
    return lhs.value == rhs.value;
}

static inline bool resource_content_id_equal(AnoContentId lhs,
                                                  AnoContentId rhs)
{
    for (size_t i = 0; i < sizeof(lhs.bytes); ++i)
        if (lhs.bytes[i] != rhs.bytes[i])
            return false;
    return true;
}

[[nodiscard]] const char *resource_error_string(AnoResourceError error);
[[nodiscard]] ResourceResult<AnoContentId> resource_content_id(
    AnoResourceBytes bytes);
[[nodiscard]] ResourceResult<AnoResourceSchema> resource_artifact_schema(
    AnoResourceTypeId type);
[[nodiscard]] ResourceResult<> resource_validate_artifact(
    AnoResourceTypeId type, AnoResourceBytes bytes);
// Returns the required dependency count. A short output span is a size query,
// not an error; written entries are the prefix that fits.
[[nodiscard]] ResourceResult<uint64_t> resource_artifact_dependencies(
    AnoResourceTypeId type, AnoResourceBytes bytes,
    AnoResourceDependency *dependencies, uint64_t dependencyCapacity);

static_assert(sizeof(AnoContentId) == 32);
static_assert(sizeof(AnoSchemaFingerprint) == 32);

} // namespace ano
