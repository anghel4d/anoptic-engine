/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Common TU: session stamp and log directory.

#include "anoptic_filesystem.h"

using namespace ano;
#include "filesystem/filesystem_internal.h"

#include <anoptic_time.h>

#include <anoptic_atomic.h>
#include <stdio.h>
#include <string.h>


/* Session Stamp */

// One stamp per process, latched by first caller. Racing loser spins on winner.
const char *ano::fs_session_stamp(void)
{
    static ANO_ATOMIC(int) state;   // 0 unset, 1 building, 2 ready
    static char stamp[24];      // "YYYY-MM-DD_XXXXXX" is 17 + NUL
    if (atomic_load_explicit(&state, memory_order_acquire) != 2) {
        int expect = 0;
        if (atomic_compare_exchange_strong_explicit(&state, &expect, 1,
                memory_order_acquire, memory_order_acquire)) {
            const auto unixTime = timestamp_unix();
            datetime d = unixTime
                ? local_datetime(*unixTime).value_or(datetime{1970, 1, 1})
                : datetime{1970, 1, 1};
            unsigned ctr = (unsigned)(timestamp_ticks() % 1000000u);
            snprintf(stamp, sizeof stamp, "%04d-%02d-%02d_%06u", d.year, d.month, d.day, ctr);
            atomic_store_explicit(&state, 2, memory_order_release);
        } else {
            while (atomic_load_explicit(&state, memory_order_acquire) != 2)
                (void)busywait(100);
        }
    }
    return stamp;
}


/* Log Path */

FilesystemResult<fspath> ano::fs_logpath(void)
{
    auto game = fs_gamepath();
    if (!game)
        return failure(game.error());
    fspath dir = *game;
    if (dir.length + 5 >= MAXPATH)
        return failure(FilesystemError::path_too_long);
    memcpy(dir.str + dir.length, "/logs", 6);   // 6 includes NUL
    dir.length += 5;
    if (fs_mkdir(dir.str) != 0)
        return failure(FilesystemError::io);
    return dir;
}
