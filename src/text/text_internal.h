/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Text internals shared by text.c / text_bake.c, exposed to the white-box unit test. FreeType-free.

#ifndef ANO_TEXT_INTERNAL_H
#define ANO_TEXT_INTERNAL_H

#include <bit>
#include <stdint.h>

#include "anoptic_text.h"


/* Face helpers */

// Backing FT_Face for a handle as opaque pointer. NULL if invalid or module down.
void *ano_text_face(AnoFontId font);

// FreeType version via non-NULL pointers. All zeros before init.
void ano_text_version(int *major, int *minor, int *patch);


/* Bake wire format */

// Shared by bake, shaper, ref rasterizer, GPU shaders.
// Point stream: uint32_t per point, binary16 x in 0..15, y in 16..31 (GLSL unpackHalf2x16), em, y-up, origin at baseline pen.
// Curves are directed monotone quadratic Beziers sharing vertices:

//   glyph  := contour (SENTINEL contour)*
//   contour := p0 (p1 p2)+          -- curve i+1 starts at curve i's p2

// ANO_TEXT_POINT_SENTINEL separates contours, never a coordinate.
// Contours close bit-exactly. Fill-right: clockwise outers in y-up, CCW holes. Every curve x- and y-monotone, control inside endpoint box.

#define ANO_TEXT_POINT_SENTINEL 0x7C007C00u

// AnoGlyphEntry.flags bits.
#define ANO_GLYPH_MISSING 0x1u  // codepoint absent from the face (blank stand-in)

// Horizontal kern pair (GPOS PairPos): xAdvance added between glyphs, em, negative pulls together. Sorted by key.
struct AnoKernPair {
    uint32_t key;       // leftSlot << 16 | rightSlot
    float    xAdvance;  // em
};

// Directory map: codepoints first..last occupy slots from slotBase. Ranges sorted ascending and disjoint.
struct AnoGlyphRange {
    uint32_t first, last, slotBase;
};

#define ANO_TEXT_SLOT_NONE UINT32_MAX

// Directory slot for a codepoint, or ANO_TEXT_SLOT_NONE. Pure, any thread.
uint32_t ano_text_bake_slot(const AnoFontBake *bake, uint32_t codepoint);

// Pen advance, em, for codepoints with no slot.
#define ANO_TEXT_GAP_EM 0.5f

// Kern between two slots, em. 0 if absent or out of range; a slot >= 65536 aliases in the packed key and may hit an unrelated pair. Pure, any thread.
float ano_text_kern(const AnoFontBake *bake, uint32_t leftSlot, uint32_t rightSlot);


/* Bake math */

// binary16 codec, bit-exact, constexpr. The exported ano_half_* symbols in text_bake.c wrap
// these for out-of-module callers (ui_path.h); in-module code and the wire proofs use them
// directly.

// float -> binary16 bits, round-to-nearest-even. |v| >= 65536 -> +-inf; RNE may overflow below that.
inline constexpr uint16_t half_pack_(float v)
{
    uint32_t x = std::bit_cast<uint32_t>(v);
    uint32_t sign = (x >> 16) & 0x8000u;
    x &= 0x7FFFFFFFu;
    if (x >= 0x47800000u) // >= 65536 after rounding: inf/nan/overflow
        return (uint16_t)(sign | (x > 0x7F800000u ? 0x7E00u : 0x7C00u));
    if (x < 0x38800000u) // subnormal or zero
    {
        if (x < 0x33000000u) // < 2^-25 -> 0
            return (uint16_t)sign;
        uint32_t shift = 126u - (x >> 23); // 14..24, implicit mant -> 10-bit
        uint32_t mant  = (x & 0x7FFFFFu) | 0x800000u;
        uint32_t half  = mant >> shift;
        uint32_t rem   = mant & ((1u << shift) - 1u);
        uint32_t mid   = 1u << (shift - 1u);
        if (rem > mid || (rem == mid && (half & 1u)))
            half++;
        return (uint16_t)(sign | half);
    }
    uint32_t mant = x & 0x7FFFFFu;
    uint32_t half = (((x >> 23) - 112u) << 10) | (mant >> 13);
    uint32_t rem  = mant & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (half & 1u)))
        half++;
    return (uint16_t)(sign | half);
}

// binary16 bits -> float, exact.
inline constexpr float half_unpack_(uint16_t h)
{
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t em   = h & 0x7FFFu;
    uint32_t bits;
    if (em >= 0x7C00u) // inf/nan
        bits = sign | 0x7F800000u | ((em & 0x3FFu) << 13);
    else if (em >= 0x0400u) // normal
        bits = sign | ((em + 0x1C000u) << 13);
    else if (em == 0u)
        bits = sign;
    else // subnormal renormalize
    {
        uint32_t e = 113u, m = em;
        while (!(m & 0x400u))
        {
            m <<= 1;
            e--;
        }
        bits = sign | (e << 23) | ((m & 0x3FFu) << 13);
    }
    return std::bit_cast<float>(bits);
}

