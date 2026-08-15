/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

/* Property fuzzer for anostr_t. Cross-kind agreement is the central property.
 * Each iteration owns a fresh scratch heap. argv[1] scales the soak. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anoptic_memory.h"

using namespace ano;
#include "anoptic_strings_utf.h"
#include "templates/rng.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

template<class Value>
static Value must(StringResult<Value> result)
{
    CHECK(result, "string operation succeeds");
    return result.value_or(Value{});
}

static int sign(int v) { return v < 0 ? -1 : v > 0; }

/* Independent oracles. */

// FNV-1a reference: twin of anostr_hash / anostr_hash32.
static uint64_t fnv64(const void *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    const uint8_t *b = static_cast<const uint8_t *>(p);
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; }
    return h;
}
static uint32_t fnv32(const void *p, size_t n)
{
    uint32_t h = 0x811c9dc5u;
    const uint8_t *b = static_cast<const uint8_t *>(p);
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x01000193u; }
    return h;
}

// Naive memmem: first needle at/after from.
static size_t naive_find(const char *s, size_t sn, const char *nd, size_t nn, size_t from)
{
    if (from > sn) from = sn;
    if (nn == 0) return from;
    if (nn > sn) return ANOSTR_NPOS;
    for (size_t i = from; i + nn <= sn; i++)
        if (memcmp(s + i, nd, nn) == 0) return i;
    return ANOSTR_NPOS;
}

// Naive LTR non-overlapping replace; returns result length.
static size_t naive_replace(const char *s, size_t sn, const char *nd, size_t nn,
                            const char *rp, size_t rn, char *out)
{
    size_t o = 0;
    if (nn == 0) { memcpy(out, s, sn); return sn; }
    for (size_t i = 0; i < sn; ) {
        if (i + nn <= sn && memcmp(s + i, nd, nn) == 0) { memcpy(out + o, rp, rn); o += rn; i += nn; }
        else out[o++] = s[i++];
    }
    return o;
}

// Naive splice: remove [a,b), insert r. SUT = slice + concat.
static size_t naive_splice(const char *s, size_t sn, size_t a, size_t b,
                           const char *r, size_t rn, char *out)
{
    if (a > sn) a = sn;
    if (b > sn) b = sn;
    if (a > b) a = b;
    size_t o = 0;
    memcpy(out + o, s, a); o += a;
    memcpy(out + o, r, rn); o += rn;
    memcpy(out + o, s + b, sn - b); o += sn - b;
    return o;
}

/* Random content generators. */

// Random rune: ASCII, accents, Cyrillic/Greek/kana, Han+SMP, ignorables.
static anorune_t rng_rune(test_rng *rng)
{
    switch (rng_below(rng, 12)) {
    case 0: case 1: case 2: case 3: return 'a' + rng_below(rng, 26);
    case 4: return 'A' + rng_below(rng, 26);
    case 5: return (anorune_t[]){ 0xE9, 0xC4, 0xF6, 0xE5, 0x101, 0x219 }[rng_below(rng, 6)];
    case 6: return 0x430 + rng_below(rng, 32);
    case 7: return 0x3B1 + rng_below(rng, 17);
    case 8: return 0x3042 + rng_below(rng, 20);
    case 9: return 0x4E00 + rng_below(rng, 64);
    case 10: return (anorune_t[]){ ' ', '!', '.', 0x1, 0x301, 0x16A0 }[rng_below(rng, 6)];
    default: return (anorune_t[]){ 0x1D568, 0x1F600 }[rng_below(rng, 2)];
    }
}

// Valid UTF-8 of up to maxRunes runes.
static anostr_t rng_str(test_rng *rng, mi_heap_t *heap, uint32_t maxRunes)
{
    uint32_t n = rng_below(rng, maxRunes + 1);
    anostr_builder_t b = must(anostr_builder_make(heap, 0));
    for (uint32_t k = 0; k < n; k++)
        CHECK(anostr_builder_append_rune(&b, rng_rune(rng)), "random rune appends");
    return must(anostr_freeze(&b));
}

// Arbitrary bytes (NUL + malformed UTF-8) into buf.
static size_t rng_bytes(test_rng *rng, char *buf, size_t maxLen)
{
    size_t n = rng_below(rng, (uint32_t)maxLen + 1);
    for (size_t i = 0; i < n; i++) buf[i] = (char)rng_next(rng);
    return n;
}

