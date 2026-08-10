/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#include <anoptic_log.h>
#include "transmission.h"
#include "vulkan_backend/instance/pipeline.h"
#include "vulkan_backend/pipeline_registry.h"
#include "graphics_contract.h"
#include <stdlib.h>

// Opaque-lane deltas for the shared graphics ladder; the blended variant (index 1) is
// minted by the explicit mutations between the create calls.
static consteval AnoGraphicsPipelineContract transmission_contract()
{
	return {
		.cullMode = VK_CULL_MODE_NONE,
		.msaaFromContext = true,
		.minSampleShading = 1.0f,
		.depthTest = VK_TRUE,
		.depthWrite = VK_TRUE,
		.depthCompare = VK_COMPARE_OP_LESS,
		.maxDepthBounds = 1.0f,
		.logicOp = VK_LOGIC_OP_COPY,
		.colorAttachmentCount = 1,
		.blend = {
			{ .blendEnable = VK_FALSE,
			  .srcColorBlendFactor = VK_BLEND_FACTOR_ONE, .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
			  .colorBlendOp = VK_BLEND_OP_ADD,
			  .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
			  .alphaBlendOp = VK_BLEND_OP_ADD,
			  .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT },
		},
		.colorFormats = { ANO_HDR_COLOR_FORMAT }, // HDR target
		.depthFormatFromState = true,
		.staticViewport = true,
		.meshCapable = true,
	};
}

// Transmission lane: opaque (index 0) + blended (index 1) variants.
// Cache refuse -> VK_NULL_HANDLE. Commit-last on implementationCount.
bool ano_pipeline_transmission_init(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto)
{
	// 1. Setup cache
	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	if (vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &proto->cache) != VK_SUCCESS)
		proto->cache = VK_NULL_HANDLE;

	// Mesh stage on capable devices, vertex stage on fallback.
	bool useMesh = ctx->deviceCapabilities.meshShader;
	bool useTask = state->taskCull;
	VkShaderStageFlagBits geometryStage = useMesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;

	// 2. Setup layout
	VkPushConstantRange pushConstantRange = {};
	pushConstantRange.stageFlags = geometryStage | VK_SHADER_STAGE_FRAGMENT_BIT | (useTask ? VK_SHADER_STAGE_TASK_BIT_EXT : 0);
	pushConstantRange.offset = 0;
	pushConstantRange.size = 2u * sizeof(uint32_t); // transformBaseOffset + shadowFrustumIndex

	// Set 2 = dynamic shadows.
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 3;
	VkDescriptorSetLayout setLayouts[3] = {state->globalSetLayout, proto->descriptorLayout, state->shadowGeomSetLayout};
	pipelineLayoutInfo.pSetLayouts = setLayouts;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(ctx->device, &pipelineLayoutInfo, NULL, &proto->layout) != VK_SUCCESS) 
	{
		ano_log(ANO_FATAL, "Failed to create transmission pipeline layout!");
		return false;
	}

	if (!ano_pipeline_prepare_prototype(proto, PIPELINE_TRANSMISSION))
		return false;

	// Load shaders: mesh on capable devices, vertex on fallback.
	// Unwind: goto fail discharges all. Clear dangling buffer on load refuse.
	struct Buffer geomShaderCode = {}, fragShaderCode = {};
	VkShaderModule geomShaderModule = VK_NULL_HANDLE, fragShaderModule = VK_NULL_HANDLE;
	VkShaderModule taskModule = VK_NULL_HANDLE;

	bool ok = [&]() -> bool {
		if (!loadFile(ano_pipeline_geometry_shader_path<PIPELINE_TRANSMISSION>(useMesh, useTask),
				&geomShaderCode)) { geomShaderCode.data = NULL; return false; }

		// fp16 CDF-reconstruct variant when shaderFloat16 available.
		if (!loadFile(ano_pipeline_fragment_shader_path<PIPELINE_TRANSMISSION>(
				ctx->deviceCapabilities.shaderFloat16), &fragShaderCode))
			{ fragShaderCode.data = NULL; return false; }

	geomShaderModule = createShaderModule(ctx->device, &geomShaderCode);
	fragShaderModule = createShaderModule(ctx->device, &fragShaderCode);

	TaskStageStorage taskStore;
	VkPipelineShaderStageCreateInfo taskStageInfo = {};
	if (useTask && !ano_pipeline_task_stage(ctx, VK_FALSE, VK_FALSE, &taskStore, &taskModule, &taskStageInfo))
		return false;

	VkPipelineShaderStageCreateInfo geomShaderStageInfo, fragShaderStageInfo;
	if (!ano_pipeline_stage(geometryStage, geomShaderModule, NULL, &geomShaderStageInfo)
		|| !ano_pipeline_stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule, NULL, &fragShaderStageInfo))
		return false;

	VkPipelineShaderStageCreateInfo shaderStages[3] = {taskStageInfo, geomShaderStageInfo, fragShaderStageInfo};
	VkPipelineShaderStageCreateInfo* stageList = useTask ? shaderStages : &shaderStages[1];
	uint32_t stageListCount = useTask ? 3u : 2u;

	// Fallback vertex: pull + triangle list.
	constexpr auto contract = ano_graphics_frame_contract_checked<PIPELINE_TRANSMISSION, transmission_contract()>();
	GraphicsPipelineStorage store;
	ano_graphics_pipeline_materialize(contract, useMesh, &store);

	// Runtime patches: extent, MSAA sample count, depth format, stages, layout.
	store.viewport.width = (float) state->imageExtent.width;
	store.viewport.height = (float) state->imageExtent.height;
	store.scissor.extent = state->imageExtent;
	store.multisampling.rasterizationSamples = ctx->msaaSamples;
	store.rendering.depthAttachmentFormat = state->depthFormat;

	// Variant mutations below write through these; the create calls read store.pipelineInfo.
	VkPipelineDepthStencilStateCreateInfo& depthStencil = store.depthStencil;
	VkPipelineColorBlendAttachmentState& colorBlendAttachment = store.blendAttachments[0];
	VkPipelineColorBlendStateCreateInfo& colorBlending = store.colorBlending;

	VkGraphicsPipelineCreateInfo& pipelineInfo = store.pipelineInfo;
	pipelineInfo.stageCount = stageListCount;
	pipelineInfo.pStages = stageList;
	pipelineInfo.layout = proto->layout;

	// Opaque variant (index 0)
	if (vkCreateGraphicsPipelines(ctx->device, proto->cache, 1, &pipelineInfo, NULL, &proto->implementations[0].pipeline) != VK_SUCCESS) return false;
	proto->implementations[0].depthWrite = VK_TRUE;
	proto->implementations[0].blendEnable = VK_FALSE;

	// Blended variant (index 1)
	depthStencil.depthWriteEnable = VK_FALSE;
	colorBlendAttachment.blendEnable = VK_TRUE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	colorBlending.pAttachments = &colorBlendAttachment;
	
	if (vkCreateGraphicsPipelines(ctx->device, proto->cache, 1, &pipelineInfo, NULL, &proto->implementations[1].pipeline) != VK_SUCCESS) return false;
	proto->implementations[1].depthWrite = VK_FALSE;
	proto->implementations[1].blendEnable = VK_TRUE;

	return true;
	}();

	ano_aligned_free(geomShaderCode.data);
	ano_aligned_free(fragShaderCode.data);
	vkDestroyShaderModule(ctx->device, geomShaderModule, NULL);
	vkDestroyShaderModule(ctx->device, fragShaderModule, NULL);
	vkDestroyShaderModule(ctx->device, taskModule, NULL);
	return ok;

}
