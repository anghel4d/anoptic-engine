/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include "resources.h"

#include <anoptic_memory.h>
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

AnoResourceError invoke_render_transform(
    RenderBinding& target, schema::RenderResourceContext& context,
    AnoResourceTypeId type, AnoResourceBytes artifact)
{
    AnoResourceError result = ANO_RESOURCE_UNSUPPORTED;
    (void)visit_render_route(
        type, [&]<auto Declaration, class Input, class Output>() {
            const ano::DecodeResult<Input> decoded =
                ano::decode<Input>(artifact);
            if (decoded.error != ANO_RESOURCE_OK) {
                result = decoded.error;
                return;
            }
            Output output = {};
            context.artifact = artifact;
            if (![:Declaration:](decoded.view.value, context, output)) {
                result = ANO_RESOURCE_OWNER_REJECTED;
                return;
            }
            publish_output(target, output);
            result = ANO_RESOURCE_OK;
        });
    return result;
}

template<class Input>
AnoResourceError prepare_decoded(AnoRenderResidency&, RenderBinding&,
                                 const Input&,
                                 AnoResourceBytes, mi_heap_t*)
{
    return ANO_RESOURCE_OK;
}

template<>
AnoResourceError prepare_decoded(
    AnoRenderResidency& residency, RenderBinding& binding,
    const schema::Texture& texture,
    AnoResourceBytes artifact, mi_heap_t*)
{
    uint64_t pixels = 0;
    uint64_t bytes = 0;
    if (texture.format != schema::TextureFormat::rgba8 || texture.mipCount != 1
        || texture.width == 0 || texture.height == 0
        || !ano::detail::checked_multiply(texture.width, texture.height, &pixels)
        || !ano::detail::checked_multiply(pixels, 4, &bytes)
        || bytes != texture.bytes.count || bytes > SIZE_MAX)
        return ANO_RESOURCE_NON_CANONICAL;
    const ano::ArtifactView<schema::Texture> view = {
        .value = texture, .bytes = artifact};
    AnoResourceBytes pixelsView = {};
    const AnoResourceError result = ano::borrow_bytes(
        view, texture.bytes, &pixelsView);
    if (result != ANO_RESOURCE_OK)
        return result;
    if (pixelsView.size != bytes)
        return ANO_RESOURCE_NON_CANONICAL;
    return !reserve_upload_slice(
                residency, static_cast<VkDeviceSize>(bytes),
                &binding.uploadOffset)
        ? ANO_RESOURCE_OVERFLOW : ANO_RESOURCE_OK;
}

template<>
AnoResourceError prepare_decoded(
    AnoRenderResidency& residency, RenderBinding& binding,
    const schema::Mesh& mesh,
    AnoResourceBytes artifact, mi_heap_t* heap)
{
    if (binding.reuseGeometry) return ANO_RESOURCE_OK;
    if (mesh.vertices.count == 0 || mesh.indices.count == 0
        || mesh.vertices.count > UINT32_MAX || mesh.indices.count > UINT32_MAX
        || mesh.vertices.count > SIZE_MAX / sizeof(schema::Vertex)
        || mesh.indices.count > SIZE_MAX / sizeof(uint32_t))
        return ANO_RESOURCE_NON_CANONICAL;
    schema::Vertex* portable = mi_heap_mallocn_tp(
        schema::Vertex, heap, static_cast<size_t>(mesh.vertices.count));
    uint32_t* indices = mi_heap_mallocn_tp(
        uint32_t, heap, static_cast<size_t>(mesh.indices.count));
    if (!portable || !indices) return ANO_RESOURCE_OUT_OF_MEMORY;
    const ano::ArtifactView<schema::Mesh> view = {
        .value = mesh, .bytes = artifact};
    AnoResourceError result = ano::resolve_span(
        view, mesh.vertices, portable, mesh.vertices.count);
    if (result == ANO_RESOURCE_OK)
        result = ano::resolve_span(
            view, mesh.indices, indices, mesh.indices.count);
    if (result != ANO_RESOURCE_OK) return result;
    static_assert(sizeof(schema::Vertex) == sizeof(::Vertex));
    const AnoLodConfig lod = ano_lod_config_default(ANO_DEFAULT_LOD_COUNT);
    if (!geometry_prepare_chain(
        heap, reinterpret_cast<const ::Vertex*>(portable),
        static_cast<uint32_t>(mesh.vertices.count), indices,
        static_cast<uint32_t>(mesh.indices.count), &lod,
        &binding.preparedGeometry))
        return ANO_RESOURCE_OWNER_REJECTED;
    return reserve_upload_slice(
        residency, binding.preparedGeometry.uploadBytes,
        &binding.uploadOffset)
        ? ANO_RESOURCE_OK : ANO_RESOURCE_OVERFLOW;
}

