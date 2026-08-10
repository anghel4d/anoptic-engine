/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#ifndef ANOPTIC_GRAPHICS_CONTRACT_H
#define ANOPTIC_GRAPHICS_CONTRACT_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "vulkan_backend/pipeline_registry.h"
#include "vulkan_backend/frame/schema/pass_contract.h"

// One graphics-builder ladder, compiled from per-lane deltas.
// AnoGraphicsPipelineContract is the constexpr delta set between the graphics builders'
// otherwise identical viewport/raster/msaa/blend/depth/rendering ladders.
// GraphicsPipelineStorage owns every sub-struct, so every create-info pointer is stable.
// ano_graphics_pipeline_materialize fills storage from a contract; the builder then patches
// runtime-only values (shader stages, extent, ctx->msaaSamples, state-sourced formats, layout)
// and keeps any between-variant mutations explicit at its create calls.
// Dynamic state is invariant across every lane: viewport + scissor.

inline constexpr uint32_t ANO_GRAPHICS_MAX_COLOR_ATTACHMENTS = 2;

struct AnoGraphicsPipelineContract final {
    VkCullModeFlags cullMode;
    bool msaaFromContext;      // builder patches rasterizationSamples = ctx->msaaSamples; else single-sample
    float minSampleShading;    // carried verbatim; sampleShadingEnable stays FALSE on every lane
    VkBool32 alphaToCoverage;
    VkBool32 depthTest;
    VkBool32 depthWrite;
    VkCompareOp depthCompare;
    float maxDepthBounds;
    VkLogicOp logicOp;         // logicOpEnable stays FALSE on every lane
    uint32_t colorAttachmentCount;
    VkPipelineColorBlendAttachmentState blend[ANO_GRAPHICS_MAX_COLOR_ATTACHMENTS];
    VkFormat colorFormats[ANO_GRAPHICS_MAX_COLOR_ATTACHMENTS];
    bool colorFormatFromState; // builder patches colorFormats[0] from state (swapchain format)
    VkFormat depthFormat;      // compile-time depth target; UNDEFINED when absent or state-sourced
    bool depthFormatFromState; // builder patches depthAttachmentFormat = state->depthFormat
    bool staticViewport;       // bake pViewports/pScissors; builder patches the extent
    bool meshCapable;          // mesh path strips vertex-input/input-assembly at build time
};

struct GraphicsPipelineStorage final {
    VkViewport viewport;
    VkRect2D scissor;
    VkPipelineViewportStateCreateInfo viewportState;
    VkPipelineRasterizationStateCreateInfo rasterizer;
    VkPipelineMultisampleStateCreateInfo multisampling;
    VkPipelineColorBlendAttachmentState blendAttachments[ANO_GRAPHICS_MAX_COLOR_ATTACHMENTS];
    VkPipelineColorBlendStateCreateInfo colorBlending;
    VkDynamicState dynamicStates[2];
    VkPipelineDynamicStateCreateInfo dynamicState;
    VkPipelineDepthStencilStateCreateInfo depthStencil;
    VkFormat colorFormats[ANO_GRAPHICS_MAX_COLOR_ATTACHMENTS];
    VkPipelineRenderingCreateInfo rendering;
    VkPipelineVertexInputStateCreateInfo vertexInput;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly;
    VkGraphicsPipelineCreateInfo pipelineInfo;
};

// True when any live attachment blends.
constexpr bool ano_graphics_contract_blends(const AnoGraphicsPipelineContract& c)
{
    for (uint32_t i = 0; i < c.colorAttachmentCount; ++i)
        if (c.blend[i].blendEnable == VK_TRUE)
            return true;
    return false;
}

// Every live attachment format is compile-time known or declared runtime-patched.
constexpr bool ano_graphics_contract_formats_resolved(const AnoGraphicsPipelineContract& c)
{
    for (uint32_t i = 0; i < c.colorAttachmentCount; ++i)
        if (c.colorFormats[i] == VK_FORMAT_UNDEFINED && !(c.colorFormatFromState && i == 0))
            return false;
    return true;
}

// Every frame pass drawing this prototype with color output agrees on the attachment count.
constexpr bool ano_graphics_contract_matches_passes(PipelineType type, uint32_t colorAttachmentCount)
{
    bool seen = false;
    for (size_t i = 0; i < ANO_FRAME_PASS_REGISTRY.count; ++i) {
        const RenderPassDef& pass = ANO_FRAME_PASS_REGISTRY.values[i];
        if (pass.type != PASS_GRAPHICS || pass.prototype != type || pass.colorAttachmentCount == 0)
            continue;
        if (pass.colorAttachmentCount != colorAttachmentCount)
            return false;
        seen = true;
    }
    return seen;
}

