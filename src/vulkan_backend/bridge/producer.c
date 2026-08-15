/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"

// Lock-free bridge producer.

RenderResult<AnoRenderBridge *> ano::anoRenderBridge(void)
{
    return result_if(rendererState.bridge.commands.buffer != nullptr,
                     &rendererState.bridge, RenderError::unavailable);
}

// Reserve next free transform-ring slice.
// inv: produceSeq unchanged
RenderResult<AnoStreamRegion> ano::render_stream_begin(void) {
    TransformStreamBuffer* ts = &rendererState.transformStream;
    uint64_t seq = ts->produceSeq + 1u;
    if (seq > ts->ringSlices) {
        uint64_t prior = seq - ts->ringSlices;
        if (atomic_load_explicit(&ts->reclaimSeq, memory_order_acquire) < prior)
            return failure(RenderError::backpressure);
    }
    uint32_t slice = (uint32_t)((seq - 1u) % ts->ringSlices);
    return AnoStreamRegion{
        .ids = ts->idRing + (size_t)slice * ts->capacity,
        .xforms = ts->xformRingMapped + (size_t)slice * ts->capacity,
        .capacity = ts->capacity,
        .token = seq,
    };
}

// Publish filled region as {seq,count}.
RenderResult<> ano::render_stream_commit(const AnoStreamRegion* region, uint32_t count) {
    if (!region)
        return failure(RenderError::invalid_argument);
    TransformStreamBuffer* ts = &rendererState.transformStream;
    if (count > ts->capacity) count = ts->capacity;
    RenderCommand cmd = { .kind = RCMD_STREAM_TRANSFORMS,
                          .stream_seq = region->token, .stream_count = count };
    if (auto submitted = render_submit(&rendererState.bridge, &cmd); !submitted)
        return submitted;
    ts->produceSeq = region->token;
    return {};
}
