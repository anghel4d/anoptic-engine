/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_log.h>
#include "vulkan_backend/instance/descriptor_layout_schema.h"
#include "vulkan_backend/instance/pipeline.h"
#include "vulkan_backend/pipeline_registry.h"
#include <stdlib.h>
#include <stddef.h>
#include <meta>

#include <vulkan/vulkan.h>

// Compute prototypes + descriptor layouts.
// Cache idiom: refused mint -> VK_NULL_HANDLE; init continues.
// Commit-last: publish implementationCount only after array exists.
template<PipelineType Type>
static bool compute_build(VulkanContext* ctx, RendererState* state,
    const VkSpecializationInfo* specialization = nullptr, uint32_t implementation = 0,
    const char* shaderPath = ano_pipeline_compute_shader_path<Type>())
{
    constexpr auto spec = ano_pipeline_spec<Type>();
    static_assert(spec.kind == AnoPipelineKind::compute);
    PipelinePrototype* proto = &state->prototypes[Type];
    if (proto->implementations == nullptr && !ano_pipeline_prepare_prototype(proto, Type))
        return false;
    if (proto->type != Type || implementation >= proto->implementationCount)
        return false;

    if (implementation == 0) {
        VkPipelineCacheCreateInfo cacheInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
        if (vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &proto->cache) != VK_SUCCESS)
            proto->cache = VK_NULL_HANDLE;
    }

    struct Buffer code = {};
    if (!loadFile(shaderPath, &code))
        return false;
    VkShaderModule module = createShaderModule(ctx->device, &code);
    VkComputePipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = proto->layout,
    };
    const bool built = ano_pipeline_stage(
        VK_SHADER_STAGE_COMPUTE_BIT, module, specialization, &pipelineInfo.stage) &&
        vkCreateComputePipelines(ctx->device, proto->cache, 1, &pipelineInfo, NULL,
            &proto->implementations[implementation].pipeline) == VK_SUCCESS;
    ano_aligned_free(code.data);
    vkDestroyShaderModule(ctx->device, module, NULL);
    return built;
}

// constant_id 1: useMeshShader
struct AnoMeshShaderSpecData { VkBool32 useMeshShader; };
static constexpr auto ANO_MESH_SHADER_SPEC_MAP =
    ano_vk_reflect_specialization_map<AnoMeshShaderSpecData, 1>();
static_assert(ano_vk_specialization_map_valid<AnoMeshShaderSpecData, 1>(ANO_MESH_SHADER_SPEC_MAP));

// Spec constants: id 0 isReduce, id 1 msaaSamples (reduce source sample count).
struct HizSpecData { VkBool32 isReduce; int32_t msaaSamples; };
static constexpr auto ANO_HIZ_SPEC_MAP = ano_vk_reflect_specialization_map<HizSpecData>();
static_assert(ano_vk_specialization_map_valid<HizSpecData>(ANO_HIZ_SPEC_MAP));

// Mint one schema-reflected descriptor set layout into its RendererState slot.
template<const auto& Specs>
static VkDescriptorSetLayout* compute_mint_set_layout(VulkanContext* ctx, VkDescriptorSetLayout* out)
{
    VkDescriptorSetLayoutBinding bindings[Specs.count] = {};
    if (!ano_vk_materialize_layout_bindings(Specs, bindings))
        return nullptr;
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = Specs.count;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(ctx->device, &layoutInfo, NULL, out) != VK_SUCCESS)
        return nullptr;
    return out;
}

// Layout class -> RendererState slot. Classes owned here mint from their schema table;
// cull/hiz/shadow_setup arrive pre-minted from layouts.c.
template<AnoComputeSetLayout Layout>
static VkDescriptorSetLayout* compute_class_set_layout(VulkanContext* ctx, RendererState* state)
{
    // Compute Update Pipeline
    if constexpr (Layout == AnoComputeSetLayout::update)
        return compute_mint_set_layout<ANO_VK_UPDATE_BINDINGS>(ctx, &state->updateSetLayout);
    // Compute Scatter Pipeline (streamed transforms, Path B)
    else if constexpr (Layout == AnoComputeSetLayout::scatter)
        return compute_mint_set_layout<ANO_VK_SCATTER_BINDINGS>(ctx, &state->scatterSetLayout);
    // Compute Culling Pipeline
    else if constexpr (Layout == AnoComputeSetLayout::cull)
        return &state->culling.setLayout;
    else if constexpr (Layout == AnoComputeSetLayout::hiz)
        return &state->hizSetLayout;
    // Compute Light-cull Pipeline (clustered-forward froxel light assignment).
    // 0: GlobalUBO (in)  1: TransformSSBO (in, light world pos)  2: LightSSBO (in)
    // 3: clusterLightCount (out)  4: clusterLightIndices (out)
    else if constexpr (Layout == AnoComputeSetLayout::lightcull)
        return compute_mint_set_layout<ANO_VK_LIGHT_CULL_BINDINGS>(ctx, &state->lightcullSetLayout);
    // Compute Light-setup Pipeline: per-light world pose (worldPos/worldDir) precompute.
    // 0: TransformSSBO (in)  1: LightSSBO (in)  2: LightRuntimeSSBO (out, 64B/light). Push constant: light count.
    else if constexpr (Layout == AnoComputeSetLayout::lightsetup)
        return compute_mint_set_layout<ANO_VK_LIGHT_SETUP_BINDINGS>(ctx, &state->lightsetupSetLayout);
    // Compute Shadow-setup Pipeline: builds each shadow frustum's light-space viewProj + planes.
    else if constexpr (Layout == AnoComputeSetLayout::shadow_setup)
        return &state->shadowSetupSetLayout;
    else
        static_assert(Layout != Layout, "external layout classes are minted outside init_compute");
}

