/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory.h>
#include <anoptic_filesystem.h>
#include <ano/log.h>
#include "vulkan_backend/instance/descriptor_layout_schema.h"
#include "vulkan_backend/instance/pipeline.h"
#include "flat.h"
#include "graphics_contract.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include <vulkan/vulkan.h>

// Depth-only caster lane deltas for the shared graphics ladder.
static consteval AnoGraphicsPipelineContract shadow_depth_contract()
{
	return {
		// NONE: one partition mixes both sidedness lanes' casters.
		.cullMode = VK_CULL_MODE_NONE,
		// No rasterizer depth bias.
		.msaaFromContext = false, // single-sample shadow atlas
		.depthTest = VK_TRUE,
		.depthWrite = VK_TRUE,
		.depthCompare = VK_COMPARE_OP_LESS,
		.maxDepthBounds = 1.0f,
		// Two CDF-stats color attachments (MRT sublayers), blending disabled. Blur reuses statsBlend[0].
		.colorAttachmentCount = 2,
		.blend = {
			{ .blendEnable = VK_FALSE,
			  .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT },
			{ .blendEnable = VK_FALSE,
			  .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT },
		},
		// Two CDF stats color targets (MRT sublayers) + transient depth. Blur reuses statsFormat[0].
		.colorFormats = { ANO_SHADOW_STATS_FORMAT, ANO_SHADOW_STATS_FORMAT },
		.depthFormat = ANO_SHADOW_TRANSIENT_DEPTH_FORMAT,
		.meshCapable = true,
	};
}

// Fullscreen moment-prefilter deltas: single stats target, no depth.
static consteval AnoGraphicsPipelineContract shadow_blur_contract()
{
	return {
		.cullMode = VK_CULL_MODE_NONE,
		.colorAttachmentCount = 1,
		.blend = {
			{ .blendEnable = VK_FALSE,
			  .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT },
		},
		.colorFormats = { ANO_SHADOW_STATS_FORMAT },
	};
}

