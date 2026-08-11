/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include "resources.h"

#include <anoptic_memory.h>
#include <anoptic_render_resources.h>

#include "vulkan_backend/backend.h"
#include "vulkan_backend/components.h"
#include "vulkan_backend/geometry.h"
#include "vulkan_backend/gpu_alloc.h"
#include "vulkan_backend/texture/texture.h"
#include "vulkan_backend/vertex/vertex.h"
#include "vulkan_backend/vulkanMaster.h"

#include <meta>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string_view>
#include <type_traits>

namespace schema = ano::asset_schema;

enum class RenderBindingState : uint8_t {
    absent,
    realizing,
    resident,
};

struct RenderBinding final {
    AnoResourceTypeId sourceType;
    RenderBindingState state;
    schema::GpuTexture texture;
    schema::GpuMaterial material;
    schema::GpuMesh mesh;
    schema::GpuScene scene;
};

struct RealizedScene final {
    AnoRenderableDesc *renderables;
    uint64_t renderableCount;
    schema::SceneLight *lights;
    uint64_t lightCount;
};

struct AnoRenderResidency {
    const AnoResidencyEpoch *source;
    RenderBinding *bindings;
    uint64_t bindingCount;
    RealizedScene *scenes;
    uint32_t sceneCount;
    uint32_t sceneCapacity;
    VkCommandBuffer textureCommands;
    VkBuffer *textureStaging;
    uint32_t textureStagingCount;
    uint32_t textureStagingCapacity;
};

namespace ano::asset_schema {

struct RenderResourceContext {
    AnoRenderResidency *residency;
    AnoAssetId asset;
    AnoResourceBytes artifact;
};

} // namespace ano::asset_schema

namespace {

struct TransformEndpoints final {
    std::meta::info input;
    std::meta::info output;
};

consteval TransformEndpoints transform_endpoints(std::meta::info declaration)
{
    TransformEndpoints result = {};
    const auto parameters = std::meta::parameters_of(declaration);
    for (const std::meta::info parameter : parameters) {
        const std::meta::info parameterType = std::meta::type_of(parameter);
        const std::meta::info valueType =
            std::meta::remove_cvref(parameterType);
        if (!ano::detail::has_artifact_marker(valueType))
            continue;
        const std::meta::info referred =
            std::meta::remove_reference(parameterType);
        if (std::meta::is_const_type(referred))
            result.input = valueType;
        else
            result.output = valueType;
    }
    return result;
}

consteval bool material_slots_complete()
{
    static constexpr auto slots = std::define_static_array(
        std::meta::enumerators_of(^^schema::MaterialTextureSlot));
    static constexpr auto fields = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^MaterialData, std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info slot : slots) {
        size_t matches = 0;
        template for (constexpr std::meta::info field : fields) {
            constexpr schema::MaterialTextureSlotMatch match =
                schema::material_texture_slot(field);
            if constexpr (match.found && match.slot == [:slot:])
                ++matches;
        }
        if (matches != 1)
            return false;
    }
    return true;
}

consteval bool vertex_layout_compatible()
{
    static constexpr auto portable = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^schema::Vertex, std::meta::access_context::unchecked()));
    static constexpr auto device = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^::Vertex, std::meta::access_context::unchecked()));
    if (portable.size() != device.size()
        || sizeof(schema::Vertex) != sizeof(::Vertex))
        return false;
    for (size_t i = 0; i < portable.size(); ++i) {
        if (std::meta::identifier_of(portable[i])
                != std::meta::identifier_of(device[i])
            || std::meta::offset_of(portable[i]).total_bits()
                != std::meta::offset_of(device[i]).total_bits()
            || std::meta::size_of(std::meta::type_of(portable[i]))
                != std::meta::size_of(std::meta::type_of(device[i])))
            return false;
        const auto nested = std::meta::nonstatic_data_members_of(
            std::meta::type_of(device[i]),
            std::meta::access_context::unchecked());
        if (nested.size() != 1
            || std::meta::identifier_of(nested[0]) != "v"
            || std::meta::type_of(nested[0])
                != std::meta::type_of(portable[i]))
            return false;
    }
    return true;
}

static_assert(material_slots_complete(),
              "canonical material slots map exhaustively to the GPU ABI");
static_assert(vertex_layout_compatible(),
              "portable vertices map exactly to the reflected GPU vertex ABI");
static_assert(static_cast<uint8_t>(schema::TextureUsage::color)
                  == TEXTURE_USE_COLOR
              && static_cast<uint8_t>(schema::TextureUsage::data)
                  == TEXTURE_USE_DATA
              && static_cast<uint8_t>(schema::TextureUsage::color_and_data)
                  == (TEXTURE_USE_COLOR | TEXTURE_USE_DATA));

consteval PbrFeatureFlags pbr_named_feature(std::string_view source)
{
    constexpr std::string_view prefix = "PBR_FEATURE_";
    if (source.starts_with("pbr"))
        source.remove_prefix(3);
    static constexpr auto features = std::define_static_array(
        std::meta::enumerators_of(^^PbrFeatureBits));
    template for (constexpr std::meta::info feature : features) {
        std::string_view name = std::meta::identifier_of(feature);
        if (!name.starts_with(prefix))
            continue;
        name.remove_prefix(prefix.size());
        if (ano::detail::semantic_name_equal(source, name))
            return static_cast<PbrFeatureFlags>([:feature:]);
    }
    return PBR_FEATURE_NONE;
}

