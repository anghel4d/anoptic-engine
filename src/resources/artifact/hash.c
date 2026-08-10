/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_resources_typed.h>

extern "C" const char *ano_resource_error_string(AnoResourceError error)
{
    static constexpr const char *messages[ANO_RESOURCE_ERROR_COUNT] = {
        "ok",
        "invalid argument",
        "buffer too small",
        "integer overflow",
        "truncated canonical artifact",
        "bad canonical artifact magic",
        "resource type mismatch",
        "schema fingerprint mismatch",
        "non-canonical artifact",
        "artifact range is out of bounds",
        "typed source range is misaligned",
        "dependency output capacity is insufficient",
    };
    const uint32_t index = static_cast<uint32_t>(error);
    return index < ANO_RESOURCE_ERROR_COUNT ? messages[index] : "unknown resource error";
}

extern "C" AnoResourceError ano_resource_content_id(AnoResourceBytes bytes,
                                                     AnoContentId *contentId)
{
    if (contentId == nullptr || (bytes.data == nullptr && bytes.size != 0))
        return ANO_RESOURCE_INVALID_ARGUMENT;
    if (bytes.size > UINT64_MAX / 8)
        return ANO_RESOURCE_OVERFLOW;
    *contentId = ano::detail::sha256(bytes.data, bytes.size);
    return ANO_RESOURCE_OK;
}