// Lane-local invariants; a violation fails the build. Registry-backed frame lanes go through
// ano_graphics_frame_contract_checked instead, which lands here after its cross-checks.
template<AnoGraphicsPipelineContract C>
consteval AnoGraphicsPipelineContract ano_graphics_contract_checked()
{
    static_assert(C.colorAttachmentCount >= 1 && C.colorAttachmentCount <= ANO_GRAPHICS_MAX_COLOR_ATTACHMENTS);
    static_assert(ano_graphics_contract_formats_resolved(C));
    static_assert(!ano_graphics_contract_blends(C) || C.depthWrite == VK_FALSE,
        "blending lanes must leave the depth buffer read-only");
    static_assert(C.depthWrite == VK_FALSE || C.depthTest == VK_TRUE);
    static_assert(C.depthCompare != VK_COMPARE_OP_EQUAL || C.depthWrite == VK_FALSE,
        "EQUAL lanes ride a pre-pass depth buffer and must not write it");
    static_assert(!C.colorFormatFromState || C.colorAttachmentCount == 1);
    static_assert(!C.depthFormatFromState || C.depthFormat == VK_FORMAT_UNDEFINED);
    return C;
}

// Frame material lanes: cross-check the contract against ANO_PIPELINE_REGISTRY + pass schema.
template<PipelineType Type, AnoGraphicsPipelineContract C>
consteval AnoGraphicsPipelineContract ano_graphics_frame_contract_checked()
{
    constexpr AnoPipelineSpec spec = ano_pipeline_spec<Type>();
    static_assert(spec.kind == AnoPipelineKind::graphics);
    static_assert(C.meshCapable, "registry lanes ride the mesh/vertex dual geometry path");
    static_assert(C.msaaFromContext && C.staticViewport && C.depthFormatFromState);
    static_assert((C.alphaToCoverage == VK_TRUE) == (spec.shader == AnoShaderFamily::flat_masked),
        "alpha-to-coverage is the masked cutout family's dither, nothing else's");
    static_assert(ano_graphics_contract_matches_passes(Type, C.colorAttachmentCount),
        "contract attachment count disagrees with the frame pass schema");
    return ano_graphics_contract_checked<C>();
}

// Fill storage from a contract; pointer wiring lands inside storage, so storage must not move
// before vkCreateGraphicsPipelines.
// in:  c = checked contract; useMesh = runtime mesh path (false on fixed-vertex lanes)
// out: storage complete except runtime patches: stages, layout, extent (staticViewport),
//      rasterizationSamples (msaaFromContext), state-sourced formats.
static inline void ano_graphics_pipeline_materialize(const AnoGraphicsPipelineContract& c,
                                                     bool useMesh, GraphicsPipelineStorage* s)
{
    *s = {};
    s->viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    s->viewportState.viewportCount = 1;
    s->viewportState.scissorCount = 1;
    if (c.staticViewport) {
        s->viewport.maxDepth = 1.0f;
        s->viewportState.pViewports = &s->viewport;
        s->viewportState.pScissors = &s->scissor;
    }

    s->rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    s->rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    s->rasterizer.cullMode = c.cullMode;
    s->rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    s->rasterizer.lineWidth = 1.0f;

    s->multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    s->multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    s->multisampling.minSampleShading = c.minSampleShading;
    s->multisampling.alphaToCoverageEnable = c.alphaToCoverage;

    for (uint32_t i = 0; i < c.colorAttachmentCount; ++i)
        s->blendAttachments[i] = c.blend[i];
    s->colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    s->colorBlending.logicOp = c.logicOp;
    s->colorBlending.attachmentCount = c.colorAttachmentCount;
    s->colorBlending.pAttachments = s->blendAttachments;

    s->dynamicStates[0] = VK_DYNAMIC_STATE_VIEWPORT;
    s->dynamicStates[1] = VK_DYNAMIC_STATE_SCISSOR;
    s->dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    s->dynamicState.dynamicStateCount = 2;
    s->dynamicState.pDynamicStates = s->dynamicStates;

    s->depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    s->depthStencil.depthTestEnable = c.depthTest;
    s->depthStencil.depthWriteEnable = c.depthWrite;
    s->depthStencil.depthCompareOp = c.depthCompare;
    s->depthStencil.maxDepthBounds = c.maxDepthBounds;

    for (uint32_t i = 0; i < c.colorAttachmentCount; ++i)
        s->colorFormats[i] = c.colorFormats[i];
    s->rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    s->rendering.colorAttachmentCount = c.colorAttachmentCount;
    s->rendering.pColorAttachmentFormats = s->colorFormats;
    s->rendering.depthAttachmentFormat = c.depthFormat;

    s->vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    s->inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    s->inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    s->pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    s->pipelineInfo.pNext = &s->rendering;
    s->pipelineInfo.pVertexInputState = useMesh ? NULL : &s->vertexInput;
    s->pipelineInfo.pInputAssemblyState = useMesh ? NULL : &s->inputAssembly;
    s->pipelineInfo.pViewportState = &s->viewportState;
    s->pipelineInfo.pRasterizationState = &s->rasterizer;
    s->pipelineInfo.pMultisampleState = &s->multisampling;
    s->pipelineInfo.pDepthStencilState = &s->depthStencil;
    s->pipelineInfo.pColorBlendState = &s->colorBlending;
    s->pipelineInfo.pDynamicState = &s->dynamicState;
    s->pipelineInfo.renderPass = VK_NULL_HANDLE;
}

#endif
