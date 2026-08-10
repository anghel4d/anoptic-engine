/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// C++26 consteval printf-plan compiler for the ring logger. C-invisible; included by anoptic_log.h.
// A literal format compiles once into a static plan: collapsed literal pool + per-conversion ops.
// Producer: typed, unrolled arg capture into the deferred blob layout 〜 zero format parsing.
// Consumer: log_core.c executes the referenced plan directly (format_planned), no spec rebuild.
// Grammar, capture sizes, and truncation mirror log_core.c capture_deferred/format_deferred exactly.
// Non-deferrable literals (%n, %L*, wide %lc/%ls), capture overflow, and NOW routes fall back to
// ano_log_write 〜 the compiled path is additive, the dynamic C ABI stays the source of truth.

#ifndef ANOPTIC_LOG_PLAN_H
#define ANOPTIC_LOG_PLAN_H
#ifdef __cplusplus

#include <meta>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ano::logplan {

// Captured argument slots, in call order. Star width/precision capture as their own i32 slots.
enum ArgKind : uint8_t {
    A_I32 = 1, A_LNG, A_I64,     // %d/%i by length: int, long, long long (z/t/j => I64)
    A_U32, A_ULNG, A_U64,        // %u/%o/%x/%X by length
    A_F64,                       // %e..%A (float promotes)
    A_CHR,                       // %c (captured as int)
    A_PTR,                       // %p
    A_STR,                       // %s (precision-aware deep copy)
    A_W32, A_P32,                // runtime '*' width / precision
};

// One rendered piece: a literal-pool run, then (conv != 0) one conversion.
struct Op final {
    const char *spec;       // prebuilt "%...x" for fancy ops without '*', else nullptr
    uint16_t litOff, litLen; // run in the collapsed literal pool ('%%' folded to '%')
    uint16_t specOff;       // fmt offset just past '%'; the '*' renderer re-walks from here
    uint16_t convOff;       // fmt offset of the conversion char
    int32_t  prec;          // %s capture precision: -1 none, -2 runtime '*', else static
    char     conv;          // conversion char, 0 = trailing literal only
    uint8_t  lng;           // 0 int, 1 long, 2 long long (l count clamped; z/t/j => 2)
    bool     plain;         // %[l|ll|z|t|j](diuoxXcs): fast render, no spec
    bool     starW, starP;
};

// Static plan handed to the drain side by pointer (stored in the record's format slot).
struct Plan final {
    const char *fmt;    // static copy of the literal (star-spec rebuild walks it)
    const char *lit;    // collapsed literal pool
    const Op   *ops;
    uint32_t    opCount;
};

// Parse scratch, oversized; build_plan cuts exact-size static products from it.
template<size_t N>
struct Parsed final {
    Op       ops[N / 2 + 2] {};
    uint32_t opCount = 0;
    ArgKind  argKind[N / 2 + 2] {};
    int32_t  argPrec[N / 2 + 2] {};
    uint32_t argCount = 0;
    char     lit[N + 1] {};
    uint32_t litLen = 0;
    bool     deferrable = true;
};

// Mirror of log_core.c capture_deferred's grammar walk, one conversion per op.
// Bails deferrable=false exactly where capture_deferred returns -1 for grammar reasons.
template<size_t N>
consteval Parsed<N> parse(const char (&fmt)[N])
{
    Parsed<N> r {};
    uint32_t lastLit = 0;
    size_t i = 0;
    while (fmt[i] != '\0') {
        if (fmt[i] != '%') { r.lit[r.litLen++] = fmt[i++]; continue; }
        ++i;
        if (fmt[i] == '%') { r.lit[r.litLen++] = '%'; ++i; continue; }
        Op op {};
        op.litOff = (uint16_t)lastLit;
        op.litLen = (uint16_t)(r.litLen - lastLit);
        op.specOff = (uint16_t)i;
        op.prec = -1;
        bool sawFlag = false, sawWidth = false, sawPrec = false, sawH = false;
        while (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#' || fmt[i] == '0') {
            sawFlag = true; ++i;
        }
        if (fmt[i] == '*') {
            op.starW = true; sawWidth = true; ++i;
            r.argKind[r.argCount] = A_W32; r.argPrec[r.argCount] = -1; ++r.argCount;
        } else {
            while (fmt[i] >= '0' && fmt[i] <= '9') { sawWidth = true; ++i; }
        }
        if (fmt[i] == '.') {
            sawPrec = true; ++i;
            if (fmt[i] == '*') {
                op.starP = true; op.prec = -2; ++i;
                r.argKind[r.argCount] = A_P32; r.argPrec[r.argCount] = -1; ++r.argCount;
            } else {
                int pr = 0;
                while (fmt[i] >= '0' && fmt[i] <= '9') { pr = pr * 10 + (fmt[i] - '0'); ++i; }
                op.prec = pr;
            }
        }
        int lng = 0;
        if (fmt[i] == 'L') { r.deferrable = false; return r; }
        while (fmt[i] == 'l') { ++lng; ++i; }
        if (fmt[i] == 'z' || fmt[i] == 't' || fmt[i] == 'j') { lng = 2; ++i; }
        while (fmt[i] == 'h') { sawH = true; ++i; }
        const char c = fmt[i];
        op.conv = c;
        op.convOff = (uint16_t)i;
        op.lng = (uint8_t)(lng > 2 ? 2 : lng);
        ArgKind k {};
        switch (c) {
        case 'd': case 'i': k = lng >= 2 ? A_I64 : lng == 1 ? A_LNG : A_I32; break;
        case 'u': case 'o': case 'x': case 'X': k = lng >= 2 ? A_U64 : lng == 1 ? A_ULNG : A_U32; break;
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': k = A_F64; break;
        case 'c': if (lng) { r.deferrable = false; return r; } k = A_CHR; break;
        case 'p': k = A_PTR; break;
        case 's': if (lng) { r.deferrable = false; return r; } k = A_STR; break;
        default: r.deferrable = false; return r;   // %n / unknown -> dynamic path
        }
        r.argKind[r.argCount] = k;
        r.argPrec[r.argCount] = k == A_STR ? op.prec : -1;
        ++r.argCount;
        op.plain = !sawFlag && !sawWidth && !sawPrec && !sawH
                   && (c == 'd' || c == 'i' || c == 'u' || c == 'o' || c == 'x' || c == 'X'
                       || c == 'c' || c == 's');
        r.ops[r.opCount++] = op;
        lastLit = r.litLen;
        ++i;
    }
    if (r.litLen > lastLit) {
        Op op {};
        op.litOff = (uint16_t)lastLit;
        op.litLen = (uint16_t)(r.litLen - lastLit);
        r.ops[r.opCount++] = op;
    }
    return r;
}

