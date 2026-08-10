/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_resources_typed.h>

extern "C" const char *ano_resource_error_string(AnoResourceError error)
{
    static constexpr auto names = ano::reflect_enum_names<AnoResourceError>(
        "ANO_RESOURCE_", ano::EnumNameCase::lower);
    const auto *name = names.find(static_cast<size_t>(error));
    return name == nullptr ? "unknown_resource_error" : *name;
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
