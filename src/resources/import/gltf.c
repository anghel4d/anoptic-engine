/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include "../cooker_internal.h"
#include "../parallel.h"

#include <anogltf.h>
#include <anoptic_memory.h>
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

struct EncodedArtifact final {
    uint8_t *bytes;
    uint64_t size;
};

enum class ImportJobKind : uint8_t {
    texture,
    mesh,
};

struct ImportJob final {
    ImportJobKind kind;
    uint32_t first;
    uint32_t second;
    AnoAssetId asset;
    EncodedArtifact artifact;
    AnoResourceError result;
};

struct ImportBatch final {
    AnoResourceCooker *cooker;
    const char *sourcePath;
    const AnoGltfData *data;
    const ImportScratch *scratch;
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
Type *zero_array(uint64_t count)
{
    if (count == 0)
        return nullptr;
    if (count > SIZE_MAX / sizeof(Type))
        return nullptr;
    return static_cast<Type *>(
        mi_calloc(static_cast<size_t>(count), sizeof(Type)));
}

void release_scratch(ImportScratch& scratch)
{
    mi_free(scratch.meshFirstPrimitive);
    mi_free(scratch.primitiveAssets);
    mi_free(scratch.materialAssets);
    mi_free(scratch.imageAssets);
    mi_free(scratch.imageUsage);
    mi_free(scratch.usedMaterials);
    mi_free(scratch.usedMeshes);
    mi_free(scratch.reachableNodes);
    scratch = {};
}

void *gltf_allocate(void *, size_t bytes)
{
    return mi_malloc(bytes);
}

void gltf_release(void *, void *allocation)
{
    mi_free(allocation);
}

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

template<class ArtifactType>
AnoResourceError encode_artifact(ArtifactSource<ArtifactType> source,
                                 EncodedArtifact *artifact)
{
    if (artifact == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *artifact = {};
    const EncodeResult measured = encoded_size(source);
    if (measured.error != ANO_RESOURCE_OK)
        return measured.error;
    if (measured.size > SIZE_MAX)
        return ANO_RESOURCE_OVERFLOW;
    uint8_t *bytes = static_cast<uint8_t *>(
        mi_malloc(static_cast<size_t>(measured.size)));
    if (bytes == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    const EncodeResult encoded = encode(
        source, {.data = bytes, .size = measured.size});
    if (encoded.error != ANO_RESOURCE_OK || encoded.size != measured.size) {
        mi_free(bytes);
        return encoded.error == ANO_RESOURCE_OK
            ? ANO_RESOURCE_NON_CANONICAL : encoded.error;
    }
    *artifact = {.bytes = bytes, .size = measured.size};
    return ANO_RESOURCE_OK;
}

template<class ArtifactType>
AnoResourceError encode_and_adopt(AnoResourceCooker& cooker, AnoAssetId asset,
                                  AnoResourceCommitGroupId group,
                                  ArtifactSource<ArtifactType> source)
{
    EncodedArtifact artifact = {};
    AnoResourceError result = encode_artifact(source, &artifact);
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cooker_adopt(
            &cooker, asset, resource_type_id<ArtifactType>(), group,
            artifact.bytes, artifact.size);
    if (result != ANO_RESOURCE_OK)
        mi_free(artifact.bytes);
    return result;
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

AnoResourceError decode_image(const AnoGltfData& data, uint32_t imageIndex,
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
        const AnoResourceError resolved = resolved_image_path(
            gltfPath, image.uri, &path);
        if (resolved != ANO_RESOURCE_OK)
            return resolved;
        *pixels = stbi_load(path, &decodedWidth, &decodedHeight, &channels,
                            STBI_rgb_alpha);
        mi_free(path);
    } else if (ano_gltf_has_index(image.bufferView)) {
        if (image.bufferView.value >= data.bufferViewsCount)
            return ANO_RESOURCE_NON_CANONICAL;
        const AnoGltfBufferView& view =
            data.bufferViews[image.bufferView.value];
        const uint8_t *bytes = ano_gltf_buffer_view_data(&data,
                                                         image.bufferView);
        if (bytes == nullptr || view.byteLength > INT_MAX)
            return ANO_RESOURCE_NON_CANONICAL;
        *pixels = stbi_load_from_memory(
            bytes, static_cast<int>(view.byteLength), &decodedWidth,
            &decodedHeight, &channels, STBI_rgb_alpha);
    } else {
        return ANO_RESOURCE_NON_CANONICAL;
    }
    if (*pixels == nullptr || decodedWidth <= 0 || decodedHeight <= 0) {
        stbi_image_free(*pixels);
        *pixels = nullptr;
        return ANO_RESOURCE_NON_CANONICAL;
    }
    uint64_t pixelCount = 0;
    if (!ano::detail::checked_multiply(
            static_cast<uint64_t>(decodedWidth),
            static_cast<uint64_t>(decodedHeight), &pixelCount)
        || !ano::detail::checked_multiply(pixelCount, 4, byteCount)) {
        stbi_image_free(*pixels);
        *pixels = nullptr;
        return ANO_RESOURCE_OVERFLOW;
    }
    *width = static_cast<uint32_t>(decodedWidth);
    *height = static_cast<uint32_t>(decodedHeight);
    return ANO_RESOURCE_OK;
}

AnoResourceError build_texture(const char *sourcePath,
                                const AnoGltfData& data,
                                const ImportScratch& scratch,
                                uint32_t imageIndex,
                                EncodedArtifact *artifact)
{
    stbi_uc *pixels = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t byteCount = 0;
    AnoResourceError result = decode_image(
        data, imageIndex, sourcePath, &pixels, &width, &height,
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
    result = encode_artifact(
        ArtifactSource<Texture>{
            .value = &texture,
            .extent = {.data = pixels, .size = byteCount},
        }, artifact);
    stbi_image_free(pixels);
    return result;
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
                             EncodedArtifact *artifact)
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
    uint64_t extentSize = 0;
    if (!ano::detail::checked_add(vertexBytes, indexBytes, &extentSize)
        || extentSize > SIZE_MAX)
        return ANO_RESOURCE_OVERFLOW;
    uint8_t *extent = static_cast<uint8_t *>(
        mi_calloc(1, static_cast<size_t>(extentSize)));
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
    if (result == ANO_RESOURCE_OK
        && ano_gltf_accessor_unpack_indices(
               &data, indicesAccessor, indices, sizeof(*indices),
               indicesAccessor->count) != indicesAccessor->count)
        result = ANO_RESOURCE_NON_CANONICAL;
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
        result = encode_artifact(
            ArtifactSource<Mesh>{
                .value = &mesh,
                .extent = {.data = extent, .size = extentSize},
            }, artifact);
    }
    mi_free(extent);
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

void run_import_job(void *argument, uint64_t index)
{
    ImportBatch& batch = *static_cast<ImportBatch *>(argument);
    ImportJob& job = batch.jobs[index];
    if (ano_resource_cooker_cancelled(batch.cooker)) {
        job.result = ANO_RESOURCE_CANCELLED;
    } else if (job.kind == ImportJobKind::texture) {
        job.result = build_texture(
            batch.sourcePath, *batch.data, *batch.scratch, job.first,
            &job.artifact);
    } else {
        job.result = build_mesh(
            *batch.data, *batch.scratch, job.first, job.second,
            &job.artifact);
    }
}

AnoResourceError build_import_artifacts(
    AnoResourceCooker& cooker, const AnoResourceImportRequest& request,
    const char *sourcePath, const AnoGltfData& data,
    const ImportScratch& scratch)
{
    uint64_t jobCount = 0;
    for (uint32_t image = 0; image < data.imagesCount; ++image)
        if (scratch.imageUsage[image] != 0)
            ++jobCount;
    for (uint32_t mesh = 0; mesh < data.meshesCount; ++mesh)
        if (scratch.usedMeshes[mesh]
            && !ano::detail::checked_add(
                jobCount, data.meshes[mesh].primitives.count, &jobCount))
            return ANO_RESOURCE_OVERFLOW;

    size_t jobBytes = 0;
    if (!ano::detail::checked_allocation_size(
            jobCount, sizeof(ImportJob), &jobBytes))
        return ANO_RESOURCE_OVERFLOW;
    ImportJob *jobs = jobBytes == 0 ? nullptr
        : static_cast<ImportJob *>(mi_calloc(1, jobBytes));
    if (jobBytes != 0 && jobs == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;

    uint64_t cursor = 0;
    for (uint32_t image = 0; image < data.imagesCount; ++image)
        if (scratch.imageUsage[image] != 0)
            jobs[cursor++] = {
                .kind = ImportJobKind::texture,
                .first = image,
                .asset = scratch.imageAssets[image],
            };
    for (uint32_t mesh = 0; mesh < data.meshesCount; ++mesh)
        if (scratch.usedMeshes[mesh])
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
        .sourcePath = sourcePath,
        .data = &data,
        .scratch = &scratch,
        .jobs = jobs,
    };
    ano::resource_detail::parallel_for(
        jobCount, &batch, run_import_job);

    AnoResourceError result = ANO_RESOURCE_OK;
    for (uint64_t i = 0; i < jobCount && result == ANO_RESOURCE_OK; ++i)
        result = jobs[i].result;
    if (result == ANO_RESOURCE_OK
        && ano_resource_cooker_cancelled(&cooker))
        result = ANO_RESOURCE_CANCELLED;
    for (uint64_t i = 0; i < jobCount && result == ANO_RESOURCE_OK; ++i) {
        ImportJob& job = jobs[i];
        const AnoResourceTypeId type = job.kind == ImportJobKind::texture
            ? resource_type_id<Texture>() : resource_type_id<Mesh>();
        result = ano_resource_cooker_adopt(
            &cooker, job.asset, type, request.commitGroup,
            job.artifact.bytes, job.artifact.size);
        if (result == ANO_RESOURCE_OK)
            job.artifact = {};
    }
    for (uint64_t i = 0; i < jobCount; ++i)
        mi_free(jobs[i].artifact.bytes);
    mi_free(jobs);
    return result;
}

AnoResourceError import_scene_root(AnoResourceCooker& cooker,
                                   const AnoResourceImportRequest& request,
                                   const AnoGltfData& data,
                                   const ImportScratch& scratch)
{
    uint64_t renderableCount = 0;
    uint64_t lightCount = 0;
    for (uint32_t node = 0; node < data.nodesCount; ++node) {
        if (!scratch.reachableNodes[node])
            continue;
        const AnoGltfNode& source = data.nodes[node];
        if (ano_gltf_has_index(source.mesh)) {
            if (!ano::detail::checked_add(
                    renderableCount,
                    data.meshes[source.mesh.value].primitives.count,
                    &renderableCount))
                return ANO_RESOURCE_OVERFLOW;
        }
        if (source.extensions.known.KHR_lights_punctual.present
            && !ano::detail::checked_add(lightCount, 1, &lightCount))
            return ANO_RESOURCE_OVERFLOW;
    }
    uint64_t renderableBytes = 0;
    uint64_t lightBytes = 0;
    uint64_t extentSize = 0;
    if (!ano::detail::checked_multiply(
            renderableCount, sizeof(SceneRenderable), &renderableBytes)
        || !ano::detail::checked_multiply(
            lightCount, sizeof(SceneLight), &lightBytes)
        || !ano::detail::checked_add(renderableBytes, lightBytes,
                                     &extentSize)
        || extentSize > SIZE_MAX)
        return ANO_RESOURCE_OVERFLOW;
    uint8_t *extent = extentSize == 0 ? nullptr
        : static_cast<uint8_t *>(
            mi_calloc(1, static_cast<size_t>(extentSize)));
    if (extentSize != 0 && extent == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    SceneRenderable *renderables =
        reinterpret_cast<SceneRenderable *>(extent);
    SceneLight *lights = reinterpret_cast<SceneLight *>(
        extent == nullptr ? nullptr : extent + renderableBytes);
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
                : RelativeSpan<SceneLight>{renderableBytes, lightCount},
        };
        result = encode_and_adopt(
            cooker, request.rootAsset, request.commitGroup,
            ArtifactSource<Scene>{
                .value = &scene,
                .extent = {.data = extent, .size = extentSize},
            });
    }
    mi_free(extent);
    return result;
}

AnoResourceError initialize_scratch(const AnoGltfData& data,
                                    ImportScratch& scratch)
{
    scratch.reachableNodes = zero_array<bool>(data.nodesCount);
    scratch.usedMeshes = zero_array<bool>(data.meshesCount);
    scratch.usedMaterials = zero_array<bool>(data.materialsCount);
    scratch.imageUsage = zero_array<uint8_t>(data.imagesCount);
    scratch.imageAssets = zero_array<AnoAssetId>(data.imagesCount);
    scratch.materialAssets = zero_array<AnoAssetId>(data.materialsCount);
    scratch.meshFirstPrimitive = zero_array<uint64_t>(
        static_cast<uint64_t>(data.meshesCount) + 1);
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
        if (!ano::detail::checked_add(
                scratch.primitiveCount,
                data.meshes[mesh].primitives.count,
                &scratch.primitiveCount))
            return ANO_RESOURCE_OVERFLOW;
    }
    scratch.meshFirstPrimitive[data.meshesCount] = scratch.primitiveCount;
    scratch.primitiveAssets = zero_array<AnoAssetId>(scratch.primitiveCount);
    if (scratch.primitiveCount != 0 && scratch.primitiveAssets == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    return ANO_RESOURCE_OK;
}

} // namespace

AnoResourceError import_gltf(AnoResourceCooker& cooker,
                             const AnoResourceImportRequest& request) noexcept
{
    if (!ano_resource_cooker_root_valid(&cooker, request.rootAsset))
        return ANO_RESOURCE_INVALID_ARGUMENT;
    const char *sourcePath = ano_resource_cooker_source_path(
        &cooker, request.source);
    if (sourcePath == nullptr)
        return ANO_RESOURCE_NOT_FOUND;
    const AnoResourceCookCheckpoint checkpoint =
        ano_resource_cooker_checkpoint(&cooker);
    AnoGltfOptions options = {
        .allocate = gltf_allocate,
        .free = gltf_release,
    };
    AnoGltfData *data = nullptr;
    AnoResourceError result = gltf_error(
        ano_gltf_parse_file(sourcePath, &options, &data));
    if (result == ANO_RESOURCE_OK)
        result = gltf_error(
            ano_gltf_load_buffers(data, sourcePath, &options));
    if (result == ANO_RESOURCE_OK)
        result = gltf_error(ano_gltf_validate_loaded_data(data));

    ImportScratch scratch = {};
    if (result == ANO_RESOURCE_OK)
        result = initialize_scratch(*data, scratch);
    if (result == ANO_RESOURCE_OK)
        result = mark_selected_scene(*data, scratch);
    if (result == ANO_RESOURCE_OK)
        result = analyze_scene(*data, scratch);
    if (result == ANO_RESOURCE_OK)
        result = allocate_asset_ids(cooker, *data, scratch);
    if (result == ANO_RESOURCE_OK)
        result = build_import_artifacts(
            cooker, request, sourcePath, *data, scratch);
    for (uint32_t material = 0;
         material < (data == nullptr ? 0 : data->materialsCount)
             && result == ANO_RESOURCE_OK; ++material) {
        if (!scratch.usedMaterials[material])
            continue;
        Material value = {};
        result = make_material(*data, &data->materials[material],
                               scratch.imageAssets, &value);
        if (result == ANO_RESOURCE_OK)
            result = encode_and_adopt(
                cooker, scratch.materialAssets[material], request.commitGroup,
                ArtifactSource<Material>{&value, {nullptr, 0}});
    }
    if (result == ANO_RESOURCE_OK && scratch.needsDefaultMaterial) {
        Material value = {};
        result = make_material(*data, nullptr, scratch.imageAssets, &value);
        if (result == ANO_RESOURCE_OK)
            result = encode_and_adopt(
                cooker, scratch.defaultMaterial, request.commitGroup,
                ArtifactSource<Material>{&value, {nullptr, 0}});
    }
    if (result == ANO_RESOURCE_OK)
        result = import_scene_root(cooker, request, *data, scratch);

    release_scratch(scratch);
    ano_gltf_free(data);
    if (result != ANO_RESOURCE_OK)
        ano_resource_cooker_rollback(&cooker, checkpoint);
    return result;
}

} // namespace ano::asset_schema