/* Sort oracle, reused verbatim from the sort exemplar. */

static int rune_cmp(const void *a, const void *b)
{
    anorune_t x = *(const anorune_t *)a, y = *(const anorune_t *)b;
    return x < y ? -1 : (x > y);
}

static int oracle_cmp(const void *a, const void *b)
{
    return anostr_collate(*(const anostr_t *)a, *(const anostr_t *)b);
}

static bool check_against_oracle(const anostr_t *items, size_t n, const char *what, mi_heap_t *heap)
{
    anostr_t *mine = mi_heap_mallocn_tp(anostr_t, heap, n);
    anostr_t *ref = mi_heap_mallocn_tp(anostr_t, heap, n);
    uint32_t *order = mi_heap_mallocn_tp(uint32_t, heap, n);
    if (mine == NULL || ref == NULL || order == NULL) {
        printf("FAIL: %s: oracle scratch alloc\n", what); failures++; return false;
    }
    memcpy(mine, items, n * sizeof *mine);
    memcpy(ref, items, n * sizeof *ref);

    anostr_sort(mine, n);
    qsort(ref, n, sizeof ref[0], oracle_cmp);
    for (size_t i = 0; i < n; i++)
        if (!anostr_eq(mine[i], ref[i])) {
            printf("FAIL: %s: anostr_sort[%zu] diverged from oracle\n", what, i); failures++; return false;
        }

    anostr_sort_idx(items, n, order);
    uint8_t *seen = static_cast<uint8_t *>(mi_heap_zalloc(heap, n));
    for (size_t i = 0; i < n; i++) {
        if (order[i] >= n || seen[order[i]]) {
            printf("FAIL: %s: order is not a permutation at %zu\n", what, i); failures++; return false;
        }
        seen[order[i]] = 1;
        if (!anostr_eq(items[order[i]], ref[i])) {
            printf("FAIL: %s: sort_idx[%zu] gathers wrong element\n", what, i); failures++; return false;
        }
        if (i > 0 && anostr_collate(items[order[i - 1]], items[order[i]]) == 0 && order[i - 1] >= order[i]) {
            printf("FAIL: %s: unstable on equal strings at %zu\n", what, i); failures++; return false;
        }
    }

    anostr_sort(mine, n);   // presorted early-out returns the identical sequence (idempotent)
    for (size_t i = 0; i < n; i++)
        if (!anostr_eq(mine[i], ref[i])) {
            printf("FAIL: %s: re-sort diverged at %zu\n", what, i); failures++; return false;
        }
    return true;
}

/* Cross-kind agreement: one logical content built through every backing must agree everywhere. */