// Exported wrappers, defined in text_bake.c.
uint16_t ano_half_pack(float v);
float ano_half_unpack(uint16_t h);

/* Wire Proofs */

// The bake wire format is pinned at translation time 〜 a codec drift fails these
// static_asserts instead of baking streams the shaders misread.

// Every non-NaN binary16 bit pattern survives unpack -> pack unchanged (the codec is exact).
consteval bool half_bits_roundtrip(void)
{
    for (uint32_t h = 0; h < 0x10000u; h++) {
        if ((h & 0x7FFFu) > 0x7C00u)
            continue;   // NaN payloads canonicalize, not identity
        if (half_pack_(half_unpack_((uint16_t)h)) != h)
            return false;
    }
    return true;
}

static_assert(((uint32_t)half_pack_(__builtin_inff()) << 16 | half_pack_(__builtin_inff()))
                  == ANO_TEXT_POINT_SENTINEL,
              "ANO_TEXT_POINT_SENTINEL is not two packed +inf halves 〜 wire format drifted");
static_assert(half_bits_roundtrip(),
              "a binary16 bit pattern fails unpack -> pack identity 〜 codec drifted");
// RNE vectors: exact, tie-down, tie-up, max normal, RNE overflow, min subnormal, underflow tie.
static_assert(half_pack_(1.0f) == 0x3C00u, "half: 1.0 packs 0x3C00");
static_assert(half_pack_(1.0f + 0x1p-11f) == 0x3C00u, "half: tie rounds to even (down)");
static_assert(half_pack_(1.0f + 3 * 0x1p-11f) == 0x3C02u, "half: tie rounds to even (up)");
static_assert(half_pack_(65504.0f) == 0x7BFFu, "half: max normal");
static_assert(half_pack_(65520.0f) == 0x7C00u, "half: RNE overflow to +inf");
static_assert(half_pack_(-65520.0f) == 0xFC00u, "half: RNE overflow to -inf");
static_assert(half_pack_(0x1p-24f) == 0x0001u, "half: min subnormal");
static_assert(half_pack_(0x1p-25f) == 0x0000u, "half: underflow tie to even zero");

// Quadratic Bezier in bake space (em, double while processing).
typedef struct AnoQuad {
    double x[3];  // p0, p1 (control), p2
    double y[3];
} AnoQuad;

// Split quad at interior per-axis extrema into 1..3 chained monotone pieces. Extrema within 1e-6 of 0/1 or each other merge.
int ano_quad_split_monotone(const AnoQuad *q, AnoQuad out[3]);

// Approximate cubic by quads within tolEm. Count >= 1, or -1 if maxOut < 1. Never exceeds maxOut. Endpoints exact.
int ano_cubic_to_quads(const double px[4], const double py[4], double tolEm,
                       AnoQuad *out, int maxOut);


/* GPOS kerning */

// FreeType-free. Accumulate latn/DFLT 'kern' PairPos xAdvance (lookups sorted by index, first applying subtable per pair) into dense[s1*slotCount+s2], font units, caller-zeroed. slotGids > 0xFFFF = absent. Bounds-checked. Malformed -> nonzero with dense possibly partial. 0 = success including "no kerns".
int ano_gpos_extract_kerns(const uint8_t *gpos, uint32_t len, const uint32_t *slotGids,
                           uint32_t slotCount, int32_t *dense);


/* Reference rasterization */

// Unclamped coverage sum for one em-space window of one glyph's curve stream. Pure, any thread.
float ano_text_window_sum(const uint32_t *pts, const AnoGlyphEntry *g, float wx, float wy,
                          float w, float h);

// CPU ref rasterizer: float mirror of GPU coverage shader, per-glyph [0,1] clamp, no gamma. FT bitmap layout: row 0 = TOP, pixel (r,c) covers x in [(left+c)/S,(left+c+1)/S), y in [(top-r-1)/S,(top-r)/S), S=pixelsPerEm, pen at (0,0). out = width*rows coverage bytes. maxSumOut optional unclamped peak (>1 = same-winding overlap).
void ano_text_raster_ref(const uint32_t *points, const AnoGlyphEntry *glyph,
                         float pixelsPerEm, int left, int top, int width, int rows,
                         uint8_t *out, float *maxSumOut);

// FreeType AA ground truth (linear 8-bit, unhinted) at pixelsPerEm into buf. FT bearings. Returns 0 or EINVAL/EIO/ENOMEM. Module thread.
int ano_text_ref_ft_render(AnoFontId font, uint32_t codepoint, uint32_t pixelsPerEm,
                           uint8_t *buf, uint32_t cap, int *width, int *rows,
                           int *left, int *top);

#endif
