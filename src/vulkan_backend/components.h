/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef RENDER_COMPONENTS_H
#define RENDER_COMPONENTS_H

#include <vulkan/vulkan.h>
#include "gpu_alloc.h"
#include <meta>
#include <stddef.h>

/* Enums */

// PBR Feature Flags mapping to glTF properties and extensions
typedef enum PbrFeatureBits {
    PBR_FEATURE_NONE = 0,
    // Core properties
    PBR_FEATURE_BASE_COLOR_FACTOR          = 1 << 0,
    PBR_FEATURE_BASE_COLOR_TEXTURE         = 1 << 1,
    PBR_FEATURE_METALLIC_ROUGHNESS_FACTOR  = 1 << 2,
    PBR_FEATURE_METALLIC_ROUGHNESS_TEXTURE = 1 << 3,
    PBR_FEATURE_NORMAL_TEXTURE             = 1 << 4,
    PBR_FEATURE_OCCLUSION_TEXTURE          = 1 << 5,
    PBR_FEATURE_EMISSIVE_FACTOR            = 1 << 6,
    PBR_FEATURE_EMISSIVE_TEXTURE           = 1 << 7,

    // Alpha modes
    PBR_FEATURE_ALPHA_MODE_OPAQUE          = 1 << 8,
    PBR_FEATURE_ALPHA_MODE_MASK            = 1 << 9,
    PBR_FEATURE_ALPHA_MODE_BLEND           = 1 << 10,

    // Double-sided
    PBR_FEATURE_DOUBLE_SIDED               = 1 << 11,

    // Ratified extensions
    PBR_FEATURE_CLEARCOAT                  = 1 << 12,
    PBR_FEATURE_TRANSMISSION               = 1 << 13,
    PBR_FEATURE_VOLUME                     = 1 << 14,
    PBR_FEATURE_IOR                        = 1 << 15,
    PBR_FEATURE_SPECULAR                   = 1 << 16,
    PBR_FEATURE_SHEEN                      = 1 << 17,
    PBR_FEATURE_IRIDESCENCE                = 1 << 18,
    PBR_FEATURE_ANISOTROPY                 = 1 << 19,
    PBR_FEATURE_DISPERSION                 = 1 << 20,
    PBR_FEATURE_DIFFUSE_TRANSMISSION       = 1 << 21,
    PBR_FEATURE_EMISSIVE_STRENGTH          = 1 << 22,

    // Legacy extensions
    PBR_FEATURE_SPECULAR_GLOSSINESS        = 1 << 23,
} PbrFeatureBits;

typedef uint32_t PbrFeatureFlags;

inline constexpr uint32_t ANO_NO_DRAW_SLOT = UINT32_MAX;
inline constexpr PbrFeatureFlags ANO_PBR_FLAT_FEATURES =
    PBR_FEATURE_BASE_COLOR_FACTOR | PBR_FEATURE_BASE_COLOR_TEXTURE |
    PBR_FEATURE_METALLIC_ROUGHNESS_FACTOR | PBR_FEATURE_METALLIC_ROUGHNESS_TEXTURE |
    PBR_FEATURE_NORMAL_TEXTURE | PBR_FEATURE_OCCLUSION_TEXTURE |
    PBR_FEATURE_ALPHA_MODE_OPAQUE | PBR_FEATURE_ALPHA_MODE_BLEND;
inline constexpr PbrFeatureFlags ANO_PBR_TRANSMISSION_FEATURES =
    ANO_PBR_FLAT_FEATURES | PBR_FEATURE_TRANSMISSION | PBR_FEATURE_VOLUME |
    PBR_FEATURE_IOR | PBR_FEATURE_DOUBLE_SIDED;
inline constexpr PbrFeatureFlags ANO_PBR_ADDITIVE_FEATURES =
    PBR_FEATURE_BASE_COLOR_FACTOR | PBR_FEATURE_BASE_COLOR_TEXTURE |
    PBR_FEATURE_EMISSIVE_FACTOR | PBR_FEATURE_EMISSIVE_TEXTURE |
    PBR_FEATURE_EMISSIVE_STRENGTH | PBR_FEATURE_ALPHA_MODE_BLEND |
    PBR_FEATURE_DOUBLE_SIDED;

