/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private time-module scaling: overflow-safe ticks->ns shared by the platform timebases.
// Pure integer math; stdint.h only, no platform includes.

#ifndef ANO_TIME_SCALE_H
#define ANO_TIME_SCALE_H

#include <stdint.h>

namespace ano::time_scale {

// Convert raw counter ticks (value or delta) at freq ticks/s to nanoseconds.
//   in:  ticks uint64_t, freq uint64_t ticks/s (nonzero)
//   out: uint64_t nanoseconds
//   inv: no intermediate exceeds uint64_t while freq <= ~18.4 GHz and total ns fits
constexpr uint64_t ticks_to_ns(uint64_t ticks, uint64_t freq) {
    // Split into seconds and sub-seconds to scale without overflow.
    uint64_t largePart = ticks / freq;    // Seconds
    uint64_t smallPart = ticks % freq;    // Sub-seconds

    // Recombine the two parts.
    smallPart = smallPart * 1000000000ULL / freq;
    return smallPart + (largePart * 1000000000ULL);
}

// Boundary vectors 〜 a naive ticks*1e9 wraps uint64_t on the large-count rows; the split stays exact.
static_assert(ticks_to_ns(0u, 24000000u) == 0u, "zero ticks is zero ns");
static_assert(ticks_to_ns(123456789u, 1u) == 123456789000000000ULL, "freq=1 scales by exactly 1e9");
static_assert(ticks_to_ns(18446744073ULL, 1u) == 18446744073000000000ULL, "largest whole-second count just below 2^64/1e9");
static_assert(ticks_to_ns(24000000u, 24000000u) == 1000000000u, "one exact second at a 24 MHz timebase");
static_assert(ticks_to_ns(24000000ULL * 10000000000ULL + 12000000ULL, 24000000ULL) == 10000000000500000000ULL,
              "1e10 s + 0.5 s at 24 MHz: naive multiplication wraps, the split is exact");

} // namespace ano::time_scale

#endif // ANO_TIME_SCALE_H
