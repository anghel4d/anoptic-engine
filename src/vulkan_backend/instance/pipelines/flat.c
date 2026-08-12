/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
#include <ano/log.h>
#include "flat.h"
#include "vulkan_backend/instance/pipeline.h"
#include "vulkan_backend/pipeline_registry.h"
#include "graphics_contract.h"
#include <stdlib.h>

// Per-lane deltas for the shared graphics ladder; runtime patches and the between-variant
// mutations stay in flat_init_with_cull.
template<PipelineType Type>
consteval AnoGraphicsPipelineContract flat_lane_contract()
{
	constexpr auto spec = ano_pipeline_spec<Type>();
	constexpr bool masked = spec.shader == AnoShaderFamily::flat_masked;
	return {
		// frontFace COUNTER_CLOCKWISE on both lanes.
		.cullMode = Type == PIPELINE_FLAT ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE,
		.msaaFromContext = true,
		.minSampleShading = 1.0f,
		// Masked lane: alpha-to-coverage dithers cutout alpha across the MSAA samples.
		.alphaToCoverage = masked ? VK_TRUE : VK_FALSE,
		// Opaque variant (index 0): EQUAL test, no depth write (the pre-pass owns depth).
		// Masked lane has no pre-pass: its variant owns depth with LESS + write.
		.depthTest = VK_TRUE,
		.depthWrite = masked ? VK_TRUE : VK_FALSE,
		.depthCompare = masked ? VK_COMPARE_OP_LESS : VK_COMPARE_OP_EQUAL,
		.maxDepthBounds = 1.0f,
		.logicOp = VK_LOGIC_OP_COPY,
		// Two color attachments: [0] HDR color, [1] R32_UINT picking id.
		.colorAttachmentCount = 2,
		.blend = {
			{ .blendEnable = VK_FALSE,
			  .srcColorBlendFactor = VK_BLEND_FACTOR_ONE, .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
			  .colorBlendOp = VK_BLEND_OP_ADD,
			  .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
			  .alphaBlendOp = VK_BLEND_OP_ADD,
			  .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT },
			{ .blendEnable = VK_FALSE,
			  .colorWriteMask = VK_COLOR_COMPONENT_R_BIT }, // opaque variant writes the id
		},
		// [0] HDR target, [1] R32_UINT picking id.
		.colorFormats = { ANO_HDR_COLOR_FORMAT, VK_FORMAT_R32_UINT },
		.depthFormatFromState = true,
		.staticViewport = true,
		.meshCapable = true,
	};
}

