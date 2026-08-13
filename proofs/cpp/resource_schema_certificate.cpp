/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include <anoptic_render_resources.h>
#include <anoptic_resources_pack.h>

#include <meta>
#include <stdio.h>
#include <string_view>

namespace ano::proof_schema {

struct [[=Artifact{}]] InputA final { uint32_t value; };
struct [[=Artifact{}]] InputB final { uint32_t value; };
struct [[=Artifact{}]] OutputA final { uint32_t value; };
struct [[=Artifact{}]] OutputB final { uint32_t value; };

[[=Transform{Executor::worker, Streaming::whole, true}]]
bool bifurcate(const InputA&, const InputB&, OutputA&, OutputB&) noexcept;

} // namespace ano::proof_schema

namespace {

using namespace ano;

struct NameSet final {
    std::string_view values[64]{};
    size_t count = 0;
};

consteval void add_unique(NameSet& names, std::string_view name)
{
    for (size_t index = 0; index < names.count; ++index)
        if (names.values[index] == name)
            return;
    if (names.count == sizeof(names.values) / sizeof(names.values[0]))
        __builtin_abort();
    names.values[names.count++] = name;
}

consteval void collect_dependencies(std::meta::info type, NameSet& names)
{
    type = std::meta::dealias(type);
    const detail::WireShape shape = detail::wire_shape(type);
    if (shape == detail::WireShape::assetRef) {
        add_unique(names,
                   std::meta::identifier_of(detail::template_element(type)));
        return;
    }
    if (shape == detail::WireShape::array) {
        collect_dependencies(std::meta::remove_extent(type), names);
        return;
    }
    if (shape == detail::WireShape::relativeSpan) {
        collect_dependencies(detail::template_element(type), names);
        return;
    }
    if (shape == detail::WireShape::record)
        for (const std::meta::info field : detail::wire_fields(type))
            collect_dependencies(std::meta::type_of(field), names);
}

consteval NameSet artifact_dependencies(std::meta::info artifact)
{
    NameSet result{};
    collect_dependencies(artifact, result);
    return result;
}

consteval NameSet transform_artifacts(std::meta::info declaration,
                                      bool outputs)
{
    NameSet result{};
    for (const std::meta::info parameter :
         std::meta::parameters_of(declaration)) {
        const std::meta::info parameterType = std::meta::type_of(parameter);
        const std::meta::info valueType =
            std::meta::remove_cvref(parameterType);
        if (!detail::has_artifact_marker(valueType))
            continue;
        const std::meta::info referred =
            std::meta::remove_reference(parameterType);
        const bool isOutput = !std::meta::is_const_type(referred);
        if (isOutput == outputs)
            add_unique(result, std::meta::identifier_of(valueType));
    }
    return result;
}

consteval size_t artifact_count()
{
    size_t result = 0;
    for (const std::meta::info declaration : std::meta::members_of(
             ^^ano::asset_schema, std::meta::access_context::unchecked()))
        if (detail::has_artifact_marker(declaration))
            ++result;
    return result;
}

consteval size_t transform_count()
{
    size_t result = 0;
    for (const std::meta::info declaration : std::meta::members_of(
             ^^ano::asset_schema, std::meta::access_context::unchecked()))
        if (!std::meta::annotations_of_with_type(
                 declaration, ^^Transform).empty())
            ++result;
    return result;
}

consteval size_t importer_count()
{
    size_t result = 0;
    for (const std::meta::info declaration : std::meta::members_of(
             ^^ano::asset_schema, std::meta::access_context::unchecked()))
        result += std::meta::annotations_of_with_type(
            declaration, ^^Importer).size();
    return result;
}

constexpr const char *executor_name(Executor executor)
{
    switch (executor) {
    case Executor::io: return "io";
    case Executor::worker: return "worker";
    case Executor::render_master: return "render_master";
    case Executor::audio_master: return "audio_master";
    case Executor::text_owner: return "text_owner";
    }
    __builtin_abort();
}

constexpr const char *streaming_name(Streaming streaming)
{
    switch (streaming) {
    case Streaming::whole: return "whole";
    case Streaming::atoms: return "atoms";
    }
    __builtin_abort();
}

void emit_string(std::string_view value)
{
    printf("\"%.*s\"", static_cast<int>(value.size()), value.data());
}

void emit_names(const NameSet& names)
{
    putchar('[');
    for (size_t index = 0; index < names.count; ++index) {
        if (index != 0)
            fputs(", ", stdout);
        emit_string(names.values[index]);
    }
    putchar(']');
}

template<class Type>
void emit_fingerprint()
{
    constexpr AnoSchemaFingerprint fingerprint = schema_fingerprint<Type>();
    putchar('"');
    for (uint8_t byte : fingerprint.bytes)
        printf("%02x", static_cast<unsigned>(byte));
    putchar('"');
}

consteval bool texture_codec_round_trip()
{
    using namespace ano::asset_schema;
    Texture source = {
        .width = 4,
        .height = 2,
        .mipCount = 1,
        .format = TextureFormat::rgba8,
        .usage = TextureUsage::color,
        .bytes = {0, 0},
    };
    constexpr uint64_t byteCount = 64 + fixed_wire_size<Texture>();
    uint8_t bytes[byteCount]{};
    const EncodeResult encoded = encode(
        ArtifactSource<Texture>{&source, {nullptr, 0}},
        {bytes, byteCount});
    if (encoded.error != ANO_RESOURCE_OK || encoded.size != byteCount)
        return false;
    const DecodeResult<Texture> decoded = decode<Texture>({bytes, byteCount});
    if (decoded.error != ANO_RESOURCE_OK
        || decoded.view.value.width != source.width
        || decoded.view.value.height != source.height
        || decoded.view.value.mipCount != source.mipCount
        || decoded.view.value.format != source.format
        || decoded.view.value.usage != source.usage
        || decoded.view.value.bytes.offset != 0
        || decoded.view.value.bytes.count != 0)
        return false;
    bytes[0] = 0;
    return validate<Texture>({bytes, byteCount}) == ANO_RESOURCE_BAD_MAGIC;
}

static_assert(compile_resource_language(^^ano::asset_schema));
static_assert(compile_resource_language(^^ano::proof_schema));
static_assert(texture_codec_round_trip());

} // namespace

