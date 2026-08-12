// SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
// SPDX-License-Identifier: LGPL-3.0
//
// anoptic_log.h 〜 engine logging emission, lifecycle, and control surface.
//
//   ano::log(ano::Info, "loaded %d meshes", n);
//   ano::log(ano::Warn, gpuRoute, "vram high: %zu", used);
//   ano::log(ano::origin, ano::Error, "bind failed: %d", vr);
//   ano::log(ano::origin, ano::Fatal, crashRoute, "device lost: %d", vr);

#pragma once

#include <cstddef>
#include <cstdint>
#include <meta>
#include <source_location>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include <stdarg.h>
#include <string.h>

// MinGW's plain printf checker models legacy MSVCRT; Anoptic targets UCRT and C99 formats.
#if defined(__MINGW32__)
#define ANO_PRINTF_FORMAT_KIND gnu_printf
#else
#define ANO_PRINTF_FORMAT_KIND printf
#endif

namespace ano {

struct Route final {
    std::uint8_t sink_mask;
};

[[nodiscard]] constexpr Route operator|(Route left, Route right) noexcept
{
    return Route{static_cast<std::uint8_t>(left.sink_mask | right.sink_mask)};
}

inline constexpr Route DefaultRoute{};
inline constexpr Route File{1u << 0};
inline constexpr Route Term{1u << 1};
inline constexpr Route Both = File | Term;
inline constexpr Route Now{1u << 2};

struct LevelContract final {
    char pad[6];
    char color[8];
    Route route;
    bool useStderr;
};

enum class Level : std::uint8_t {
    Trace [[=LevelContract{"TRACE", "",           File,       false}]] = 0,
    Debug [[=LevelContract{"DEBUG", "\x1b[36m",  File,       false}]],
    Info  [[=LevelContract{"INFO ", "",           File,       false}]],
    Warn  [[=LevelContract{"WARN ", "\x1b[33m",  File,       false}]],
    Error [[=LevelContract{"ERROR", "\x1b[31m",  File,       true}]],
    Fatal [[=LevelContract{"FATAL", "\x1b[1;31m", Both | Now, true}]],
};
using enum Level;

struct origin_t final { explicit origin_t() = default; };
inline constexpr origin_t origin{};

// Process lifecycle, configuration, and the dynamic-format boundary.
int log_init();
int log_cleanup();
void log_scope_release(const int *initStatus);
int log_write(Level level, Route route, const char *sourceFile, int lineNumber,
              const char *printFormat, ...)
    __attribute__((format(ANO_PRINTF_FORMAT_KIND, 5, 6)));
int log_vwrite(Level level, Route route, const char *sourceFile, int lineNumber,
               const char *printFormat, va_list arguments)
    __attribute__((format(ANO_PRINTF_FORMAT_KIND, 5, 0)));
int log_output_dir(const char *directoryPath);
void log_set_level(Level minimum);
void log_set_route(Level level, Route route);
void log_flush();

namespace detail {

enum ArgKind : std::uint8_t {
    A_I32 = 1, A_LNG, A_I64,
    A_U32, A_ULNG, A_U64,
    A_F64,
    A_CHR,
    A_PTR,
    A_STR,
    A_W32, A_P32,
};

struct Op final {
    const char *spec;
    std::uint16_t litOff, litLen;
    std::uint16_t specOff;
    std::uint16_t convOff;
    std::int32_t prec;
    char conv;
    std::uint8_t lng;
    bool plain;
    bool starW, starP;
};

struct Plan final {
    const char *fmt;
    const char *lit;
    const Op *ops;
    const ArgKind *argKinds;
    const std::int32_t *argPrec;
    std::uint32_t opCount;
    std::uint32_t argCount;
};

template<std::size_t N>
struct Parsed final {
    Op ops[N / 2 + 2] {};
    std::uint32_t opCount = 0;
    ArgKind argKinds[N / 2 + 2] {};
    std::int32_t argPrec[N / 2 + 2] {};
    std::uint32_t argCount = 0;
    char lit[N + 1] {};
    std::uint32_t litLen = 0;
    bool valid = true;
};

template<std::size_t N>
consteval Parsed<N> parse(const char (&fmt)[N])
{
    Parsed<N> parsed{};
    std::uint32_t lastLit = 0;
    std::size_t i = 0;
    while (fmt[i] != '\0') {
        if (fmt[i] != '%') {
            parsed.lit[parsed.litLen++] = fmt[i++];
            continue;
        }
        ++i;
        if (fmt[i] == '%') {
            parsed.lit[parsed.litLen++] = '%';
            ++i;
            continue;
        }

        Op op{};
        op.litOff = static_cast<std::uint16_t>(lastLit);
        op.litLen = static_cast<std::uint16_t>(parsed.litLen - lastLit);
        op.specOff = static_cast<std::uint16_t>(i);
        op.prec = -1;
        bool sawFlag = false, sawWidth = false, sawPrec = false;
        int shortLength = 0;
        while (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#'
               || fmt[i] == '0') {
            sawFlag = true;
            ++i;
        }
        if (fmt[i] == '*') {
            op.starW = true;
            sawWidth = true;
            ++i;
            parsed.argKinds[parsed.argCount] = A_W32;
            parsed.argPrec[parsed.argCount++] = -1;
        } else {
            while (fmt[i] >= '0' && fmt[i] <= '9') {
                sawWidth = true;
                ++i;
            }
        }
        if (fmt[i] == '.') {
            sawPrec = true;
            ++i;
            if (fmt[i] == '*') {
                op.starP = true;
                op.prec = -2;
                ++i;
                parsed.argKinds[parsed.argCount] = A_P32;
                parsed.argPrec[parsed.argCount++] = -1;
            } else {
                int precision = 0;
                while (fmt[i] >= '0' && fmt[i] <= '9')
                    precision = precision * 10 + (fmt[i++] - '0');
                op.prec = precision;
            }
        }

        int length = 0;
        if (fmt[i] == 'L') {
            parsed.valid = false;
            return parsed;
        }
        while (fmt[i] == 'l') {
            ++length;
            ++i;
        }
        if (fmt[i] == 'z' || fmt[i] == 't' || fmt[i] == 'j') {
            length = 2;
            ++i;
        }
        while (fmt[i] == 'h') {
            ++shortLength;
            ++i;
        }
        if (length > 2 || shortLength > 2 || (length != 0 && shortLength != 0)
            || i - op.specOff >= 44) {
            parsed.valid = false;
            return parsed;
        }

        const char conversion = fmt[i];
        op.conv = conversion;
        op.convOff = static_cast<std::uint16_t>(i);
        op.lng = static_cast<std::uint8_t>(length > 2 ? 2 : length);
        ArgKind kind{};
        switch (conversion) {
        case 'd': case 'i': kind = length >= 2 ? A_I64 : length == 1 ? A_LNG : A_I32; break;
        case 'u': case 'o': case 'x': case 'X':
            kind = length >= 2 ? A_U64 : length == 1 ? A_ULNG : A_U32;
            break;
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A':
            if (length > 1 || shortLength != 0) { parsed.valid = false; return parsed; }
            kind = A_F64;
            break;
        case 'c':
            if (length != 0 || shortLength != 0) { parsed.valid = false; return parsed; }
            kind = A_CHR;
            break;
        case 'p':
            if (length != 0 || shortLength != 0) { parsed.valid = false; return parsed; }
            kind = A_PTR;
            break;
        case 's':
            if (length != 0 || shortLength != 0) { parsed.valid = false; return parsed; }
            kind = A_STR;
            break;
        default:
            parsed.valid = false;
            return parsed;
        }
        if (conversion == '\0') {
            parsed.valid = false;
            return parsed;
        }
        parsed.argKinds[parsed.argCount] = kind;
        parsed.argPrec[parsed.argCount++] = kind == A_STR ? op.prec : -1;
        op.plain = !sawFlag && !sawWidth && !sawPrec && shortLength == 0
                && (conversion == 'd' || conversion == 'i' || conversion == 'u'
                    || conversion == 'o' || conversion == 'x' || conversion == 'X'
                    || conversion == 'c' || conversion == 's');
        parsed.ops[parsed.opCount++] = op;
        lastLit = parsed.litLen;
        ++i;
    }
    if (parsed.litLen > lastLit) {
        Op op{};
        op.litOff = static_cast<std::uint16_t>(lastLit);
        op.litLen = static_cast<std::uint16_t>(parsed.litLen - lastLit);
        parsed.ops[parsed.opCount++] = op;
    }
    return parsed;
}

template<class A>
consteval bool arg_ok(ArgKind kind)
{
    using T = std::remove_cvref_t<A>;
    using D = std::decay_t<A>;
    constexpr bool integral = std::is_integral_v<T> || std::is_enum_v<T>;
    switch (kind) {
    case A_I32: case A_U32: case A_CHR: case A_W32: case A_P32:
        return integral && sizeof(T) <= 4;
    case A_LNG: case A_ULNG:
        return integral && sizeof(T) == sizeof(long);
    case A_I64: case A_U64:
        return integral && sizeof(T) == 8;
    case A_F64:
        return std::is_floating_point_v<T> && sizeof(T) <= 8;
    case A_PTR:
        return std::is_pointer_v<D> || std::is_null_pointer_v<D>;
    case A_STR:
        return std::is_null_pointer_v<D>
            || (std::is_pointer_v<D>
                && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<D>>, char>);
    }
    return false;
}

template<std::size_t N, class... Args>
consteval Parsed<N> validate(const char (&fmt)[N])
{
    constexpr std::size_t maxArgs = 64;
    contract_assert(sizeof...(Args) <= maxArgs);
    auto parsed = parse(fmt);
    contract_assert(parsed.valid);
    contract_assert(parsed.argCount == sizeof...(Args));
    bool matches = true;
    if constexpr (sizeof...(Args) > 0) {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            ((matches = matches && arg_ok<Args...[I]>(parsed.argKinds[I])), ...);
        }(std::make_index_sequence<sizeof...(Args)>{});
    }
    contract_assert(matches);
    return parsed;
}

template<std::size_t N>
consteval std::string_view build_spec(const char (&fmt)[N], const Op &op,
                                      char (&buffer)[48])
{
    int size = 0;
    buffer[size++] = '%';
    std::size_t index = op.specOff;
    while (index < op.convOff) {
        if (fmt[index] == 'l') {
            while (fmt[index] == 'l')
                ++index;
            buffer[size++] = 'l';
            if (op.lng >= 2)
                buffer[size++] = 'l';
        } else if (fmt[index] == 'z' || fmt[index] == 't' || fmt[index] == 'j') {
            ++index;
            buffer[size++] = 'l';
            buffer[size++] = 'l';
        } else {
            if (size < 46)
                buffer[size++] = fmt[index];
            ++index;
        }
    }
    buffer[size++] = fmt[op.convOff];
    buffer[size] = '\0';
    return std::string_view(buffer, static_cast<std::size_t>(size));
}

template<std::size_t N, class... Args>
consteval const Plan *plan_of(const char (&fmt)[N])
{
    const auto parsed = validate<N, Args...>(fmt);
    Plan plan{};
    plan.fmt = std::define_static_string(std::string_view(fmt, N - 1));
    plan.lit = std::define_static_string(std::string_view(parsed.lit, parsed.litLen));

    Op ops[N / 2 + 2] {};
    for (std::uint32_t i = 0; i < parsed.opCount; ++i) {
        ops[i] = parsed.ops[i];
        ops[i].spec = nullptr;
        if (ops[i].conv != '\0' && !ops[i].plain && !ops[i].starW && !ops[i].starP) {
            char buffer[48] {};
            ops[i].spec = std::define_static_string(build_spec(fmt, ops[i], buffer));
        }
    }
    plan.opCount = parsed.opCount;
    plan.ops = parsed.opCount > 0
        ? std::define_static_array(std::span<const Op>(ops, parsed.opCount)).data()
        : nullptr;

    ArgKind kinds[N / 2 + 2] {};
    std::int32_t precision[N / 2 + 2] {};
    for (std::uint32_t i = 0; i < parsed.argCount; ++i) {
        kinds[i] = parsed.argKinds[i];
        precision[i] = parsed.argPrec[i];
    }
    plan.argCount = parsed.argCount;
    plan.argKinds = parsed.argCount > 0
        ? std::define_static_array(std::span<const ArgKind>(kinds, parsed.argCount)).data()
        : nullptr;
    plan.argPrec = parsed.argCount > 0
        ? std::define_static_array(std::span<const std::int32_t>(precision, parsed.argCount)).data()
        : nullptr;
    return std::define_static_array(std::span<const Plan>(&plan, 1)).data();
}

inline constexpr std::size_t arg_pack_capacity = 4080;

struct ArgPack final {
    alignas(8) char bytes[arg_pack_capacity];
    std::uint16_t size;
    bool valid;
};

inline bool pack_raw(char *&out, char *end, const void *source, std::size_t size) noexcept
{
    if (out + static_cast<std::ptrdiff_t>(size) > end)
        return false;
    __builtin_memcpy(out, source, size);
    out += size;
    return true;
}

[[nodiscard]] constexpr std::size_t pack_minimum(ArgKind kind) noexcept
{
    switch (kind) {
    case A_I32: case A_U32: case A_CHR: case A_W32: case A_P32:
        return 4;
    case A_STR:
        return 3;
    case A_LNG: case A_I64: case A_ULNG: case A_U64: case A_F64: case A_PTR:
        return 8;
    }
    __builtin_unreachable();
}

inline bool pack_string(char *&out, char *end, const char *text,
                        std::int32_t precision, int starPrecision,
                        std::size_t reserved) noexcept
{
    if (text == nullptr)
        text = "(null)";
    const std::size_t room = static_cast<std::size_t>(end - out);
    if (room < reserved + 3)
        return false;
    const std::size_t available = room - reserved - 3;
    const int selectedPrecision = precision == -2 ? starPrecision : precision;
    std::size_t limit = selectedPrecision >= 0
        ? static_cast<std::size_t>(selectedPrecision) : available;
    if (limit > available)
        limit = available;
    if (limit > 0xffffu)
        limit = 0xffffu;
    const std::uint16_t length = static_cast<std::uint16_t>(strnlen(text, limit));
    return pack_raw(out, end, &length, 2)
        && pack_raw(out, end, text, static_cast<std::size_t>(length) + 1);
}

template<class A>
[[gnu::always_inline]] inline bool pack_one(char *&out, char *end, ArgKind kind,
                                             std::int32_t precision, int &starPrecision,
                                             std::size_t reserved, A &&argument) noexcept
{
    using T = std::remove_cvref_t<A>;
    using D = std::decay_t<A>;
    if constexpr (std::is_null_pointer_v<D>) {
        if (kind == A_STR)
            return pack_string(out, end, "(null)", precision, starPrecision, reserved);
        const void *value = nullptr;
        return pack_raw(out, end, &value, sizeof value);
    } else if constexpr (std::is_pointer_v<D>
                         && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<D>>, char>) {
        return pack_string(out, end, argument, precision, starPrecision, reserved);
    } else if constexpr (std::is_pointer_v<D>) {
        const void *value = reinterpret_cast<const void *>(argument);
        return pack_raw(out, end, &value, sizeof value);
    } else if constexpr (std::is_floating_point_v<T>) {
        const double value = static_cast<double>(argument);
        return pack_raw(out, end, &value, sizeof value);
    } else {
        if (kind == A_I32 || kind == A_CHR || kind == A_W32 || kind == A_P32) {
            const int value = static_cast<int>(argument);
            if (kind == A_P32)
                starPrecision = value >= 0 ? value : -1;
            return pack_raw(out, end, &value, sizeof value);
        }
        if (kind == A_U32) {
            const unsigned value = static_cast<unsigned>(argument);
            return pack_raw(out, end, &value, sizeof value);
        }
        if (kind == A_LNG || kind == A_I64) {
            const long long value = static_cast<long long>(argument);
            return pack_raw(out, end, &value, sizeof value);
        }
        const unsigned long long value = static_cast<unsigned long long>(argument);
        return pack_raw(out, end, &value, sizeof value);
    }
}

template<class... Args>
[[gnu::always_inline]] inline void pack(ArgPack &result, const Plan &plan,
                                        Args &&...arguments) noexcept
{
    char *out = result.bytes;
    char *end = result.bytes + arg_pack_capacity;
    std::size_t index = 0;
    std::size_t reserved = 0;
    for (std::uint32_t i = 0; i < plan.argCount; ++i)
        reserved += pack_minimum(plan.argKinds[i]);
    int starPrecision = -1;
    bool valid = true;
    ([&] {
        const ArgKind kind = plan.argKinds[index];
        reserved -= pack_minimum(kind);
        if (valid)
            valid = pack_one(out, end, kind, plan.argPrec[index], starPrecision,
                             reserved, static_cast<Args &&>(arguments));
        ++index;
    }(), ...);
    result.size = static_cast<std::uint16_t>(out - result.bytes);
    result.valid = valid;
}

[[nodiscard]] Route prepare(Level, Route) noexcept;
void emit(Level, Route, const Plan &, const ArgPack &,
          const std::source_location *) noexcept;

} // namespace detail

