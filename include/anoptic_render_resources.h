/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Portable render artifacts consumed by renderer-owned realization transforms.

#ifndef ANOPTICENGINE_ANOPTIC_RENDER_RESOURCES_H
#define ANOPTICENGINE_ANOPTIC_RENDER_RESOURCES_H

#include "anoptic_resources_typed.h"
#include "anoptic_resources_cook.h"
#include "anoptic_resources_runtime.h"

typedef struct AnoRenderResourcePublication AnoRenderResourcePublication;

typedef enum AnoRenderResourceReloadStatus {
    ANO_RENDER_RESOURCE_RELOAD_PENDING,
    ANO_RENDER_RESOURCE_RELOAD_COMMITTED,
    ANO_RENDER_RESOURCE_RELOAD_REJECTED,
} AnoRenderResourceReloadStatus;

// Preparation takes ownership of reload. The published epoch remains unchanged
// until a ready publication is committed at a render-frame boundary.
extern "C" AnoResourceError ano_render_resources_prepare_reload(
    AnoResourceReload *reload, AnoRenderResourcePublication **publication);
// Polls owner work and commits a ready candidate. A terminal result consumes
// *publication and sets it to null; rejection preserves the current epoch.
extern "C" AnoRenderResourceReloadStatus ano_render_resources_poll_reload(
    AnoRenderResourcePublication **publication);
// Cancels and consumes an unpublished candidate.
extern "C" void ano_render_resources_cancel_reload(
    AnoRenderResourcePublication *publication);

