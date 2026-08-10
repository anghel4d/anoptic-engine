/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// anostr_t value type, hash, slicing, keep, builder. Long bytes live in backing (caller heap or external borrow).

#include "strings/ano_strings_internal.h"

#include <stdarg.h>
#include <stdio.h>

anostr_t anostr_from(mi_heap_t *heap, const void *bytes, size_t len)
{
    if (bytes == NULL || len > UINT32_MAX)
        return anostr_empty();
    if (len <= ANOSTR_INLINE_CAP)
        return anostr_make_inline_(bytes, len);
    if (heap == NULL)
        return anostr_empty();
    char *copy = static_cast<char *>(mi_heap_malloc(heap, len));
    if (copy == NULL)
        return anostr_empty();
    memcpy(copy, bytes, len);
    return anostr_make_long_(copy, len);
}

anostr_t anostr_from_cstr(mi_heap_t *heap, const char *cstr)
{
    if (cstr == NULL)
        return anostr_empty();
    return anostr_from(heap, cstr, strlen(cstr));
}

anostr_t anostr_view(const char *bytes, size_t len)
{
    if (bytes == NULL || len > UINT32_MAX)
        return anostr_empty();
    if (len <= ANOSTR_INLINE_CAP)
        return anostr_make_inline_(bytes, len);
    return anostr_make_long_(bytes, len);
}

/* Hash */

// FNV-1a, both widths. Runtime twins of ANOSTR_SID/ANOSTR_SID32, same anostr_fnv1a_ core.

uint64_t anostr_hash(anostr_t s)
{
    return anostr_fnv1a64_(anostr_bytes(&s), s.len);
}

uint32_t anostr_hash32(anostr_t s)
{
    return anostr_fnv1a32_(anostr_bytes(&s), s.len);
}

/* Twin Proofs */

// ANOSTR_SID(x) == anostr_hash(anostr_lit(x)) at translation time 〜 the shared anostr_fnv1a_
// core over the consteval-built value's bytes, gathered per the layout contract (anostr_bytes
// reads inline bytes through prefix's contiguity, which constant evaluation cannot).
template <size_t N>
consteval bool sid_twins_(const char (&s)[N])
{
    anostr_t v = anostr_lit_(s);
    char b[N] = {};
    for (size_t i = 0; i < v.len; i++)
        b[i] = v.len <= ANOSTR_INLINE_CAP ? (i < 4 ? v.prefix[i] : v.suffix[i - 4]) : v.ptr[i];
    return anostr_sid_(s) == anostr_fnv1a64_(b, v.len)
        && anostr_sid32_(s) == anostr_fnv1a32_(b, v.len);
}

// I3 for the consteval constructor: bytes [0..len) match the literal, inline padding is 0x00,
// long form borrows the literal and caches its first four bytes.
template <size_t N>
consteval bool lit_canonical_(const char (&s)[N])
{
    constexpr size_t len = N - 1;
    anostr_t v = anostr_lit_(s);
    if (v.len != len)
        return false;
    if constexpr (len <= ANOSTR_INLINE_CAP) {
        for (size_t i = 0; i < 12; i++)
            if ((i < 4 ? v.prefix[i] : v.suffix[i - 4]) != (i < len ? s[i] : '\0'))
                return false;
    } else {
        if (v.ptr != s)
            return false;
        for (size_t i = 0; i < 4; i++)
            if (v.prefix[i] != s[i])
                return false;
    }
    return true;
}

#define SID_PROOF_16_  "0123456789abcdef"
#define SID_PROOF_128_ SID_PROOF_16_ SID_PROOF_16_ SID_PROOF_16_ SID_PROOF_16_ \
                       SID_PROOF_16_ SID_PROOF_16_ SID_PROOF_16_ SID_PROOF_16_

static_assert(sid_twins_(""), "SID/hash twins diverge on the empty string");
static_assert(sid_twins_("a"), "SID/hash twins diverge on one byte");
static_assert(sid_twins_("a\0b"), "SID/hash twins diverge on an embedded NUL");
static_assert(sid_twins_("player_spawn"), "SID/hash twins diverge on a 12-byte inline value");
static_assert(sid_twins_("a-string-longer-than-twelve"), "SID/hash twins diverge on a long value");
static_assert(sid_twins_(SID_PROOF_128_), "SID/hash twins diverge at the ANOSTR_SID_MAX cap");

static_assert(lit_canonical_(""), "anostr_lit: empty is not all-zero");
static_assert(lit_canonical_("abc"), "anostr_lit: sub-prefix inline form not canonical (I3)");
static_assert(lit_canonical_("hello, world"), "anostr_lit: 12-byte inline form not canonical (I3)");
static_assert(lit_canonical_("categorically"), "anostr_lit: long form not canonical");
static_assert(lit_canonical_(SID_PROOF_128_), "anostr_lit: 128-byte long form not canonical");

