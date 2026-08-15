/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "anoptic_results.h"

namespace ano {


/* Paths */

#define MAXPATH 256

// Directory name under the platform user-data root. Callers lay out Config/, Saves/, etc. inside it.
// Windows: `%APPDATA%\ANO_GAME_NAME`  Linux: `~/.ANO_GAME_NAME`  macOS: `~/Library/Application Support/ANO_GAME_NAME`
#define ANO_GAME_NAME "anoptic"

// NUL-terminated syscall path. length excludes NUL.
struct fspath {
    uint16_t length;
    char str[MAXPATH];
};

enum class FilesystemError : uint8_t {
    invalid_argument, unavailable, path_too_long,
    out_of_memory, io,
};

template<class Value = void>
using FilesystemResult = Result<Value, FilesystemError>;

// Executable directory: no file name, no trailing separator except drive/FS root.
// Thread-safe: computed fresh per call, no shared state, no dirname().
[[nodiscard]] FilesystemResult<fspath> fs_gamepath(void);

// User data path (profiles, saves, settings). Creates if absent. Thread-safe.
[[nodiscard]] FilesystemResult<fspath> fs_userpath(void);

// Log directory: "<gamepath>/logs", home of <stamp>_ano.log / <stamp>_CRASH.log.
// Creates if absent. Thread-safe.
[[nodiscard]] FilesystemResult<fspath> fs_logpath(void);

// Session stamp: "YYYY-MM-DD_XXXXXX" = local date + low six digits of raw ticks.
// Latched at first call; names this session's files. Thread-safe.
// Output: NUL-terminated stamp, static storage.
const char *fs_session_stamp(void);

// Set CWD to the executable directory so relative asset loads resolve.
[[nodiscard]] FilesystemResult<> fs_chdir_gamepath(void);


/* Append-Only File */

// Opaque handle: platform fd/HANDLE stays in the per-OS source.
struct fs_file;

// Open `path` for append (OS append mode), create if absent. Concurrent appends do not interleave.
[[nodiscard]] FilesystemResult<fs_file*> fs_open_append(const char *path);

// Open `path` for append after truncate to zero, create if absent.
// For a file this session owns from its first byte (the logger's init open).
[[nodiscard]] FilesystemResult<fs_file*> fs_open_trunc(const char *path);

// Write all `length` bytes, looping past short writes.
[[nodiscard]] FilesystemResult<> fs_write(
    fs_file *file, const void *data, size_t length);

// Flush to the device (fsync / FlushFileBuffers).
[[nodiscard]] FilesystemResult<> fs_sync(fs_file *file);

// Close and free without sync. Call fs_sync for durability.
[[nodiscard]] FilesystemResult<> fs_close(fs_file *file);

} // namespace ano