// Shared builder for the reflected flat lanes.
// Cache idiom: a pipeline cache is an optimization and VK_NULL_HANDLE is a legal pipelineCache
// argument, so a refused mint zeroes the handle and the build carries on.
// Commit-last idiom: implementationCount is published only once its array exists.
template<PipelineType Type>
static bool flat_init_with_cull(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto)
{
	static_assert(Type == PIPELINE_FLAT || Type == PIPELINE_FLAT_TWOSIDED || Type == PIPELINE_FLAT_MASKED);
	constexpr auto contract = ano_graphics_frame_contract_checked<Type, flat_lane_contract<Type>()>();

	// 1. Setup cache
	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	if (vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &proto->cache) != VK_SUCCESS)
		proto->cache = VK_NULL_HANDLE;

	// Mesh stage on capable devices, vertex stage on the fallback path.
	// Task cull prepends a flat.task stage to every mesh-path variant.
	bool useMesh = ctx->deviceCapabilities.meshShader;
	bool useTask = state->taskCull;
	VkShaderStageFlagBits geometryStage = useMesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;

	// 2. Setup layout
	// Push: transformBaseOffset + shadowFrustumIndex. Task stage flag joins the range.
	VkPushConstantRange pushConstantRange = {};
	// FRAGMENT joins the range: shadow_depth.frag reads shadowFrustumIndex.
	pushConstantRange.stageFlags = geometryStage | VK_SHADER_STAGE_FRAGMENT_BIT | (useTask ? VK_SHADER_STAGE_TASK_BIT_EXT : 0);
	pushConstantRange.offset = 0;
	pushConstantRange.size = 2u * sizeof(uint32_t);

	// Set 2 = dynamic shadows: geometry reads shadow viewProj, fragment samples the shadow atlas.
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 3;
	VkDescriptorSetLayout setLayouts[3] = {state->globalSetLayout, proto->descriptorLayout, state->shadowGeomSetLayout};
	pipelineLayoutInfo.pSetLayouts = setLayouts;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(ctx->device, &pipelineLayoutInfo, NULL, &proto->layout) != VK_SUCCESS) 
	{
		ano::log(ano::Fatal, "Failed to create flat pipeline layout!");
		return false;
	}

	if (!ano_pipeline_prepare_prototype(proto, Type))
		return false;

	// Load shaders: mesh shader on capable devices, vertex shader on the fallback.
	// Depth pre-pass variant (index 2) uses the ANO_DEPTH_ONLY compile of the same source.
	// Paths are exe-relative; loadFile resolves them against ano_fs_gamepath().
	// Unwind idiom: every buffer and module is inert at declaration, so any arm past this point
	// returns from a build lambda, then discharges whatever is live. loadFile leaves its buffer
	// indeterminate when it refuses, so a refused load re-inerts before unwinding.
	struct Buffer geomShaderCode = {}, depthGeomShaderCode = {}, fragShaderCode = {};
	VkShaderModule geomShaderModule = VK_NULL_HANDLE, depthGeomShaderModule = VK_NULL_HANDLE,
		fragShaderModule = VK_NULL_HANDLE, taskModule = VK_NULL_HANDLE;

	bool ok = [&]() -> bool {
		if (!loadFile(ano_pipeline_geometry_shader_path<Type>(useMesh, useTask), &geomShaderCode))
			{ geomShaderCode.data = NULL; return false; }
		if (!loadFile(ano_pipeline_geometry_shader_path<Type>(useMesh, useTask, true), &depthGeomShaderCode))
			{ depthGeomShaderCode.data = NULL; return false; }

	// fp16 variant when the device has shaderFloat16; masked lane loads the ANO_ALPHA_MASK compile.
		const char* fragPath =
			ano_pipeline_fragment_shader_path<Type>(ctx->deviceCapabilities.shaderFloat16);
	if (!loadFile(fragPath, &fragShaderCode)) { fragShaderCode.data = NULL; return false; }

	geomShaderModule = createShaderModule(ctx->device, &geomShaderCode);
	depthGeomShaderModule = createShaderModule(ctx->device, &depthGeomShaderCode);
	fragShaderModule = createShaderModule(ctx->device, &fragShaderCode);

	// Task meshlet-cull stage: cone culling only on the BACK-culled lane.
	TaskStageStorage taskStore;
	VkPipelineShaderStageCreateInfo taskStageInfo = {};
	if (useTask && !ano_pipeline_task_stage(ctx, VK_FALSE,
			contract.cullMode == VK_CULL_MODE_BACK_BIT ? VK_TRUE : VK_FALSE,
			&taskStore, &taskModule, &taskStageInfo))
		return false;

	// One gate for all three mints: the depth stage is minted complete here, not patched later.
	VkPipelineShaderStageCreateInfo geomShaderStageInfo, depthGeomStageInfo, fragShaderStageInfo;
	if (!ano_pipeline_stage(geometryStage, geomShaderModule, NULL, &geomShaderStageInfo)
		|| !ano_pipeline_stage(geometryStage, depthGeomShaderModule, NULL, &depthGeomStageInfo)
		|| !ano_pipeline_stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule, NULL, &fragShaderStageInfo))
		return false;

	// [task,] geom, frag stage array, task slot first.
	VkPipelineShaderStageCreateInfo shaderStages[3] = {taskStageInfo, geomShaderStageInfo, fragShaderStageInfo};
	VkPipelineShaderStageCreateInfo* colorStages = useTask ? shaderStages : &shaderStages[1];
	uint32_t colorStageCount = useTask ? 3u : 2u;

	// Mesh path needs no vertex-input or input-assembly state.
	// Vertex fallback: empty vertex-input (programmable pulling) + triangle-list assembly.
	GraphicsPipelineStorage store;
	ano_graphics_pipeline_materialize(contract, useMesh, &store);

	// Runtime patches: extent, MSAA sample count, depth format, stages, layout.
	store.viewport.width = (float) state->imageExtent.width;
	store.viewport.height = (float) state->imageExtent.height;
	store.scissor.extent = state->imageExtent;
	store.multisampling.rasterizationSamples = ctx->msaaSamples;
	VkFormat depthFormat = state->depthFormat;
	store.rendering.depthAttachmentFormat = depthFormat;

	// Variant mutations below write through these; the create calls read store.pipelineInfo.
	VkPipelineDepthStencilStateCreateInfo& depthStencil = store.depthStencil;
	VkPipelineMultisampleStateCreateInfo& multisampling = store.multisampling;
	auto& blendAttachments = store.blendAttachments;
	VkPipelineColorBlendAttachmentState* colorBlendAttachment = &blendAttachments[0];

	VkGraphicsPipelineCreateInfo& pipelineInfo = store.pipelineInfo;
	pipelineInfo.stageCount = colorStageCount;
	pipelineInfo.pStages = colorStages;
	pipelineInfo.layout = proto->layout;

	// Opaque variant (index 0): EQUAL, no write. Masked: LESS + write + alpha-to-coverage.
	if (vkCreateGraphicsPipelines(ctx->device, proto->cache, 1, &pipelineInfo, NULL, &proto->implementations[0].pipeline) != VK_SUCCESS) return false;
		proto->implementations[0].depthWrite = contract.depthWrite;
	proto->implementations[0].blendEnable = VK_FALSE;

	// Blended variant (index 1): mask off the id write, restore LESS.
	depthStencil.depthWriteEnable = VK_FALSE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	colorBlendAttachment->blendEnable = VK_TRUE;
	colorBlendAttachment->srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment->dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment->colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment->srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment->dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment->alphaBlendOp = VK_BLEND_OP_ADD;
	blendAttachments[1].colorWriteMask = 0; // id unwritten

	if (vkCreateGraphicsPipelines(ctx->device, proto->cache, 1, &pipelineInfo, NULL, &proto->implementations[1].pipeline) != VK_SUCCESS) return false;
		proto->implementations[1].depthWrite = VK_FALSE;
	proto->implementations[1].blendEnable = VK_TRUE;

	// Depth pre-pass variant (index 2): ANO_DEPTH_ONLY geometry, no fragment stage, no color attachments.
	// depthWrite ON + LESS. Same task module/spec as the color variants.
	VkPipelineShaderStageCreateInfo depthStages[2] = {taskStageInfo, depthGeomStageInfo};

	VkPipelineDepthStencilStateCreateInfo prepassDepth = depthStencil;
	prepassDepth.depthWriteEnable = VK_TRUE;
	prepassDepth.depthCompareOp = VK_COMPARE_OP_LESS;

	VkPipelineColorBlendStateCreateInfo prepassBlend = {};
	prepassBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	prepassBlend.attachmentCount = 0;

	VkPipelineRenderingCreateInfo prepassRendering = {};
	prepassRendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	prepassRendering.colorAttachmentCount = 0;
	prepassRendering.depthAttachmentFormat = depthFormat;

	// No fragment stage: disable alpha-to-coverage (unwritten alpha).
	VkPipelineMultisampleStateCreateInfo prepassMs = multisampling;
	prepassMs.alphaToCoverageEnable = VK_FALSE;

	VkGraphicsPipelineCreateInfo prepassInfo = pipelineInfo; // inherit shared raster/viewport/msaa/layout
	prepassInfo.pNext = &prepassRendering;
	prepassInfo.stageCount = useTask ? 2 : 1;
	prepassInfo.pStages = useTask ? depthStages : &depthStages[1];
	prepassInfo.pDepthStencilState = &prepassDepth;
	prepassInfo.pColorBlendState = &prepassBlend;
	prepassInfo.pMultisampleState = &prepassMs;

	if (vkCreateGraphicsPipelines(ctx->device, proto->cache, 1, &prepassInfo, NULL, &proto->implementations[2].pipeline) != VK_SUCCESS) return false;
		proto->implementations[2].depthWrite = VK_TRUE;
	proto->implementations[2].blendEnable = VK_FALSE;

	return true;
	}();

	// Both paths, unconditional. Pipelines and the layout stay for ano_pipeline_flat_cleanup.
	ano_aligned_free(geomShaderCode.data);
	ano_aligned_free(depthGeomShaderCode.data);
	ano_aligned_free(fragShaderCode.data);
	vkDestroyShaderModule(ctx->device, geomShaderModule, NULL);
	vkDestroyShaderModule(ctx->device, depthGeomShaderModule, NULL);
	vkDestroyShaderModule(ctx->device, fragShaderModule, NULL);
	vkDestroyShaderModule(ctx->device, taskModule, NULL);
	return ok;
}