#undef SID_PROOF_16_
#undef SID_PROOF_128_

/* Slicing and Promotion */

anostr_t anostr_slice(anostr_t s, size_t start, size_t end)
{
    if (end > s.len)
        end = s.len;
    if (start > end)
        start = end;
    size_t n = end - start;
    if (n <= ANOSTR_INLINE_CAP)
        return anostr_make_inline_(anostr_bytes(&s) + start, n);
    // n > 12: s was long, borrow its backing (I4).
    return anostr_make_long_(s.ptr + start, n);
}

anostr_t anostr_keep(mi_heap_t *heap, anostr_t s)
{
    if (s.len <= ANOSTR_INLINE_CAP)
        return s;   // inline: identity
    return anostr_from(heap, s.ptr, s.len);
}

char *anostr_to_cstr(mi_heap_t *heap, anostr_t s)
{
    if (heap == NULL)
        return NULL;
    char *out = static_cast<char *>(mi_heap_malloc(heap, (size_t)s.len + 1));
    if (out == NULL)
        return NULL;
    memcpy(out, anostr_bytes(&s), s.len);
    out[s.len] = '\0';
    return out;
}

/* Builder */

anostr_builder_t anostr_builder_make(mi_heap_t *heap, uint32_t reserve)
{
    anostr_builder_t b = { .ptr = NULL, .len = 0, .cap = 0, .heap = heap };
    if (heap != NULL && reserve > 0) {
        b.ptr = static_cast<char *>(mi_heap_malloc(heap, reserve));
        if (b.ptr != NULL)
            b.cap = reserve;
    }
    return b;
}

// Grow so cap >= need. Geometric doubling from 16, clamped to UINT32_MAX. Untouched on fail.
static int builder_reserve(anostr_builder_t *b, uint64_t need)
{
    if (need <= b->cap)
        return 0;
    uint64_t cap = b->cap ? (uint64_t)b->cap * 2 : 16;
    while (cap < need)
        cap *= 2;
    if (cap > UINT32_MAX)
        cap = UINT32_MAX;
    char *grown = static_cast<char *>(mi_heap_realloc(b->heap, b->ptr, cap));
    if (grown == NULL)
        return -1;
    b->ptr = grown;
    b->cap = (uint32_t)cap;
    return 0;
}

int anostr_builder_append(anostr_builder_t *b, const void *bytes, size_t n)
{
    if (b->heap == NULL || bytes == NULL)
        return -1;
    if (n == 0)
        return 0;
    uint64_t need = (uint64_t)b->len + n;
    if (need > UINT32_MAX || builder_reserve(b, need) != 0)
        return -1;
    memcpy(b->ptr + b->len, bytes, n);
    b->len = (uint32_t)need;
    return 0;
}

int anostr_builder_append_str(anostr_builder_t *b, anostr_t s)
{
    return anostr_builder_append(b, anostr_bytes(&s), s.len);
}

int anostr_builder_append_cstr(anostr_builder_t *b, const char *cstr)
{
    if (cstr == NULL)
        return -1;
    return anostr_builder_append(b, cstr, strlen(cstr));
}

int anostr_builder_appendf(anostr_builder_t *b, const char *fmt, ...)
{
    if (b->heap == NULL || fmt == NULL)
        return -1;

    va_list args, measure;
    va_start(args, fmt);
    va_copy(measure, args);
    int need = vsnprintf(NULL, 0, fmt, measure);
    va_end(measure);
    if (need < 0) {
        va_end(args);
        return -1;
    }

    // +1: vsnprintf writes a NUL into spare capacity, not counted in len.
    uint64_t total = (uint64_t)b->len + (uint64_t)need + 1;
    if (total > UINT32_MAX || builder_reserve(b, total) != 0) {
        va_end(args);
        return -1;
    }
    vsnprintf(b->ptr + b->len, (size_t)need + 1, fmt, args);
    va_end(args);
    b->len += (uint32_t)need;
    return 0;
}

anostr_t anostr_freeze(anostr_builder_t *b)
{
    if (b->heap == NULL)
        return anostr_empty();

    anostr_t s;
    if (b->len <= ANOSTR_INLINE_CAP) {
        s = anostr_make_inline_(b->ptr != NULL ? b->ptr : "", b->len);
        mi_free(b->ptr);    // inline owes nothing to the buffer
    } else {
        // Shrink to len and hand buffer to the value. Failed shrink keeps the original block.
        char *exact = static_cast<char *>(mi_heap_realloc(b->heap, b->ptr, b->len));
        s = anostr_make_long_(exact != NULL ? exact : b->ptr, b->len);
    }
    *b = (anostr_builder_t){0};     // consumed: heap == NULL fails further appends
    return s;
}

void anostr_builder_discard(anostr_builder_t *b)
{
    mi_free(b->ptr);
    *b = (anostr_builder_t){0};
}
