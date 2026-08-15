/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include "resources.h"

#include <anoptic_compose.h>
#include <anoptic_memory.h>

using namespace ano;
#include <anoptic_render_resources.h>

#include "vulkan_backend/backend.h"
#include "vulkan_backend/bridge/bridge.h"
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

enum class BindingState : uint8_t {
    absent,
    visiting,
    prepared,
    realizing,
    resident,
};

struct RenderBinding final {
    AnoResourceTypeId sourceType;
    BindingState state;
    bool reuseGeometry;
    bool ownsReservations;
    bool affected;
    VkDeviceSize uploadOffset;
    AnoPreparedGeometry preparedGeometry;
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
    AnoAssetId *realizationPlan;
    uint64_t realizationCount;
    uint64_t realizationCursor;
    VkCommandBuffer uploadCommands;
    VkFence uploadFence;
    VkBuffer uploadStaging;
    GpuAllocation uploadAllocation;
    VkDeviceSize uploadBytes;
    bool realizationComplete;
    bool reusedReservationsRetained;
};

static void release_upload_storage(AnoRenderResidency *residency);

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
    }
    const auto arguments = std::meta::template_arguments_of(
        std::meta::dealias(std::meta::return_type_of(declaration)));
    result.output = arguments[0];
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

constexpr VkDeviceSize upload_alignment = 4;

bool reserve_upload_slice(AnoRenderResidency& residency, VkDeviceSize bytes,
                          VkDeviceSize *offset)
{
    const VkDeviceSize aligned = (residency.uploadBytes
        + upload_alignment - 1u) & ~(upload_alignment - 1u);
    if (aligned < residency.uploadBytes || bytes > UINT64_MAX - aligned)
        return false;
    *offset = aligned;
    residency.uploadBytes = aligned + bytes;
    return true;
}

template<class Output>
consteval size_t output_member_count()
{
    size_t count = 0;
    template for (constexpr std::meta::info member :
                  std::define_static_array(std::meta::nonstatic_data_members_of(
                      ^^RenderBinding, std::meta::access_context::unchecked())))
        if constexpr (std::meta::type_of(member) == ^^Output)
            ++count;
    return count;
}

static uint32_t reserve_material_slot()
{
    MaterialBuffer& buffer = rendererState.materialBuffer;
    if (buffer.freeCount != 0)
        return buffer.freeSlots[--buffer.freeCount];
    return buffer.count < buffer.capacity ? buffer.count++ : UINT32_MAX;
}

template<schema::RenderReservationKind Kind, class Output>
void retain_reservation(uint32_t slot, const Output&)
{
    if constexpr (Kind == schema::RenderReservationKind::texture)
        ano_vk_increment_texture_usage(&rendererState.primitives, slot);
    else if constexpr (Kind == schema::RenderReservationKind::material) {
        if (slot < rendererState.materialBuffer.capacity)
            ++rendererState.materialBuffer.references[slot];
    } else if constexpr (Kind == schema::RenderReservationKind::geometry)
        geometry_pool_retain_chain(&rendererState.globalGeometryPool, slot);
}

template<schema::RenderReservationKind Kind, class Output>
void release_reservation(uint32_t slot, const Output& output)
{
    if constexpr (Kind == schema::RenderReservationKind::texture) {
        RenderPrimitives& primitives = rendererState.primitives;
        if (slot >= primitives.textureCount) return;
        TextureData& texture = primitives.textureBuffers[slot];
        if (texture.usageCount == 0 || --texture.usageCount != 0) return;
        bindless_release_texture(&rendererState.bindlessTextures,
                                 output.colorSlot);
        bindless_release_texture(&rendererState.bindlessTextures,
                                 output.dataSlot);
        vkDestroyImageView(ctx.device, texture.unormView, nullptr);
        vkDestroyImageView(ctx.device, texture.srgbView, nullptr);
        vkDestroyImage(ctx.device, texture.textureImage, nullptr);
        gpu_free(&textureAllocator, texture.textureImageAlloc);
        texture = {};
        primitives.freeTextureSlots[primitives.freeTextureCount++] = slot;
    } else if constexpr (Kind == schema::RenderReservationKind::material) {
        MaterialBuffer& buffer = rendererState.materialBuffer;
        if (slot == 0 || slot >= buffer.capacity
            || buffer.references[slot] == 0 || --buffer.references[slot] != 0)
            return;
        buffer.freeSlots[buffer.freeCount++] = slot;
    } else if constexpr (Kind == schema::RenderReservationKind::geometry) {
        geometry_pool_release_chain(&rendererState.globalGeometryPool, slot);
    }
}

template<class Output, class Operation>
void visit_reservations(const Output& output, Operation operation)
{
    template for (constexpr std::meta::info member :
                  std::define_static_array(std::meta::nonstatic_data_members_of(
                      ^^Output, std::meta::access_context::unchecked()))) {
        static constexpr auto reservations = std::define_static_array(
            std::meta::annotations_of_with_type(member, ^^schema::RenderReservation));
        static_assert(reservations.size() <= 1);
        if constexpr (!reservations.empty()) {
            constexpr auto reservation =
                std::meta::extract<schema::RenderReservation>(reservations[0]);
            operation.template operator()<reservation.kind>(
                output.[:member:], output);
        }
    }
}