bool ano_pipeline_flat_init(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto)
{
	return flat_init_with_cull<PIPELINE_FLAT>(ctx, state, proto);
}

bool ano_pipeline_flat_twosided_init(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto)
{
	return flat_init_with_cull<PIPELINE_FLAT_TWOSIDED>(ctx, state, proto);
}

bool ano_pipeline_flat_masked_init(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto)
{
	return flat_init_with_cull<PIPELINE_FLAT_MASKED>(ctx, state, proto);
}

void ano_pipeline_flat_cleanup(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto)
{
	(void)state;
	if (proto->cache != VK_NULL_HANDLE)
	{
		vkDestroyPipelineCache(ctx->device, proto->cache, NULL);
		proto->cache = VK_NULL_HANDLE;
	}

	if (proto->layout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(ctx->device, proto->layout, NULL);
		proto->layout = VK_NULL_HANDLE;
	}

	if (proto->implementations != NULL)
	{
		// Dissolve the pair count-first, mirroring the builder's commit-last: no observable point
		// has implementationCount > 0 beside an array that is gone.
		uint32_t count = proto->implementationCount;
		proto->implementationCount = 0;
		for (uint32_t j = 0; j < count; ++j)
		{
			if (proto->implementations[j].pipeline != VK_NULL_HANDLE)
			{
				vkDestroyPipeline(ctx->device, proto->implementations[j].pipeline, NULL);
				proto->implementations[j].pipeline = VK_NULL_HANDLE;
			}
		}
		free(proto->implementations);
		proto->implementations = NULL;
	}
}
