/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include "../cooker_internal.h"
#include "../parallel.h"


#include <anogltf.h>
#include <anoptic_memory_typed.h>

using namespace ano;
#include <anoptic_render_resources.h>
#include <stb_image.h>

#include <limits.h>
#include <meta>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string_view>
#include <type_traits>

namespace ano::asset_schema {
namespace {

struct ImportScratch final {
    bool *reachableNodes;
    bool *usedMeshes;
    bool *usedMaterials;
    uint8_t *imageUsage;
    AnoAssetId *imageAssets;
    AnoAssetId *materialAssets;
    AnoAssetId *primitiveAssets;
    uint64_t *meshFirstPrimitive;
    uint64_t primitiveCount;
    bool needsDefaultMaterial;
    AnoAssetId defaultMaterial;
};

struct PreparedArtifact final {
    const void *value;
    AnoResourceBytes extent;
};

enum class ImportJobKind : uint8_t {
    texture,
    mesh,
    material,
    scene,
};

AnoResourceTypeId import_job_type(ImportJobKind kind)
{
    switch (kind) {
    case ImportJobKind::texture:
        return resource_type_id<Texture>();
    case ImportJobKind::mesh:
        return resource_type_id<Mesh>();
    case ImportJobKind::material:
        return resource_type_id<Material>();
    case ImportJobKind::scene:
        return resource_type_id<Scene>();
    }
    return {};
}

struct ImportJob final {
    ImportJobKind kind;
    uint32_t first;
    uint32_t second;
    AnoAssetId asset;
    AnoContentId inputIdentity;
    PreparedArtifact prepared;
    AnoResourceCookArtifact artifact;
    uint8_t *ownedExtent;
    AnoResourceError result;
};

struct ImportBatch final {
    AnoResourceCooker *cooker;
    AnoResourceCommitGroupId commitGroup;
    const char *sourcePath;
    const AnoGltfData *data;
    const ImportScratch *scratch;
    ano::MemoryRegion *region;
    ImportJob *jobs;
};

consteval MaterialFeature extension_feature(std::meta::info member)
{
    constexpr std::string_view prefix = "KHR_materials_";
    std::string_view name = std::meta::identifier_of(member);
    if (!name.starts_with(prefix))
        __builtin_abort();
    name.remove_prefix(prefix.size());
    static constexpr auto features = std::define_static_array(
        std::meta::enumerators_of(^^MaterialFeature));
    template for (constexpr std::meta::info feature : features)
        if (ano::detail::semantic_name_equal(
                name, std::meta::identifier_of(feature)))
            return [:feature:];
    __builtin_abort();
}

consteval size_t texture_slot_count(std::meta::info sourceType,
                                    MaterialTextureSlot sought)
{
    size_t count = 0;
    const auto fields = std::meta::nonstatic_data_members_of(
        sourceType, std::meta::access_context::unchecked());
    for (const std::meta::info field : fields) {
        if (std::meta::type_of(field) != ^^AnoGltfTextureInfo)
            continue;
        const MaterialTextureSlotMatch match = material_texture_slot(field);
        if (match.found && match.slot == sought)
            ++count;
    }
    return count;
}

consteval bool material_texture_schema_valid()
{
    static constexpr auto slots = std::define_static_array(
        std::meta::enumerators_of(^^MaterialTextureSlot));
    static constexpr auto extensions = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^AnoGltfMaterialExtensionsKnown,
            std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info slot : slots) {
        size_t count = texture_slot_count(^^AnoGltfMaterial, [:slot:])
            + texture_slot_count(^^AnoGltfPbrMetallicRoughness, [:slot:]);
        template for (constexpr std::meta::info extension : extensions) {
            constexpr std::meta::info optionalType =
                std::meta::type_of(extension);
            constexpr std::meta::info sourceType =
                ano::detail::template_element(optionalType);
            count += texture_slot_count(sourceType, [:slot:]);
        }
        if (count != 1)
            return false;
    }
    return true;
}

template<class Destination, class Source>
struct MaterialValueCompatible final {
    static constexpr bool value = std::is_same_v<Destination, Source>
        || (std::is_enum_v<Destination> && std::is_enum_v<Source>);
};

template<size_t DestinationCount, size_t SourceCount>
struct MaterialValueCompatible<
    float[DestinationCount], AnoGltfFixedArray<float, SourceCount>> final {
    static constexpr bool value = DestinationCount == SourceCount
        || DestinationCount == SourceCount + 1;
};

template<std::meta::info Destination, class Source>
consteval size_t material_source_matches()
{
    size_t matches = 0;
    static constexpr auto sources = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^Source, std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info source : sources) {
        if constexpr (std::meta::identifier_of(Destination)
                      == std::meta::identifier_of(source)) {
            using DestinationType = [:std::meta::type_of(Destination):];
            using SourceType = [:std::meta::type_of(source):];
            if constexpr (MaterialValueCompatible<DestinationType,
                                                   SourceType>::value)
                ++matches;
        }
    }
    return matches;
}

template<std::meta::info Destination>
consteval size_t material_value_matches()
{
    size_t matches = material_source_matches<Destination, AnoGltfMaterial>()
        + material_source_matches<Destination,
                                  AnoGltfPbrMetallicRoughness>();
    static constexpr auto extensions = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^AnoGltfMaterialExtensionsKnown,
            std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info extension : extensions) {
        constexpr std::meta::info optionalType = std::meta::type_of(extension);
        constexpr std::meta::info sourceType =
            ano::detail::template_element(optionalType);
        using Source = [:sourceType:];
        matches += material_source_matches<Destination, Source>();
    }
    return matches;
}

consteval bool material_value_schema_valid()
{
    static constexpr auto destinations = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^Material, std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info destination : destinations) {
        constexpr std::string_view name =
            std::meta::identifier_of(destination);
        if constexpr (name != "features" && name != "unlit"
                      && name != "textures")
            if (material_value_matches<destination>() != 1)
                return false;
    }
    return true;
}

static_assert(material_texture_schema_valid(),
              "every canonical texture slot has one reflected glTF source");
static_assert(material_value_schema_valid(),
              "every canonical material value has one reflected glTF source");

template<class Destination, class Source>
bool reflected_enum_cast(Source source, Destination *destination)
{
    static_assert(std::is_enum_v<Destination> && std::is_enum_v<Source>);
    if (destination == nullptr)
        return false;
    static constexpr auto sources = std::define_static_array(
        std::meta::enumerators_of(^^Source));
    static constexpr auto destinations = std::define_static_array(
        std::meta::enumerators_of(^^Destination));
    template for (constexpr std::meta::info sourceValue : sources) {
        if (source == [:sourceValue:]) {
            template for (constexpr std::meta::info destinationValue
                          : destinations) {
                if constexpr (std::meta::identifier_of(sourceValue)
                              == std::meta::identifier_of(destinationValue)) {
                    *destination = [:destinationValue:];
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

template<class Destination, class Source>
void project_value(Destination& destination, const Source& source)
{
    if constexpr (std::is_same_v<Destination, Source>)
        destination = source;
    else if constexpr (std::is_enum_v<Destination> && std::is_enum_v<Source>)
        (void)reflected_enum_cast(source, &destination);
}

template<size_t DestinationCount, size_t SourceCount>
void project_value(float (&destination)[DestinationCount],
                   const AnoGltfFixedArray<float, SourceCount>& source)
{
    static_assert(DestinationCount == SourceCount
                  || DestinationCount == SourceCount + 1);
    for (size_t i = 0; i < SourceCount; ++i)
        destination[i] = source.values[i];
    if constexpr (DestinationCount == SourceCount + 1)
        destination[SourceCount] = 1.0f;
}

template<class Source>
void project_material_values(Material& destination, const Source& source)
{
    static constexpr auto destinations = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^Material, std::meta::access_context::unchecked()));
    static constexpr auto sources = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^Source, std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info target : destinations)
        template for (constexpr std::meta::info input : sources)
            if constexpr (std::meta::identifier_of(target)
                          == std::meta::identifier_of(input))
                project_value(destination.[:target:], source.[:input:]);
}

template<class Function>
void visit_material_extensions(
    const AnoGltfMaterialExtensionsKnown& extensions, Function& function)
{
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^AnoGltfMaterialExtensionsKnown,
            std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info member : members) {
        const auto& optional = extensions.[:member:];
        if (optional.present)
            function.template operator()<member>(optional.value);
    }
}

template<class Object, class Function>
void visit_texture_fields(const Object& object, Function& function)
{
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^Object, std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info member : members) {
        using Field = [:std::meta::type_of(member):];
        const Field& field = object.[:member:];
        if constexpr (std::is_same_v<Field, AnoGltfTextureInfo>) {
            function.template operator()<member>(field);
        } else if constexpr (ano::detail::specialization_of(
                                 ^^Field, ^^AnoGltfOptional)) {
            if (field.present)
                visit_texture_fields(field.value, function);
        } else if constexpr (ano::detail::specialization_of(
                                 ^^Field, ^^AnoGltfExtensions)) {
            visit_texture_fields(field.known, function);
        }
    }
}