int main()
{
    puts("/-");
    puts("SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors");
    puts("");
    puts("SPDX-License-Identifier: LGPL-3.0");
    puts("Anoptic targets ISO C++26.");
    puts("-/");
    puts("");
    puts("namespace Anoptic.Generated.ResourceSchema");
    puts("");
    puts("structure Artifact where");
    puts("  name : String");
    puts("  typeId : Nat");
    puts("  fingerprint : String");
    puts("  fixedSize : Nat");
    puts("  fieldCount : Nat");
    puts("  dependencies : List String");
    puts("  deriving DecidableEq, Repr");
    puts("");
    puts("structure Transform where");
    puts("  name : String");
    puts("  inputs : List String");
    puts("  outputs : List String");
    puts("  executor : String");
    puts("  streaming : String");
    puts("  deterministic : Bool");
    puts("  deriving DecidableEq, Repr");
    puts("");
    puts("structure Importer where");
    puts("  name : String");
    puts("  extension : String");
    puts("  producerId : Nat");
    puts("  deterministic : Bool");
    puts("  deriving DecidableEq, Repr");
    puts("");

    puts("def artifacts : List Artifact := [");
    bool firstArtifact = true;
    static constexpr auto declarations = std::define_static_array(
        std::meta::members_of(^^ano::asset_schema,
                              std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info declaration : declarations) {
        if constexpr (ano::detail::has_artifact_marker(declaration)) {
            using Type = [:declaration:];
            constexpr std::string_view name =
                std::meta::identifier_of(declaration);
            constexpr uint64_t typeId = resource_type_id<Type>().value;
            constexpr uint64_t fixedSize = fixed_wire_size<Type>();
            constexpr size_t fieldCount =
                detail::wire_fields(declaration).size();
            if (!firstArtifact)
                puts(",");
            firstArtifact = false;
            fputs("  { name := ", stdout);
            emit_string(name);
            printf(", typeId := %llu, fingerprint := ",
                   static_cast<unsigned long long>(typeId));
            emit_fingerprint<Type>();
            printf(", fixedSize := %llu, fieldCount := %llu, dependencies := ",
                   static_cast<unsigned long long>(fixedSize),
                   static_cast<unsigned long long>(fieldCount));
            static constexpr NameSet dependencies =
                artifact_dependencies(declaration);
            emit_names(dependencies);
            fputs(" }", stdout);
        }
    }
    puts("");
    puts("]");
    puts("");

    puts("def transforms : List Transform := [");
    bool firstTransform = true;
    template for (constexpr std::meta::info declaration : declarations) {
        static constexpr auto annotations = std::define_static_array(
            std::meta::annotations_of_with_type(declaration, ^^Transform));
        if constexpr (!annotations.empty()) {
            constexpr Transform specification =
                std::meta::extract<Transform>(annotations[0]);
            constexpr std::string_view name =
                std::meta::identifier_of(declaration);
            if (!firstTransform)
                puts(",");
            firstTransform = false;
            fputs("  { name := ", stdout);
            emit_string(name);
            fputs(", inputs := ", stdout);
            static constexpr NameSet inputs =
                transform_artifacts(declaration, false);
            emit_names(inputs);
            fputs(", outputs := ", stdout);
            static constexpr NameSet outputs =
                transform_artifacts(declaration, true);
            emit_names(outputs);
            printf(", executor := \"%s\", streaming := \"%s\", deterministic := %s }",
                   executor_name(specification.executor),
                   streaming_name(specification.streaming),
                   specification.deterministic ? "true" : "false");
        }
    }
    puts("");
    puts("]");
    puts("");

    puts("def importers : List Importer := [");
    bool firstImporter = true;
    template for (constexpr std::meta::info declaration : declarations) {
        static constexpr auto annotations = std::define_static_array(
            std::meta::annotations_of_with_type(declaration, ^^Importer));
        template for (constexpr std::meta::info annotation : annotations) {
            constexpr Importer specification =
                std::meta::extract<Importer>(annotation);
            constexpr std::string_view name =
                std::meta::identifier_of(declaration);
            constexpr uint64_t producerId =
                detail::reflected_importer_id(declaration, specification);
            if (!firstImporter)
                puts(",");
            firstImporter = false;
            fputs("  { name := ", stdout);
            emit_string(name);
            fputs(", extension := ", stdout);
            emit_string(std::string_view(specification.extension,
                                         specification.length));
            printf(", producerId := %llu, deterministic := %s }",
                   static_cast<unsigned long long>(
                       producerId),
                   specification.deterministic ? "true" : "false");
        }
    }
    puts("");
    puts("]");
    puts("");

    printf("theorem reflectedArtifactCount : artifacts.length = %llu := rfl\n",
           static_cast<unsigned long long>(artifact_count()));
    printf("theorem reflectedTransformCount : transforms.length = %llu := rfl\n",
           static_cast<unsigned long long>(transform_count()));
    printf("theorem reflectedImporterCount : importers.length = %llu := rfl\n",
           static_cast<unsigned long long>(importer_count()));
    puts("");
    puts("end Anoptic.Generated.ResourceSchema");
    return 0;
}
