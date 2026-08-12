/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Logger lifecycle, configuration, and raw dynamic-format boundary.
// Message emission uses <ano/log.h> and ano::log(...).

#pragma once

#include <ano/log.h>

#include <stdarg.h>

// MinGW's plain printf checker models legacy MSVCRT; Anoptic targets UCRT and C99 formats.
#if defined(__MINGW32__)
#define ANO_PRINTF_FORMAT_KIND gnu_printf
#else
#define ANO_PRINTF_FORMAT_KIND printf
#endif

extern "C" {

// Startup / shutdown. 0 on success.
int ano_log_init(void);
int ano_log_cleanup(void);

// Scope-bound teardown, LOCALHEAPATTR-style (anoptic_memory.h).
void ano_log_scope_release(const int *initStatus);
#define ANO_LOG_SCOPE_ATTR __attribute__((__cleanup__(ano_log_scope_release)))

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