// Literal as an NTTP; CTAD lets callsites write ano::logplan::write<"fmt %d">(...).
template<size_t N>
struct FixedString final {
    char value[N] {};
    consteval FixedString(const char (&s)[N]) { for (size_t i = 0; i < N; ++i) value[i] = s[i]; }
    static consteval size_t size() { return N - 1; }
};

// Static copy of the literal for the dynamic fallback call.
template<FixedString F>
inline constexpr auto fmt_static = F;

// Prebuilt spec text for one fancy star-less op: verbatim '%'..conv with format_deferred's
// spec[48] truncation rule (chars past slot 45 drop, terminator always lands).
template<size_t N>
consteval std::string_view build_spec(const char (&fmt)[N], const Op &op, char (&buf)[48])
{
    int si = 0;
    buf[si++] = '%';
    for (size_t j = op.specOff; j <= op.convOff; ++j)
        if (si < 46) buf[si++] = fmt[j];
    buf[si] = '\0';
    return std::string_view(buf, (size_t)si);
}

// The static plan: define_static_* stabilized products cut to exact size.
template<FixedString F>
consteval Plan build_plan()
{
    constexpr auto ps = parse(F.value);
    static_assert(ps.deferrable);
    Plan p {};
    p.fmt = std::define_static_string(std::string_view(F.value, F.size()));
    p.lit = std::define_static_string(std::string_view(ps.lit, ps.litLen));
    Op ops[ps.opCount > 0 ? ps.opCount : 1] {};
    for (uint32_t i = 0; i < ps.opCount; ++i) {
        ops[i] = ps.ops[i];
        ops[i].spec = nullptr;
        if (ops[i].conv != 0 && !ops[i].plain && !ops[i].starW && !ops[i].starP) {
            char buf[48] {};
            ops[i].spec = std::define_static_string(build_spec(F.value, ops[i], buf));
        }
    }
    p.opCount = ps.opCount;
    p.ops = ps.opCount > 0
        ? std::define_static_array(std::span<const Op>(ops, ps.opCount)).data()
        : nullptr;
    return p;
}

template<FixedString F>
inline constexpr Plan plan_v = build_plan<F>();

// Per-arg admissibility, aligned with what -Werror=format already admits at existing callsites.
template<class A>
consteval bool arg_ok(ArgKind k)
{
    constexpr bool integral = std::is_integral_v<A> || std::is_enum_v<A>;
    switch (k) {
    case A_I32: case A_U32: case A_CHR: case A_W32: case A_P32:
        return integral && sizeof(A) <= 4;
    case A_LNG: case A_ULNG:
        return integral && sizeof(A) == sizeof(long);
    case A_I64: case A_U64:
        return integral && sizeof(A) == 8;
    case A_F64:
        return std::is_floating_point_v<A> && sizeof(A) <= 8;
    case A_PTR:
        return std::is_pointer_v<A> || std::is_null_pointer_v<A>;
    case A_STR:
        return std::is_null_pointer_v<A>
            || (std::is_pointer_v<A>
                && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<A>>, char>);
    }
    return false;
}