// Dynamic shadow depth pipeline: depth-only FLAT geometry variant + shadowPass spec constant.
// Run after ano_vk_init_pipelines.
// in:  ctx, state (prototypes[PIPELINE_FLAT].layout must exist)
// out: true on success; populates state->shadow{Pipeline,Cache,Sampler}
// Cache idiom: refused mint zeroes handle to VK_NULL_HANDLE; init continues.
bool ano_vk_init_shadow(VulkanContext* ctx, RendererState* state)
{
	// Linear/clamp sampler for the moment atlas.
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	samplerInfo.maxLod = 1.0f;
	if (vkCreateSampler(ctx->device, &samplerInfo, NULL, &state->shadowSampler) != VK_SUCCESS)
		return false;

	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	if (vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &state->shadowCache) != VK_SUCCESS)
		state->shadowCache = VK_NULL_HANDLE;

	bool useMesh = ctx->deviceCapabilities.meshShader;
	bool useTask = state->taskCull;
	VkShaderStageFlagBits geometryStage = useMesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;

	// Depth-only geometry variant (ANO_DEPTH_ONLY compile of flat.mesh/flat.vert).
	// Unwind idiom: buffers/modules inert at declare; goto fail discharges live. Masked pair too.
	// loadFile refuse: buffer indeterminate -> re-inert before unwind.
	struct Buffer geomCode = {}, fragCode = {}, mGeomCode = {}, mFragCode = {};
	VkShaderModule geomModule = VK_NULL_HANDLE, fragModule = VK_NULL_HANDLE, taskModule = VK_NULL_HANDLE,
		mGeomModule = VK_NULL_HANDLE, mFragModule = VK_NULL_HANDLE;

	char path[64];
	bool depthOk = [&]() -> bool {
	snprintf(path, sizeof(path), "resources/shaders/%s.spv",
		useMesh ? (useTask ? "flat_depth_task.mesh" : "flat_depth.mesh") : "flat_depth.vert");
	if (!loadFile(path, &geomCode)) { geomCode.data = NULL; return false; }
	if (!loadFile("resources/shaders/shadow_depth.frag.spv", &fragCode)) { fragCode.data = NULL; return false; }
	geomModule = createShaderModule(ctx->device, &geomCode);
	fragModule = createShaderModule(ctx->device, &fragCode);

	// Task meshlet cull, shadow variant: frustum-only.
	TaskStageStorage taskStore;
	VkPipelineShaderStageCreateInfo taskStageInfo = {};
	if (useTask && !ano_pipeline_task_stage(ctx, VK_TRUE, VK_FALSE, &taskStore, &taskModule, &taskStageInfo))
		return false;

	// shadowPass = true (constant_id 0 in flat.mesh / flat.vert).
	VkBool32 shadowPassTrue = VK_TRUE;
	VkSpecializationMapEntry specEntry = { .constantID = 0, .offset = 0, .size = sizeof(VkBool32) };
	VkSpecializationInfo specInfo = { .mapEntryCount = 1, .pMapEntries = &specEntry, .dataSize = sizeof(VkBool32), .pData = &shadowPassTrue };

	VkPipelineShaderStageCreateInfo stages[3] = {};
	stages[0] = taskStageInfo; // leading task slot
	if (!ano_pipeline_stage(geometryStage, geomModule, &specInfo, &stages[1])
		|| !ano_pipeline_stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, NULL, &stages[2]))
		return false;

	constexpr auto contract = ano_graphics_contract_checked<shadow_depth_contract()>();
	GraphicsPipelineStorage store;
	ano_graphics_pipeline_materialize(contract, useMesh, &store);

	// Runtime patches: stages, layout.
	VkGraphicsPipelineCreateInfo& pipelineInfo = store.pipelineInfo;
	pipelineInfo.stageCount = useTask ? 3 : 2;
	pipelineInfo.pStages = useTask ? stages : &stages[1];
	pipelineInfo.layout = state->prototypes[PIPELINE_FLAT].layout; // flat's sets 0/1/2 + push

	VkResult r = vkCreateGraphicsPipelines(ctx->device, state->shadowCache, 1, &pipelineInfo, NULL, &state->shadowPipeline);

	// Alpha-tested caster variant: ANO_DEPTH_MASKED geometry + shadow_depth_masked.frag.
	// Shares fixed state, only stages differ. Must precede the shader frees (reuses taskModule).
	VkResult mr = VK_SUCCESS;
	if (r == VK_SUCCESS) {
		snprintf(path, sizeof(path), "resources/shaders/%s.spv",
			useMesh ? (useTask ? "flat_depth_masked_task.mesh" : "flat_depth_masked.mesh") : "flat_depth_masked.vert");
		if (!loadFile(path, &mGeomCode)) { mGeomCode.data = NULL; return false; }
		if (!loadFile("resources/shaders/shadow_depth_masked.frag.spv", &mFragCode)) { mFragCode.data = NULL; return false; }
		mGeomModule = createShaderModule(ctx->device, &mGeomCode);
		mFragModule = createShaderModule(ctx->device, &mFragCode);

		VkPipelineShaderStageCreateInfo mStages[3] = { stages[0], stages[1], stages[2] };
		// Same shadowPass spec info, masked modules.
		if (!ano_pipeline_stage(geometryStage, mGeomModule, &specInfo, &mStages[1])
			|| !ano_pipeline_stage(VK_SHADER_STAGE_FRAGMENT_BIT, mFragModule, NULL, &mStages[2]))
			return false;
		pipelineInfo.pStages = useTask ? mStages : &mStages[1];

		mr = vkCreateGraphicsPipelines(ctx->device, state->shadowCache, 1, &pipelineInfo, NULL, &state->shadowPipelineMasked);

	}

	if (r != VK_SUCCESS) { ano::log(ano::Fatal, "Failed to create shadow depth pipeline!"); return false; }
	if (mr != VK_SUCCESS) { ano::log(ano::Fatal, "Failed to create masked shadow depth pipeline!"); return false; }
	return true;
	}();

	ano_aligned_free(geomCode.data);
	ano_aligned_free(fragCode.data);
	ano_aligned_free(mGeomCode.data);
	ano_aligned_free(mFragCode.data);
	vkDestroyShaderModule(ctx->device, geomModule, NULL);
	vkDestroyShaderModule(ctx->device, fragModule, NULL);
	vkDestroyShaderModule(ctx->device, mGeomModule, NULL);
	vkDestroyShaderModule(ctx->device, mFragModule, NULL);
	vkDestroyShaderModule(ctx->device, taskModule, NULL);
	if (!depthOk) return false;

	// --- Moment prefilter pipeline: fullscreen separable box over the atlas (X then Y) ---
	// One combined-image-sampler at set 0 + 16-byte push (dir + layer).
	// Vertex: shadowblur.vert (layered) with vertex-stage gl_Layer, else tonemap.vert. Frag: shadowblur.frag.
	const auto& blurSpecs = ANO_VK_SHADOW_BLUR_BINDINGS;
	VkDescriptorSetLayoutBinding blurBindings[ANO_VK_SHADOW_BLUR_BINDINGS.count] = {};
	if (!ano_vk_materialize_layout_bindings(blurSpecs, blurBindings))
		return false;
	VkDescriptorSetLayoutCreateInfo blurSetInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = blurSpecs.count, .pBindings = blurBindings };
	if (vkCreateDescriptorSetLayout(ctx->device, &blurSetInfo, NULL, &state->shadowBlurSetLayout) != VK_SUCCESS) {
		ano::log(ano::Fatal, "Failed to create shadow blur set layout!"); return false; }

	// vec2 dir + int layer + int pad. VERTEX for shadowblur.vert's gl_Layer routing.
	VkPushConstantRange blurPush = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16 };
	VkPipelineLayoutCreateInfo blurLayoutInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1, .pSetLayouts = &state->shadowBlurSetLayout,
		.pushConstantRangeCount = 1, .pPushConstantRanges = &blurPush };
	if (vkCreatePipelineLayout(ctx->device, &blurLayoutInfo, NULL, &state->shadowBlurLayout) != VK_SUCCESS) {
		ano::log(ano::Fatal, "Failed to create shadow blur pipeline layout!"); return false; }

	// Blur phase: resources remain inert until acquired, then discharge once.
	struct Buffer blurVertCode = {}, blurFragCode = {};
	VkShaderModule blurVert = VK_NULL_HANDLE, blurFrag = VK_NULL_HANDLE;
	bool blurOk = [&]() -> bool {
	snprintf(path, sizeof(path), "resources/shaders/%s.spv",
		ctx->deviceCapabilities.shaderOutputLayer ? "shadowblur.vert" : "tonemap.vert");
	if (!loadFile(path, &blurVertCode)) { blurVertCode.data = NULL; return false; }
	if (!loadFile("resources/shaders/shadowblur.frag.spv", &blurFragCode)) { blurFragCode.data = NULL; return false; }
	blurVert = createShaderModule(ctx->device, &blurVertCode);
	blurFrag = createShaderModule(ctx->device, &blurFragCode);

	VkPipelineShaderStageCreateInfo blurStages[2];
	if (!ano_pipeline_stage(VK_SHADER_STAGE_VERTEX_BIT, blurVert, NULL, &blurStages[0])
		|| !ano_pipeline_stage(VK_SHADER_STAGE_FRAGMENT_BIT, blurFrag, NULL, &blurStages[1]))
		return false;

	constexpr auto blurContract = ano_graphics_contract_checked<shadow_blur_contract()>();
	GraphicsPipelineStorage blurStore;
	ano_graphics_pipeline_materialize(blurContract, false, &blurStore);

	// Runtime patches: stages, layout.
	VkGraphicsPipelineCreateInfo& blurPipeline = blurStore.pipelineInfo;
	blurPipeline.stageCount = 2; blurPipeline.pStages = blurStages;
	blurPipeline.layout = state->shadowBlurLayout;

	return vkCreateGraphicsPipelines(ctx->device, state->shadowCache, 1, &blurPipeline, NULL,
	                                 &state->shadowBlurPipeline) == VK_SUCCESS;
	}();
	if (!blurOk) ano::log(ano::Fatal, "Failed to create shadow blur pipeline!");

	// Both paths, unconditional.
	ano_aligned_free(blurVertCode.data);
	ano_aligned_free(blurFragCode.data);
	vkDestroyShaderModule(ctx->device, blurVert, NULL);
	vkDestroyShaderModule(ctx->device, blurFrag, NULL);
	return blurOk;

}
