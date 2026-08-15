/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(__APPLE__)

#include "anoptic_filesystem.h"

using namespace ano;
#include "filesystem/filesystem_internal.h"

#include <mach-o/dyld.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <mimalloc.h>


/* Paths */

// _NSGetExecutablePath + realpath. Hand-rolled split: dirname() is not portably reentrant.
FilesystemResult<fspath> ano::fs_gamepath(void)
{
    fspath result = {0};

    char raw[PATH_MAX];
    uint32_t size = sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) != 0)
        return failure(FilesystemError::path_too_long);

    char resolved[PATH_MAX];
    if (realpath(raw, resolved) == NULL)
        return failure(FilesystemError::unavailable);

    size_t len = strlen(resolved);
    while (len > 0 && resolved[len - 1] != '/')
        len--;
    if (len > 1)
        len--; // drop trailing slash, keep "/" for root

    if (len >= MAXPATH)
        return failure(FilesystemError::path_too_long);
    memcpy(result.str, resolved, len);
    result.str[len] = '\0';
    result.length = (uint16_t)len;
    return result;
}

// ~/Library/Application Support/anoptic, created if absent. Parent always exists on macOS.
FilesystemResult<fspath> ano::fs_userpath(void)
{
    fspath result = {0};

    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0')
        return failure(FilesystemError::unavailable);

    int len = snprintf(result.str, MAXPATH, "%s/Library/Application Support/" ANO_GAME_NAME, home);
    if (len < 0 || len >= MAXPATH)
        return failure(FilesystemError::path_too_long);

    if (fs_mkdir(result.str) != 0)
        return failure(FilesystemError::io);

    result.length = (uint16_t)len;
    return result;
}

FilesystemResult<> ano::fs_chdir_gamepath(void)
{
    const auto dir = fs_gamepath();
    if (!dir)
        return failure(dir.error());
    return result_if(chdir(dir->str) == 0, FilesystemError::io);
}

// 0 when path is a directory afterwards. EEXIST succeeds only if it is a real directory.
int fs_mkdir(const char *path)
{
    if (mkdir(path, 0755) == 0)
        return 0;
    if (errno != EEXIST)
        return -1;

    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : -1; // stat follows symlinks
}


/* Append-Only File */

struct ano::fs_file {
    int fd;
};

static FilesystemResult<fs_file*> open_file(const char *path, int flags)
{
    if (path == NULL)
        return failure(FilesystemError::invalid_argument);

    const int fd = open(path, flags, 0644);
    if (fd < 0)
        return failure(FilesystemError::io);

    fs_file *file = mi_malloc_tp(fs_file);
    if (file == NULL) {
        close(fd);
        return failure(FilesystemError::out_of_memory);
    }
    file->fd = fd;
    return file;
}

FilesystemResult<fs_file*> ano::fs_open_append(const char *path)
{
    return open_file(path, O_WRONLY | O_CREAT | O_APPEND);
}

FilesystemResult<fs_file*> ano::fs_open_trunc(const char *path)
{
    return open_file(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND);
}

// 0 once all bytes are written. Loops past short writes and EINTR.
FilesystemResult<> ano::fs_write(
    fs_file *file, const void *data, size_t length)
{
    if (file == NULL || (data == NULL && length != 0))
        return failure(FilesystemError::invalid_argument);

    const char *cursor = static_cast<const char *>(data);
    size_t remaining = length;
    while (remaining > 0) {
        ssize_t written = write(file->fd, cursor, remaining);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return failure(FilesystemError::io);
        }
        cursor += written;
        remaining -= (size_t)written;
    }
    return {};
}

FilesystemResult<> ano::fs_sync(fs_file *file)
{
    if (file == NULL)
        return failure(FilesystemError::invalid_argument);
    return result_if(fsync(file->fd) == 0, FilesystemError::io);
}

// Handle freed either way.
FilesystemResult<> ano::fs_close(fs_file *file)
{
    if (file == NULL)
        return failure(FilesystemError::invalid_argument);
    const bool closed = close(file->fd) == 0;
    mi_free(file);
    return result_if(closed, FilesystemError::io);
}

#endif // __APPLE__
