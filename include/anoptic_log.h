/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Lock-free MPSC logger: producers capture/format off-ring, publish into a shared ring, one owned consumer drains.
// log_flush drains inline on the caller. NOW drains then write-through (+ fsync when a file is open).
// Four macro families over log_write. Severity = how bad, route = where/when.
//   ano_log(ANO_WARN, "fmt %d", x);                        level's default route
//   ano_rlog(ANO_ERROR, ANO_TERM | ANO_NOW, "fmt %d", x);  explicit route
//   ano_debug_log(...) / ano_debug_rlog(...)               Debug builds only
// ANO_ prefix is load-bearing (windows.h ERROR, stdio FILE).

#pragma once

#include <stdarg.h>
#include <stdint.h>
#include "anoptic_results.h"

namespace ano {

// MinGW's plain printf checker models legacy MSVCRT; Anoptic targets UCRT and C99 formats.
#if defined(__MINGW32__)
#define ANO_PRINTF_FORMAT_KIND gnu_printf
#else
#define ANO_PRINTF_FORMAT_KIND printf
#endif


/* Types */

// Severity ascending.
enum loglevel_t {
    ANO_INFO = 0,
    ANO_WARN,
    ANO_ERROR,
    ANO_FATAL
};

enum logroute_t {
    ANO_ROUTE_DEFAULT = 0,           // use the level's configured route
    ANO_FILE = 1 << 0,              // output file (terminal when none open)
    ANO_TERM = 1 << 1,              // stdout, ERROR+ to stderr, ANSI on tty
    ANO_BOTH = ANO_FILE | ANO_TERM,
    ANO_NOW  = 1 << 2,              // sync: drain, write-through, fsync if file open
};

enum class LogError : uint8_t {
    invalid_argument, not_initialized, out_of_memory,
    io, platform,
};

template<class Value = void>
using LogResult = Result<Value, LogError>;


/* Lifecycle Functions */

[[nodiscard]] LogResult<> log_init(void);
void log_cleanup(void);

// Scope-bound teardown, ANO_SCOPED_HEAP-style (anoptic_memory.h).
void log_scope_release(const LogResult<> *initStatus);
#define ANO_LOG_SCOPE_ATTR __attribute__((__cleanup__(log_scope_release)))

/* Entry Points */

int log_write(loglevel_t level, logroute_t route,
                  const char* sourceFile, int lineNumber,
                  /* printFormat MUST be a string literal. */
                  const char* printFormat, ...) __attribute__((format(ANO_PRINTF_FORMAT_KIND, 5, 6)));

int log_vwrite(loglevel_t level, logroute_t route,
                   const char* sourceFile, int lineNumber,
                   const char* printFormat, va_list args) __attribute__((format(ANO_PRINTF_FORMAT_KIND, 5, 0)));


/* Configuration Functions */

// Open dir/<session-stamp>_ano.log (stamp: fs_session_stamp). Failure keeps previous.
[[nodiscard]] LogResult<> log_output_dir(const char* directoryPath);

// Runtime severity gate.
void log_set_level(loglevel_t min);

// Replace a level's default route. Must name a sink. Out-of-range ignored.
void log_set_route(loglevel_t level, logroute_t route);

// Drain all buffered records on the calling thread.
void log_flush(void);

[[nodiscard]] constexpr logroute_t operator|(logroute_t left,
                                                  logroute_t right) noexcept
{
    return static_cast<logroute_t>(
        static_cast<unsigned>(left) | static_cast<unsigned>(right));
}


/* Call-site Macros */

// _log  : level
// _rlog : level + route
// _olog : level + callsite file/line
// _rolog: level + route + callsite
#define ano_log(level, ...)                 log_write((level), ANO_ROUTE_DEFAULT, NULL, 0, __VA_ARGS__)
#define ano_rlog(level, route, ...)         log_write((level), (route), NULL, 0, __VA_ARGS__)
#define ano_olog(level, ...)                log_write((level), ANO_ROUTE_DEFAULT, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define ano_rolog(level, route, ...)        log_write((level), (route), __FILE_NAME__, __LINE__, __VA_ARGS__)

#ifdef DEBUG_BUILD
#define ano_debug_log(level, ...)           log_write((level), ANO_ROUTE_DEFAULT, NULL, 0, __VA_ARGS__)
#define ano_debug_rlog(level, route, ...)   log_write((level), (route), NULL, 0, __VA_ARGS__)
#define ano_debug_olog(level, ...)          log_write((level), ANO_ROUTE_DEFAULT, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define ano_debug_rolog(level, route, ...)  log_write((level), (route), __FILE_NAME__, __LINE__, __VA_ARGS__)
#else
#define ano_debug_log(...)  ((void)0)
#define ano_debug_rlog(...) ((void)0)
#define ano_debug_olog(...) ((void)0)
#define ano_debug_rolog(...)((void)0)
#endif

} // namespace ano