template<FixedString F, class... Args>
consteval bool args_ok()
{
    constexpr auto ps = parse(F.value);
    if (!ps.deferrable)
        return true;    // dynamic fallback; -Wformat has already vetted the literal
    if (ps.argCount != sizeof...(Args))
        return false;
    bool ok = true;
    if constexpr (sizeof...(Args) > 0) {
        [&]<size_t... I>(std::index_sequence<I...>) {
            ((ok = ok && arg_ok<Args...[I]>(ps.argKind[I])), ...);
        }(std::make_index_sequence<sizeof...(Args)>{});
    }
    return ok;
}

// Bounded raw append.
inline bool cap_raw(char *&p, char *end, const void *src, size_t n)
{
    if (p + (ptrdiff_t)n > end) return false;
    memcpy(p, src, n); p += n;
    return true;
}

// Capture one typed argument with capture_deferred's exact widths and %s semantics.
template<ArgKind K, int32_t Prec, class A>
[[gnu::always_inline]] inline bool cap_one(char *&p, char *end, int &starPrec, A a)
{
    if constexpr (K == A_I32 || K == A_CHR || K == A_W32) {
        int v = (int)a; return cap_raw(p, end, &v, 4);
    } else if constexpr (K == A_P32) {
        int v = (int)a; starPrec = v >= 0 ? v : -1; return cap_raw(p, end, &v, 4);
    } else if constexpr (K == A_LNG) {
        long long v = (long long)(long)a; return cap_raw(p, end, &v, 8);
    } else if constexpr (K == A_I64) {
        long long v = (long long)a; return cap_raw(p, end, &v, 8);
    } else if constexpr (K == A_U32) {
        unsigned v = (unsigned)a; return cap_raw(p, end, &v, 4);
    } else if constexpr (K == A_ULNG) {
        unsigned long long v = (unsigned long long)(unsigned long)a; return cap_raw(p, end, &v, 8);
    } else if constexpr (K == A_U64) {
        unsigned long long v = (unsigned long long)a; return cap_raw(p, end, &v, 8);
    } else if constexpr (K == A_F64) {
        double v = (double)a; return cap_raw(p, end, &v, 8);
    } else if constexpr (K == A_PTR) {
        const void *v = (const void *)a; return cap_raw(p, end, &v, 8);
    } else {    // A_STR
        const char *s = (const char *)a;
        if (s == nullptr) s = "(null)";
        const int pr = Prec == -2 ? starPrec : Prec;
        size_t sl = pr >= 0 ? strnlen(s, (size_t)pr) : strlen(s);
        if (sl > 0xffffu) sl = 0xffffu;
        if (p + 2 + (ptrdiff_t)sl + 1 > end) return false;
        uint16_t sl16 = (uint16_t)sl;
        memcpy(p, &sl16, 2); p += 2;
        memcpy(p, s, sl); p += sl;
        *p++ = '\0';
        return true;
    }
}

// Wrapper blob mirrors ANO_LOG_MSG_MAX (contract-checked in log_core.c).
inline constexpr int blob_cap = 4080;
// Bridge sentinel: the record must take the dynamic path (NOW route or oversize header).
inline constexpr int submit_fallback = -2147483647 - 1;

// Resolve route/gate and publish a captured record whose format slot carries `plan`.
// Defined in src/log/log_core.c beside the ring internals.
int submit(ano_loglevel_t level, ano_logroute_t route, const Plan *plan,
           const char *sourceFile, int lineNumber, const char *args, int argsLen) noexcept;

// The compiled callsite. Deferrable literal: typed capture + plan publish; anything else
// (non-deferrable literal, capture overflow, NOW route) re-enters ano_log_write untouched.
template<FixedString F, class... Args>
[[gnu::always_inline]] inline int write(ano_loglevel_t level, ano_logroute_t route,
                                        const char *sourceFile, int lineNumber, Args... args)
{
    static_assert(args_ok<F, Args...>(),
                  "ano_log: arguments do not match the format literal");
    constexpr auto ps = parse(F.value);
    if constexpr (ps.deferrable) {
        char blob[blob_cap];
        char *p = blob, *end = blob + blob_cap;
        int starPrec = -1;
        bool ok = true;
        if constexpr (sizeof...(Args) > 0) {
            [&]<size_t... I>(std::index_sequence<I...>) {
                ((ok = ok && cap_one<ps.argKind[I], ps.argPrec[I]>(p, end, starPrec, args...[I])), ...);
            }(std::make_index_sequence<sizeof...(Args)>{});
        }
        if (ok) {
            const int rc = submit(level, route, &plan_v<F>, sourceFile, lineNumber,
                                  blob, (int)(p - blob));
            if (rc != submit_fallback)
                return rc;
        }
    }
// fmt_static<F> is a consteval-validated literal (args_ok-checked NTTP array); the format
// diagnostics cannot see through the NTTP, so silence -nonliteral/-security here only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wformat-security"
    return ano_log_write(level, route, sourceFile, lineNumber, fmt_static<F>.value, args...);
#pragma GCC diagnostic pop
}

} // namespace ano::logplan

#endif // __cplusplus
#endif // ANOPTIC_LOG_PLAN_H
