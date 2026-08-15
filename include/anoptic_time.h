/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Platform time: monotonic clocks, civil time, busywait, and yielding sleep.

#pragma once

#include <stdint.h>
#include "anoptic_results.h"

namespace ano {

enum class TimeError : uint8_t {
    invalid_argument, overflow, unavailable, platform,
};

template<class Value = void>
using TimeResult = Result<Value, TimeError>;


/* Timestamps */

// Busywait hard cap: 1 second.
#define MAX_BUSYWAIT_NS 1000000000ULL

// Monotonic timestamp in nanoseconds. Excludes time the system spends suspended.
uint64_t timestamp_raw();

// Raw monotonic hardware counter. No unit conversion.
// Units: mach ticks (Darwin), TSC/QPC (Windows), ns (Linux). Value or delta. Convert via ticks_to_ns.
uint64_t timestamp_ticks();

// Convert a raw counter value or delta from timestamp_ticks to nanoseconds.
uint64_t ticks_to_ns(uint64_t ticks);

// Monotonic timestamp in microseconds.
uint64_t timestamp_us();

// Monotonic timestamp in milliseconds.
uint32_t timestamp_ms();

// Unix UTC timestamp (seconds). Not guaranteed monotonic.
[[nodiscard]] TimeResult<int64_t> timestamp_unix();


/* Civil Time */

// Local civil time. The platform layer wraps localtime_r / localtime_s.
struct datetime {
    int year;    // full year, e.g. 2026
    int month;   // 1-12
    int day;     // 1-31
    int hour;    // 0-23
    int minute;  // 0-59
    int second;  // 0-60 (60 on a leap second)
};

// Unix seconds to local civil time.
[[nodiscard]] TimeResult<datetime> local_datetime(int64_t unix_seconds);


/* Sleep */

// Spin the calling thread for ns nanoseconds. Cap is MAX_BUSYWAIT_NS.
[[nodiscard]] TimeResult<> busywait(uint64_t ns);

// Sleep for us microseconds. Yields to the scheduler.
[[nodiscard]] TimeResult<> sleep_us(uint64_t us);

} // namespace ano
