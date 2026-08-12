/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory.h>
#include <anoptic_filesystem.h>
#include <anoptic_log.h>
#include "vulkan_backend/instance/descriptor_layout_schema.h"
#include "vulkan_backend/instance/pipeline.h"
#include "graphics_contract.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include <vulkan/vulkan.h>

// Fullscreen encode deltas for the shared graphics ladder.
static consteval AnoGraphicsPipelineContract tonemap_contract()
{
	return {
		.cullMode = VK_CULL_MODE_NONE,
		// Writes single-sample swapchain directly, no MSAA.
		.msaaFromContext = false,
		.colorAttachmentCount = 1,
		.blend = {
			{ .blendEnable = VK_FALSE,
			  .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT },
		},
		// Swapchain LDR format, no depth attachment.
		.colorFormatFromState = true,
	};
}

// Fullscreen tonemap pass: encodes the HDR resolve target to the swapchain.
// in:  ctx, state (imageFormat = swapchain target format must be set)
// out: true on success; populates state->tonemap{SetLayout,Layout,Cache,Pipeline}
// Cache idiom: refused mint -> VK_NULL_HANDLE, init continues.
bool ano_vk_init_tonemap(VulkanContext* ctx, RendererState* state)
{
	const auto& specs = ANO_VK_TONEMAP_BINDINGS;
	VkDescriptorSetLayoutBinding bindings[ANO_VK_TONEMAP_BINDINGS.count] = {};
	if (!ano_vk_materialize_layout_bindings(specs, bindings))
		return false;

	VkDescriptorSetLayoutCreateInfo setLayoutInfo = {};
	setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setLayoutInfo.bindingCount = specs.count;
	setLayoutInfo.pBindings = bindings;
	if (vkCreateDescriptorSetLayout(ctx->device, &setLayoutInfo, NULL, &state->tonemapSetLayout) != VK_SUCCESS)
	{
		ano::log(ano::Fatal, "Failed to create tonemap descriptor set layout!");
		return false;
	}

	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &state->tonemapSetLayout;
	if (vkCreatePipelineLayout(ctx->device, &layoutInfo, NULL, &state->tonemapLayout) != VK_SUCCESS)
	{
		ano::log(ano::Fatal, "Failed to create tonemap pipeline layout!");
		return false;
	}

	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	if (vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &state->tonemapCache) != VK_SUCCESS)
		state->tonemapCache = VK_NULL_HANDLE;

	// Unwind idiom: hold blobs/modules until fail/tail; refused load clears buffer (loadFile dangles on short read).
	struct Buffer vertCode = {}, fragCode = {};
	VkShaderModule vertModule = VK_NULL_HANDLE, fragModule = VK_NULL_HANDLE;

	bool ok = [&]() -> bool {
	if (!loadFile("resources/shaders/tonemap.vert.spv", &vertCode)) { vertCode.data = NULL; return false; }
	if (!loadFile("resources/shaders/tonemap.frag.spv", &fragCode)) { fragCode.data = NULL; return false; }
	vertModule = createShaderModule(ctx->device, &vertCode);
	fragModule = createShaderModule(ctx->device, &fragCode);

	VkPipelineShaderStageCreateInfo stages[2];
	if (!ano_pipeline_stage(VK_SHADER_STAGE_VERTEX_BIT, vertModule, NULL, &stages[0])
		|| !ano_pipeline_stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, NULL, &stages[1]))
		return false;

	// No vertex buffers, fullscreen triangle from gl_VertexIndex.
	constexpr auto contract = ano_graphics_contract_checked<tonemap_contract()>();
	GraphicsPipelineStorage store;
	ano_graphics_pipeline_materialize(contract, false, &store);

	// Runtime patches: swapchain format, stages, layout.
	store.colorFormats[0] = state->imageFormat;
	VkGraphicsPipelineCreateInfo& pipelineInfo = store.pipelineInfo;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = stages;
	pipelineInfo.layout = state->tonemapLayout;

	VkResult r = vkCreateGraphicsPipelines(ctx->device, state->tonemapCache, 1, &pipelineInfo, NULL, &state->tonemapPipeline);
	return r == VK_SUCCESS;
	}();

	ano_aligned_free(vertCode.data);
	ano_aligned_free(fragCode.data);
	vkDestroyShaderModule(ctx->device, vertModule, NULL);
	vkDestroyShaderModule(ctx->device, fragModule, NULL);

	if (!ok)
		ano::log(ano::Fatal, "Failed to create tonemap pipeline!");
	return ok;

}
