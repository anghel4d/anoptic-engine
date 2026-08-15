/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(__linux__)
#include "anoptic_time.h"

using namespace ano;
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

/* Precision Timestamps */

// Returned ticks are nanoseconds (CLOCK_MONOTONIC timespec).
uint64_t ano::timestamp_ticks() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);   // constant clockid + valid pointer: cannot fail
    return (uint64_t)(ts.tv_sec * 1000000000LL) + ts.tv_nsec;
}

// Identity: Linux ticks are already ns.
uint64_t ano::ticks_to_ns(uint64_t ticks) {
    return ticks;
}

uint64_t ano::timestamp_raw() {
    return timestamp_ticks();
}

// Direct timespec; not timestamp_raw / 1000.
uint64_t ano::timestamp_us() {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000000LL) + (ts.tv_nsec / 1000);
}

// Direct timespec; not timestamp_raw / 1e6.
uint32_t ano::timestamp_ms() {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000) + (ts.tv_nsec / 1000000LL);
}


/* Generic Date-Time Stamps */

TimeResult<int64_t> ano::timestamp_unix() {
    time_t currentTime;
    currentTime = time(NULL);

    if (currentTime == (time_t)-1) {
        return failure(TimeError::unavailable);
    }

    return (int64_t)currentTime;
}

TimeResult<datetime> ano::local_datetime(int64_t unix_seconds) {
    time_t t = (time_t)unix_seconds;
    struct tm tm;
    if (localtime_r(&t, &tm) == NULL)
        return failure(TimeError::invalid_argument);

    return (datetime){
        .year = tm.tm_year + 1900, .month = tm.tm_mon + 1, .day = tm.tm_mday,
        .hour = tm.tm_hour, .minute = tm.tm_min, .second = tm.tm_sec,
    };
}

/* Waiting Facilities */

TimeResult<> ano::busywait(uint64_t ns) {
    if (ns > MAX_BUSYWAIT_NS)
        return failure(TimeError::invalid_argument);

    uint64_t startTime = timestamp_raw();
    uint64_t endTime;

    do {
        endTime = timestamp_raw();
    } while (endTime - startTime < ns);

    return {};
}

// Relative clock_nanosleep(CLOCK_MONOTONIC). Restarts on EINTR.
TimeResult<> ano::sleep_us(uint64_t us) {
    struct timespec request = {0};
    struct timespec remaining = {0};

    request.tv_sec = us / 1000000LL;
    request.tv_nsec = (us % (uint64_t)1000000LL) * 1000;

    int sleepStatus;
    while ((sleepStatus = clock_nanosleep(CLOCK_MONOTONIC, 0, &request, &remaining)) != 0) {
        if (sleepStatus == EINTR) {
            request = remaining;
        } else {
            return failure(sleepStatus == EINVAL
                ? TimeError::invalid_argument : TimeError::platform);
        }
    }

    return {};
}

#endif