template<class... Args>
struct FormatString final {
    const detail::Plan *plan;

    template<std::size_t N>
    consteval FormatString(const char (&format)[N])
        : plan(detail::plan_of<N, Args...>(format))
    {
    }
};

template<class... Args>
struct OriginFormatString final {
    const detail::Plan *plan;
    std::source_location location;

    template<std::size_t N>
    consteval OriginFormatString(
        const char (&format)[N],
        std::source_location origin = std::source_location::current())
        : plan(detail::plan_of<N, Args...>(format)), location(origin)
    {
    }
};

namespace detail {

template<class Format, class... Args>
[[gnu::always_inline]] inline void dispatch(Level level, Route route, Format format,
        const std::source_location *location, Args &&...arguments) noexcept
{
    route = prepare(level, route);
    if (route.sink_mask == 0)
        return;
    ArgPack packed __attribute__((uninitialized));
    pack(packed, *format.plan, static_cast<Args &&>(arguments)...);
    if (packed.valid)
        emit(level, route, *format.plan, packed, location);
}

} // namespace detail

template<class... Args>
void log(Level level, FormatString<std::type_identity_t<Args>...> format,
         Args &&...arguments) noexcept
{
    detail::dispatch(level, DefaultRoute, format, nullptr,
                     static_cast<Args &&>(arguments)...);
}