// nulfree gates from_cstr backing.
static void check_kinds(mi_heap_t *h, anostr_intern_t *it, const char *buf, size_t n,
                        bool nulfree, const char *what)
{
    anostr_t v[8];
    size_t k = 0;
    v[k++] = must(anostr_from(h, buf, n));                             // arena
    v[k++] = anostr_view(buf, n);                                     // borrow
    v[k++] = must(anostr_keep(h, anostr_view(buf, n)));               // owned copy
    anostr_builder_t b = must(anostr_builder_make(h, 0));             // builder-frozen
    CHECK(anostr_builder_append(&b, buf, n), what);
    v[k++] = must(anostr_freeze(&b));
    size_t half = n / 2;                                             // concat of two halves
    v[k++] = must(anostr_concat(
        h, anostr_view(buf, half), anostr_view(buf + half, n - half)));
    v[k++] = anostr_slice(must(anostr_keep(h, anostr_view(buf, n))), 0, n);
    v[k++] = must(anostr_dedupe(it, anostr_view(buf, n)));            // canonical
    if (nulfree) {
        char *cs = static_cast<char *>(mi_heap_malloc(h, n + 1));
        memcpy(cs, buf, n); cs[n] = 0;
        v[k++] = must(anostr_from_cstr(h, cs));
    }

    for (size_t i = 0; i < k; i++) {
        CHECK(anostr_len(v[i]) == n, what);
        CHECK(anostr_is_empty(v[i]) == (n == 0), what);
        CHECK(anostr_is_inline(v[i]) == (n <= ANOSTR_INLINE_CAP), what);   // I2
        CHECK(anostr_eq(v[i], v[0]), what);
        CHECK(anostr_eq(v[0], v[i]), what);                                // symmetric
        CHECK(anostr_compare(v[i], v[0]) == 0, what);                      // eq <=> compare 0
        CHECK(anostr_collate(v[i], v[0]) == 0, what);
        CHECK(anostr_collate_prefix(v[i]) == anostr_collate_prefix(v[0]), what);
        CHECK(anostr_hash(v[i]) == anostr_hash(v[0]), what);
        CHECK(anostr_hash32(v[i]) == anostr_hash32(v[0]), what);
        CHECK(memcmp(anostr_bytes(&v[i]), buf, n) == 0, what);
        if (n <= ANOSTR_INLINE_CAP)                                        // Regime A: bit-identical
            CHECK(memcmp(&v[i], &v[0], sizeof(anostr_t)) == 0, what);
        // Result depends on content, not construction path.
        if (n > 0) {
            anostr_t nd = anostr_view(buf, 1);
            anostr_t tail = anostr_lit("|tail");
            CHECK(anostr_find(v[i], nd, 0) == anostr_find(v[0], nd, 0), what);
            CHECK(anostr_eq(anostr_slice(v[i], 1, n), anostr_slice(v[0], 1, n)), what);
            // split/replace/concat/join depend on content, not kind.
            CHECK(anostr_eq(must(anostr_concat(h, v[i], tail)),
                            must(anostr_concat(h, v[0], tail))), what);
            CHECK(anostr_eq(must(anostr_replace_all(h, v[i], nd, anostr_lit("Q"))),
                            must(anostr_replace_all(h, v[0], nd, anostr_lit("Q")))), what);
            anostr_t jpi[2] = { v[i], tail }, jp0[2] = { v[0], tail };
            CHECK(anostr_eq(must(anostr_join(h, anostr_lit(","), jpi, 2)),
                            must(anostr_join(h, anostr_lit(","), jp0, 2))), what);
            anostr_t pi, p0; bool ni, n0;                    // split sequences agree across kinds
            anostr_split_t si = anostr_split(v[i], nd), s0 = anostr_split(v[0], nd);
            do { ni = anostr_split_next(&si, &pi); n0 = anostr_split_next(&s0, &p0);
                 CHECK(ni == n0 && (!ni || anostr_eq(pi, p0)), what); } while (ni && n0);
        }
    }

    // Deduped twins share backing; ptr short-circuit and memcmp agree.
    anostr_t d1 = must(anostr_dedupe(it, anostr_view(buf, n)));
    anostr_t d2 = must(anostr_dedupe(it, must(anostr_from(h, buf, n))));
    CHECK(anostr_eq(d1, d2), what);
    if (n > ANOSTR_INLINE_CAP)
        CHECK(anostr_bytes(&d1) == anostr_bytes(&d2), what);               // shared canonical ptr
}

/* Randomized property soak. Per-iter scratch heap. */