template<class Type>
Type *zero_array(ano::MemoryRegion *region, uint64_t count)
{
    if (count == 0 || count > SIZE_MAX)
        return nullptr;
    return ano::memory_region_allocate_zero<Type>(
        region, static_cast<size_t>(count)).value_or(nullptr);
}

void *gltf_allocate(void *user, size_t bytes)
{
    return ano::memory_region_allocate(
        static_cast<ano::MemoryRegion *>(user), bytes,
        alignof(max_align_t)).value_or(nullptr);
}

void gltf_release(void *, void *) {}

AnoResourceError gltf_error(AnoGltfResult result)
{
    switch (result) {
    case AnoGltfResult::success:
        return ANO_RESOURCE_OK;
    case AnoGltfResult::file_not_found:
    case AnoGltfResult::io_error:
        return ANO_RESOURCE_IO_ERROR;
    case AnoGltfResult::out_of_memory:
        return ANO_RESOURCE_OUT_OF_MEMORY;
    case AnoGltfResult::unsupported_required_extension:
        return ANO_RESOURCE_UNSUPPORTED;
    case AnoGltfResult::invalid_options:
        return ANO_RESOURCE_INVALID_ARGUMENT;
    case AnoGltfResult::data_too_short:
    case AnoGltfResult::unknown_format:
    case AnoGltfResult::invalid_json:
    case AnoGltfResult::invalid_gltf:
    case AnoGltfResult::limit_exceeded:
        return ANO_RESOURCE_NON_CANONICAL;
    }
    return ANO_RESOURCE_NON_CANONICAL;
}

template<class Value>
AnoResourceError gltf_error(const GltfResult<Value>& result)
{
    return result ? ANO_RESOURCE_OK : gltf_error(result.error());
}

template<class ArtifactType>
ResourceResult<> encode_prepared(void *context,
                                 AnoResourceMutableBytes destination)
{
    if (context == nullptr)
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    const auto& prepared = *static_cast<const PreparedArtifact *>(context);
    const EncodeResult encoded = encode(
        ArtifactSource<ArtifactType>{
            .value = static_cast<const ArtifactType *>(prepared.value),
            .extent = prepared.extent,
        }, destination);
    if (!encoded)
        return failure(encoded.error());
    if (*encoded != destination.size)
        return failure(ANO_RESOURCE_NON_CANONICAL);
    return {};
}