AnoResourceError invoke_prepare_transform(
    AnoRenderResidency& residency, RenderBinding& target,
    AnoResourceTypeId type,
    AnoResourceBytes artifact, mi_heap_t* heap)
{
    AnoResourceError result = ANO_RESOURCE_UNSUPPORTED;
    (void)visit_render_route(
        type, [&]<auto, class Input, class>() {
            const ano::DecodeResult<Input> decoded =
                ano::decode<Input>(artifact);
            result = decoded.error == ANO_RESOURCE_OK
                ? prepare_decoded(
                    residency, target, decoded.view.value, artifact, heap)
                : decoded.error;
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
    AnoResourceBytes artifact = {};
    AnoResourceError result = ano_resource_epoch_resolve(
        candidate.source, asset, type, &artifact);
    const AnoResourceDependency *dependencies = nullptr;
    uint64_t dependencyCount = 0;
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_epoch_dependencies(
            candidate.source, asset, &dependencies, &dependencyCount);
    bool dependencyChanged = false;
    for (uint64_t i = 0; i < dependencyCount
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
    result = invoke_prepare_transform(
        candidate, *target, type, artifact, heap);
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
    const uint64_t changedCount = ano_resource_epoch_changed_count(
        candidate.source);
    AnoResourceError result = ANO_RESOURCE_OK;
    for (uint64_t i = 0; i < changedCount && result == ANO_RESOURCE_OK; ++i) {
        AnoAssetId changed = {};
        result = ano_resource_epoch_changed(candidate.source, i, &changed);
        if (result == ANO_RESOURCE_OK && changed.value != 0
            && changed.value <= candidate.bindingCount)
            candidate.bindings[changed.value - 1].affected = true;
    }
    for (uint64_t i = 0; i < candidate.bindingCount
                         && result == ANO_RESOURCE_OK; ++i) {
        bool resident = false;
        result = ano_resource_epoch_asset(
            candidate.source, {i + 1}, &candidate.bindings[i].sourceType,
            &resident);
        if (result == ANO_RESOURCE_OK && resident
            && is_render_input(candidate.bindings[i].sourceType))
            result = plan_asset(
                candidate, previous, {i + 1},
                candidate.bindings[i].sourceType, heap);
    }
    return result;
}

AnoResourceError realize_planned_asset(
    AnoRenderResidency& residency, AnoAssetId asset)
{
    if (asset.value == 0 || asset.value > residency.bindingCount)
        return ANO_RESOURCE_BAD_MANIFEST;
    RenderBinding& target = residency.bindings[asset.value - 1];
    if (target.state != BindingState::prepared)
        return target.state == BindingState::resident
            ? ANO_RESOURCE_OK : ANO_RESOURCE_BAD_MANIFEST;
    target.state = BindingState::realizing;
    AnoResourceBytes artifact = {};
    AnoResourceError result = ano_resource_epoch_resolve(
        residency.source, asset, target.sourceType, &artifact);
    if (result == ANO_RESOURCE_OK) {
        schema::RenderResourceContext context = {
            .residency = &residency,
            .asset = asset,
            .artifact = artifact,
        };
        result = invoke_render_transform(
            target, context, target.sourceType, artifact);
    }
    target.preparedGeometry = {};
    target.uploadOffset = 0;
    target.state = result == ANO_RESOURCE_OK
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
    RenderBinding* target = binding(
        *context.residency, context.asset, ano::resource_type_id<Texture>());
    if (!target) return false;
    AnoRenderResidency& residency = *context.residency;
    if (residency.uploadCommands == VK_NULL_HANDLE
        || residency.uploadStaging == VK_NULL_HANDLE
        || !residency.uploadAllocation.mapped)
        return false;
    const ArtifactView<Texture> view = {
        .value = texture,
        .bytes = context.artifact,
    };
    AnoResourceBytes pixels = {};
    if (borrow_bytes(view, texture.bytes, &pixels) != ANO_RESOURCE_OK
        || pixels.size != requiredBytes)
        return false;
    memcpy(static_cast<uint8_t*>(residency.uploadAllocation.mapped)
               + static_cast<size_t>(target->uploadOffset),
           pixels.data, static_cast<size_t>(requiredBytes));
    TexturePackage package = {};
    const TextureUsageFlags usage = static_cast<TextureUsageFlags>(texture.usage);
    const AnoTextureResult built = createTextureImageFromStaging(
        &ctx, residency.uploadCommands, &package,
        residency.uploadStaging, target->uploadOffset, texture.width,
        texture.height, usage);
    if (built.code != ANO_TEXTURE_BUILT)
        return false;
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
        return false;
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
        return false;
    }
    return true;
}

bool realize_material(const Material& material,
                      RenderResourceContext& context,
                      GpuMaterial& output) noexcept
{
    if (material.unlit)
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
                    || texture->state != BindingState::resident)
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

    output.slot = reserve_material_slot();
    if (output.slot == UINT32_MAX) return false;
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
    RenderBinding *material = binding(*context.residency, mesh.material);
    RenderBinding *target = binding(
        *context.residency, context.asset, ano::resource_type_id<Mesh>());
    if (material == nullptr
        || material->state != BindingState::resident)
        return false;
    if (target != nullptr && target->reuseGeometry) {
        output.geometrySlot = target->mesh.geometrySlot;
        output.materialSlot = material->material.slot;
        return true;
    }

    if (target == nullptr || target->preparedGeometry.lodCount == 0)
        return false;
    AnoRenderResidency& residency = *context.residency;
    if (residency.uploadCommands == VK_NULL_HANDLE
        || residency.uploadStaging == VK_NULL_HANDLE
        || !residency.uploadAllocation.mapped)
        return false;
    const uint32_t base = geometry_pool_record_prepared_chain(
        &rendererState.globalGeometryPool, &target->preparedGeometry,
        residency.uploadCommands, residency.uploadStaging,
        residency.uploadAllocation.mapped, target->uploadOffset);
    if (base == ANO_MESH_NONE)
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
        return false;
    }
    const ArtifactView<Scene> view = {
        .value = scene,
        .bytes = context.artifact,
    };
    AnoResourceError decoded = resolve_span(
        view, scene.lights, lights, scene.lights.count);
    auto project = [&](const SceneRenderable& portable,
                       uint64_t index) -> AnoResourceError {
        RenderBinding *mesh = binding(
            *context.residency, portable.mesh);
        if (mesh == nullptr || mesh->state != BindingState::resident)
            return ANO_RESOURCE_OWNER_REJECTED;
        memcpy(renderables[index].transform, portable.transform,
               sizeof(portable.transform));
        renderables[index].mesh_index = mesh->mesh.geometrySlot;
        renderables[index].material_index = mesh->mesh.materialSlot;
        return ANO_RESOURCE_OK;
    };
    if (decoded == ANO_RESOURCE_OK)
        decoded = visit_span(view, scene.renderables, project);
    uint32_t slot = 0;
    if (decoded != ANO_RESOURCE_OK
        || !reserve_scene(*context.residency, &slot)) {
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
    AnoResourceError result = ano_resource_epoch_retain(epoch);
    if (result == ANO_RESOURCE_OK)
        created->source = epoch;
    if (result == ANO_RESOURCE_OK)
        created->bindingCount = ano_resource_epoch_asset_count(created->source);
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
        bool resident = false;
        result = ano_resource_epoch_asset(
            created->source, {i + 1}, &created->bindings[i].sourceType,
            &resident);
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
        result = realize_planned_asset(*residency, asset);
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
    const AnoResidencyEpoch *epoch = nullptr;
    mi_heap_t *preparationHeap = ano_heap_create();
    if (preparationHeap == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    AnoResourceError result = ano_resource_epoch_acquire(manager, &epoch);
    if (result == ANO_RESOURCE_OK)
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
    ano_resource_epoch_release(epoch);
    ano_heap_destroy(preparationHeap);
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
    ano_resource_epoch_release(residency->source);
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

uint32_t ano_vk_resource_scene_primitives(
    const AnoRenderResidency *residency, AnoAssetId asset, const mat4 root,
    AnoRenderableDesc *output, uint32_t capacity)
{
    if (residency == nullptr || asset.value == 0
        || asset.value > residency->bindingCount)
        return 0;
    const RenderBinding& binding = residency->bindings[asset.value - 1];
    if (binding.sourceType.value != ano::resource_type_id<schema::Scene>().value
        || binding.state != BindingState::resident
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
        || binding.state != BindingState::resident
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
        || binding.state != BindingState::resident
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
