/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Shaper over immutable AnoFontBake. Any thread. No ligatures, marks, or bidi.

#include "anoptic_text.h"

using namespace ano;
#include "anoptic_strings_utf.h"
#include "text/text_internal.h"

#include <math.h>

uint32_t ano_text_bake_slot(const AnoFontBake *bake, uint32_t codepoint)
{
    uint32_t lo = 0, hi = bake->rangeCount;
    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (bake->ranges[mid].last < codepoint)
            lo = mid + 1u;
        else
            hi = mid;
    }
    if (lo >= bake->rangeCount || codepoint < bake->ranges[lo].first)
        return ANO_TEXT_SLOT_NONE;
    return bake->ranges[lo].slotBase + (codepoint - bake->ranges[lo].first);
}

float ano_text_kern(const AnoFontBake *bake, uint32_t leftSlot, uint32_t rightSlot)
{
    if (bake == NULL || bake->kernCount == 0)
        return 0.0f;
    uint32_t key = leftSlot << 16 | rightSlot;
    uint32_t lo = 0, hi = bake->kernCount;
    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (bake->kerns[mid].key < key)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return (lo < bake->kernCount && bake->kerns[lo].key == key) ? bake->kerns[lo].xAdvance
                                                                : 0.0f;
}

// Shared pen walk for shape/measure. Pair-kern survives a run boundary iff size is unchanged.
// endStepOut is lineHeight * sizePx of the last run that styled a codepoint; byteCount 0 never sets it.
static uint32_t shape_core(const AnoFontBake *bake, anostr_t text,
                           const AnoTextRun *runs, uint32_t runCount,
                           const float origin[2], AnoGlyphInstance *out, uint32_t cap,
                           float *penOut, float *maxWOut, uint32_t *linesOut,
                           float *endStepOut)
{
    size_t total = anostr_len(text);
    float penX = origin[0], penY = origin[1];
    float maxW = 0.0f;
    uint32_t lines = total > 0 ? 1u : 0u;
    uint32_t needed = 0, emitted = 0;
    size_t runEnd = runs[0].byteCount;
    uint32_t runIdx = 0;
    uint32_t prevSlot = UINT32_MAX; // pair-kern chain: broken by newline/gap/size change
    float prevSlotSizePx = 0.0f;

    for (size_t i = 0; i < total;)
    {
        while (i >= runEnd && runIdx + 1 < runCount)
        {
            runIdx++;
            runEnd += runs[runIdx].byteCount;
        }
        float sizePx = runs[runIdx].sizePx; // lead byte's run styles the codepoint
        anorune_t cp = anostr_rune_next(text, &i);
        if (cp == '\r')
            continue;
        if (cp == '\n')
        {
            maxW = fmaxf(maxW, penX - origin[0]);
            penX = origin[0];
            penY += bake->lineHeight * sizePx;
            lines++;
            prevSlot = UINT32_MAX;
            continue;
        }
        uint32_t slot = ano_text_bake_slot(bake, cp);
        if (slot == ANO_TEXT_SLOT_NONE)
        {
            penX += ANO_TEXT_GAP_EM * sizePx;
            prevSlot = UINT32_MAX;
            continue;
        }
        if (prevSlot != UINT32_MAX && sizePx == prevSlotSizePx)
            penX += ano_text_kern(bake, prevSlot, slot) * sizePx;
        prevSlot = slot;
        prevSlotSizePx = sizePx;
        const AnoGlyphEntry *e = &bake->glyphs[slot];
        if (e->curveCount > 0)
        {
            needed++;
            if (out != NULL && emitted < cap)
            {
                out[emitted++] = (AnoGlyphInstance){
                    .inv     = { 1.0f / sizePx, 0.0f, 0.0f, -1.0f / sizePx },
                    .color   = { runs[runIdx].color[0], runs[runIdx].color[1],
                                 runs[runIdx].color[2], runs[runIdx].color[3] },
                    .origin  = { penX, penY },
                    .glyphID = slot,
                    .flags   = 0,
                };
            }
        }
        penX += e->advance * sizePx;
    }
    maxW = fmaxf(maxW, penX - origin[0]);
    if (penOut != NULL)
    {
        penOut[0] = penX;
        penOut[1] = penY;
    }
    if (maxWOut != NULL)
        *maxWOut = maxW;
    if (linesOut != NULL)
        *linesOut = lines;
    if (endStepOut != NULL) // runIdx rests on the run that styled the last codepoint
        *endStepOut = bake->lineHeight * runs[runIdx].sizePx;
    return needed;
}

static bool runs_valid(const AnoTextRun *runs, uint32_t runCount, anostr_t text)
{
    if (runs == NULL || runCount == 0)
        return false;
    uint64_t sum = 0;
    for (uint32_t r = 0; r < runCount; r++)
    {
        if (runs[r].sizePx <= 0.0f)
            return false;
        sum += runs[r].byteCount;
    }
    return sum == anostr_len(text);
}

TextResult<uint32_t> ano::ano_text_shape(
    const AnoFontBake *bake, anostr_t text, float sizePx,
    const float origin[2], const float color[4], AnoGlyphInstance *out,
    uint32_t cap, float *penOut)
{
    if (bake == NULL || origin == NULL || color == NULL || sizePx <= 0.0f)
        return failure(TextError::invalid_argument);
    AnoTextRun run = { .byteCount = (uint32_t)anostr_len(text), .sizePx = sizePx,
                       .color = { color[0], color[1], color[2], color[3] } };
    return shape_core(bake, text, &run, 1, origin, out, cap, penOut, NULL, NULL, NULL);
}

TextResult<uint32_t> ano::ano_text_shape_runs(
    const AnoFontBake *bake, anostr_t text, const AnoTextRun *runs,
    uint32_t runCount, const float origin[2], AnoGlyphInstance *out,
    uint32_t cap, float *penOut)
{
    if (bake == NULL || origin == NULL || !runs_valid(runs, runCount, text))
        return failure(TextError::invalid_argument);
    return shape_core(bake, text, runs, runCount, origin, out, cap, penOut,
                      NULL, NULL, NULL);
}

TextResult<AnoTextMeasure> ano::ano_text_measure(
    const AnoFontBake *bake, anostr_t text, float sizePx)
{
    if (bake == NULL || sizePx <= 0.0f)
        return failure(TextError::invalid_argument);
    float maxW = 0.0f;
    uint32_t lines = 0;
    if (anostr_len(text) > 0)
    {
        AnoTextRun run = { .byteCount = (uint32_t)anostr_len(text), .sizePx = sizePx };
        const float zero[2] = { 0.0f, 0.0f };
        shape_core(bake, text, &run, 1, zero, NULL, 0, NULL, &maxW, &lines, NULL);
    }
    return AnoTextMeasure{maxW, (float)lines * bake->lineHeight * sizePx};
}

TextResult<AnoTextMeasure> ano::ano_text_measure_runs(
    const AnoFontBake *bake, anostr_t text, const AnoTextRun *runs,
    uint32_t runCount)
{
    if (bake == NULL || !runs_valid(runs, runCount, text))
        return failure(TextError::invalid_argument);
    float maxW = 0.0f, h = 0.0f;
    if (anostr_len(text) > 0)
    {
        const float zero[2] = { 0.0f, 0.0f };
        float pen[2], endStep;
        shape_core(bake, text, runs, runCount, zero, NULL, 0, pen, &maxW, NULL,
                   &endStep);
        h = pen[1] + endStep;
    }
    return AnoTextMeasure{maxW, h};
}