enum class AnoPipelineKind : uint8_t { skeleton, graphics, compute };
enum class AnoPipelineSchedule : uint8_t { none, frame, explicit_path };
enum class AnoShaderFamily : uint8_t {
    none, flat, flat_masked, transmission, additive, cull, update, scatter,
    tpsort, lightcull, shadowsetup, lightsetup, hiz, textraster
};

struct AnoPipelineSpec final {
    AnoPipelineKind kind;
    AnoPipelineSchedule schedule;
    uint32_t implementationCount;
    uint32_t drawSlot;
    PbrFeatureFlags supportedFeatures;
    AnoShaderFamily shader;
};
struct AnoPipelineSentinel final {};

// Companion init contracts, reflected by instance/{pipeline,layouts}.c and pipelines/compute.c.
// Graphics: which builder entry point mints the prototype, and whether its material set is the bindless layout.
enum class AnoGraphicsInitFn : uint8_t { flat, flat_twosided, flat_masked, transmission, additive, count };
struct AnoGraphicsPipelineInit final {
    AnoGraphicsInitFn init;
    bool bindlessMaterialSet;
};

// Compute: descriptor-set-layout class + push-constant byte size + specialization tag.
// external 〜 prototype minted outside ano_vk_init_compute (text_raster.c owns PIPELINE_COMPUTE_TEXTRASTER).
enum class AnoComputeSetLayout : uint8_t {
    update, scatter, cull, hiz, lightcull, lightsetup, shadow_setup, external
};
enum class AnoComputeSpecialization : uint8_t { none, mesh_shader, hiz };
struct AnoComputePipelineContract final {
    AnoComputeSetLayout layoutClass;
    uint32_t pushConstantSize;
    AnoComputeSpecialization specialization;
};