static void fuzz(uint32_t iterations)
{
    test_rng rng = rng_make(0xF0FBEEF5u);
    for (uint32_t it = 0; it < iterations; it++) {
        mi_heap_t *scratch ANO_SCOPED_HEAP = heap_create();
        if (scratch == NULL) { printf("FAIL: fuzz scratch heap\n"); failures++; return; }
        anostr_intern_t *tab = must(anostr_intern_make(scratch));

        // Cross-kind agreement across 12/13 boundary.
        char vbuf[128];                                  // 24 runes * up to 4 UTF-8 bytes = 96 worst case
        anostr_t vs = rng_str(&rng, scratch, 24);
        size_t vn = anostr_len(vs);
        memcpy(vbuf, anostr_bytes(&vs), vn);
        check_kinds(scratch, tab, vbuf, vn, true, "kinds: valid utf8");   // rng_str never emits a NUL byte

        char bbuf[80];
        size_t bn = rng_bytes(&rng, bbuf, 40);           // embedded NUL + malformed included
        check_kinds(scratch, tab, bbuf, bn, false, "kinds: arbitrary bytes");

        // Slice + splice vs naive oracle.
        anostr_t src = anostr_view(bbuf, bn);
        size_t a = bn ? rng_below(&rng, (uint32_t)bn + 1) : 0;
        size_t b = bn ? rng_below(&rng, (uint32_t)bn + 1) : 0;
        if (a > b) { size_t tmp = a; a = b; b = tmp; }
        char rb[8]; size_t rn = rng_bytes(&rng, rb, 6);
        anostr_t r = anostr_view(rb, rn);
        anostr_t sliced = anostr_slice(src, a, b);
        CHECK(anostr_len(sliced) == b - a, "slice width");
        CHECK(anostr_is_inline(sliced) == (b - a <= ANOSTR_INLINE_CAP), "slice canonical rule");
        anostr_t spl = must(anostr_concat(
            scratch, anostr_slice(src, 0, a),
            must(anostr_concat(scratch, r, anostr_slice(src, b, bn)))));
        char soracle[128];
        size_t sn = naive_splice(bbuf, bn, a, b, rb, rn, soracle);
        CHECK(anostr_eq(spl, anostr_view(soracle, sn)), "splice = slice+concat vs naive bytes");

        // Concat vs naive buffer; empty is identity.
        char cbuf[80]; size_t cn = rng_bytes(&rng, cbuf, 20);
        anostr_t ca = anostr_view(bbuf, bn), cb = anostr_view(cbuf, cn);
        anostr_t cc = must(anostr_concat(scratch, ca, cb));
        char cor[160]; memcpy(cor, bbuf, bn); memcpy(cor + bn, cbuf, cn);
        CHECK(anostr_len(cc) == bn + cn && anostr_eq(cc, anostr_view(cor, bn + cn)), "concat bytes");
        CHECK(anostr_eq(must(anostr_concat(scratch, ca, anostr_empty())), ca),
              "concat empty identity");

        // Find + replace_all vs naive scans.
        if (bn > 0) {
            char nbuf[4]; size_t nn = 1 + rng_below(&rng, 3);
            for (size_t k = 0; k < nn; k++) nbuf[k] = bbuf[rng_below(&rng, (uint32_t)bn)];
            anostr_t nd = anostr_view(nbuf, nn);
            size_t from = rng_below(&rng, (uint32_t)bn + 2);
            CHECK(anostr_find(src, nd, from) == naive_find(bbuf, bn, nbuf, nn, from), "find vs naive");
            char reb[4]; size_t ren = rng_below(&rng, 3);
            for (size_t k = 0; k < ren; k++) reb[k] = (char)('A' + rng_below(&rng, 4));
            anostr_t got = must(anostr_replace_all(scratch, src, nd, anostr_view(reb, ren)));
            char rout[512];
            size_t rlen = naive_replace(bbuf, bn, nbuf, nn, reb, ren, rout);
            CHECK(anostr_eq(got, anostr_view(rout, rlen)), "replace_all vs naive");
        }
        // Empty needle / no-op replace -> s bit-identical.
        anostr_t idrep = must(anostr_replace_all(
            scratch, src, anostr_empty(), anostr_lit("!")));
        CHECK(anostr_eq(idrep, src) && memcmp(&idrep, &src, sizeof(anostr_t)) == 0, "empty needle identity");

        // Split/join round-trip; count == finds+1.
        char sepb[2]; size_t sepn = 1 + rng_below(&rng, 2);
        for (size_t k = 0; k < sepn; k++) sepb[k] = bn ? bbuf[rng_below(&rng, (uint32_t)bn)] : ',';
        anostr_t sep = anostr_view(sepb, sepn);
        anostr_t pieces[96]; size_t np = 0;
        anostr_split_t sit = anostr_split(src, sep);
        anostr_t piece;
        while (np < 96 && anostr_split_next(&sit, &piece)) pieces[np++] = piece;
        size_t expect = 1;
        for (size_t i = 0; ; ) {
            size_t f = anostr_find(src, sep, i);
            if (f == ANOSTR_NPOS) break;
            expect++; i = f + sepn;
        }
        CHECK(np == expect, "split piece count == find-count + 1");
        CHECK(anostr_eq(must(anostr_join(scratch, sep, pieces, np)), src),
              "split/join round-trip");

        // Sort vs collation oracle.
        enum { M = 32 };
        anostr_t list[M];
        for (size_t k = 0; k < M; k++) list[k] = rng_str(&rng, scratch, 12);
        check_against_oracle(list, M, "fuzz sort", scratch);

        // Cull vs naive; rune_sort multiset + idempotence.
        uint32_t classes = rng_below(&rng, 8);
        {
            anostr_builder_t nb = must(anostr_builder_make(scratch, 0)); // naive cull rebuild
            for (size_t i = 0; i < vn; ) {
                size_t start = i;
                anorune_t ru = anostr_rune_next(vs, &i);
                bool drop = ((classes & ANOSTR_CULL_WHITESPACE) && anorune_is_whitespace(ru)) ||
                            ((classes & ANOSTR_CULL_PUNCT) && anorune_is_punct(ru)) ||
                            ((classes & ANOSTR_CULL_MARK) && anorune_is_mark(ru));
                if (!drop)
                    CHECK(anostr_builder_append(&nb, anostr_bytes(&vs) + start, i - start),
                          "naive cull append");
            }
            CHECK(anostr_eq(must(anostr_cull(scratch, vs, classes)), must(anostr_freeze(&nb))),
                  "cull vs naive");
        }
        anostr_t rs = must(anostr_rune_sort(scratch, vs));
        size_t an = 0, gn = 0;
        anorune_t *awant = must(anostr_to_utf32(scratch, vs, &an));
        anorune_t *agot  = must(anostr_to_utf32(scratch, rs, &gn));
        CHECK(gn == an, "rune_sort preserves rune count");
        CHECK(anostr_eq(must(anostr_rune_sort(scratch, rs)), rs), "rune_sort idempotent");

        // UTF round-trips; to_utf32 matches rune_next.
        size_t u16n = 0;
        char16_t *u16 = must(anostr_to_utf16(scratch, vs, &u16n));
        CHECK(anostr_eq(must(anostr_from_utf16(scratch, u16, u16n)), vs),
              "utf16 round-trip");
        CHECK(anostr_eq(must(anostr_from_utf32(scratch, awant, an)), vs),
              "utf32 round-trip");
        size_t di = 0, dk = 0;
        while (di < vn && dk < an) { CHECK(anostr_rune_next(vs, &di) == awant[dk++], "to_utf32 == rune_next"); }

        // rune_sort is a permutation of source runes.
        qsort(awant, an, sizeof awant[0], rune_cmp);
        for (size_t i = 0; i < an && i < gn; i++)
            CHECK(awant[i] == agot[i], "rune_sort is a permutation of the source runes");

        // collate_prefix/key agree with collate in sign.
        anostr_t p = rng_str(&rng, scratch, 10), q = rng_str(&rng, scratch, 10);
        uint64_t pk = anostr_collate_prefix(p), qk = anostr_collate_prefix(q);
        if (pk != qk)
            CHECK(sign(pk < qk ? -1 : 1) == sign(anostr_collate(p, q)), "prefix key agrees with collate");
        anostr_t kp = must(anostr_collate_key(scratch, p));
        anostr_t kq = must(anostr_collate_key(scratch, q));
        CHECK(sign(anostr_compare(kp, kq)) == sign(anostr_collate(p, q)), "collate_key agrees with collate");

        // Base folds: ASCII case-only oracle for eq_base/find_base.
        char lo[24], up[24]; size_t ln = 1 + rng_below(&rng, 10);
        for (size_t k = 0; k < ln; k++) { char c = (char)('a' + rng_below(&rng, 26)); lo[k] = c; up[k] = (char)(c - 32); }
        CHECK(anostr_eq_base(anostr_view(lo, ln), anostr_view(up, ln)), "eq_base case-only");
        CHECK(anostr_starts_base(anostr_view(lo, ln), anostr_view(up, ln)), "starts_base case-only");
    }
}

int main(int argc, char **argv)
{
    mi_heap_t *heap ANO_SCOPED_HEAP = heap_create();
    if (heap == NULL) { printf("FAIL: heap_create\n"); return 1; }


    uint32_t iterations = 200;
    if (argc > 1) iterations = (uint32_t)strtoul(argv[1], NULL, 10);
    fuzz(iterations);


    if (failures == 0) { printf("anotest_strings_fuzz: all checks passed\n"); return 0; }
    printf("anotest_strings_fuzz: %d check(s) failed\n", failures);
    return 1;
}