template<class... Args>
void log(Level level, Route route, FormatString<std::type_identity_t<Args>...> format,
         Args &&...arguments) noexcept
{
    detail::dispatch(level, route, format, nullptr,
                     static_cast<Args &&>(arguments)...);
}

template<class... Args>
void log(origin_t, Level level, OriginFormatString<std::type_identity_t<Args>...> format,
         Args &&...arguments) noexcept
{
    detail::dispatch(level, DefaultRoute, format, &format.location,
                     static_cast<Args &&>(arguments)...);
}

template<class... Args>
void log(origin_t, Level level, Route route,
         OriginFormatString<std::type_identity_t<Args>...> format,
         Args &&...arguments) noexcept
{
    detail::dispatch(level, route, format, &format.location,
                     static_cast<Args &&>(arguments)...);
}

} // namespace ano

#ifdef DEBUG_BUILD
#define ANO_DEBUG_LOG(...) ::ano::log(__VA_ARGS__)
#else
#define ANO_DEBUG_LOG(...) ((void)0)
#endif


extern "C" {

// Stable C ABI for foreign callers.
int ano_log_init(void);
int ano_log_cleanup(void);

// Scope-bound teardown, LOCALHEAPATTR-style (anoptic_memory.h).
void ano_log_scope_release(const int *initStatus);

// Dynamic-format boundary for wrappers, fuzzers, and foreign APIs.
int ano_log_write(ano::Level level, ano::Route route,
                  const char *sourceFile, int lineNumber,
                  const char *printFormat, ...)
    __attribute__((format(ANO_PRINTF_FORMAT_KIND, 5, 6)));
int ano_log_vwrite(ano::Level level, ano::Route route,
                   const char *sourceFile, int lineNumber,
                   const char *printFormat, va_list args)
    __attribute__((format(ANO_PRINTF_FORMAT_KIND, 5, 0)));

// Open dir/<session-stamp>_ano.log (stamp: ano_fs_session_stamp). 0 ok, -1 keeps previous.
int ano_log_output_dir(const char *directoryPath);

// Runtime severity gate and per-level default route.
void ano_log_set_level(ano::Level minimum);
void ano_log_set_route(ano::Level level, ano::Route route);

// Drain all buffered records on the calling thread.
void ano_log_flush(void);

}