typedef enum PipelineType
{
    PIPELINE_FLAT [[=AnoPipelineSpec{AnoPipelineKind::graphics, AnoPipelineSchedule::frame, 3, 0, ANO_PBR_FLAT_FEATURES, AnoShaderFamily::flat}]]
        [[=AnoGraphicsPipelineInit{AnoGraphicsInitFn::flat, true}]] = 0,
    PIPELINE_PARTICLE [[=AnoPipelineSpec{AnoPipelineKind::skeleton, AnoPipelineSchedule::none, 0, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::none}]],
    PIPELINE_SDF_COMPOSITE [[=AnoPipelineSpec{AnoPipelineKind::skeleton, AnoPipelineSchedule::none, 0, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::none}]],
    PIPELINE_UI [[=AnoPipelineSpec{AnoPipelineKind::skeleton, AnoPipelineSchedule::none, 0, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::none}]],
    PIPELINE_TRANSMISSION [[=AnoPipelineSpec{AnoPipelineKind::graphics, AnoPipelineSchedule::frame, 2, 1, ANO_PBR_TRANSMISSION_FEATURES, AnoShaderFamily::transmission}]]
        [[=AnoGraphicsPipelineInit{AnoGraphicsInitFn::transmission, true}]],
    PIPELINE_ADDITIVE [[=AnoPipelineSpec{AnoPipelineKind::graphics, AnoPipelineSchedule::frame, 1, 2, ANO_PBR_ADDITIVE_FEATURES, AnoShaderFamily::additive}]]
        [[=AnoGraphicsPipelineInit{AnoGraphicsInitFn::additive, true}]],
    PIPELINE_FLAT_TWOSIDED [[=AnoPipelineSpec{AnoPipelineKind::graphics, AnoPipelineSchedule::frame, 3, 3, ANO_PBR_FLAT_FEATURES | PBR_FEATURE_DOUBLE_SIDED, AnoShaderFamily::flat}]]
        [[=AnoGraphicsPipelineInit{AnoGraphicsInitFn::flat_twosided, true}]],
    PIPELINE_FLAT_MASKED [[=AnoPipelineSpec{AnoPipelineKind::graphics, AnoPipelineSchedule::frame, 3, 4, ANO_PBR_FLAT_FEATURES | PBR_FEATURE_DOUBLE_SIDED | PBR_FEATURE_ALPHA_MODE_MASK, AnoShaderFamily::flat_masked}]]
        [[=AnoGraphicsPipelineInit{AnoGraphicsInitFn::flat_masked, true}]],
    PIPELINE_COMPUTE_CULL [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::frame, 1, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::cull}]]
        [[=AnoComputePipelineContract{AnoComputeSetLayout::cull, 0, AnoComputeSpecialization::mesh_shader}]],
    PIPELINE_COMPUTE_UPDATE [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::frame, 1, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::update}]]
        [[=AnoComputePipelineContract{AnoComputeSetLayout::update, sizeof(uint32_t), AnoComputeSpecialization::none}]],
    PIPELINE_COMPUTE_SCATTER [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::frame, 1, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::scatter}]]
        [[=AnoComputePipelineContract{AnoComputeSetLayout::scatter, sizeof(uint32_t) /* streamCount */, AnoComputeSpecialization::none}]],
    // Compute Transparency-Sort Pipeline. Reuses the cull descriptor set layout; shares useMeshShader spec constant.
    PIPELINE_COMPUTE_TPSORT [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::explicit_path, 1, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::tpsort}]]
        [[=AnoComputePipelineContract{AnoComputeSetLayout::cull, 0, AnoComputeSpecialization::mesh_shader}]],
    // Skeleton slots: buffers/prototype table size for them, no pipeline created yet
    PIPELINE_DECAL [[=AnoPipelineSpec{AnoPipelineKind::skeleton, AnoPipelineSchedule::none, 0, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::none}]],
    PIPELINE_SKINNED [[=AnoPipelineSpec{AnoPipelineKind::skeleton, AnoPipelineSchedule::none, 0, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::none}]],
    PIPELINE_COMPUTE_LIGHTCULL [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::frame, 1, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::lightcull}]]
        [[=AnoComputePipelineContract{AnoComputeSetLayout::lightcull, 0, AnoComputeSpecialization::none}]],
    PIPELINE_COMPUTE_SHADOWSETUP [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::frame, 1, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::shadowsetup}]]
        [[=AnoComputePipelineContract{AnoComputeSetLayout::shadow_setup, 0, AnoComputeSpecialization::none}]],
    PIPELINE_COMPUTE_LIGHTSETUP [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::frame, 1, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::lightsetup}]]
        [[=AnoComputePipelineContract{AnoComputeSetLayout::lightsetup, sizeof(uint32_t) /* lightCount */, AnoComputeSpecialization::none}]],
    // Push constant 24 B: { int srcMip; ivec2 dstSize; ivec2 srcSize; }
    PIPELINE_COMPUTE_HIZ [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::explicit_path, 2, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::hiz}]]
        [[=AnoComputePipelineContract{AnoComputeSetLayout::hiz, 24, AnoComputeSpecialization::hiz}]],
    PIPELINE_COMPUTE_TEXTRASTER [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::explicit_path, 1, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::textraster}]]
        [[=AnoComputePipelineContract{AnoComputeSetLayout::external, 0, AnoComputeSpecialization::none}]],
    PIPELINE_TYPE_COUNT [[=AnoPipelineSentinel{}]]
} PipelineType;

