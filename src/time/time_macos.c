/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Darwin: mach_absolute_time() is a register read. Apple Silicon is 24 MHz, not ns.
// ano_sleep uses absolute mach_wait_until + spin tail: QoS leeway stretches relative waits.

#if defined(__APPLE__)
#include "anoptic_time.h"

using namespace ano;
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <anoptic_atomic.h>


/* Precision Timestamps */

// ticks/s = 1e9 * denom / numer, cached once.
static ANO_ATOMIC(uint64_t) cachedTimebaseFreq = 0;

// Resolve timebase. cachedTimebaseFreq is nonzero on return.
static void initialize_timebase(void) {

    mach_timebase_info_data_t tb;
    uint64_t freq = 0;
    if (mach_timebase_info(&tb) == KERN_SUCCESS && tb.numer != 0)
        freq = (uint64_t)1000000000ULL * tb.denom / tb.numer;
    if (freq == 0) {
        printf("Failed to query mach timebase.\n");
        abort();   // No timebase, no engine; trips the crash blackbox.
    }
    atomic_store_explicit(&cachedTimebaseFreq, freq, memory_order_release);

    #ifdef DEBUG_BUILD
    printf("\nTimebase Frequency: %llu (numer=%u denom=%u)\n\n",
           (unsigned long long)freq, tb.numer, tb.denom);
    #endif
}

static inline uint64_t timebase_freq(void) {
    uint64_t freq = atomic_load_explicit(&cachedTimebaseFreq, memory_order_acquire);
    if (freq == 0) {
        initialize_timebase();
        freq = atomic_load_explicit(&cachedTimebaseFreq, memory_order_acquire);
    }
    return freq;
}

uint64_t ano::ano_timestamp_ticks() {
    return mach_absolute_time();
}

uint64_t ano::ano_ticks_to_ns(uint64_t ticks) {

    uint64_t freq = timebase_freq();

    // Scale in seconds + remainder to avoid overflow.
    uint64_t seconds = ticks / freq;
    uint64_t remainder = ticks % freq;
    remainder = remainder * 1000000000LL / freq;
    return remainder + (seconds * 1000000000LL);
}

uint64_t ano::ano_timestamp_raw() {
    return ano_ticks_to_ns(ano_timestamp_ticks());
}

uint64_t ano::ano_timestamp_us() {
    return ano_timestamp_raw() / 1000;
}

uint32_t ano::ano_timestamp_ms() {
    return (uint32_t)(ano_timestamp_raw() / 1000000LL);
}


/* Generic Date-Time Stamps */

int64_t ano::ano_timestamp_unix() {

    time_t currentTime;
    currentTime = time(NULL);

    if (currentTime == (time_t)-1) {
        perror("time()");
        return INT64_MIN; // Out-of-range sentinel.
    }

    return (int64_t)currentTime;
}

ano_datetime ano::ano_localtime(int64_t unix_seconds) {

    time_t t = (time_t)unix_seconds;
    struct tm tm;
    if (localtime_r(&t, &tm) == NULL)
        return (ano_datetime){0};

    return (ano_datetime){
        .year = tm.tm_year + 1900, .month = tm.tm_mon + 1, .day = tm.tm_mday,
        .hour = tm.tm_hour, .minute = tm.tm_min, .second = tm.tm_sec,
    };
}


/* Waiting Facilities */

int ano::ano_busywait(uint64_t ns) {

    if (ns > MAX_BUSYWAIT_NS) {
        printf("Requested busywait time exceeds maximum limit. Returning.\n");
        return -1;
    }

    uint64_t startTime = ano_timestamp_raw();
    uint64_t endTime;

    do {
        endTime = ano_timestamp_raw();
    } while (endTime - startTime < ns);

    return 0;
}

static uint64_t ano_ns_to_ticks(uint64_t ns) {

    uint64_t freq = timebase_freq();

    // Scale in seconds + remainder to avoid overflow.
    uint64_t seconds = ns / 1000000000LL;
    uint64_t remainder = ns % 1000000000LL;
    return seconds * freq + remainder * freq / 1000000000LL;
}

// Tail window spun instead of slept: kernel timer leeway cannot land closer than this.
#define ANO_SLEEP_SPIN_NS 500000ULL

// Absolute mach_wait_until in half-remainder steps, then spin ANO_SLEEP_SPIN_NS.
// QoS stretches relative waits ~1.5x; half-remainder is immune below 2x.
// Re-arming the absolute deadline absorbs KERN_ABORTED without drift.
int ano::ano_sleep(uint64_t us) {

    uint64_t waitTicks = ano_ns_to_ticks(us * 1000ULL);
    uint64_t deadline = mach_absolute_time() + waitTicks;
    uint64_t spinTicks = ano_ns_to_ticks(ANO_SLEEP_SPIN_NS);

    // Whole wait inside the spin window: a single kernel wait keeps the yield contract.
    if (us * 1000ULL <= ANO_SLEEP_SPIN_NS) {
        while (mach_absolute_time() < deadline)
            mach_wait_until(deadline);
        return 0;
    }

    uint64_t now;
    while ((now = mach_absolute_time()) + spinTicks < deadline) {
        uint64_t half = (deadline - spinTicks - now) / 2;
        mach_wait_until(now + (half > 0 ? half : 1));
    }

    while (mach_absolute_time() < deadline)
        ;

    return 0;
}

#endif