namespace ano::asset_schema {

enum class TextureFormat : uint8_t {
    rgba8,
};

enum class TextureUsage : uint8_t {
    color = 1,
    data = 2,
    color_and_data = 3,
};

struct [[=Artifact{}]] Texture final {
    uint32_t width;
    uint32_t height;
    uint32_t mipCount;
    TextureFormat format;
    TextureUsage usage;
    RelativeSpan<uint8_t> bytes;
};

enum class MaterialAlphaMode : uint8_t {
    opaque,
    mask,
    blend,
};

enum class MaterialFeature : uint8_t {
    pbrMetallicRoughness,
    pbrSpecularGlossiness,
    clearcoat,
    transmission,
    ior,
    specular,
    volume,
    sheen,
    emissiveStrength,
    iridescence,
    diffuseTransmission,
    anisotropy,
    dispersion,
    unlit,
};

consteval uint32_t material_feature_bit(MaterialFeature feature)
{
    static_assert(std::meta::enumerators_of(^^MaterialFeature).size() <= 32);
    return UINT32_C(1) << ano::detail::enum_index(feature);
}

struct MaterialTextureUse final {
    TextureUsage usage;
};

enum class MaterialTextureSlot : uint8_t {
    baseColor [[=MaterialTextureUse{TextureUsage::color}]],
    metallicRoughness [[=MaterialTextureUse{TextureUsage::data}]],
    normal [[=MaterialTextureUse{TextureUsage::data}]],
    occlusion [[=MaterialTextureUse{TextureUsage::data}]],
    emissive [[=MaterialTextureUse{TextureUsage::color}]],
    clearcoat [[=MaterialTextureUse{TextureUsage::data}]],
    clearcoatRoughness [[=MaterialTextureUse{TextureUsage::data}]],
    clearcoatNormal [[=MaterialTextureUse{TextureUsage::data}]],
    transmission [[=MaterialTextureUse{TextureUsage::data}]],
    thickness [[=MaterialTextureUse{TextureUsage::data}]],
    specular [[=MaterialTextureUse{TextureUsage::data}]],
    specularColor [[=MaterialTextureUse{TextureUsage::color}]],
    sheenColor [[=MaterialTextureUse{TextureUsage::color}]],
    sheenRoughness [[=MaterialTextureUse{TextureUsage::data}]],
    iridescence [[=MaterialTextureUse{TextureUsage::data}]],
    iridescenceThickness [[=MaterialTextureUse{TextureUsage::data}]],
    anisotropy [[=MaterialTextureUse{TextureUsage::data}]],
    diffuseTransmission [[=MaterialTextureUse{TextureUsage::data}]],
    diffuseTransmissionColor [[=MaterialTextureUse{TextureUsage::color}]],
};

struct MaterialTextureSlotMatch final {
    bool found;
    MaterialTextureSlot slot;
};

consteval MaterialTextureSlotMatch material_texture_slot(
    std::meta::info member)
{
    constexpr std::string_view suffix = "Texture";
    std::string_view name = std::meta::identifier_of(member);
    if (!name.ends_with(suffix))
        return {false, MaterialTextureSlot::baseColor};
    name.remove_suffix(suffix.size());
    static constexpr auto slots = std::define_static_array(
        std::meta::enumerators_of(^^MaterialTextureSlot));
    template for (constexpr std::meta::info slot : slots)
        if (ano::detail::semantic_name_equal(
                name, std::meta::identifier_of(slot)))
            return {true, [:slot:]};
    return {false, MaterialTextureSlot::baseColor};
}

consteval TextureUsage material_texture_usage(MaterialTextureSlot sought)
{
    static constexpr auto slots = std::define_static_array(
        std::meta::enumerators_of(^^MaterialTextureSlot));
    template for (constexpr std::meta::info slot : slots)
        if (sought == [:slot:]) {
            static constexpr auto annotations = std::define_static_array(
                std::meta::annotations_of_with_type(
                    slot, ^^MaterialTextureUse));
            static_assert(annotations.size() == 1);
            return std::meta::extract<MaterialTextureUse>(annotations[0]).usage;
        }
    __builtin_abort();
}

inline constexpr size_t materialTextureCount =
    std::meta::enumerators_of(^^MaterialTextureSlot).size();

struct MaterialTexture final {
    AssetRef<Texture> texture;
    uint32_t texCoord;
    float scale;
    float strength;
    float offset[2];
    float rotation;
    float transformScale[2];
    uint32_t transformTexcoord;
    bool hasTransform;
    bool transformsTexcoord;
};

struct [[=Artifact{}]] Material final {
    uint32_t features;
    float baseColorFactor[4];
    float metallicFactor;
    float roughnessFactor;
    float emissiveFactor[3];
    MaterialAlphaMode alphaMode;
    float alphaCutoff;
    bool doubleSided;
    bool unlit;
    float clearcoatFactor;
    float clearcoatRoughnessFactor;
    float transmissionFactor;
    float thicknessFactor;
    float attenuationDistance;
    float attenuationColor[3];
    float ior;
    float specularFactor;
    float specularColorFactor[3];
    float sheenColorFactor[3];
    float sheenRoughnessFactor;
    float iridescenceFactor;
    float iridescenceIor;
    float iridescenceThicknessMinimum;
    float iridescenceThicknessMaximum;
    float anisotropyStrength;
    float anisotropyRotation;
    float dispersion;
    float diffuseTransmissionFactor;
    float diffuseTransmissionColorFactor[3];
    float emissiveStrength;
    MaterialTexture textures[materialTextureCount];
};

struct Vertex final {
    float position[3];
    float normal[3];
    float texCoord[2];
};

struct [[=Artifact{}]] Mesh final {
    RelativeSpan<Vertex> vertices;
    RelativeSpan<uint32_t> indices;
    AssetRef<Material> material;
    float boundsMinimum[3];
    float boundsMaximum[3];
};

struct SceneRenderable final {
    AssetRef<Mesh> mesh;
    float transform[16];
};

enum class SceneLightType : uint8_t {
    directional,
    point,
    spot,
};

struct SceneLight final {
    float transform[16];
    float color[3];
    float intensity;
    float range;
    float innerConeAngle;
    float outerConeAngle;
    SceneLightType type;
    bool castsShadow;
};

struct [[=Artifact{}]] Scene final {
    RelativeSpan<SceneRenderable> renderables;
    RelativeSpan<SceneLight> lights;
};

enum class RenderReservationKind : uint8_t {
    texture,
    material,
    geometry,
};

struct RenderReservation final {
    RenderReservationKind kind;
};

struct [[=Artifact{}]] GpuTexture final {
    uint32_t ownerSlot [[=RenderReservation{RenderReservationKind::texture}]];
    uint32_t colorSlot;
    uint32_t dataSlot;
};

struct [[=Artifact{}]] GpuMaterial final {
    uint32_t slot [[=RenderReservation{RenderReservationKind::material}]];
};

struct [[=Artifact{}]] GpuMesh final {
    uint32_t geometrySlot [[=RenderReservation{RenderReservationKind::geometry}]];
    uint32_t materialSlot;
};

struct [[=Artifact{}]] GpuScene final {
    uint32_t slot;
};

struct RenderResourceContext;

[[=Importer{".gltf"}]][[=Importer{".glb"}]]
AnoResourceError import_gltf(AnoResourceCooker& cooker,
                             const AnoResourceImportRequest& request) noexcept;

[[=Transform{Executor::render_master, Streaming::whole, true}]]
bool realize_texture(const Texture&, RenderResourceContext&, GpuTexture&) noexcept;

[[=Transform{Executor::render_master, Streaming::whole, true}]]
bool realize_material(const Material&, RenderResourceContext&, GpuMaterial&) noexcept;

[[=Transform{Executor::render_master, Streaming::whole, true}]]
bool realize_mesh(const Mesh&, RenderResourceContext&, GpuMesh&) noexcept;

[[=Transform{Executor::render_master, Streaming::whole, true}]]
bool realize_scene(const Scene&, RenderResourceContext&, GpuScene&) noexcept;

} // namespace ano::asset_schema

#endif // ANOPTICENGINE_ANOPTIC_RENDER_RESOURCES_H