// Exactly one init contract per active enumerator, none on skeletons or the sentinel;
// one builder per graphics prototype; compute contracts stay inside layout limits.
consteval bool ano_validate_pipeline_init_contracts()
{
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^PipelineType));
    bool builderSeen[static_cast<size_t>(AnoGraphicsInitFn::count)] = {};
    template for (constexpr auto enumerator : enumerators) {
        constexpr auto specs = std::define_static_array(
            std::meta::annotations_of_with_type(enumerator, ^^AnoPipelineSpec));
        constexpr auto inits = std::define_static_array(
            std::meta::annotations_of_with_type(enumerator, ^^AnoGraphicsPipelineInit));
        constexpr auto contracts = std::define_static_array(
            std::meta::annotations_of_with_type(enumerator, ^^AnoComputePipelineContract));
        if constexpr (specs.empty()) {
            static_assert(inits.empty() && contracts.empty(),
                "placeholder enumerators carry no init contract");
        } else {
            constexpr AnoPipelineSpec spec = std::meta::extract<AnoPipelineSpec>(specs[0]);
            constexpr bool graphicsActive =
                spec.kind == AnoPipelineKind::graphics && spec.implementationCount > 0;
            constexpr bool computeActive =
                spec.kind == AnoPipelineKind::compute && spec.implementationCount > 0;
            static_assert(inits.size() == (graphicsActive ? 1u : 0u),
                "active graphics enumerators carry exactly one AnoGraphicsPipelineInit");
            static_assert(contracts.size() == (computeActive ? 1u : 0u),
                "active compute enumerators carry exactly one AnoComputePipelineContract");
            if constexpr (graphicsActive) {
                constexpr auto builder = static_cast<size_t>(
                    std::meta::extract<AnoGraphicsPipelineInit>(inits[0]).init);
                static_assert(builder < static_cast<size_t>(AnoGraphicsInitFn::count));
                if (builderSeen[builder])
                    __builtin_abort();
                builderSeen[builder] = true;
            }
            if constexpr (computeActive) {
                constexpr auto contract =
                    std::meta::extract<AnoComputePipelineContract>(contracts[0]);
                static_assert(contract.pushConstantSize % sizeof(uint32_t) == 0 &&
                    contract.pushConstantSize <= 128);
                static_assert((contract.specialization == AnoComputeSpecialization::hiz) ==
                    (spec.shader == AnoShaderFamily::hiz));
                static_assert(contract.layoutClass != AnoComputeSetLayout::external ||
                    (contract.pushConstantSize == 0 &&
                     contract.specialization == AnoComputeSpecialization::none));
            }
        }
    }
    return true;
}
static_assert(ano_validate_pipeline_init_contracts());

// VkSpecializationMapEntry table reflected from the spec-data struct's own members:
// constantID FirstConstantId..FirstConstantId+N-1 in member order, offset/size from the layout.
template<size_t Count>
struct AnoVkSpecializationMap final {
    VkSpecializationMapEntry entries[Count];
    static constexpr uint32_t count = (uint32_t)Count;
};

template<class Data, uint32_t FirstConstantId = 0>
consteval auto ano_vk_reflect_specialization_map()
{
    static constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(
        ^^Data, std::meta::access_context::unchecked()));
    static_assert(members.size() > 0);
    AnoVkSpecializationMap<members.size()> result{};
    uint32_t at = 0;
    template for (constexpr auto member : members) {
        result.entries[at] = {
            .constantID = FirstConstantId + at,
            .offset = (uint32_t)std::meta::offset_of(member).bytes,
            .size = std::meta::size_of(std::meta::type_of(member)),
        };
        ++at;
    }
    return result;
}

// Contiguous IDs, padding-free offsets, total == sizeof(Data): pData can be the struct itself.
template<class Data, uint32_t FirstConstantId = 0, size_t Count>
consteval bool ano_vk_specialization_map_valid(const AnoVkSpecializationMap<Count>& map)
{
    uint32_t offset = 0;
    for (uint32_t i = 0; i < Count; ++i) {
        if (map.entries[i].constantID != FirstConstantId + i || map.entries[i].offset != offset)
            return false;
        offset += (uint32_t)map.entries[i].size;
    }
    return offset == sizeof(Data);
}

uint32_t ano_draw_pipeline_count(void);         // number of drawing types == per-camera-view draw-slot stride
uint32_t ano_draw_slot_of(PipelineType type);   // enum -> draw slot, ANO_NO_DRAW_SLOT if it never draws

