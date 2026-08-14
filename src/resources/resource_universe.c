/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_render_resources.h>

using namespace ano;
#include <anoptic_resources_cook.h>
#include <anoptic_resources_pack.h>
#include <anoptic_resources_typed.h>

#include "cooker_internal.h"

#include <string.h>

static_assert(ano::compile_resource_language(^^ano::asset_schema));

namespace {

bool importer_matches(const char *path, const ano::Importer& importer)
{
    const size_t length = strlen(path);
    if (length < importer.length)
        return false;
    const char *extension = path + length - importer.length;
    for (uint8_t i = 0; i < importer.length; ++i) {
        char value = extension[i];
        if (value >= 'A' && value <= 'Z')
            value = static_cast<char>(value - 'A' + 'a');
        if (value != importer.extension[i])
            return false;
    }
    return true;
}

} // namespace

ResourceResult<> ano::ano_resource_import(
    AnoResourceCooker *cooker, const AnoResourceImportRequest& request)
{
    if (cooker == nullptr || request.source.value == 0
        || request.rootAsset.value == 0 || request.commitGroup.value == 0)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    const char *path = ano_resource_cooker_source_path(
        cooker, request.source);
    if (path == nullptr)
        return failure(ANO_RESOURCE_NOT_FOUND);
    bool execute = false;
    AnoResourceError result = ano_resource_cooker_import_begin(
        cooker, request.source, &execute);
    if (result != ANO_RESOURCE_OK || !execute)
        return resource_status(result);
    const AnoResourceCookCheckpoint checkpoint =
        ano_resource_cooker_checkpoint(cooker);
    result = ANO_RESOURCE_UNSUPPORTED;
    bool matched = false;
    static constexpr auto resourceDeclarations = std::define_static_array(
        std::meta::members_of(^^ano::asset_schema,
                              std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info declaration : resourceDeclarations) {
        static constexpr auto importers = std::define_static_array(
            std::meta::annotations_of_with_type(declaration, ^^ano::Importer));
        template for (constexpr std::meta::info annotation : importers) {
            constexpr ano::Importer importer =
                std::meta::extract<ano::Importer>(annotation);
            if (!matched && importer_matches(path, importer)) {
                matched = true;
                constexpr uint64_t producer =
                    ano::detail::reflected_importer_id(
                        declaration, importer);
                ano_resource_cooker_select_producer(cooker, producer);
                const auto imported = [:declaration:](*cooker, request);
                result = imported ? ANO_RESOURCE_OK : imported.error();
            }
        }
    }
    const AnoResourceError ended = ano_resource_cooker_import_end(
        cooker, request.source, result == ANO_RESOURCE_OK);
    if (result == ANO_RESOURCE_OK)
        result = ended;
    if (result != ANO_RESOURCE_OK)
        ano_resource_cooker_rollback(cooker, checkpoint);
    return resource_status(result);
}

ResourceResult<AnoResourceSchema> ano::ano_resource_artifact_schema(
    AnoResourceTypeId type)
{
    static constexpr auto resourceDeclarations = std::define_static_array(
        std::meta::members_of(^^ano::asset_schema,
                              std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info declaration : resourceDeclarations) {
        if constexpr (ano::detail::has_artifact_marker(declaration)) {
            using Type = [:declaration:];
            constexpr AnoResourceTypeId expected = ano::resource_type_id<Type>();
            if (type.value == expected.value)
                return AnoResourceSchema{
                    .type = expected,
                    .fingerprint = ano::schema_fingerprint<Type>(),
                    .fixedSize = ano::fixed_wire_size<Type>(),
                };
        }
    }
    return failure(ANO_RESOURCE_TYPE_MISMATCH);
}

ResourceResult<> ano::ano_resource_validate_artifact(
    AnoResourceTypeId type, AnoResourceBytes bytes)
{
    static constexpr auto resourceDeclarations = std::define_static_array(
        std::meta::members_of(^^ano::asset_schema,
                              std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info declaration : resourceDeclarations) {
        if constexpr (ano::detail::has_artifact_marker(declaration)) {
            using Type = [:declaration:];
            if (type.value == ano::resource_type_id<Type>().value)
                return ano::validate<Type>(bytes);
        }
    }
    return failure(ANO_RESOURCE_TYPE_MISMATCH);
}

ResourceResult<uint64_t> ano::ano_resource_artifact_dependencies(
    AnoResourceTypeId type, AnoResourceBytes bytes,
    AnoResourceDependency *dependencies, uint64_t dependencyCapacity)
{
    static constexpr auto resourceDeclarations = std::define_static_array(
        std::meta::members_of(^^ano::asset_schema,
                              std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info declaration : resourceDeclarations) {
        if constexpr (ano::detail::has_artifact_marker(declaration)) {
            using Type = [:declaration:];
            if (type.value == ano::resource_type_id<Type>().value)
                return ano::dependencies<Type>(
                    bytes, dependencies, dependencyCapacity);
        }
    }
    return failure(ANO_RESOURCE_TYPE_MISMATCH);
}