// Minted classes belong to exactly one contract; shared classes may repeat.
consteval uint32_t compute_layout_class_uses(AnoComputeSetLayout layout)
{
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^PipelineType));
    uint32_t uses = 0;
    template for (constexpr auto enumerator : enumerators) {
        constexpr auto contracts = std::define_static_array(
            std::meta::annotations_of_with_type(enumerator, ^^AnoComputePipelineContract));
        if constexpr (!contracts.empty()) {
            if (std::meta::extract<AnoComputePipelineContract>(contracts[0]).layoutClass == layout)
                ++uses;
        }
    }
    return uses;
}
static_assert(compute_layout_class_uses(AnoComputeSetLayout::update) == 1 &&
    compute_layout_class_uses(AnoComputeSetLayout::scatter) == 1 &&
    compute_layout_class_uses(AnoComputeSetLayout::lightcull) == 1 &&
    compute_layout_class_uses(AnoComputeSetLayout::lightsetup) == 1);

// One contract expansion: class set layout, pipeline layout + optional push range, then the build.
template<PipelineType Type, AnoComputePipelineContract Contract, const char* Name>
static bool compute_init(VulkanContext* ctx, RendererState* state)
{
    VkDescriptorSetLayout* setLayout = compute_class_set_layout<Contract.layoutClass>(ctx, state);
    if (setLayout == nullptr)
    {
        ano_log(ANO_FATAL, "Failed to create %s descriptor set layout!", Name);
        return false;
    }

    VkPushConstantRange pcRange = { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0, .size = Contract.pushConstantSize };
    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = setLayout;
    if constexpr (Contract.pushConstantSize != 0)
    {
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pcRange;
    }
    if (vkCreatePipelineLayout(ctx->device, &layoutInfo, NULL, &state->prototypes[Type].layout) != VK_SUCCESS)
    {
        ano_log(ANO_FATAL, "Failed to create %s pipeline layout!", Name);
        return false;
    }

    if constexpr (Contract.specialization == AnoComputeSpecialization::mesh_shader)
    {
        AnoMeshShaderSpecData specData = { ctx->deviceCapabilities.meshShader ? VK_TRUE : VK_FALSE };
        VkSpecializationInfo specInfo = {};
        specInfo.mapEntryCount = ANO_MESH_SHADER_SPEC_MAP.count;
        specInfo.pMapEntries = ANO_MESH_SHADER_SPEC_MAP.entries;
        specInfo.dataSize = sizeof(specData);
        specInfo.pData = &specData;
        return compute_build<Type>(ctx, state, &specInfo);
    }
    else if constexpr (Contract.specialization == AnoComputeSpecialization::hiz)
    {
        // Compute Hi-Z Pyramid Build Pipeline. [0] reduce, [1] downsample via isReduce spec constant (0).
        const char* hizShaderPath = ctx->deviceCapabilities.depthMaxResolve
            ? "resources/shaders/hiz_resolve.comp.spv"
            : ano_pipeline_compute_shader_path<Type>();

        constexpr auto hizPipeline = ano_pipeline_spec<Type>();
        for (uint32_t impl = 0; impl < hizPipeline.implementationCount; ++impl)
        {
            struct HizSpecData hizSpecData = {
                .isReduce    = (impl == 0u) ? VK_TRUE : VK_FALSE, // [0] reduce, [1] downsample
                .msaaSamples = (int32_t)ctx->msaaSamples,
            };
            VkSpecializationInfo hizSpec = {};
            hizSpec.mapEntryCount = ANO_HIZ_SPEC_MAP.count;
            hizSpec.pMapEntries = ANO_HIZ_SPEC_MAP.entries;
            hizSpec.dataSize = sizeof(hizSpecData);
            hizSpec.pData = &hizSpecData;

            if (!compute_build<Type>(ctx, state, &hizSpec, impl, hizShaderPath))
                return false;
        }
        return true;
    }
    else
    {
        return compute_build<Type>(ctx, state);
    }
}

// Unwind: the build lambda returns false; each typed mint discharges its shader immediately.
bool ano_vk_init_compute(VulkanContext* ctx, RendererState* state)
{
    bool ok = [&]() -> bool {

    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^PipelineType));
    template for (constexpr auto enumerator : enumerators) {
        constexpr auto contracts = std::define_static_array(
            std::meta::annotations_of_with_type(enumerator, ^^AnoComputePipelineContract));
        if constexpr (!contracts.empty()) {
            constexpr auto contract = std::meta::extract<AnoComputePipelineContract>(contracts[0]);
            if constexpr (contract.layoutClass != AnoComputeSetLayout::external) {
                constexpr PipelineType type = [:enumerator:];
                constexpr const char* name = std::define_static_string(std::meta::identifier_of(enumerator));
                if (!compute_init<type, contract, name>(ctx, state))
                    return false;
            }
        }
    }
    return true;
    }();

    return ok;
}