consteval PbrFeatureFlags pbr_material_feature(schema::MaterialFeature sought)
{
    static constexpr auto features = std::define_static_array(
        std::meta::enumerators_of(^^schema::MaterialFeature));
    template for (constexpr std::meta::info feature : features) {
        if (sought != [:feature:])
            continue;
        constexpr std::string_view name = std::meta::identifier_of(feature);
        if constexpr (name == "pbrMetallicRoughness")
            return PBR_FEATURE_BASE_COLOR_FACTOR
                | PBR_FEATURE_METALLIC_ROUGHNESS_FACTOR;
        if constexpr (name == "unlit")
            return PBR_FEATURE_NONE;
        return pbr_named_feature(name);
    }
    __builtin_abort();
}

consteval PbrFeatureFlags pbr_texture_feature(
    schema::MaterialTextureSlot sought)
{
    static constexpr auto slots = std::define_static_array(
        std::meta::enumerators_of(^^schema::MaterialTextureSlot));
    template for (constexpr std::meta::info slot : slots) {
        if (sought == [:slot:]) {
            constexpr auto name = std::meta::identifier_of(slot);
            static constexpr auto features = std::define_static_array(
                std::meta::enumerators_of(^^PbrFeatureBits));
            constexpr std::string_view prefix = "PBR_FEATURE_";
            template for (constexpr std::meta::info feature : features) {
                std::string_view target = std::meta::identifier_of(feature);
                if (!target.starts_with(prefix)
                    || !target.ends_with("_TEXTURE"))
                    continue;
                target.remove_prefix(prefix.size());
                target.remove_suffix(std::string_view("_TEXTURE").size());
                if (ano::detail::semantic_name_equal(name, target))
                    return static_cast<PbrFeatureFlags>([:feature:]);
            }
            return PBR_FEATURE_NONE;
        }
    }
    __builtin_abort();
}

template<class Destination, class Source>
void project_material_value(Destination& destination, const Source& source)
{
    if constexpr (std::is_same_v<Destination, Source>)
        destination = source;
    else if constexpr (std::is_same_v<Destination, uint32_t>
                       && std::is_same_v<Source, bool>)
        destination = source ? 1u : 0u;
    else if constexpr (std::is_same_v<Destination, uint32_t>
                       && std::is_enum_v<Source>)
        destination = static_cast<uint32_t>(ano::detail::enum_index(source));
}

template<size_t DestinationCount, size_t SourceCount>
void project_material_value(float (&destination)[DestinationCount],
                            const float (&source)[SourceCount])
{
    static_assert(DestinationCount == SourceCount
                  || DestinationCount == SourceCount + 1);
    for (size_t i = 0; i < SourceCount; ++i)
        destination[i] = source[i];
    if constexpr (DestinationCount == SourceCount + 1)
        destination[SourceCount] = 1.0f;
}

void project_material_data(MaterialData& destination,
                           const schema::Material& source)
{
    static constexpr auto destinations = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^MaterialData, std::meta::access_context::unchecked()));
    static constexpr auto sources = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^schema::Material, std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info target : destinations) {
        if constexpr (std::meta::identifier_of(target) != "features")
            template for (constexpr std::meta::info input : sources)
                if constexpr (std::meta::identifier_of(target)
                              == std::meta::identifier_of(input))
                    project_material_value(destination.[:target:],
                                           source.[:input:]);
    }
}

RenderBinding *binding(AnoRenderResidency& residency, AnoAssetId asset,
                       AnoResourceTypeId type)
{
    if (asset.value == 0 || asset.value > residency.bindingCount)
        return nullptr;
    RenderBinding& found = residency.bindings[asset.value - 1];
    return found.sourceType.value == type.value ? &found : nullptr;
}

template<class Type>
RenderBinding *binding(AnoRenderResidency& residency,
                       const ano::AssetRef<Type>& reference)
{
    return binding(residency, reference.id, ano::resource_type_id<Type>());
}

bool append_texture_staging(AnoRenderResidency& residency, VkBuffer staging)
{
    if (residency.textureStagingCount == residency.textureStagingCapacity) {
        const uint32_t capacity = residency.textureStagingCapacity == 0
            ? 16 : residency.textureStagingCapacity * 2;
        if (capacity < residency.textureStagingCapacity
            || capacity > SIZE_MAX / sizeof(VkBuffer))
            return false;
        void *grown = mi_realloc(
            residency.textureStaging,
            static_cast<size_t>(capacity) * sizeof(VkBuffer));
        if (grown == nullptr)
            return false;
        residency.textureStaging = static_cast<VkBuffer *>(grown);
        residency.textureStagingCapacity = capacity;
    }
    residency.textureStaging[residency.textureStagingCount++] = staging;
    return true;
}

void destroy_texture_package(TexturePackage& package)
{
    vkDestroyImageView(ctx.device, package.unormView, nullptr);
    vkDestroyImageView(ctx.device, package.srgbView, nullptr);
    vkDestroyImage(ctx.device, package.image, nullptr);
    vkDestroyBuffer(ctx.device, package.staging, nullptr);
    package = {};
}