template<class Output>
void retain_output(const Output& output)
{
    visit_reservations(output, []<schema::RenderReservationKind Kind>(
        uint32_t slot, const Output& owner) {
        retain_reservation<Kind>(slot, owner);
    });
}

template<class Output>
void release_output(const Output& output)
{
    visit_reservations(output, []<schema::RenderReservationKind Kind>(
        uint32_t slot, const Output& owner) {
        release_reservation<Kind>(slot, owner);
    });
}

template<class Output>
void publish_output(RenderBinding& binding, const Output& output)
{
    static_assert(output_member_count<Output>() == 1,
                  "render output must have exactly one resident field");
    template for (constexpr std::meta::info member :
                  std::define_static_array(std::meta::nonstatic_data_members_of(
                      ^^RenderBinding, std::meta::access_context::unchecked())))
        if constexpr (std::meta::type_of(member) == ^^Output) {
            binding.[:member:] = output;
            retain_output(output);
            binding.ownsReservations = true;
        }
}

template<class Operation>
bool visit_render_route(AnoResourceTypeId type, Operation operation)
{
    static constexpr auto declarations = std::define_static_array(
        std::meta::members_of(
            ^^ano::asset_schema, std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info declaration : declarations) {
        static constexpr auto annotations = std::define_static_array(
            std::meta::annotations_of_with_type(declaration, ^^ano::Transform));
        if constexpr (!annotations.empty()) {
            constexpr auto transform = std::meta::extract<ano::Transform>(annotations[0]);
            if constexpr (transform.executor == ano::Executor::render_master) {
                constexpr TransformEndpoints endpoints = transform_endpoints(declaration);
                using Input = [:endpoints.input:];
                using Output = [:endpoints.output:];
                if (type.value == ano::resource_type_id<Input>().value) {
                    operation.template operator()<declaration, Input, Output>();
                    return true;
                }
            }
        }
    }
    return false;
}

template<class Operation>
void visit_binding_output(RenderBinding& binding, Operation operation)
{
    (void)visit_render_route(
        binding.sourceType,
        [&]<auto, class, class Output>() {
            template for (constexpr std::meta::info member :
                          std::define_static_array(
                              std::meta::nonstatic_data_members_of(
                                  ^^RenderBinding,
                                  std::meta::access_context::unchecked())))
                if constexpr (std::meta::type_of(member) == ^^Output)
                    operation(binding.[:member:]);
        });
}

void retain_binding(RenderBinding& binding)
{
    visit_binding_output(binding, []<class Output>(const Output& output) {
        retain_output(output);
    });
}

void release_binding(RenderBinding& binding)
{
    visit_binding_output(binding, []<class Output>(const Output& output) {
        release_output(output);
    });
}

bool is_render_input(AnoResourceTypeId type)
{
    return visit_render_route(type, []<auto, class, class>() {});
}

ResourceResult<> invoke_render_transform(
    RenderBinding& target, schema::RenderResourceContext& context,
    AnoResourceTypeId type)
{
    ResourceResult<> result = failure(ANO_RESOURCE_UNSUPPORTED);
    (void)visit_render_route(
        type, [&]<auto Declaration, class Input, class Output>() {
            const auto route = ano::compose(ano::decode<Input>)
                .and_then([&](const ano::ArtifactView<Input>& decoded) noexcept {
                    return [:Declaration:](decoded.value, context);
                })
                .transform([&](const Output& output) noexcept {
                    publish_output(target, output);
                });
            result = route(context.artifact);
        });
    return result;
}

template<class Input>
ResourceResult<> prepare_decoded(AnoRenderResidency&, RenderBinding&,
                                 const Input&, AnoResourceBytes,
                                 mi_heap_t*) noexcept
{
    return {};
}

template<>
ResourceResult<> prepare_decoded(
    AnoRenderResidency& residency, RenderBinding& binding,
    const schema::Texture& texture,
    AnoResourceBytes artifact, mi_heap_t*) noexcept
{
    const auto pixels = ano::checked_multiply(texture.width, texture.height);
    const auto bytes = pixels
        ? ano::checked_multiply(*pixels, UINT64_C(4))
        : ano::ArithmeticResult<uint64_t>(ano::failure(pixels.error()));
    if (texture.format != schema::TextureFormat::rgba8 || texture.mipCount != 1
        || texture.width == 0 || texture.height == 0
        || !bytes || *bytes != texture.bytes.count || *bytes > SIZE_MAX)
        return failure(ANO_RESOURCE_NON_CANONICAL);
    const ano::ArtifactView<schema::Texture> view = {
        .value = texture, .bytes = artifact};
    const auto pixelsView = ano::borrow_bytes(view, texture.bytes);
    if (!pixelsView)
        return failure(pixelsView.error());
    if (pixelsView->size != *bytes)
        return failure(ANO_RESOURCE_NON_CANONICAL);
    return result_if(
        reserve_upload_slice(
            residency, static_cast<VkDeviceSize>(*bytes),
            &binding.uploadOffset),
        ANO_RESOURCE_OVERFLOW);
}

template<>
ResourceResult<> prepare_decoded(
    AnoRenderResidency& residency, RenderBinding& binding,
    const schema::Mesh& mesh,
    AnoResourceBytes artifact, mi_heap_t* heap) noexcept
{
    if (binding.reuseGeometry) return {};
    if (mesh.vertices.count == 0 || mesh.indices.count == 0
        || mesh.vertices.count > UINT32_MAX || mesh.indices.count > UINT32_MAX
        || mesh.vertices.count > SIZE_MAX / sizeof(schema::Vertex)
        || mesh.indices.count > SIZE_MAX / sizeof(uint32_t))
        return failure(ANO_RESOURCE_NON_CANONICAL);
    schema::Vertex* portable = mi_heap_mallocn_tp(
        schema::Vertex, heap, static_cast<size_t>(mesh.vertices.count));
    uint32_t* indices = mi_heap_mallocn_tp(
        uint32_t, heap, static_cast<size_t>(mesh.indices.count));
    if (!portable || !indices)
        return failure(ANO_RESOURCE_OUT_OF_MEMORY);
    const ano::ArtifactView<schema::Mesh> view = {
        .value = mesh, .bytes = artifact};
    const auto resolved = ano::resolve_span(
        view, mesh.vertices, portable, mesh.vertices.count)
        .and_then([&] {
            return ano::resolve_span(
                view, mesh.indices, indices, mesh.indices.count);
        });
    if (!resolved) return failure(resolved.error());
    static_assert(sizeof(schema::Vertex) == sizeof(::Vertex));
    const AnoLodConfig lod = ano_lod_config_default(ANO_DEFAULT_LOD_COUNT);
    if (!geometry_prepare_chain(
        heap, reinterpret_cast<const ::Vertex*>(portable),
        static_cast<uint32_t>(mesh.vertices.count), indices,
        static_cast<uint32_t>(mesh.indices.count), &lod,
        &binding.preparedGeometry))
        return failure(ANO_RESOURCE_OWNER_REJECTED);
    return result_if(
        reserve_upload_slice(
            residency, binding.preparedGeometry.uploadBytes,
            &binding.uploadOffset),
        ANO_RESOURCE_OVERFLOW);
}

ResourceResult<> invoke_prepare_transform(
    AnoRenderResidency& residency, RenderBinding& target,
    AnoResourceTypeId type,
    AnoResourceBytes artifact, mi_heap_t* heap)
{
    ResourceResult<> result = failure(ANO_RESOURCE_UNSUPPORTED);
    (void)visit_render_route(
        type, [&]<auto, class Input, class>() {
            const auto route = ano::compose(ano::decode<Input>)
                .and_then([&](const ano::ArtifactView<Input>& decoded) noexcept {
                    return prepare_decoded(
                        residency, target, decoded.value,
                        decoded.bytes, heap);
                });
            result = route(artifact);
        });
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
        void *grown = mi_reallocn(
            residency.scenes, static_cast<size_t>(capacity),
            sizeof(RealizedScene));
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
            : mi_mallocn_tp(
                AnoRenderableDesc,
                static_cast<size_t>(oldScene.renderableCount)),
        .renderableCount = oldScene.renderableCount,
        .lights = oldScene.lightCount == 0 ? nullptr
            : mi_mallocn_tp(
                schema::SceneLight, static_cast<size_t>(oldScene.lightCount)),
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
    target.ownsReservations = false;
    target.affected = false;
    target.preparedGeometry = {};
    target.uploadOffset = 0;
    target.scene.slot = slot;
    return true;
}

bool append_realization(AnoRenderResidency& residency, AnoAssetId asset)
{
    if (residency.realizationCount >= residency.bindingCount)
        return false;
    residency.realizationPlan[residency.realizationCount++] = asset;
    return true;
}

AnoResourceError plan_asset(
    AnoRenderResidency& candidate, const AnoRenderResidency *previous,
    AnoAssetId asset, AnoResourceTypeId type, mi_heap_t *heap)
{
    RenderBinding *target = binding(candidate, asset, type);
    if (!target) return ANO_RESOURCE_TYPE_MISMATCH;
    if (target->state == BindingState::resident
        || target->state == BindingState::prepared)
        return ANO_RESOURCE_OK;
    if (target->state == BindingState::visiting)
        return ANO_RESOURCE_BAD_MANIFEST;
    target->state = BindingState::visiting;

    const uint64_t index = asset.value - 1;
    const bool sourceChanged = target->affected
        || previous == nullptr || asset.value > previous->bindingCount
        || previous->bindings[index].state != BindingState::resident
        || previous->bindings[index].sourceType.value != type.value;
    const auto artifact = resource_epoch_resolve(
        candidate.source, asset, type);
    auto dependencyResult = artifact
        ? resource_epoch_dependencies(candidate.source, asset)
        : ResourceResult<std::span<const AnoResourceDependency>>(
              failure(artifact.error()));
    AnoResourceError result = dependencyResult
        ? ANO_RESOURCE_OK : dependencyResult.error();
    const auto dependencies = dependencyResult.value_or(
        std::span<const AnoResourceDependency>{});
    bool dependencyChanged = false;
    for (uint64_t i = 0; i < dependencies.size()
                         && result == ANO_RESOURCE_OK; ++i) {
        if (!is_render_input(dependencies[i].type)) continue;
        result = plan_asset(
            candidate, previous, dependencies[i].asset,
            dependencies[i].type, heap);
        if (result == ANO_RESOURCE_OK) {
            const uint64_t dependencyIndex =
                dependencies[i].asset.value - 1;
            if (dependencyIndex >= candidate.bindingCount)
                result = ANO_RESOURCE_BAD_MANIFEST;
            else
                dependencyChanged |=
                    candidate.bindings[dependencyIndex].affected;
        }
    }
    if (result != ANO_RESOURCE_OK) {
        target->state = BindingState::absent;
        return result;
    }
    target->affected = sourceChanged || dependencyChanged;
    if (!target->affected) {
        const RenderBinding& source = previous->bindings[index];
        if (type.value == ano::resource_type_id<schema::Scene>().value) {
            if (!clone_scene_binding(candidate, *previous, *target, source))
                result = ANO_RESOURCE_OUT_OF_MEMORY;
        } else {
            *target = source;
            target->ownsReservations = false;
            target->affected = false;
            target->preparedGeometry = {};
            target->uploadOffset = 0;
        }
        if (result != ANO_RESOURCE_OK)
            target->state = BindingState::absent;
        return result;
    }

    if (!sourceChanged
        && type.value == ano::resource_type_id<schema::Mesh>().value) {
        target->mesh.geometrySlot = previous->bindings[index].mesh.geometrySlot;
        target->reuseGeometry = true;
    }
    const auto preparation = invoke_prepare_transform(
        candidate, *target, type, *artifact, heap);
    result = preparation ? ANO_RESOURCE_OK : preparation.error();
    if (result == ANO_RESOURCE_OK && !append_realization(candidate, asset))
        result = ANO_RESOURCE_OVERFLOW;
    target->state = result == ANO_RESOURCE_OK
        ? BindingState::prepared : BindingState::absent;
    return result;
}

AnoResourceError plan_resident_assets(
    AnoRenderResidency& candidate, const AnoRenderResidency *previous,
    mi_heap_t *heap)
{
    const uint64_t changedCount = resource_epoch_changed_count(
        candidate.source);
    AnoResourceError result = ANO_RESOURCE_OK;
    for (uint64_t i = 0; i < changedCount && result == ANO_RESOURCE_OK; ++i) {
        const auto changed = resource_epoch_changed(candidate.source, i);
        result = changed ? ANO_RESOURCE_OK : changed.error();
        if (changed && changed->value != 0
            && changed->value <= candidate.bindingCount)
            candidate.bindings[changed->value - 1].affected = true;
    }
    for (uint64_t i = 0; i < candidate.bindingCount
                         && result == ANO_RESOURCE_OK; ++i) {
        const auto asset = resource_epoch_asset(
            candidate.source, {i + 1});
        result = asset ? ANO_RESOURCE_OK : asset.error();
        if (asset)
            candidate.bindings[i].sourceType = asset->type;
        if (asset && asset->resident
            && is_render_input(candidate.bindings[i].sourceType))
            result = plan_asset(
                candidate, previous, {i + 1},
                candidate.bindings[i].sourceType, heap);
    }
    return result;
}

ResourceResult<> realize_planned_asset(
    AnoRenderResidency& residency, AnoAssetId asset)
{
    if (asset.value == 0 || asset.value > residency.bindingCount)
        return failure(ANO_RESOURCE_BAD_MANIFEST);
    RenderBinding& target = residency.bindings[asset.value - 1];
    if (target.state != BindingState::prepared)
        return target.state == BindingState::resident
            ? ResourceResult<>{} : failure(ANO_RESOURCE_BAD_MANIFEST);
    target.state = BindingState::realizing;
    auto result = resource_epoch_resolve(
        residency.source, asset, target.sourceType)
        .and_then([&](AnoResourceBytes artifact) {
            schema::RenderResourceContext context = {
                .residency = &residency,
                .asset = asset,
                .artifact = artifact,
            };
            return invoke_render_transform(
                target, context, target.sourceType);
        });
    target.preparedGeometry = {};
    target.uploadOffset = 0;
    target.state = result
        ? BindingState::resident : BindingState::absent;
    return result;
}

PbrFeatureFlags material_features(const schema::Material& material)
{
    PbrFeatureFlags result = PBR_FEATURE_NONE;
    static constexpr auto features = std::define_static_array(
        std::meta::enumerators_of(^^schema::MaterialFeature));
    static_assert(features.size() <= 32);
    template for (constexpr std::meta::info feature : features) {
        constexpr uint32_t bit = 1u
            << ano::detail::enum_index([:feature:]);
        if ((material.features & bit) != 0)
            result |= pbr_material_feature([:feature:]);
    }
    static constexpr auto slots = std::define_static_array(
        std::meta::enumerators_of(^^schema::MaterialTextureSlot));
    template for (constexpr std::meta::info slot : slots) {
        constexpr size_t index = ano::detail::enum_index([:slot:]);
        if (material.textures[index].texture.id.value != 0)
            result |= pbr_texture_feature([:slot:]);
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

ResourceResult<GpuTexture> realize_texture(
    const Texture& texture, RenderResourceContext& context) noexcept
{
    if (texture.format != TextureFormat::rgba8 || texture.mipCount != 1
        || texture.width == 0 || texture.height == 0)
        return failure(ANO_RESOURCE_NON_CANONICAL);
    const auto pixelCount = ano::checked_multiply(
        texture.width, texture.height);
    const auto requiredBytes = pixelCount
        ? ano::checked_multiply(*pixelCount, UINT64_C(4))
        : ArithmeticResult<uint64_t>(failure(pixelCount.error()));
    if (!requiredBytes || *requiredBytes != texture.bytes.count
        || *requiredBytes > SIZE_MAX)
        return failure(ANO_RESOURCE_NON_CANONICAL);
    RenderBinding* target = binding(
        *context.residency, context.asset, ano::resource_type_id<Texture>());
    if (!target)
        return failure(ANO_RESOURCE_BAD_MANIFEST);
    AnoRenderResidency& residency = *context.residency;
    if (residency.uploadCommands == VK_NULL_HANDLE
        || residency.uploadStaging == VK_NULL_HANDLE
        || !residency.uploadAllocation.mapped)
        return failure(ANO_RESOURCE_OWNER_REJECTED);
    const ArtifactView<Texture> view = {
        .value = texture,
        .bytes = context.artifact,
    };
    const auto pixels = borrow_bytes(view, texture.bytes);
    if (!pixels)
        return failure(pixels.error());
    if (pixels->size != *requiredBytes)
        return failure(ANO_RESOURCE_NON_CANONICAL);
    memcpy(static_cast<uint8_t*>(residency.uploadAllocation.mapped)
               + static_cast<size_t>(target->uploadOffset),
           pixels->data, static_cast<size_t>(*requiredBytes));
    TexturePackage package = {};
    const TextureUsageFlags usage = static_cast<TextureUsageFlags>(texture.usage);
    const AnoTextureResult built = createTextureImageFromStaging(
        &ctx, residency.uploadCommands, &package,
        residency.uploadStaging, target->uploadOffset, texture.width,
        texture.height, usage);
    if (built != ANO_TEXTURE_BUILT)
        return failure(ANO_RESOURCE_OWNER_REJECTED);
    GpuTexture output{};
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
    if (((usage & TEXTURE_USE_COLOR) && output.colorSlot == ANO_BINDLESS_NONE)
        || ((usage & TEXTURE_USE_DATA) && output.dataSlot == ANO_BINDLESS_NONE)) {
        bindless_release_texture(&rendererState.bindlessTextures,
                                 output.colorSlot);
        bindless_release_texture(&rendererState.bindlessTextures,
                                 output.dataSlot);
        ::release_upload_storage(context.residency);
        destroyTexturePackage(&ctx, &package);
        return failure(ANO_RESOURCE_OWNER_REJECTED);
    }
    output.ownerSlot = ano_vk_register_texture(
        &rendererState.primitives, ano_texture_record(&package));
    if (output.ownerSlot == UINT32_MAX) {
        bindless_release_texture(&rendererState.bindlessTextures,
                                 output.colorSlot);
        bindless_release_texture(&rendererState.bindlessTextures,
                                 output.dataSlot);
        ::release_upload_storage(context.residency);
        destroyTexturePackage(&ctx, &package);
        return failure(ANO_RESOURCE_OWNER_REJECTED);
    }
    return output;
}

ResourceResult<GpuMaterial> realize_material(
    const Material& material, RenderResourceContext& context) noexcept
{
    if (material.unlit)
        return failure(ANO_RESOURCE_UNSUPPORTED);
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
                return failure(ANO_RESOURCE_UNSUPPORTED);
            uint32_t slot = ANO_BINDLESS_NONE;
            if (use.texture.id.value != 0) {
                RenderBinding *texture = binding(
                    *context.residency, use.texture);
                if (texture == nullptr
                    || texture->state != BindingState::resident)
                    return failure(ANO_RESOURCE_BAD_MANIFEST);
                constexpr TextureUsage usage =
                    schema::material_texture_usage(match.slot);
                if constexpr (usage == TextureUsage::color)
                    slot = texture->texture.colorSlot;
                else if constexpr (usage == TextureUsage::data)
                    slot = texture->texture.dataSlot;
                else
                    return failure(ANO_RESOURCE_UNSUPPORTED);
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
        return failure(ANO_RESOURCE_UNSUPPORTED);
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

    GpuMaterial output{.slot = reserve_material_slot()};
    if (output.slot == UINT32_MAX)
        return failure(ANO_RESOURCE_OWNER_REJECTED);
    for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
        rendererState.materialBuffer.mapped[frame][output.slot] = destination;
    return output;
}

ResourceResult<GpuMesh> realize_mesh(
    const Mesh& mesh, RenderResourceContext& context) noexcept
{
    if (mesh.vertices.count == 0 || mesh.indices.count == 0
        || mesh.vertices.count > UINT32_MAX
        || mesh.indices.count > UINT32_MAX
        || mesh.vertices.count > SIZE_MAX / sizeof(schema::Vertex)
        || mesh.indices.count > SIZE_MAX / sizeof(uint32_t))
        return failure(ANO_RESOURCE_NON_CANONICAL);
    RenderBinding *material = binding(*context.residency, mesh.material);
    RenderBinding *target = binding(
        *context.residency, context.asset, ano::resource_type_id<Mesh>());
    if (material == nullptr
        || material->state != BindingState::resident)
        return failure(ANO_RESOURCE_BAD_MANIFEST);
    GpuMesh output{};
    if (target != nullptr && target->reuseGeometry) {
        output.geometrySlot = target->mesh.geometrySlot;
        output.materialSlot = material->material.slot;
        return output;
    }

    if (target == nullptr || target->preparedGeometry.lodCount == 0)
        return failure(ANO_RESOURCE_OWNER_REJECTED);
    AnoRenderResidency& residency = *context.residency;
    if (residency.uploadCommands == VK_NULL_HANDLE
        || residency.uploadStaging == VK_NULL_HANDLE
        || !residency.uploadAllocation.mapped)
        return failure(ANO_RESOURCE_OWNER_REJECTED);
    const uint32_t base = geometry_pool_record_prepared_chain(
        &rendererState.globalGeometryPool, &target->preparedGeometry,
        residency.uploadCommands, residency.uploadStaging,
        residency.uploadAllocation.mapped, target->uploadOffset);
    if (base == ANO_MESH_NONE)
        return failure(ANO_RESOURCE_OWNER_REJECTED);
    output.geometrySlot = base;
    output.materialSlot = material->material.slot;
    return output;
}

ResourceResult<GpuScene> realize_scene(
    const Scene& scene, RenderResourceContext& context) noexcept
{
    if (scene.renderables.count > SIZE_MAX / sizeof(SceneRenderable)
        || scene.lights.count > SIZE_MAX / sizeof(SceneLight))
        return failure(ANO_RESOURCE_NON_CANONICAL);
    SceneLight *lights = scene.lights.count == 0 ? nullptr
        : mi_mallocn_tp(SceneLight,
                        static_cast<size_t>(scene.lights.count));
    AnoRenderableDesc *renderables = scene.renderables.count == 0 ? nullptr
        : mi_calloc_tp(AnoRenderableDesc,
                       static_cast<size_t>(scene.renderables.count));
    if ((scene.renderables.count != 0
         && renderables == nullptr)
        || (scene.lights.count != 0 && lights == nullptr)) {
        mi_free(renderables);
        mi_free(lights);
        return failure(ANO_RESOURCE_OUT_OF_MEMORY);
    }
    const ArtifactView<Scene> view = {
        .value = scene,
        .bytes = context.artifact,
    };
    auto decoded = resolve_span(
        view, scene.lights, lights, scene.lights.count);
    auto project = [&](const SceneRenderable& portable,
                       uint64_t index) -> ResourceResult<> {
        RenderBinding *mesh = binding(
            *context.residency, portable.mesh);
        if (mesh == nullptr || mesh->state != BindingState::resident)
            return failure(ANO_RESOURCE_BAD_MANIFEST);
        memcpy(renderables[index].transform, portable.transform,
               sizeof(portable.transform));
        renderables[index].mesh_index = mesh->mesh.geometrySlot;
        renderables[index].material_index = mesh->mesh.materialSlot;
        return {};
    };
    if (decoded)
        decoded = visit_span(view, scene.renderables, project);
    uint32_t slot = 0;
    if (!decoded
        || !reserve_scene(*context.residency, &slot)) {
        mi_free(renderables);
        mi_free(lights);
        return decoded ? failure(ANO_RESOURCE_OUT_OF_MEMORY)
                       : ResourceResult<GpuScene>(failure(decoded.error()));
    }
    context.residency->scenes[slot] = {
        .renderables = renderables,
        .renderableCount = scene.renderables.count,
        .lights = lights,
        .lightCount = scene.lights.count,
    };
    return GpuScene{.slot = slot};
}

} // namespace ano::asset_schema

static void release_upload_storage(AnoRenderResidency *residency)
{
    if (residency->uploadCommands != VK_NULL_HANDLE)
        vkFreeCommandBuffers(ctx.device, rendererState.commandPool, 1,
                             &residency->uploadCommands);
    if (residency->uploadFence != VK_NULL_HANDLE)
        vkDestroyFence(ctx.device, residency->uploadFence, nullptr);
    vkDestroyBuffer(ctx.device, residency->uploadStaging, nullptr);
    gpu_free(&stagingAllocator, residency->uploadAllocation);
    residency->uploadCommands = VK_NULL_HANDLE;
    residency->uploadFence = VK_NULL_HANDLE;
    residency->uploadStaging = VK_NULL_HANDLE;
    residency->uploadAllocation = {};
}

static AnoResourceError begin_upload(AnoRenderResidency *residency)
{
    if (residency->uploadBytes == 0
        || residency->uploadStaging != VK_NULL_HANDLE)
        return ANO_RESOURCE_OK;
    if (!createDataBuffer(
            &ctx, &stagingAllocator, residency->uploadBytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &residency->uploadStaging,
            &residency->uploadAllocation)
        || !residency->uploadAllocation.mapped) {
        release_upload_storage(residency);
        return ANO_RESOURCE_OWNER_REJECTED;
    }
    residency->uploadCommands = beginSingleTimeCommands(&ctx);
    if (residency->uploadCommands == VK_NULL_HANDLE) {
        release_upload_storage(residency);
        return ANO_RESOURCE_OWNER_REJECTED;
    }
    return ANO_RESOURCE_OK;
}

static AnoResourceError submit_upload(AnoRenderResidency *residency)
{
    if (residency->uploadCommands == VK_NULL_HANDLE)
        return ANO_RESOURCE_OK;
    const VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    const VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &residency->uploadCommands,
    };
    if (vkEndCommandBuffer(residency->uploadCommands) != VK_SUCCESS
        || vkCreateFence(ctx.device, &fenceInfo, nullptr,
                         &residency->uploadFence) != VK_SUCCESS
        || vkQueueSubmit(ctx.graphicsQueue, 1, &submitInfo,
                         residency->uploadFence) != VK_SUCCESS) {
        release_upload_storage(residency);
        return ANO_RESOURCE_OWNER_REJECTED;
    }
    return ANO_RESOURCE_OK;
}

AnoResourceError ano_vk_resource_residency_prepare_from_epoch(
    const AnoResidencyEpoch *epoch, const AnoRenderResidency *previous,
    mi_heap_t *preparationHeap, AnoRenderResidency **residency)
{
    if (epoch == nullptr || preparationHeap == nullptr || residency == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *residency = nullptr;
    AnoRenderResidency *created = mi_zalloc_tp(AnoRenderResidency);
    if (created == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    const auto retained = resource_epoch_retain(epoch);
    AnoResourceError result = retained ? ANO_RESOURCE_OK : retained.error();
    if (retained)
        created->source = epoch;
    if (result == ANO_RESOURCE_OK)
        created->bindingCount = resource_epoch_asset_count(created->source);
    if (result == ANO_RESOURCE_OK && created->bindingCount != 0) {
        if (created->bindingCount > SIZE_MAX / sizeof(RenderBinding)
            || created->bindingCount > SIZE_MAX / sizeof(AnoAssetId))
            result = ANO_RESOURCE_OVERFLOW;
        else {
            created->bindings = mi_calloc_tp(
                RenderBinding, static_cast<size_t>(created->bindingCount));
            created->realizationPlan = mi_heap_mallocn_tp(
                AnoAssetId, preparationHeap,
                static_cast<size_t>(created->bindingCount));
        }
        if (result == ANO_RESOURCE_OK
            && (!created->bindings || !created->realizationPlan))
            result = ANO_RESOURCE_OUT_OF_MEMORY;
    }
    for (uint64_t i = 0; i < created->bindingCount
                         && result == ANO_RESOURCE_OK; ++i) {
        const auto asset = resource_epoch_asset(
            created->source, {i + 1});
        result = asset ? ANO_RESOURCE_OK : asset.error();
        if (asset)
            created->bindings[i].sourceType = asset->type;
    }
    if (result == ANO_RESOURCE_OK)
        result = plan_resident_assets(
            *created, previous, preparationHeap);
    if (result != ANO_RESOURCE_OK) {
        ano_vk_resource_residency_destroy(created);
        return result;
    }
    *residency = created;
    return ANO_RESOURCE_OK;
}

AnoResourceError ano_vk_resource_residency_realize_step(
    AnoRenderResidency *residency, uint32_t assetBudget, bool *complete)
{
    if (!residency || !complete || assetBudget == 0)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *complete = residency->realizationComplete;
    if (*complete) return ANO_RESOURCE_OK;
    if (!residency->reusedReservationsRetained) {
        for (uint64_t i = 0; i < residency->bindingCount; ++i)
            if (residency->bindings[i].state == BindingState::resident
                && !residency->bindings[i].ownsReservations) {
                retain_binding(residency->bindings[i]);
                residency->bindings[i].ownsReservations = true;
            }
        residency->reusedReservationsRetained = true;
    }
    AnoResourceError result = begin_upload(residency);
    if (result != ANO_RESOURCE_OK) return result;
    uint32_t realized = 0;
    while (residency->realizationCursor < residency->realizationCount
           && realized < assetBudget && result == ANO_RESOURCE_OK) {
        const AnoAssetId asset = residency->realizationPlan[
            residency->realizationCursor++];
        const auto realization = realize_planned_asset(*residency, asset);
        result = realization ? ANO_RESOURCE_OK : realization.error();
        ++realized;
    }
    if (result != ANO_RESOURCE_OK) return result;
    if (residency->realizationCursor != residency->realizationCount)
        return ANO_RESOURCE_OK;
    if (!render_resource_epoch_compatible(&rendererState, residency))
        return ANO_RESOURCE_OWNER_REJECTED;
    result = submit_upload(residency);
    if (result == ANO_RESOURCE_OK) {
        residency->realizationComplete = true;
        residency->realizationPlan = nullptr;
        *complete = true;
    }
    return result;
}

VkResult ano_vk_resource_residency_poll_upload(
    AnoRenderResidency *residency)
{
    if (residency == nullptr)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (residency->uploadCommands == VK_NULL_HANDLE)
        return VK_SUCCESS;
    if (residency->uploadFence == VK_NULL_HANDLE)
        return VK_ERROR_INITIALIZATION_FAILED;
    const VkResult status = vkGetFenceStatus(ctx.device, residency->uploadFence);
    if (status == VK_SUCCESS)
        release_upload_storage(residency);
    return status;
}

AnoResourceError ano_vk_resource_residency_wait_upload(
    AnoRenderResidency *residency)
{
    if (residency == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    if (residency->uploadCommands == VK_NULL_HANDLE)
        return ANO_RESOURCE_OK;
    if (residency->uploadFence == VK_NULL_HANDLE)
        return ANO_RESOURCE_OWNER_REJECTED;
    if (vkWaitForFences(ctx.device, 1, &residency->uploadFence,
                        VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return ANO_RESOURCE_OWNER_REJECTED;
    release_upload_storage(residency);
    return ANO_RESOURCE_OK;
}

AnoResourceError ano_vk_resource_residency_create(
    AnoResourceManager *manager, AnoRenderResidency **residency)
{
    if (manager == nullptr || residency == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    mi_heap_t *preparationHeap = heap_create();
    if (preparationHeap == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    const auto acquired = resource_epoch_acquire(manager);
    const AnoResidencyEpoch *epoch = acquired.value_or(nullptr);
    AnoResourceError result = acquired ? ANO_RESOURCE_OK : acquired.error();
    if (acquired)
        result = ano_vk_resource_residency_prepare_from_epoch(
            epoch, nullptr, preparationHeap, residency);
    bool complete = false;
    while (result == ANO_RESOURCE_OK && !complete)
        result = ano_vk_resource_residency_realize_step(
            *residency, UINT32_MAX, &complete);
    if (result == ANO_RESOURCE_OK)
        result = ano_vk_resource_residency_wait_upload(*residency);
    if (result != ANO_RESOURCE_OK) {
        ano_vk_resource_residency_destroy(*residency);
        *residency = nullptr;
    }
    resource_epoch_release(epoch);
    heap_destroy(preparationHeap);
    return result;
}

void ano_vk_resource_residency_destroy(AnoRenderResidency *residency)
{
    if (residency == nullptr)
        return;
    if (residency->uploadFence != VK_NULL_HANDLE) {
        (void)ano_vk_resource_residency_wait_upload(residency);
        if (residency->uploadFence != VK_NULL_HANDLE)
            release_upload_storage(residency);
    } else if (residency->uploadCommands != VK_NULL_HANDLE
               || residency->uploadStaging != VK_NULL_HANDLE) {
        release_upload_storage(residency);
    }
    for (uint32_t i = 0; i < residency->sceneCount; ++i) {
        mi_free(residency->scenes[i].lights);
        mi_free(residency->scenes[i].renderables);
    }
    for (uint64_t i = 0; i < residency->bindingCount; ++i)
        if (residency->bindings[i].state == BindingState::resident
            && residency->bindings[i].ownsReservations)
            release_binding(residency->bindings[i]);
    mi_free(residency->scenes);
    mi_free(residency->bindings);
    resource_epoch_release(residency->source);
    mi_free(residency);
}

bool ano_vk_resource_scene_affected(
    const AnoRenderResidency *residency, AnoAssetId asset)
{
    if (!residency || asset.value == 0
        || asset.value > residency->bindingCount)
        return true;
    const RenderBinding& binding = residency->bindings[asset.value - 1];
    return binding.sourceType.value
            != ano::resource_type_id<schema::Scene>().value
        || binding.state != BindingState::resident
        || binding.scene.slot >= residency->sceneCount
        || binding.affected;
}

static RenderResult<const RealizedScene *> resource_scene(
    const AnoRenderResidency *residency, AnoAssetId asset)
{
    if (residency == nullptr)
        return failure(RenderError::unavailable);
    if (asset.value == 0 || asset.value > residency->bindingCount)
        return failure(RenderError::invalid_argument);
    const RenderBinding& binding = residency->bindings[asset.value - 1];
    if (binding.sourceType.value != ano::resource_type_id<schema::Scene>().value)
        return failure(RenderError::invalid_argument);
    if (binding.state != BindingState::resident
        || binding.scene.slot >= residency->sceneCount)
        return failure(RenderError::unavailable);
    return &residency->scenes[binding.scene.slot];
}

static AnoRenderableDesc instance_desc(
    const AnoRenderableDesc& source, AnoAssetId asset,
    uint32_t primitive, const mat4 root)
{
    AnoRenderableDesc output = source;
    mat4 local;
    memcpy(local, source.transform, sizeof(local));
    multiplyMat4(output.transform, root, local);
    output.resource_asset = asset;
    output.resource_primitive = primitive;
    return output;
}

RenderResult<uint32_t> ano_vk_resource_scene_primitives(
    const AnoRenderResidency *residency, AnoAssetId asset, const mat4 root,
    AnoRenderableDesc *output, uint32_t capacity)
{
    const auto found = resource_scene(residency, asset);
    if (!found)
        return failure(found.error());
    const RealizedScene& scene = **found;
    const uint32_t count = scene.renderableCount > UINT32_MAX
        ? UINT32_MAX : static_cast<uint32_t>(scene.renderableCount);
    const uint32_t written = count < capacity ? count : capacity;
    for (uint32_t i = 0; output != nullptr && i < written; ++i)
        output[i] = instance_desc(scene.renderables[i], asset, i, root);
    return count;
}

RenderResult<AnoRenderableDesc> ano_vk_resource_scene_primitive(
    const AnoRenderResidency *residency, AnoAssetId asset,
    uint32_t primitive, const mat4 root)
{
    const auto found = resource_scene(residency, asset);
    if (!found)
        return failure(found.error());
    const RealizedScene& scene = **found;
    if (primitive >= scene.renderableCount)
        return failure(RenderError::invalid_argument);
    return instance_desc(scene.renderables[primitive], asset, primitive, root);
}

RenderResult<uint32_t> ano_vk_resource_scene_lights(
    const AnoRenderResidency *residency, AnoAssetId asset, const mat4 root,
    AnoSceneLightDesc *output, uint32_t capacity)
{
    const auto found = resource_scene(residency, asset);
    if (!found)
        return failure(found.error());
    const RealizedScene& scene = **found;
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