// Compacted-draw partitions: camera views get every draw slot (view*drawSlotCount+slot); each shadow frustum gets solid + MASKED. Single sizing source for the three buffers and their cull map.
uint32_t ano_draw_partition_count(void);

typedef enum PassType
{
    PASS_COMPUTE,       // compute dispatch
    PASS_GRAPHICS,      // rasterization pass
} PassType;

typedef struct RenderPassDef
{
    PassType            type;
    PipelineType        prototype;              // which pipeline prototype to bind
    uint32_t            implementationIndex;    // which variant (opaque, transparent, etc.)
    // Recorded once per view vs once per frame
    bool                perView;

    // Graphics-only:
    uint32_t                colorAttachmentCount;
    VkFormat                colorFormats[4];
    VkFormat                depthFormat;
    VkAttachmentLoadOp      colorLoadOp;
    VkAttachmentLoadOp      depthLoadOp;
    // STORE when a later pass must read this pass's depth (opaque -> transmission)
    VkAttachmentStoreOp     depthStoreOp;
    VkClearValue            colorClear;
    VkClearValue            depthClear;
    VkResolveModeFlagBits   resolveMode;
    // Compute-only:
    uint32_t                dispatchX, dispatchY, dispatchZ;
} RenderPassDef;

/* Primitive Assets */

// One image + alloc; optional srgbView (colour) and unormView (data)
typedef struct TextureData
{
	uint32_t usageCount; // number of active meshes using this resource
	VkImage textureImage;
	GpuAllocation textureImageAlloc;
	VkImageView srgbView;    // colour, or VK_NULL_HANDLE
	VkImageView unormView;   // data, or VK_NULL_HANDLE
} TextureData;

typedef struct MeshData
{
	uint32_t usageCount; // number of active meshes using this resource
	uint32_t meshRegionIndex;
} MeshData;

// Tracks loaded graphics resources and their usage
typedef struct RenderPrimitives
{
	uint32_t meshCount;
	uint32_t meshCapacity;
	MeshData* meshes;
	uint32_t textureCount;
	uint32_t textureCapacity;
	TextureData* textureBuffers;
} RenderPrimitives;

void ano_vk_register_mesh(RenderPrimitives* primitives, MeshData data);
void ano_vk_increment_mesh_usage(RenderPrimitives* primitives, uint32_t index);
void ano_vk_decrement_mesh_usage(RenderPrimitives* primitives, uint32_t index);

// Out: true if registered; false leaves registry unchanged (caller keeps handles)
[[nodiscard]] bool ano_vk_register_texture(RenderPrimitives* primitives, TextureData data);
void ano_vk_increment_texture_usage(RenderPrimitives* primitives, uint32_t index);
void ano_vk_decrement_texture_usage(RenderPrimitives* primitives, uint32_t index);

void ano_vk_cleanup_primitives(RenderPrimitives* primitives);

typedef struct PipelineImplementation
{
    VkPipeline           pipeline;
    VkPipelineBindPoint  bindPoint;
    VkBool32             depthWrite;    // whether this variant writes depth
    VkBool32             blendEnable;   // opaque vs. transparent
} PipelineImplementation;

// A logical pipeline class. Owns the layout and the cache.
typedef struct PipelinePrototype
{
    PipelineType                type;
    VkPipelineLayout            layout;           // shared across all implementations
    VkDescriptorSetLayout       descriptorLayout; // material descriptor layout
    uint32_t                    implementationCount;
    PipelineImplementation*     implementations;  // flat array
    VkPipelineCache             cache;
    PbrFeatureFlags             supportedFeatures; // PBR features supported by this pipeline
} PipelinePrototype;

struct RendererState;

// Pure compatibility check helper
bool ano_vk_check_feature_compatibility(PbrFeatureFlags pipelineFeatures, PbrFeatureFlags requiredFeatures, PbrFeatureFlags* outUnsupported);

// Query features globally supported by all active graphics pipelines
PbrFeatureFlags ano_vk_get_active_pipelines_supported_features(const struct RendererState* state);

struct MaterialData;
void ano_vk_init_default_material_data(struct MaterialData* mat);

#endif