template<class ArtifactType>
AnoResourceError prepare_artifact(
    ano::MemoryRegion *region, ImportJob& job, AnoAssetId asset,
    AnoResourceCommitGroupId group, const ArtifactType& value,
    AnoResourceBytes extent = {})
{
    ArtifactType *stored = ano::memory_region_allocate<ArtifactType>(
        region, 1).value_or(nullptr);
    if (stored == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    *stored = value;
    job.prepared = {.value = stored, .extent = extent};
    const EncodeResult measured = encoded_size(
        ArtifactSource<ArtifactType>{stored, extent});
    if (!measured)
        return measured.error();
    job.artifact = {
        .asset = asset,
        .type = resource_type_id<ArtifactType>(),
        .commitGroup = group,
        .encodedSize = *measured,
        .inputIdentity = job.inputIdentity,
        .context = &job.prepared,
        .encode = encode_prepared<ArtifactType>,
    };
    return ANO_RESOURCE_OK;
}

AnoResourceError texture_image(const AnoGltfData& data,
                               const AnoGltfTextureInfo& info,
                               uint32_t *image)
{
    if (image == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *image = ANO_GLTF_NO_INDEX;
    if (!ano_gltf_has_index(info.index))
        return ANO_RESOURCE_OK;
    if (info.index.value >= data.texturesCount)
        return ANO_RESOURCE_NON_CANONICAL;
    const AnoGltfTexture& texture = data.textures[info.index.value];
    if (ano_gltf_has_index(texture.source)) {
        if (texture.source.value >= data.imagesCount)
            return ANO_RESOURCE_NON_CANONICAL;
        *image = texture.source.value;
        return ANO_RESOURCE_OK;
    }
    if (texture.extensions.known.KHR_texture_basisu.present
        || texture.extensions.known.EXT_texture_webp.present)
        return ANO_RESOURCE_UNSUPPORTED;
    return ANO_RESOURCE_NON_CANONICAL;
}

AnoResourceError mark_material_images(const AnoGltfData& data,
                                      const AnoGltfMaterial& material,
                                      uint8_t *imageUsage)
{
    AnoResourceError result = ANO_RESOURCE_OK;
    auto mark = [&]<std::meta::info source>(const AnoGltfTextureInfo& info) {
        if (result != ANO_RESOURCE_OK || !ano_gltf_has_index(info.index))
            return;
        constexpr MaterialTextureSlotMatch match =
            material_texture_slot(source);
        if constexpr (!match.found) {
            result = ANO_RESOURCE_UNSUPPORTED;
        } else {
            uint32_t image = ANO_GLTF_NO_INDEX;
            result = texture_image(data, info, &image);
            if (result == ANO_RESOURCE_OK)
                imageUsage[image] |= static_cast<uint8_t>(
                    material_texture_usage(match.slot));
        }
    };
    visit_texture_fields(material, mark);
    return result;
}

MaterialTexture material_texture(const AnoGltfData& data,
                                 const AnoGltfTextureInfo& source,
                                 const AnoAssetId *imageAssets,
                                 AnoResourceError *error)
{
    MaterialTexture result = {
        .texture = {{0}},
        .texCoord = source.texCoord,
        .scale = source.scale,
        .strength = source.strength,
        .offset = {0.0f, 0.0f},
        .rotation = 0.0f,
        .transformScale = {1.0f, 1.0f},
        .transformTexcoord = 0,
        .hasTransform = false,
        .transformsTexcoord = false,
    };
    if (ano_gltf_has_index(source.index)) {
        uint32_t image = ANO_GLTF_NO_INDEX;
        *error = texture_image(data, source, &image);
        if (*error != ANO_RESOURCE_OK)
            return result;
        result.texture.id = imageAssets[image];
    }
    const auto& transform =
        source.extensions.known.KHR_texture_transform;
    if (transform.present) {
        result.hasTransform = true;
        result.offset[0] = transform.value.offset.values[0];
        result.offset[1] = transform.value.offset.values[1];
        result.rotation = transform.value.rotation;
        result.transformScale[0] = transform.value.scale.values[0];
        result.transformScale[1] = transform.value.scale.values[1];
        result.transformsTexcoord = transform.value.texCoord.present;
        if (result.transformsTexcoord)
            result.transformTexcoord = transform.value.texCoord.value;
    }
    return result;
}

AnoResourceError make_material(const AnoGltfData& data,
                               const AnoGltfMaterial *source,
                               const AnoAssetId *imageAssets,
                               Material *material)
{
    if (material == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *material = {};
    project_material_values(*material, AnoGltfMaterial{});
    project_material_values(*material, AnoGltfPbrMetallicRoughness{});

    static constexpr auto extensionMembers = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^AnoGltfMaterialExtensionsKnown,
            std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info member : extensionMembers) {
        constexpr std::meta::info optionalType = std::meta::type_of(member);
        constexpr std::meta::info sourceType =
            ano::detail::template_element(optionalType);
        using Extension = [:sourceType:];
        project_material_values(*material, Extension{});
    }

    const AnoGltfTextureInfo defaultTexture;
    AnoResourceError defaultTextureError = ANO_RESOURCE_OK;
    for (size_t i = 0; i < materialTextureCount; ++i)
        material->textures[i] = material_texture(
            data, defaultTexture, imageAssets,
            &defaultTextureError);
    if (defaultTextureError != ANO_RESOURCE_OK)
        return defaultTextureError;

    if (source == nullptr)
        return ANO_RESOURCE_OK;
    if (source->extensions.known.KHR_materials_pbrSpecularGlossiness.present)
        return ANO_RESOURCE_UNSUPPORTED;

    project_material_values(*material, *source);
    if (source->pbrMetallicRoughness.present) {
        project_material_values(*material,
                                source->pbrMetallicRoughness.value);
        material->features |= material_feature_bit(
            MaterialFeature::pbrMetallicRoughness);
    }

    AnoResourceError result = ANO_RESOURCE_OK;
    auto projectExtension = [&]<std::meta::info member>(const auto& extension) {
        constexpr MaterialFeature feature = extension_feature(member);
        material->features |= material_feature_bit(feature);
        if constexpr (feature == MaterialFeature::unlit)
            material->unlit = true;
        else
            project_material_values(*material, extension);
    };
    visit_material_extensions(source->extensions.known, projectExtension);

    auto projectTexture = [&]<std::meta::info field>(
                              const AnoGltfTextureInfo& texture) {
        if (result != ANO_RESOURCE_OK)
            return;
        constexpr MaterialTextureSlotMatch match =
            material_texture_slot(field);
        if constexpr (!match.found) {
            if (ano_gltf_has_index(texture.index))
                result = ANO_RESOURCE_UNSUPPORTED;
        } else {
            material->textures[ano::detail::enum_index(match.slot)] =
                material_texture(data, texture, imageAssets, &result);
        }
    };
    visit_texture_fields(*source, projectTexture);
    return result;
}

AnoResourceError resolved_image_path(const char *gltfPath,
                                     AnoGltfString uri, char **path)
{
    if (gltfPath == nullptr || path == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *path = nullptr;
    if (uri.length >= 5 && memcmp(uri.data, "data:", 5) == 0)
        return ANO_RESOURCE_UNSUPPORTED;
    const char *slash = strrchr(gltfPath, '/');
    const char *backslash = strrchr(gltfPath, '\\');
    const char *separator = slash;
    if (separator == nullptr || (backslash != nullptr && backslash > separator))
        separator = backslash;
    const size_t prefix = separator == nullptr
        ? 0 : static_cast<size_t>(separator - gltfPath) + 1;
    if (uri.length > SIZE_MAX - prefix - 1)
        return ANO_RESOURCE_OVERFLOW;
    const size_t bytes = prefix + uri.length + 1;
    char *resolved = static_cast<char *>(mi_malloc(bytes));
    if (resolved == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    if (prefix != 0)
        memcpy(resolved, gltfPath, prefix);
    if (uri.length != 0)
        memcpy(resolved + prefix, uri.data, uri.length);
    resolved[prefix + uri.length] = '\0';
    const size_t decoded = ano_gltf_decode_uri(resolved + prefix);
    if (strlen(resolved + prefix) != decoded) {
        mi_free(resolved);
        return ANO_RESOURCE_NON_CANONICAL;
    }
    *path = resolved;
    return ANO_RESOURCE_OK;
}

AnoContentId finish_input_identity(ano::detail::Sha256& hash)
{
    return hash.finish();
}

void begin_input_identity(ano::detail::Sha256& hash, uint64_t count)
{
    hash.append("anoptic.resource.inputs.v1");
    ano::detail::hash_u64(hash, count);
}

AnoResourceError append_file_identity(
    AnoResourceCooker& cooker, const char *path,
    ano::detail::Sha256& hash)
{
    AnoContentId content{};
    const AnoResourceError result = ano_resource_cooker_source_content(
        &cooker, path, &content);
    if (result == ANO_RESOURCE_OK)
        hash.append(content.bytes, sizeof(content.bytes));
    return result;
}

bool external_uri(AnoGltfString uri)
{
    return uri.length != 0
        && !(uri.length >= 5 && memcmp(uri.data, "data:", 5) == 0);
}

AnoResourceError append_uri_identity(
    AnoResourceCooker& cooker, const char *sourcePath, AnoGltfString uri,
    ano::detail::Sha256& hash)
{
    char *path = nullptr;
    AnoResourceError result = resolved_image_path(sourcePath, uri, &path);
    if (result == ANO_RESOURCE_OK)
        result = append_file_identity(cooker, path, hash);
    mi_free(path);
    return result;
}

AnoResourceError root_input_identity(
    AnoResourceCooker& cooker, const char *sourcePath,
    AnoContentId *identity)
{
    ano::detail::Sha256 hash;
    begin_input_identity(hash, 1);
    const AnoResourceError result = append_file_identity(
        cooker, sourcePath, hash);
    if (result == ANO_RESOURCE_OK)
        *identity = finish_input_identity(hash);
    return result;
}

AnoResourceError mesh_input_identity(
    AnoResourceCooker& cooker, const char *sourcePath,
    const AnoGltfData& data, AnoContentId *identity)
{
    uint64_t count = 1;
    for (uint32_t i = 0; i < data.buffersCount; ++i)
        count += external_uri(data.buffers[i].uri) ? 1u : 0u;
    ano::detail::Sha256 hash;
    begin_input_identity(hash, count);
    AnoResourceError result = append_file_identity(cooker, sourcePath, hash);
    for (uint32_t i = 0;
         i < data.buffersCount && result == ANO_RESOURCE_OK; ++i)
        if (external_uri(data.buffers[i].uri))
            result = append_uri_identity(
                cooker, sourcePath, data.buffers[i].uri, hash);
    if (result == ANO_RESOURCE_OK)
        *identity = finish_input_identity(hash);
    return result;
}

AnoResourceError texture_input_identity(
    AnoResourceCooker& cooker, const char *sourcePath,
    const AnoGltfData& data, uint32_t imageIndex, AnoContentId *identity)
{
    if (imageIndex >= data.imagesCount || identity == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    AnoGltfString external{};
    const AnoGltfImage& image = data.images[imageIndex];
    if (external_uri(image.uri)) {
        external = image.uri;
    } else if (ano_gltf_has_index(image.bufferView)) {
        if (image.bufferView.value >= data.bufferViewsCount)
            return ANO_RESOURCE_NON_CANONICAL;
        const AnoGltfBufferIndex buffer =
            data.bufferViews[image.bufferView.value].buffer;
        if (!ano_gltf_has_index(buffer) || buffer.value >= data.buffersCount)
            return ANO_RESOURCE_NON_CANONICAL;
        if (external_uri(data.buffers[buffer.value].uri))
            external = data.buffers[buffer.value].uri;
    }
    ano::detail::Sha256 hash;
    begin_input_identity(hash, external_uri(external) ? 2 : 1);
    AnoResourceError result = append_file_identity(cooker, sourcePath, hash);
    if (result == ANO_RESOURCE_OK && external_uri(external))
        result = append_uri_identity(cooker, sourcePath, external, hash);
    if (result == ANO_RESOURCE_OK)
        *identity = finish_input_identity(hash);
    return result;
}

AnoResourceError register_source_files(AnoResourceCooker& cooker,
                                       const char *sourcePath,
                                       const AnoGltfData& data)
{
    const auto capacity = ano::checked_add(
        uint64_t{data.buffersCount}, uint64_t{data.imagesCount});
    if (!capacity || *capacity > SIZE_MAX / sizeof(char *))
        return ANO_RESOURCE_OVERFLOW;
    char **paths = *capacity == 0 ? nullptr
        : mi_calloc_tp(char *, static_cast<size_t>(*capacity));
    if (*capacity != 0 && paths == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    uint64_t count = 0;
    auto registerUri = [&](AnoGltfString uri) {
        if (uri.length == 0
            || (uri.length >= 5 && memcmp(uri.data, "data:", 5) == 0))
            return ANO_RESOURCE_OK;
        char *path = nullptr;
        AnoResourceError result = resolved_image_path(
            sourcePath, uri, &path);
        if (result == ANO_RESOURCE_OK)
            paths[count++] = path;
        return result;
    };
    AnoResourceError result = ANO_RESOURCE_OK;
    for (uint32_t i = 0; i < data.buffersCount
                         && result == ANO_RESOURCE_OK; ++i)
        result = registerUri(data.buffers[i].uri);
    for (uint32_t i = 0; i < data.imagesCount
                         && result == ANO_RESOURCE_OK; ++i)
        result = registerUri(data.images[i].uri);
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cooker_source_dependencies(
            &cooker, paths, count);
    for (uint64_t i = 0; i < count; ++i)
        mi_free(paths[i]);
    mi_free(paths);
    return result;
}

AnoResourceError bind_external_buffers(AnoResourceCooker& cooker,
                                       const char *sourcePath,
                                       AnoGltfData& data)
{
    AnoResourceError result = ANO_RESOURCE_OK;
    for (uint32_t i = 0; i < data.buffersCount
                         && result == ANO_RESOURCE_OK; ++i) {
        const AnoGltfString uri = data.buffers[i].uri;
        if (uri.length == 0
            || (uri.length >= 5 && memcmp(uri.data, "data:", 5) == 0))
            continue;
        char *path = nullptr;
        result = resolved_image_path(sourcePath, uri, &path);
        AnoResourceBytes bytes{};
        if (result == ANO_RESOURCE_OK)
            result = ano_resource_cooker_source_bytes(
                &cooker, path, &bytes);
        if (result == ANO_RESOURCE_OK
            && gltf_error(ano_gltf_bind_buffer(
                   &data, {i}, bytes.data, static_cast<size_t>(bytes.size)))
                != ANO_RESOURCE_OK)
            result = ANO_RESOURCE_NON_CANONICAL;
        mi_free(path);
    }
    return result;
}

bool external_file_changed(AnoResourceCooker& cooker,
                           const char *sourcePath, AnoGltfString uri)
{
    if (uri.length == 0
        || (uri.length >= 5 && memcmp(uri.data, "data:", 5) == 0))
        return false;
    char *path = nullptr;
    const bool changed = resolved_image_path(sourcePath, uri, &path)
            != ANO_RESOURCE_OK
        || ano_resource_cooker_source_changed(&cooker, path);
    mi_free(path);
    return changed;
}

bool buffer_changed(AnoResourceCooker& cooker, const char *sourcePath,
                    const AnoGltfData& data, AnoGltfBufferIndex buffer)
{
    return !ano_gltf_has_index(buffer) || buffer.value >= data.buffersCount
        || external_file_changed(
            cooker, sourcePath, data.buffers[buffer.value].uri);
}

bool image_changed(AnoResourceCooker& cooker, const char *sourcePath,
                   const AnoGltfData& data, uint32_t imageIndex)
{
    if (imageIndex >= data.imagesCount)
        return true;
    const AnoGltfImage& image = data.images[imageIndex];
    if (image.uri.length != 0)
        return external_file_changed(cooker, sourcePath, image.uri);
    if (!ano_gltf_has_index(image.bufferView)
        || image.bufferView.value >= data.bufferViewsCount)
        return true;
    return buffer_changed(
        cooker, sourcePath, data,
        data.bufferViews[image.bufferView.value].buffer);
}

bool mesh_inputs_changed(AnoResourceCooker& cooker, const char *sourcePath,
                         const AnoGltfData& data)
{
    for (uint32_t i = 0; i < data.buffersCount; ++i)
        if (external_file_changed(
                cooker, sourcePath, data.buffers[i].uri))
            return true;
    return false;
}

AnoResourceError decode_image(AnoResourceCooker& cooker,
                              const AnoGltfData& data, uint32_t imageIndex,
                              const char *gltfPath, stbi_uc **pixels,
                              uint32_t *width, uint32_t *height,
                              uint64_t *byteCount)
{
    if (pixels == nullptr || width == nullptr || height == nullptr
        || byteCount == nullptr || imageIndex >= data.imagesCount)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *pixels = nullptr;
    const AnoGltfImage& image = data.images[imageIndex];
    int decodedWidth = 0;
    int decodedHeight = 0;
    int channels = 0;
    if (image.uri.length != 0) {
        char *path = nullptr;
        AnoResourceError resolved = resolved_image_path(
            gltfPath, image.uri, &path);
        AnoResourceBytes encoded{};
        if (resolved == ANO_RESOURCE_OK)
            resolved = ano_resource_cooker_source_bytes(
                &cooker, path, &encoded);
        mi_free(path);
        if (resolved != ANO_RESOURCE_OK)
            return resolved;
        if (encoded.size > INT_MAX)
            return ANO_RESOURCE_OVERFLOW;
        *pixels = stbi_load_from_memory(
            encoded.data, static_cast<int>(encoded.size), &decodedWidth,
            &decodedHeight, &channels, STBI_rgb_alpha);
    } else if (ano_gltf_has_index(image.bufferView)) {
        if (image.bufferView.value >= data.bufferViewsCount)
            return ANO_RESOURCE_NON_CANONICAL;
        const AnoGltfBufferView& view =
            data.bufferViews[image.bufferView.value];
        const auto bytes = ano_gltf_buffer_view_data(&data, image.bufferView);
        if (!bytes || view.byteLength > INT_MAX)
            return ANO_RESOURCE_NON_CANONICAL;
        *pixels = stbi_load_from_memory(
            *bytes, static_cast<int>(view.byteLength), &decodedWidth,
            &decodedHeight, &channels, STBI_rgb_alpha);
    } else {
        return ANO_RESOURCE_NON_CANONICAL;
    }
    if (*pixels == nullptr || decodedWidth <= 0 || decodedHeight <= 0) {
        stbi_image_free(*pixels);
        *pixels = nullptr;
        return ANO_RESOURCE_NON_CANONICAL;
    }
    const auto pixelCount = ano::checked_multiply(
        static_cast<uint64_t>(decodedWidth),
        static_cast<uint64_t>(decodedHeight));
    const auto bytes = pixelCount
        ? ano::checked_multiply(*pixelCount, UINT64_C(4))
        : ano::ArithmeticResult<uint64_t>(failure(pixelCount.error()));
    if (!bytes) {
        stbi_image_free(*pixels);
        *pixels = nullptr;
        return ANO_RESOURCE_OVERFLOW;
    }
    *byteCount = *bytes;
    *width = static_cast<uint32_t>(decodedWidth);
    *height = static_cast<uint32_t>(decodedHeight);
    return ANO_RESOURCE_OK;
}

AnoResourceError build_texture(AnoResourceCooker& cooker,
                                const char *sourcePath,
                                const AnoGltfData& data,
                                const ImportScratch& scratch,
                                uint32_t imageIndex,
                                AnoResourceCommitGroupId group,
                                ano::MemoryRegion *region,
                                ImportJob& job)
{
    stbi_uc *pixels = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t byteCount = 0;
    AnoResourceError result = decode_image(
        cooker, data, imageIndex, sourcePath, &pixels, &width, &height,
        &byteCount);
    if (result != ANO_RESOURCE_OK)
        return result;
    const Texture texture = {
        .width = width,
        .height = height,
        .mipCount = 1,
        .format = TextureFormat::rgba8,
        .usage = static_cast<TextureUsage>(scratch.imageUsage[imageIndex]),
        .bytes = {0, byteCount},
    };
    job.ownedExtent = pixels;
    return prepare_artifact(
        region, job, job.asset, group, texture,
        {.data = pixels, .size = byteCount});
}

AnoResourceError primitive_accessors(
    const AnoGltfData& data, const AnoGltfPrimitive& primitive,
    const AnoGltfAccessor **position, const AnoGltfAccessor **normal,
    const AnoGltfAccessor **texCoord, const AnoGltfAccessor **indices)
{
    if (primitive.mode != AnoGltfPrimitiveMode::triangles
        || primitive.targets.count != 0
        || primitive.extensions.known.KHR_draco_mesh_compression.present)
        return ANO_RESOURCE_UNSUPPORTED;
    *position = ano_gltf_find_accessor(
        &data, &primitive, AnoGltfAttributeType::position, 0);
    *normal = ano_gltf_find_accessor(
        &data, &primitive, AnoGltfAttributeType::normal, 0);
    *texCoord = ano_gltf_find_accessor(
        &data, &primitive, AnoGltfAttributeType::texcoord, 0);
    if (*position == nullptr || !ano_gltf_has_index(primitive.indices)
        || primitive.indices.value >= data.accessorsCount)
        return ANO_RESOURCE_UNSUPPORTED;
    *indices = &data.accessors[primitive.indices.value];
    if (ano_gltf_component_count((*position)->type) != 3
        || (*normal != nullptr
            && ano_gltf_component_count((*normal)->type) != 3)
        || (*texCoord != nullptr
            && ano_gltf_component_count((*texCoord)->type) != 2)
        || ano_gltf_component_count((*indices)->type) != 1)
        return ANO_RESOURCE_NON_CANONICAL;
    if ((*position)->count == 0 || (*indices)->count == 0
        || (*indices)->count % 3 != 0
        || (*normal != nullptr && (*normal)->count != (*position)->count)
        || (*texCoord != nullptr && (*texCoord)->count != (*position)->count))
        return ANO_RESOURCE_NON_CANONICAL;
    return ANO_RESOURCE_OK;
}

AnoResourceError build_mesh(const AnoGltfData& data,
                             const ImportScratch& scratch, uint32_t meshIndex,
                             uint32_t primitiveIndex,
                             ano::MemoryRegion *region,
                             AnoResourceCommitGroupId group,
                             ImportJob& job)
{
    const AnoGltfPrimitive& primitive =
        data.meshes[meshIndex].primitives.data[primitiveIndex];
    const AnoGltfAccessor *position = nullptr;
    const AnoGltfAccessor *normal = nullptr;
    const AnoGltfAccessor *texCoord = nullptr;
    const AnoGltfAccessor *indicesAccessor = nullptr;
    AnoResourceError result = primitive_accessors(
        data, primitive, &position, &normal, &texCoord, &indicesAccessor);
    if (result != ANO_RESOURCE_OK)
        return result;
    if (position->count > SIZE_MAX / sizeof(Vertex)
        || indicesAccessor->count > SIZE_MAX / sizeof(uint32_t))
        return ANO_RESOURCE_OVERFLOW;
    const uint64_t vertexBytes = position->count * sizeof(Vertex);
    const uint64_t indexBytes = indicesAccessor->count * sizeof(uint32_t);
    const auto extentSize = ano::checked_add(vertexBytes, indexBytes);
    if (!extentSize || *extentSize > SIZE_MAX)
        return ANO_RESOURCE_OVERFLOW;
    uint8_t *extent = ano::memory_region_allocate_zero<uint8_t>(
        region, static_cast<size_t>(*extentSize)).value_or(nullptr);
    if (extent == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    Vertex *vertices = reinterpret_cast<Vertex *>(extent);
    uint32_t *indices = reinterpret_cast<uint32_t *>(extent + vertexBytes);
    for (uint64_t vertex = 0; vertex < position->count; ++vertex) {
        if (!ano_gltf_accessor_read_float(
                &data, position, vertex, vertices[vertex].position, 3)) {
            result = ANO_RESOURCE_NON_CANONICAL;
            break;
        }
        if (normal != nullptr) {
            if (!ano_gltf_accessor_read_float(
                    &data, normal, vertex, vertices[vertex].normal, 3)) {
                result = ANO_RESOURCE_NON_CANONICAL;
                break;
            }
        } else {
            vertices[vertex].normal[1] = 1.0f;
        }
        if (texCoord != nullptr
            && !ano_gltf_accessor_read_float(
                &data, texCoord, vertex, vertices[vertex].texCoord, 2)) {
            result = ANO_RESOURCE_NON_CANONICAL;
            break;
        }
        for (float value : vertices[vertex].position)
            if (!__builtin_isfinite(value))
                result = ANO_RESOURCE_NON_CANONICAL;
    }
    if (result == ANO_RESOURCE_OK) {
        const auto unpacked = ano_gltf_accessor_unpack_indices(
            &data, indicesAccessor, indices, sizeof(*indices),
            indicesAccessor->count);
        if (!unpacked || *unpacked != indicesAccessor->count)
            result = ANO_RESOURCE_NON_CANONICAL;
    }
    for (uint64_t i = 0; i < indicesAccessor->count
                         && result == ANO_RESOURCE_OK; ++i)
        if (indices[i] >= position->count)
            result = ANO_RESOURCE_NON_CANONICAL;

    float boundsMinimum[3] = {};
    float boundsMaximum[3] = {};
    if (result == ANO_RESOURCE_OK) {
        for (size_t axis = 0; axis < 3; ++axis)
            boundsMinimum[axis] = boundsMaximum[axis] =
                vertices[0].position[axis];
        for (uint64_t vertex = 1; vertex < position->count; ++vertex)
            for (size_t axis = 0; axis < 3; ++axis) {
                const float value = vertices[vertex].position[axis];
                if (value < boundsMinimum[axis])
                    boundsMinimum[axis] = value;
                if (value > boundsMaximum[axis])
                    boundsMaximum[axis] = value;
            }
    }
    if (result == ANO_RESOURCE_OK) {
        AnoAssetId materialAsset = scratch.defaultMaterial;
        if (ano_gltf_has_index(primitive.material))
            materialAsset = scratch.materialAssets[primitive.material.value];
        const Mesh mesh = {
            .vertices = {0, position->count},
            .indices = {vertexBytes, indicesAccessor->count},
            .material = {materialAsset},
            .boundsMinimum = {
                boundsMinimum[0], boundsMinimum[1], boundsMinimum[2]},
            .boundsMaximum = {
                boundsMaximum[0], boundsMaximum[1], boundsMaximum[2]},
        };
        result = prepare_artifact(
            region, job, job.asset, group, mesh,
            {.data = extent, .size = *extentSize});
    }
    return result;
}

AnoResourceError mark_selected_scene(const AnoGltfData& data,
                                     ImportScratch& scratch)
{
    if (data.scenesCount == 0)
        return ANO_RESOURCE_NON_CANONICAL;
    const uint32_t sceneIndex = ano_gltf_has_index(data.scene)
        ? data.scene.value : 0;
    if (sceneIndex >= data.scenesCount)
        return ANO_RESOURCE_NON_CANONICAL;
    const AnoGltfScene& scene = data.scenes[sceneIndex];
    for (uint32_t root = 0; root < scene.nodes.count; ++root) {
        if (scene.nodes.data[root].value >= data.nodesCount)
            return ANO_RESOURCE_NON_CANONICAL;
        scratch.reachableNodes[scene.nodes.data[root].value] = true;
    }
    for (uint32_t pass = 0; pass < data.nodesCount; ++pass) {
        bool changed = false;
        for (uint32_t node = 0; node < data.nodesCount; ++node) {
            if (!scratch.reachableNodes[node])
                continue;
            const AnoGltfNode& source = data.nodes[node];
            for (uint32_t child = 0; child < source.children.count; ++child) {
                const uint32_t childIndex = source.children.data[child].value;
                if (childIndex >= data.nodesCount)
                    return ANO_RESOURCE_NON_CANONICAL;
                if (!scratch.reachableNodes[childIndex]) {
                    scratch.reachableNodes[childIndex] = true;
                    changed = true;
                }
            }
        }
        if (!changed)
            return ANO_RESOURCE_OK;
    }
    return ANO_RESOURCE_NON_CANONICAL;
}

AnoResourceError analyze_scene(const AnoGltfData& data,
                               ImportScratch& scratch)
{
    for (uint32_t node = 0; node < data.nodesCount; ++node) {
        if (!scratch.reachableNodes[node])
            continue;
        const AnoGltfNode& source = data.nodes[node];
        if (ano_gltf_has_index(source.skin)
            || source.extensions.known.EXT_mesh_gpu_instancing.present)
            return ANO_RESOURCE_UNSUPPORTED;
        if (ano_gltf_has_index(source.mesh)) {
            if (source.mesh.value >= data.meshesCount)
                return ANO_RESOURCE_NON_CANONICAL;
            scratch.usedMeshes[source.mesh.value] = true;
        }
        const auto& light = source.extensions.known.KHR_lights_punctual;
        if (light.present
            && (!ano_gltf_has_index(light.value.light)
                || light.value.light.value >= data.lightsCount))
            return ANO_RESOURCE_NON_CANONICAL;
    }
    for (uint32_t mesh = 0; mesh < data.meshesCount; ++mesh) {
        if (!scratch.usedMeshes[mesh])
            continue;
        const AnoGltfMesh& source = data.meshes[mesh];
        for (uint32_t primitive = 0; primitive < source.primitives.count;
             ++primitive) {
            const AnoGltfPrimitive& value = source.primitives.data[primitive];
            const AnoGltfAccessor *position = nullptr;
            const AnoGltfAccessor *normal = nullptr;
            const AnoGltfAccessor *texCoord = nullptr;
            const AnoGltfAccessor *indices = nullptr;
            const AnoResourceError supported = primitive_accessors(
                data, value, &position, &normal, &texCoord, &indices);
            if (supported != ANO_RESOURCE_OK)
                return supported;
            if (ano_gltf_has_index(value.material)) {
                if (value.material.value >= data.materialsCount)
                    return ANO_RESOURCE_NON_CANONICAL;
                scratch.usedMaterials[value.material.value] = true;
            } else {
                scratch.needsDefaultMaterial = true;
            }
        }
    }
    for (uint32_t material = 0; material < data.materialsCount; ++material) {
        if (!scratch.usedMaterials[material])
            continue;
        if (data.materials[material].extensions.known
                .KHR_materials_pbrSpecularGlossiness.present)
            return ANO_RESOURCE_UNSUPPORTED;
        const AnoResourceError marked = mark_material_images(
            data, data.materials[material], scratch.imageUsage);
        if (marked != ANO_RESOURCE_OK)
            return marked;
    }
    return ANO_RESOURCE_OK;
}

AnoResourceError allocate_asset_ids(AnoResourceCooker& cooker,
                                    const AnoGltfData& data,
                                    ImportScratch& scratch)
{
    for (uint32_t image = 0; image < data.imagesCount; ++image)
        if (scratch.imageUsage[image] != 0) {
            const AnoResourceError result = ano_resource_cooker_allocate_derived(
                &cooker, &scratch.imageAssets[image]);
            if (result != ANO_RESOURCE_OK)
                return result;
        }
    for (uint32_t material = 0; material < data.materialsCount; ++material)
        if (scratch.usedMaterials[material]) {
            const AnoResourceError result = ano_resource_cooker_allocate_derived(
                &cooker, &scratch.materialAssets[material]);
            if (result != ANO_RESOURCE_OK)
                return result;
        }
    if (scratch.needsDefaultMaterial) {
        const AnoResourceError result = ano_resource_cooker_allocate_derived(
            &cooker, &scratch.defaultMaterial);
        if (result != ANO_RESOURCE_OK)
            return result;
    }
    for (uint32_t mesh = 0; mesh < data.meshesCount; ++mesh)
        if (scratch.usedMeshes[mesh])
            for (uint32_t primitive = 0;
                 primitive < data.meshes[mesh].primitives.count; ++primitive) {
                const uint64_t index = scratch.meshFirstPrimitive[mesh]
                    + primitive;
                const AnoResourceError result =
                    ano_resource_cooker_allocate_derived(
                        &cooker, &scratch.primitiveAssets[index]);
                if (result != ANO_RESOURCE_OK)
                    return result;
            }
    return ANO_RESOURCE_OK;
}

void run_import_job(void *argument, uint64_t index,
                    ano::MemoryRegion *)
{
    ImportBatch& batch = *static_cast<ImportBatch *>(argument);
    ImportJob& job = batch.jobs[index];
    if (ano_resource_cooker_cancelled(batch.cooker)) {
        job.result = ANO_RESOURCE_CANCELLED;
    } else if (job.kind == ImportJobKind::texture) {
        job.result = build_texture(
            *batch.cooker, batch.sourcePath, *batch.data, *batch.scratch,
            job.first, batch.commitGroup, batch.region, job);
    } else {
        job.result = build_mesh(
            *batch.data, *batch.scratch, job.first, job.second,
            batch.region, batch.commitGroup, job);
    }
}

AnoResourceError prepare_scene_root(
    const AnoResourceImportRequest& request, const AnoGltfData& data,
    const ImportScratch& scratch, ano::MemoryRegion *region,
    ImportJob& job);

AnoResourceError build_import_artifacts(
    AnoResourceCooker& cooker, const AnoResourceImportRequest& request,
    const char *sourcePath, const AnoGltfData& data,
    const ImportScratch& scratch, ano::MemoryRegion *region)
{
    const bool all = ano_resource_cooker_import_all(&cooker);
    const bool meshesChanged = all
        || mesh_inputs_changed(cooker, sourcePath, data);
    uint64_t jobCount = 0;
    for (uint32_t image = 0; image < data.imagesCount; ++image)
        if (scratch.imageUsage[image] != 0
            && (all || image_changed(cooker, sourcePath, data, image)))
            ++jobCount;
    for (uint32_t mesh = 0; mesh < data.meshesCount; ++mesh)
        if (meshesChanged && scratch.usedMeshes[mesh]
            && !ano::checked_accumulate(
                jobCount, uint64_t{data.meshes[mesh].primitives.count}))
            return ANO_RESOURCE_OVERFLOW;
    uint64_t parallelCount = jobCount;
    if (all) {
        for (uint32_t material = 0; material < data.materialsCount;
             ++material)
            if (scratch.usedMaterials[material]
                && !ano::checked_accumulate(jobCount, UINT64_C(1)))
                return ANO_RESOURCE_OVERFLOW;
        const uint64_t tail = (scratch.needsDefaultMaterial ? 1u : 0u) + 1u;
        if (!ano::checked_accumulate(jobCount, tail))
            return ANO_RESOURCE_OVERFLOW;
    }

    const auto jobBytes = ano::checked_allocation_size(
        jobCount, sizeof(ImportJob));
    if (!jobBytes)
        return ANO_RESOURCE_OVERFLOW;
    ImportJob *jobs = *jobBytes == 0 ? nullptr
        : ano::memory_region_allocate_zero<ImportJob>(
              region, static_cast<size_t>(jobCount)).value_or(nullptr);
    if (*jobBytes != 0 && jobs == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;

    uint64_t cursor = 0;
    for (uint32_t image = 0; image < data.imagesCount; ++image)
        if (scratch.imageUsage[image] != 0
            && (all || image_changed(cooker, sourcePath, data, image)))
            jobs[cursor++] = {
                .kind = ImportJobKind::texture,
                .first = image,
                .asset = scratch.imageAssets[image],
            };
    for (uint32_t mesh = 0; mesh < data.meshesCount; ++mesh)
        if (meshesChanged && scratch.usedMeshes[mesh])
            for (uint32_t primitive = 0;
                 primitive < data.meshes[mesh].primitives.count; ++primitive) {
                const uint64_t index = scratch.meshFirstPrimitive[mesh]
                    + primitive;
                jobs[cursor++] = {
                    .kind = ImportJobKind::mesh,
                    .first = mesh,
                    .second = primitive,
                    .asset = scratch.primitiveAssets[index],
                };
            }

    ImportBatch batch = {
        .cooker = &cooker,
        .commitGroup = request.commitGroup,
        .sourcePath = sourcePath,
        .data = &data,
        .scratch = &scratch,
        .region = region,
        .jobs = jobs,
    };
    AnoContentId rootInput{};
    AnoContentId meshInput{};
    AnoResourceError result = root_input_identity(
        cooker, sourcePath, &rootInput);
    if (result == ANO_RESOURCE_OK && meshesChanged)
        result = mesh_input_identity(
            cooker, sourcePath, data, &meshInput);
    for (uint64_t i = 0;
         i < parallelCount && result == ANO_RESOURCE_OK; ++i) {
        if (jobs[i].kind == ImportJobKind::texture)
            result = texture_input_identity(
                cooker, sourcePath, data, jobs[i].first,
                &jobs[i].inputIdentity);
        else
            jobs[i].inputIdentity = meshInput;
    }
    uint64_t requiredParallelCount = 0;
    for (uint64_t i = 0;
         i < parallelCount && result == ANO_RESOURCE_OK; ++i) {
        bool required = true;
        result = ano_resource_cooker_action_required(
            &cooker, jobs[i].asset, import_job_type(jobs[i].kind),
            request.commitGroup, jobs[i].inputIdentity, &required);
        if (required)
            jobs[requiredParallelCount++] = jobs[i];
    }
    parallelCount = requiredParallelCount;
    cursor = parallelCount;
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cooker_parallel(
            &cooker, parallelCount, &batch, run_import_job);

    for (uint64_t i = 0; i < parallelCount && result == ANO_RESOURCE_OK; ++i)
        result = jobs[i].result;
    if (result == ANO_RESOURCE_OK
        && ano_resource_cooker_cancelled(&cooker))
        result = ANO_RESOURCE_CANCELLED;
    if (all) {
        for (uint32_t material = 0;
             material < data.materialsCount && result == ANO_RESOURCE_OK;
             ++material) {
            if (!scratch.usedMaterials[material])
                continue;
            ImportJob& job = jobs[cursor];
            job.kind = ImportJobKind::material;
            job.asset = scratch.materialAssets[material];
            job.inputIdentity = rootInput;
            bool required = true;
            result = ano_resource_cooker_action_required(
                &cooker, job.asset, resource_type_id<Material>(),
                request.commitGroup, job.inputIdentity, &required);
            if (result == ANO_RESOURCE_OK && required) {
                Material value{};
                result = make_material(
                    data, &data.materials[material], scratch.imageAssets,
                    &value);
                if (result == ANO_RESOURCE_OK)
                    result = prepare_artifact(
                        region, job, job.asset, request.commitGroup, value);
                if (result == ANO_RESOURCE_OK)
                    ++cursor;
            }
        }
        if (result == ANO_RESOURCE_OK && scratch.needsDefaultMaterial) {
            ImportJob& job = jobs[cursor];
            job.kind = ImportJobKind::material;
            job.asset = scratch.defaultMaterial;
            job.inputIdentity = rootInput;
            bool required = true;
            result = ano_resource_cooker_action_required(
                &cooker, job.asset, resource_type_id<Material>(),
                request.commitGroup, job.inputIdentity, &required);
            if (result == ANO_RESOURCE_OK && required) {
                Material value{};
                result = make_material(
                    data, nullptr, scratch.imageAssets, &value);
                if (result == ANO_RESOURCE_OK)
                    result = prepare_artifact(
                        region, job, job.asset, request.commitGroup, value);
                if (result == ANO_RESOURCE_OK)
                    ++cursor;
            }
        }
        if (result == ANO_RESOURCE_OK) {
            ImportJob& job = jobs[cursor];
            job.kind = ImportJobKind::scene;
            job.asset = request.rootAsset;
            job.inputIdentity = rootInput;
            bool required = true;
            result = ano_resource_cooker_action_required(
                &cooker, job.asset, resource_type_id<Scene>(),
                request.commitGroup, job.inputIdentity, &required);
            if (result == ANO_RESOURCE_OK && required) {
                result = prepare_scene_root(
                    request, data, scratch, region, job);
                if (result == ANO_RESOURCE_OK)
                    ++cursor;
            }
        }
    }
    jobCount = cursor;
    AnoResourceCookArtifact *artifacts = jobCount == 0 ? nullptr
        : ano::memory_region_allocate<AnoResourceCookArtifact>(
              region, static_cast<size_t>(jobCount)).value_or(nullptr);
    if (result == ANO_RESOURCE_OK && jobCount != 0 && artifacts == nullptr)
        result = ANO_RESOURCE_OUT_OF_MEMORY;
    for (uint64_t i = 0; i < jobCount && result == ANO_RESOURCE_OK; ++i)
        artifacts[i] = jobs[i].artifact;
    if (result == ANO_RESOURCE_OK) {
        const auto encoded = ano_resource_cooker_encode_batch(
            &cooker, artifacts, jobCount);
        result = encoded ? ANO_RESOURCE_OK : encoded.error();
    }
    for (uint64_t i = 0; i < jobCount; ++i)
        stbi_image_free(jobs[i].ownedExtent);
    return result;
}

AnoResourceError prepare_scene_root(
    const AnoResourceImportRequest& request, const AnoGltfData& data,
    const ImportScratch& scratch, ano::MemoryRegion *region,
    ImportJob& job)
{
    uint64_t renderableCount = 0;
    uint64_t lightCount = 0;
    for (uint32_t node = 0; node < data.nodesCount; ++node) {
        if (!scratch.reachableNodes[node])
            continue;
        const AnoGltfNode& source = data.nodes[node];
        if (ano_gltf_has_index(source.mesh)) {
            if (!ano::checked_accumulate(
                    renderableCount, uint64_t{
                        data.meshes[source.mesh.value].primitives.count}))
                return ANO_RESOURCE_OVERFLOW;
        }
        if (source.extensions.known.KHR_lights_punctual.present
            && !ano::checked_accumulate(lightCount, UINT64_C(1)))
            return ANO_RESOURCE_OVERFLOW;
    }
    const auto renderableBytes = ano::checked_multiply(
        renderableCount, uint64_t{sizeof(SceneRenderable)});
    const auto lightBytes = ano::checked_multiply(
        lightCount, uint64_t{sizeof(SceneLight)});
    const auto extentSize = renderableBytes && lightBytes
        ? ano::checked_add(*renderableBytes, *lightBytes)
        : ano::ArithmeticResult<uint64_t>(failure(ano::ArithmeticError::overflow));
    if (!extentSize || *extentSize > SIZE_MAX)
        return ANO_RESOURCE_OVERFLOW;
    uint8_t *extent = *extentSize == 0 ? nullptr
        : ano::memory_region_allocate_zero<uint8_t>(
              region, static_cast<size_t>(*extentSize)).value_or(nullptr);
    if (*extentSize != 0 && extent == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    SceneRenderable *renderables =
        reinterpret_cast<SceneRenderable *>(extent);
    SceneLight *lights = reinterpret_cast<SceneLight *>(
        extent == nullptr ? nullptr : extent + *renderableBytes);
    uint64_t renderableCursor = 0;
    uint64_t lightCursor = 0;
    AnoResourceError result = ANO_RESOURCE_OK;
    for (uint32_t node = 0; node < data.nodesCount
                         && result == ANO_RESOURCE_OK; ++node) {
        if (!scratch.reachableNodes[node])
            continue;
        const AnoGltfNode& source = data.nodes[node];
        float world[16] = {};
        if (!ano_gltf_node_transform_world(
                &data, AnoGltfNodeIndex{node}, world)) {
            result = ANO_RESOURCE_NON_CANONICAL;
            break;
        }
        if (ano_gltf_has_index(source.mesh)) {
            const uint32_t mesh = source.mesh.value;
            for (uint32_t primitive = 0;
                 primitive < data.meshes[mesh].primitives.count; ++primitive) {
                SceneRenderable& output = renderables[renderableCursor++];
                output.mesh.id = scratch.primitiveAssets[
                    scratch.meshFirstPrimitive[mesh] + primitive];
                memcpy(output.transform, world, sizeof(world));
            }
        }
        const auto& lightReference =
            source.extensions.known.KHR_lights_punctual;
        if (lightReference.present) {
            const AnoGltfLight& input =
                data.lights[lightReference.value.light.value];
            SceneLight& output = lights[lightCursor++];
            memcpy(output.transform, world, sizeof(world));
            memcpy(output.color, input.color.values, sizeof(output.color));
            output.intensity = input.intensity;
            output.range = input.range.present ? input.range.value : 0.0f;
            output.innerConeAngle = input.spot.innerConeAngle;
            output.outerConeAngle = input.spot.outerConeAngle;
            output.castsShadow = false;
            if (!reflected_enum_cast(input.type, &output.type))
                result = ANO_RESOURCE_NON_CANONICAL;
        }
    }
    if (result == ANO_RESOURCE_OK) {
        const Scene scene = {
            .renderables = renderableCount == 0
                ? RelativeSpan<SceneRenderable>{0, 0}
                : RelativeSpan<SceneRenderable>{0, renderableCount},
            .lights = lightCount == 0
                ? RelativeSpan<SceneLight>{0, 0}
                : RelativeSpan<SceneLight>{*renderableBytes, lightCount},
        };
        result = prepare_artifact(
            region, job, request.rootAsset, request.commitGroup, scene,
            {.data = extent, .size = *extentSize});
    }
    return result;
}

AnoResourceError initialize_scratch(ano::MemoryRegion *region,
                                    const AnoGltfData& data,
                                    ImportScratch& scratch)
{
    scratch.reachableNodes = zero_array<bool>(region, data.nodesCount);
    scratch.usedMeshes = zero_array<bool>(region, data.meshesCount);
    scratch.usedMaterials = zero_array<bool>(region, data.materialsCount);
    scratch.imageUsage = zero_array<uint8_t>(region, data.imagesCount);
    scratch.imageAssets = zero_array<AnoAssetId>(region, data.imagesCount);
    scratch.materialAssets = zero_array<AnoAssetId>(
        region, data.materialsCount);
    scratch.meshFirstPrimitive = zero_array<uint64_t>(
        region, static_cast<uint64_t>(data.meshesCount) + 1);
    if ((data.nodesCount != 0 && scratch.reachableNodes == nullptr)
        || (data.meshesCount != 0
            && (scratch.usedMeshes == nullptr
                || scratch.meshFirstPrimitive == nullptr))
        || (data.materialsCount != 0
            && (scratch.usedMaterials == nullptr
                || scratch.materialAssets == nullptr))
        || (data.imagesCount != 0
            && (scratch.imageUsage == nullptr
                || scratch.imageAssets == nullptr)))
        return ANO_RESOURCE_OUT_OF_MEMORY;
    for (uint32_t mesh = 0; mesh < data.meshesCount; ++mesh) {
        scratch.meshFirstPrimitive[mesh] = scratch.primitiveCount;
        if (!ano::checked_accumulate(
                scratch.primitiveCount,
                uint64_t{data.meshes[mesh].primitives.count}))
            return ANO_RESOURCE_OVERFLOW;
    }
    scratch.meshFirstPrimitive[data.meshesCount] = scratch.primitiveCount;
    scratch.primitiveAssets = zero_array<AnoAssetId>(
        region, scratch.primitiveCount);
    if (scratch.primitiveCount != 0 && scratch.primitiveAssets == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    return ANO_RESOURCE_OK;
}

} // namespace

ResourceResult<> import_gltf(AnoResourceCooker& cooker,
                             const AnoResourceImportRequest& request) noexcept
{
    if (!ano_resource_cooker_root_valid(&cooker, request.rootAsset))
        return failure(ANO_RESOURCE_INVALID_ARGUMENT);
    const char *sourcePath = ano_resource_cooker_source_path(
        &cooker, request.source);
    if (sourcePath == nullptr)
        return failure(ANO_RESOURCE_NOT_FOUND);
    const AnoResourceCookCheckpoint checkpoint =
        ano_resource_cooker_checkpoint(&cooker);
    ano::MemoryRegion *region = ano::memory_region_create().value_or(nullptr);
    if (region == nullptr)
        return failure(ANO_RESOURCE_OUT_OF_MEMORY);
    AnoGltfOptions options = {
        .allocate = gltf_allocate,
        .free = gltf_release,
        .user = region,
    };
    AnoGltfData *data = nullptr;
    AnoResourceBytes rootBytes{};
    AnoResourceError result = ano_resource_cooker_source_bytes(
        &cooker, sourcePath, &rootBytes);
    if (result == ANO_RESOURCE_OK) {
        const auto parsed = ano_gltf_parse_memory(
            rootBytes.data, static_cast<size_t>(rootBytes.size), &options);
        result = gltf_error(parsed);
        if (parsed)
            data = *parsed;
    }
    if (result == ANO_RESOURCE_OK)
        result = register_source_files(cooker, sourcePath, *data);
    if (result == ANO_RESOURCE_OK)
        result = bind_external_buffers(cooker, sourcePath, *data);
    if (result == ANO_RESOURCE_OK)
        result = gltf_error(
            ano_gltf_load_buffers(data, sourcePath, &options));
    if (result == ANO_RESOURCE_OK)
        result = gltf_error(ano_gltf_validate_loaded_data(data));

    ImportScratch scratch = {};
    if (result == ANO_RESOURCE_OK)
        result = initialize_scratch(region, *data, scratch);
    if (result == ANO_RESOURCE_OK)
        result = mark_selected_scene(*data, scratch);
    if (result == ANO_RESOURCE_OK)
        result = analyze_scene(*data, scratch);
    if (result == ANO_RESOURCE_OK)
        result = allocate_asset_ids(cooker, *data, scratch);
    if (result == ANO_RESOURCE_OK)
        result = build_import_artifacts(
            cooker, request, sourcePath, *data, scratch, region);

    ano_gltf_free(data);
    ano::memory_region_destroy(region);
    if (result != ANO_RESOURCE_OK)
        ano_resource_cooker_rollback(&cooker, checkpoint);
    return resource_status(result);
}

} // namespace ano::asset_schema