AnoResourceError collect_dependencies(
    AnoResourceTypeId type, AnoResourceBytes artifact,
    AnoResourceDependency **dependencies, uint64_t *count)
{
    *dependencies = nullptr;
    *count = 0;
    uint64_t required = 0;
    AnoResourceError result = ano_resource_artifact_dependencies(
        type, artifact, nullptr, 0, &required);
    if (required == 0)
        return result;
    if (result != ANO_RESOURCE_DEPENDENCY_CAPACITY
        || required > SIZE_MAX / sizeof(AnoResourceDependency))
        return result == ANO_RESOURCE_DEPENDENCY_CAPACITY
            ? ANO_RESOURCE_OVERFLOW : result;
    AnoResourceDependency *values = static_cast<AnoResourceDependency *>(
        mi_malloc(static_cast<size_t>(required)
                  * sizeof(AnoResourceDependency)));
    if (values == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    uint64_t actual = 0;
    result = ano_resource_artifact_dependencies(
        type, artifact, values, required, &actual);
    if (result != ANO_RESOURCE_OK || actual != required) {
        mi_free(values);
        return result == ANO_RESOURCE_OK ? ANO_RESOURCE_NON_CANONICAL : result;
    }
    *dependencies = values;
    *count = actual;
    return ANO_RESOURCE_OK;
}

template<class Output>
void publish_output(RenderBinding& binding, const Output& output)
{
    if constexpr (std::is_same_v<Output, schema::GpuTexture>)
        binding.texture = output;
    else if constexpr (std::is_same_v<Output, schema::GpuMaterial>)
        binding.material = output;
    else if constexpr (std::is_same_v<Output, schema::GpuMesh>)
        binding.mesh = output;
    else if constexpr (std::is_same_v<Output, schema::GpuScene>)
        binding.scene = output;
    else
        static_assert(!std::is_same_v<Output, Output>,
                      "render transform output lacks a resident binding");
}

bool is_render_input(AnoResourceTypeId type)
{
    static constexpr auto declarations = std::define_static_array(
        std::meta::members_of(
            ^^ano::asset_schema, std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info declaration : declarations) {
        static constexpr auto annotations = std::define_static_array(
            std::meta::annotations_of_with_type(
                declaration, ^^ano::Transform));
        if constexpr (!annotations.empty()) {
            constexpr ano::Transform transform =
                std::meta::extract<ano::Transform>(annotations[0]);
            if constexpr (transform.executor == ano::Executor::render_master) {
                constexpr TransformEndpoints endpoints =
                    transform_endpoints(declaration);
                using Input = [:endpoints.input:];
                if (type.value == ano::resource_type_id<Input>().value)
                    return true;
            }
        }
    }
    return false;
}

AnoResourceError invoke_render_transform(
    RenderBinding& target, schema::RenderResourceContext& context,
    AnoResourceTypeId type, AnoResourceBytes artifact)
{
    static constexpr auto declarations = std::define_static_array(
        std::meta::members_of(
            ^^ano::asset_schema, std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info declaration : declarations) {
        static constexpr auto annotations = std::define_static_array(
            std::meta::annotations_of_with_type(
                declaration, ^^ano::Transform));
        if constexpr (!annotations.empty()) {
            constexpr ano::Transform transform =
                std::meta::extract<ano::Transform>(annotations[0]);
            if constexpr (transform.executor == ano::Executor::render_master) {
                constexpr TransformEndpoints endpoints =
                    transform_endpoints(declaration);
                using Input = [:endpoints.input:];
                using Output = [:endpoints.output:];
                if (type.value == ano::resource_type_id<Input>().value) {
                    const ano::DecodeResult<Input> decoded =
                        ano::decode<Input>(artifact);
                    if (decoded.error != ANO_RESOURCE_OK)
                        return decoded.error;
                    Output output = {};
                    context.artifact = artifact;
                    if (![:declaration:](decoded.view.value, context, output))
                        return ANO_RESOURCE_OWNER_REJECTED;
                    publish_output(target, output);
                    return ANO_RESOURCE_OK;
                }
            }
        }
    }
    return ANO_RESOURCE_UNSUPPORTED;
}

AnoResourceError realize_asset(AnoRenderResidency& residency,
                               AnoAssetId asset, AnoResourceTypeId type)
{
    RenderBinding *target = binding(residency, asset, type);
    if (target == nullptr)
        return ANO_RESOURCE_TYPE_MISMATCH;
    if (target->state == RenderBindingState::resident)
        return ANO_RESOURCE_OK;
    if (target->state == RenderBindingState::realizing)
        return ANO_RESOURCE_BAD_MANIFEST;
    target->state = RenderBindingState::realizing;

    AnoResourceBytes artifact = {};
    AnoResourceError result = ano_resource_epoch_resolve(
        residency.source, asset, type, &artifact);
    AnoResourceDependency *dependencies = nullptr;
    uint64_t dependencyCount = 0;
    if (result == ANO_RESOURCE_OK)
        result = collect_dependencies(
            type, artifact, &dependencies, &dependencyCount);
    for (uint64_t i = 0; i < dependencyCount
                         && result == ANO_RESOURCE_OK; ++i)
        if (is_render_input(dependencies[i].type))
            result = realize_asset(residency, dependencies[i].asset,
                                   dependencies[i].type);
    if (result == ANO_RESOURCE_OK) {
        schema::RenderResourceContext context = {
            .residency = &residency,
            .asset = asset,
            .artifact = artifact,
        };
        result = invoke_render_transform(*target, context, type, artifact);
    }
    mi_free(dependencies);
    target->state = result == ANO_RESOURCE_OK
        ? RenderBindingState::resident : RenderBindingState::absent;
    return result;
}

bool reserve_scene(AnoRenderResidency& residency, uint32_t *slot)
{
    if (residency.sceneCount == residency.sceneCapacity) {
        const uint32_t capacity = residency.sceneCapacity == 0
            ? 4 : residency.sceneCapacity * 2;
        if (capacity < residency.sceneCapacity
            || capacity > SIZE_MAX / sizeof(RealizedScene))
            return false;
        void *grown = mi_realloc(
            residency.scenes,
            static_cast<size_t>(capacity) * sizeof(RealizedScene));
        if (grown == nullptr)
            return false;
        residency.scenes = static_cast<RealizedScene *>(grown);
        memset(residency.scenes + residency.sceneCapacity, 0,
               static_cast<size_t>(capacity - residency.sceneCapacity)
                   * sizeof(RealizedScene));
        residency.sceneCapacity = capacity;
    }
    *slot = residency.sceneCount++;
    return true;
}

enum class ReuseState : uint8_t {
    unknown,
    visiting,
    reusable,
    dirty,
};

AnoResourceError classify_reuse(
    AnoRenderResidency& candidate, const AnoRenderResidency& previous,
    AnoAssetId asset, const bool *directlyChanged, ReuseState *states)
{
    if (asset.value == 0 || asset.value > candidate.bindingCount)
        return ANO_RESOURCE_BAD_MANIFEST;
    const uint64_t index = asset.value - 1;
    if (states[index] == ReuseState::reusable
        || states[index] == ReuseState::dirty)
        return ANO_RESOURCE_OK;
    if (states[index] == ReuseState::visiting)
        return ANO_RESOURCE_BAD_MANIFEST;
    if (directlyChanged[index]
        || asset.value > previous.bindingCount
        || previous.bindings[index].state != RenderBindingState::resident
        || previous.bindings[index].sourceType.value
            != candidate.bindings[index].sourceType.value) {
        states[index] = ReuseState::dirty;
        return ANO_RESOURCE_OK;
    }

    states[index] = ReuseState::visiting;
    AnoResourceBytes artifact = {};
    AnoResourceError result = ano_resource_epoch_resolve(
        candidate.source, asset, candidate.bindings[index].sourceType,
        &artifact);
    AnoResourceDependency *dependencies = nullptr;
    uint64_t dependencyCount = 0;
    if (result == ANO_RESOURCE_OK)
        result = collect_dependencies(candidate.bindings[index].sourceType,
                                      artifact, &dependencies,
                                      &dependencyCount);
    for (uint64_t i = 0; i < dependencyCount
                         && result == ANO_RESOURCE_OK; ++i) {
        if (!is_render_input(dependencies[i].type))
            continue;
        result = classify_reuse(candidate, previous, dependencies[i].asset,
                                directlyChanged, states);
        if (result == ANO_RESOURCE_OK
            && states[dependencies[i].asset.value - 1]
                == ReuseState::dirty)
            states[index] = ReuseState::dirty;
    }
    mi_free(dependencies);
    if (result != ANO_RESOURCE_OK)
        return result;
    if (states[index] == ReuseState::visiting)
        states[index] = ReuseState::reusable;
    return ANO_RESOURCE_OK;
}

bool clone_scene_binding(AnoRenderResidency& candidate,
                         const AnoRenderResidency& previous,
                         RenderBinding& target,
                         const RenderBinding& source)
{
    if (source.scene.slot >= previous.sceneCount)
        return false;
    const RealizedScene& oldScene = previous.scenes[source.scene.slot];
    if (oldScene.renderableCount > SIZE_MAX / sizeof(AnoRenderableDesc)
        || oldScene.lightCount > SIZE_MAX / sizeof(schema::SceneLight))
        return false;

    RealizedScene copied = {
        .renderables = oldScene.renderableCount == 0 ? nullptr
            : static_cast<AnoRenderableDesc *>(mi_malloc(
                static_cast<size_t>(oldScene.renderableCount)
                * sizeof(AnoRenderableDesc))),
        .renderableCount = oldScene.renderableCount,
        .lights = oldScene.lightCount == 0 ? nullptr
            : static_cast<schema::SceneLight *>(mi_malloc(
                static_cast<size_t>(oldScene.lightCount)
                * sizeof(schema::SceneLight))),
        .lightCount = oldScene.lightCount,
    };
    if ((copied.renderableCount != 0 && copied.renderables == nullptr)
        || (copied.lightCount != 0 && copied.lights == nullptr)) {
        mi_free(copied.lights);
        mi_free(copied.renderables);
        return false;
    }
    if (copied.renderableCount != 0)
        memcpy(copied.renderables, oldScene.renderables,
               static_cast<size_t>(copied.renderableCount)
               * sizeof(AnoRenderableDesc));
    if (copied.lightCount != 0)
        memcpy(copied.lights, oldScene.lights,
               static_cast<size_t>(copied.lightCount)
               * sizeof(schema::SceneLight));

    uint32_t slot = 0;
    if (!reserve_scene(candidate, &slot)) {
        mi_free(copied.lights);
        mi_free(copied.renderables);
        return false;
    }
    candidate.scenes[slot] = copied;
    target = source;
    target.scene.slot = slot;
    return true;
}

AnoResourceError reuse_unchanged_bindings(
    AnoRenderResidency& candidate, const AnoRenderResidency *previous)
{
    if (previous == nullptr || candidate.bindingCount == 0)
        return ANO_RESOURCE_OK;
    if (candidate.bindingCount > SIZE_MAX / sizeof(bool)
        || candidate.bindingCount > SIZE_MAX / sizeof(ReuseState))
        return ANO_RESOURCE_OVERFLOW;
    bool *directlyChanged = static_cast<bool *>(mi_calloc(
        static_cast<size_t>(candidate.bindingCount), sizeof(bool)));
    ReuseState *states = static_cast<ReuseState *>(mi_calloc(
        static_cast<size_t>(candidate.bindingCount), sizeof(ReuseState)));
    if (directlyChanged == nullptr || states == nullptr) {
        mi_free(states);
        mi_free(directlyChanged);
        return ANO_RESOURCE_OUT_OF_MEMORY;
    }
    const uint64_t changedCount = ano_resource_epoch_changed_count(
        candidate.source);
    AnoResourceError result = ANO_RESOURCE_OK;
    for (uint64_t i = 0; i < changedCount && result == ANO_RESOURCE_OK; ++i) {
        AnoAssetId changed = {};
        result = ano_resource_epoch_changed(candidate.source, i, &changed);
        if (result == ANO_RESOURCE_OK
            && changed.value <= candidate.bindingCount)
            directlyChanged[changed.value - 1] = true;
    }
    for (uint64_t i = 0; i < candidate.bindingCount
                         && result == ANO_RESOURCE_OK; ++i)
        if (is_render_input(candidate.bindings[i].sourceType))
            result = classify_reuse(candidate, *previous, {i + 1},
                                    directlyChanged, states);

    const AnoResourceTypeId sceneType =
        ano::resource_type_id<schema::Scene>();
    for (uint64_t i = 0; i < candidate.bindingCount
                         && result == ANO_RESOURCE_OK; ++i) {
        if (states[i] != ReuseState::reusable)
            continue;
        RenderBinding& target = candidate.bindings[i];
        const RenderBinding& source = previous->bindings[i];
        if (target.sourceType.value == sceneType.value) {
            if (!clone_scene_binding(candidate, *previous, target, source))
                result = ANO_RESOURCE_OUT_OF_MEMORY;
        } else {
            target = source;
        }
    }
    mi_free(states);
    mi_free(directlyChanged);
    return result;
}

PbrFeatureFlags material_features(const schema::Material& material)
{
    PbrFeatureFlags result = PBR_FEATURE_NONE;
    uint32_t bit = 1;
    static constexpr auto features = std::define_static_array(
        std::meta::enumerators_of(^^schema::MaterialFeature));
    template for (constexpr std::meta::info feature : features) {
        if ((material.features & bit) != 0)
            result |= pbr_material_feature([:feature:]);
        bit <<= 1;
    }
    for (size_t i = 0; i < schema::materialTextureCount; ++i)
        if (material.textures[i].texture.id.value != 0) {
            static constexpr auto slots = std::define_static_array(
                std::meta::enumerators_of(^^schema::MaterialTextureSlot));
            size_t index = 0;
            template for (constexpr std::meta::info slot : slots) {
                if (i == index)
                    result |= pbr_texture_feature([:slot:]);
                ++index;
            }
        }
    if (material.emissiveFactor[0] > 0.0f
        || material.emissiveFactor[1] > 0.0f
        || material.emissiveFactor[2] > 0.0f)
        result |= PBR_FEATURE_EMISSIVE_FACTOR;
    result |= PBR_FEATURE_ALPHA_MODE_OPAQUE
        << ano::detail::enum_index(material.alphaMode);
    if (material.doubleSided)
        result |= PBR_FEATURE_DOUBLE_SIDED;
    return result;
}

} // namespace

namespace ano::asset_schema {

bool realize_texture(const Texture& texture, RenderResourceContext& context,
                     GpuTexture& output) noexcept
{
    if (texture.format != TextureFormat::rgba8 || texture.mipCount != 1
        || texture.width == 0 || texture.height == 0)
        return false;
    uint64_t pixelCount = 0;
    uint64_t requiredBytes = 0;
    if (!ano::detail::checked_multiply(texture.width, texture.height,
                                       &pixelCount)
        || !ano::detail::checked_multiply(pixelCount, 4, &requiredBytes)
        || requiredBytes != texture.bytes.count || requiredBytes > SIZE_MAX)
        return false;
    uint8_t *pixels = static_cast<uint8_t *>(
        mi_malloc(static_cast<size_t>(requiredBytes)));
    if (pixels == nullptr)
        return false;
    const ArtifactView<Texture> view = {
        .value = texture,
        .bytes = context.artifact,
    };
    const AnoResourceError resolved = resolve_span(
        view, texture.bytes, pixels, requiredBytes);
    if (resolved != ANO_RESOURCE_OK) {
        mi_free(pixels);
        return false;
    }
    AnoRenderResidency& residency = *context.residency;
    if (residency.textureCommands == VK_NULL_HANDLE)
        residency.textureCommands = beginSingleTimeCommands(&ctx);
    if (residency.textureCommands == VK_NULL_HANDLE) {
        mi_free(pixels);
        return false;
    }
    TexturePackage package = {};
    const TextureUsageFlags usage = static_cast<TextureUsageFlags>(texture.usage);
    const AnoTextureResult built = createTextureImageFromPixels(
        &ctx, residency.textureCommands, &package, pixels, texture.width,
        texture.height, usage, true);
    mi_free(pixels);
    if (built.code != ANO_TEXTURE_BUILT)
        return false;
    if (!append_texture_staging(residency, package.staging)) {
        destroy_texture_package(package);
        return false;
    }
    package.staging = VK_NULL_HANDLE;
    if (!ano_vk_register_texture(&rendererState.primitives,
                                 ano_texture_record(&package))) {
        destroy_texture_package(package);
        return false;
    }
    output.colorSlot = (usage & TEXTURE_USE_COLOR)
        ? bindless_register_texture(
            &ctx, &rendererState.bindlessTextures, package.srgbView,
            rendererState.textureSampler)
        : ANO_BINDLESS_NONE;
    output.dataSlot = (usage & TEXTURE_USE_DATA)
        ? bindless_register_texture(
            &ctx, &rendererState.bindlessTextures, package.unormView,
            rendererState.textureSampler)
        : ANO_BINDLESS_NONE;
    return (!(usage & TEXTURE_USE_COLOR)
            || output.colorSlot != ANO_BINDLESS_NONE)
        && (!(usage & TEXTURE_USE_DATA)
            || output.dataSlot != ANO_BINDLESS_NONE);
}

bool realize_material(const Material& material,
                      RenderResourceContext& context,
                      GpuMaterial& output) noexcept
{
    if (material.unlit
        || rendererState.materialBuffer.count
            >= rendererState.materialBuffer.capacity)
        return false;
    MaterialData destination;
    ano_vk_init_default_material_data(&destination);
    project_material_data(destination, material);
    PbrFeatureFlags required = material_features(material);

    static constexpr auto fields = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^MaterialData, std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info field : fields) {
        constexpr schema::MaterialTextureSlotMatch match =
            schema::material_texture_slot(field);
        if constexpr (match.found) {
            using Field = [:std::meta::type_of(field):];
            static_assert(std::is_same_v<Field, uint32_t>);
            const MaterialTexture& use = material.textures[
                ano::detail::enum_index(match.slot)];
            if (use.hasTransform || use.texCoord != 0)
                return false;
            uint32_t slot = ANO_BINDLESS_NONE;
            if (use.texture.id.value != 0) {
                RenderBinding *texture = binding(
                    *context.residency, use.texture);
                if (texture == nullptr
                    || texture->state != RenderBindingState::resident)
                    return false;
                constexpr TextureUsage usage =
                    schema::material_texture_usage(match.slot);
                if constexpr (usage == TextureUsage::color)
                    slot = texture->texture.colorSlot;
                else if constexpr (usage == TextureUsage::data)
                    slot = texture->texture.dataSlot;
                else
                    return false;
            }
            destination.[:field:] = slot;
        }
    }
    destination.normalScale = material.textures[
        ano::detail::enum_index(MaterialTextureSlot::normal)].scale;
    destination.occlusionStrength = material.textures[
        ano::detail::enum_index(MaterialTextureSlot::occlusion)].strength;

    PbrFeatureFlags unsupported = PBR_FEATURE_NONE;
    const PbrFeatureFlags available =
        ano_vk_get_active_pipelines_supported_features(&rendererState);
    if (!ano_vk_check_feature_compatibility(
            available, required, &unsupported))
        return false;
    destination.features = required;
    if (required & (PBR_FEATURE_TRANSMISSION | PBR_FEATURE_VOLUME))
        destination.pipelineType = PIPELINE_TRANSMISSION;
    else if (destination.emissiveStrength > 1.0f
             || material.alphaMode == MaterialAlphaMode::blend)
        destination.pipelineType = PIPELINE_ADDITIVE;
    else if (material.alphaMode == MaterialAlphaMode::mask)
        destination.pipelineType = PIPELINE_FLAT_MASKED;
    else if (material.doubleSided)
        destination.pipelineType = PIPELINE_FLAT_TWOSIDED;
    else
        destination.pipelineType = PIPELINE_FLAT;

    output.slot = rendererState.materialBuffer.count++;
    for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
        rendererState.materialBuffer.mapped[frame][output.slot] = destination;
    return true;
}

bool realize_mesh(const Mesh& mesh, RenderResourceContext& context,
                  GpuMesh& output) noexcept
{
    if (mesh.vertices.count == 0 || mesh.indices.count == 0
        || mesh.vertices.count > UINT32_MAX
        || mesh.indices.count > UINT32_MAX
        || mesh.vertices.count > SIZE_MAX / sizeof(schema::Vertex)
        || mesh.indices.count > SIZE_MAX / sizeof(uint32_t))
        return false;
    schema::Vertex *portable = static_cast<schema::Vertex *>(
        mi_malloc(static_cast<size_t>(mesh.vertices.count)
                  * sizeof(schema::Vertex)));
    uint32_t *indices = static_cast<uint32_t *>(
        mi_malloc(static_cast<size_t>(mesh.indices.count)
                  * sizeof(uint32_t)));
    if (portable == nullptr || indices == nullptr) {
        mi_free(indices);
        mi_free(portable);
        return false;
    }
    const ArtifactView<Mesh> view = {
        .value = mesh,
        .bytes = context.artifact,
    };
    bool valid = resolve_span(
        view, mesh.vertices, portable, mesh.vertices.count) == ANO_RESOURCE_OK
        && resolve_span(view, mesh.indices, indices, mesh.indices.count)
            == ANO_RESOURCE_OK;
    static_assert(sizeof(schema::Vertex) == sizeof(::Vertex));
    ::Vertex *vertices = reinterpret_cast<::Vertex *>(portable);
    uint32_t base = ANO_MESH_NONE;
    uint32_t produced = 0;
    if (valid) {
        const AnoLodConfig lod = ano_lod_config_default(
            ANO_DEFAULT_LOD_COUNT);
        geometry_pool_upload_chain(
            &rendererState.globalGeometryPool, &stagingAllocator, ctx.device,
            ctx.queueFamilyIndices.transferFamily, ctx.transferQueue,
            vertices, static_cast<uint32_t>(mesh.vertices.count), indices,
            static_cast<uint32_t>(mesh.indices.count), &lod, &base,
            &produced);
        valid = base != ANO_MESH_NONE && produced != 0;
    }
    mi_free(indices);
    mi_free(portable);
    RenderBinding *material = binding(*context.residency, mesh.material);
    if (!valid || material == nullptr
        || material->state != RenderBindingState::resident)
        return false;
    output.geometrySlot = base;
    output.materialSlot = material->material.slot;
    return true;
}

bool realize_scene(const Scene& scene, RenderResourceContext& context,
                   GpuScene& output) noexcept
{
    if (scene.renderables.count > SIZE_MAX / sizeof(SceneRenderable)
        || scene.lights.count > SIZE_MAX / sizeof(SceneLight))
        return false;
    SceneRenderable *portable = scene.renderables.count == 0 ? nullptr
        : static_cast<SceneRenderable *>(mi_malloc(
            static_cast<size_t>(scene.renderables.count)
            * sizeof(SceneRenderable)));
    SceneLight *lights = scene.lights.count == 0 ? nullptr
        : static_cast<SceneLight *>(mi_malloc(
            static_cast<size_t>(scene.lights.count) * sizeof(SceneLight)));
    AnoRenderableDesc *renderables = scene.renderables.count == 0 ? nullptr
        : static_cast<AnoRenderableDesc *>(mi_calloc(
            static_cast<size_t>(scene.renderables.count),
            sizeof(AnoRenderableDesc)));
    if ((scene.renderables.count != 0
         && (portable == nullptr || renderables == nullptr))
        || (scene.lights.count != 0 && lights == nullptr)) {
        mi_free(renderables);
        mi_free(lights);
        mi_free(portable);
        return false;
    }
    const ArtifactView<Scene> view = {
        .value = scene,
        .bytes = context.artifact,
    };
    bool valid = resolve_span(
        view, scene.renderables, portable, scene.renderables.count)
            == ANO_RESOURCE_OK
        && resolve_span(view, scene.lights, lights, scene.lights.count)
            == ANO_RESOURCE_OK;
    for (uint64_t i = 0; i < scene.renderables.count && valid; ++i) {
        RenderBinding *mesh = binding(
            *context.residency, portable[i].mesh);
        if (mesh == nullptr || mesh->state != RenderBindingState::resident) {
            valid = false;
            break;
        }
        memcpy(renderables[i].transform, portable[i].transform,
               sizeof(portable[i].transform));
        renderables[i].mesh_index = mesh->mesh.geometrySlot;
        renderables[i].material_index = mesh->mesh.materialSlot;
    }
    mi_free(portable);
    uint32_t slot = 0;
    if (!valid || !reserve_scene(*context.residency, &slot)) {
        mi_free(renderables);
        mi_free(lights);
        return false;
    }
    context.residency->scenes[slot] = {
        .renderables = renderables,
        .renderableCount = scene.renderables.count,
        .lights = lights,
        .lightCount = scene.lights.count,
    };
    output.slot = slot;
    return true;
}

} // namespace ano::asset_schema

AnoResourceError ano_vk_resource_residency_create_from_epoch(
    const AnoResidencyEpoch *epoch, const AnoRenderResidency *previous,
    AnoRenderResidency **residency)
{
    if (epoch == nullptr || residency == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *residency = nullptr;
    AnoRenderResidency *created = static_cast<AnoRenderResidency *>(
        mi_calloc(1, sizeof(AnoRenderResidency)));
    if (created == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    AnoResourceError result = ano_resource_epoch_retain(epoch);
    if (result == ANO_RESOURCE_OK)
        created->source = epoch;
    if (result == ANO_RESOURCE_OK)
        created->bindingCount = ano_resource_epoch_asset_count(created->source);
    if (result == ANO_RESOURCE_OK && created->bindingCount != 0) {
        if (created->bindingCount > SIZE_MAX / sizeof(RenderBinding))
            result = ANO_RESOURCE_OVERFLOW;
        else
            created->bindings = static_cast<RenderBinding *>(mi_calloc(
                static_cast<size_t>(created->bindingCount),
                sizeof(RenderBinding)));
        if (result == ANO_RESOURCE_OK && created->bindings == nullptr)
            result = ANO_RESOURCE_OUT_OF_MEMORY;
    }
    for (uint64_t i = 0; i < created->bindingCount
                         && result == ANO_RESOURCE_OK; ++i) {
        bool resident = false;
        result = ano_resource_epoch_asset(
            created->source, {i + 1}, &created->bindings[i].sourceType,
            &resident);
    }
    if (result == ANO_RESOURCE_OK)
        result = reuse_unchanged_bindings(*created, previous);
    for (uint64_t i = 0; i < created->bindingCount
                         && result == ANO_RESOURCE_OK; ++i) {
        bool resident = false;
        result = ano_resource_epoch_asset(
            created->source, {i + 1}, &created->bindings[i].sourceType,
            &resident);
        if (result == ANO_RESOURCE_OK && resident
            && is_render_input(created->bindings[i].sourceType))
            result = realize_asset(
                *created, {i + 1}, created->bindings[i].sourceType);
    }
    if (created->textureCommands != VK_NULL_HANDLE) {
        if (!endSingleTimeCommandsChecked(&ctx, created->textureCommands)
            && result == ANO_RESOURCE_OK)
            result = ANO_RESOURCE_OWNER_REJECTED;
        created->textureCommands = VK_NULL_HANDLE;
    }
    for (uint32_t i = 0; i < created->textureStagingCount; ++i)
        vkDestroyBuffer(ctx.device, created->textureStaging[i], nullptr);
    created->textureStagingCount = 0;
    gpu_alloc_reset(&stagingAllocator);
    if (result != ANO_RESOURCE_OK) {
        ano_vk_resource_residency_destroy(created);
        return result;
    }
    *residency = created;
    return ANO_RESOURCE_OK;
}

AnoResourceError ano_vk_resource_residency_create(
    AnoResourceManager *manager, AnoRenderResidency **residency)
{
    if (manager == nullptr || residency == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    const AnoResidencyEpoch *epoch = nullptr;
    AnoResourceError result = ano_resource_epoch_acquire(manager, &epoch);
    if (result == ANO_RESOURCE_OK)
        result = ano_vk_resource_residency_create_from_epoch(
            epoch, nullptr, residency);
    ano_resource_epoch_release(epoch);
    return result;
}

void ano_vk_resource_residency_destroy(AnoRenderResidency *residency)
{
    if (residency == nullptr)
        return;
    if (residency->textureCommands != VK_NULL_HANDLE)
        endSingleTimeCommands(&ctx, residency->textureCommands);
    for (uint32_t i = 0; i < residency->textureStagingCount; ++i)
        vkDestroyBuffer(ctx.device, residency->textureStaging[i], nullptr);
    for (uint32_t i = 0; i < residency->sceneCount; ++i) {
        mi_free(residency->scenes[i].lights);
        mi_free(residency->scenes[i].renderables);
    }
    mi_free(residency->textureStaging);
    mi_free(residency->scenes);
    mi_free(residency->bindings);
    ano_resource_epoch_release(residency->source);
    mi_free(residency);
}

uint32_t ano_vk_resource_scene_primitives(
    const AnoRenderResidency *residency, AnoAssetId asset, const mat4 root,
    AnoRenderableDesc *output, uint32_t capacity)
{
    if (residency == nullptr || asset.value == 0
        || asset.value > residency->bindingCount)
        return 0;
    const RenderBinding& binding = residency->bindings[asset.value - 1];
    if (binding.sourceType.value != ano::resource_type_id<schema::Scene>().value
        || binding.state != RenderBindingState::resident
        || binding.scene.slot >= residency->sceneCount)
        return 0;
    const RealizedScene& scene = residency->scenes[binding.scene.slot];
    const uint32_t count = scene.renderableCount > UINT32_MAX
        ? UINT32_MAX : static_cast<uint32_t>(scene.renderableCount);
    const uint32_t written = count < capacity ? count : capacity;
    for (uint32_t i = 0; output != nullptr && i < written; ++i)
        (void)ano_vk_resource_scene_primitive(
            residency, asset, i, root, &output[i]);
    return count;
}

bool ano_vk_resource_scene_primitive(
    const AnoRenderResidency *residency, AnoAssetId asset,
    uint32_t primitive, const mat4 root, AnoRenderableDesc *output)
{
    if (residency == nullptr || output == nullptr || asset.value == 0
        || asset.value > residency->bindingCount)
        return false;
    const RenderBinding& binding = residency->bindings[asset.value - 1];
    if (binding.sourceType.value != ano::resource_type_id<schema::Scene>().value
        || binding.state != RenderBindingState::resident
        || binding.scene.slot >= residency->sceneCount)
        return false;
    const RealizedScene& scene = residency->scenes[binding.scene.slot];
    if (primitive >= scene.renderableCount)
        return false;
    mat4 local;
    memcpy(local, scene.renderables[primitive].transform, sizeof(local));
    multiplyMat4(output->transform, root, local);
    output->mesh_index = scene.renderables[primitive].mesh_index;
    output->material_index = scene.renderables[primitive].material_index;
    output->resource_asset = asset;
    output->resource_primitive = primitive;
    return true;
}

uint32_t ano_vk_resource_scene_lights(
    const AnoRenderResidency *residency, AnoAssetId asset, const mat4 root,
    AnoSceneLightDesc *output, uint32_t capacity)
{
    if (residency == nullptr || asset.value == 0
        || asset.value > residency->bindingCount)
        return 0;
    const RenderBinding& binding = residency->bindings[asset.value - 1];
    if (binding.sourceType.value != ano::resource_type_id<schema::Scene>().value
        || binding.state != RenderBindingState::resident
        || binding.scene.slot >= residency->sceneCount)
        return 0;
    const RealizedScene& scene = residency->scenes[binding.scene.slot];
    const uint32_t count = scene.lightCount > UINT32_MAX
        ? UINT32_MAX : static_cast<uint32_t>(scene.lightCount);
    const uint32_t written = count < capacity ? count : capacity;
    for (uint32_t i = 0; output != nullptr && i < written; ++i) {
        const schema::SceneLight& source = scene.lights[i];
        mat4 local;
        memcpy(local, source.transform, sizeof(local));
        multiplyMat4(output[i].transform, root, local);
        memcpy(output[i].light.color, source.color,
               sizeof(output[i].light.color));
        output[i].light.intensity = source.intensity;
        output[i].light.range = source.range;
        output[i].light.innerConeCos = cosf(source.innerConeAngle);
        output[i].light.outerConeCos = cosf(source.outerConeAngle);
        output[i].light.type = static_cast<RenderLightType>(source.type);
        output[i].light.localDir[0] = 0.0f;
        output[i].light.localDir[1] = 0.0f;
        output[i].light.localDir[2] = 0.0f;
        output[i].light.castsShadow = source.castsShadow ? 1u : 0u;
    }
    return count;
}

uint32_t ano_vk_resource_default_material(
    const AnoRenderResidency *residency)
{
    if (residency == nullptr)
        return 0;
    for (uint32_t scene = 0; scene < residency->sceneCount; ++scene)
        if (residency->scenes[scene].renderableCount != 0)
            return residency->scenes[scene].renderables[0].material_index;
    return 0;
}
