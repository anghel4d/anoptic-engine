/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(__linux__)

#include "anoptic_filesystem.h"

using namespace ano;
#include "filesystem/filesystem_internal.h"

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

// readlink("/proc/self/exe"). Hand-rolled split: dirname() is not portably reentrant.
FilesystemResult<ano_fspath> ano::ano_fs_gamepath(void)
{
    ano_fspath result = {0};

    char raw[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", raw, sizeof(raw) - 1);
    if (n <= 0)
        return failure(FilesystemError::unavailable);
    raw[n] = '\0'; // readlink does not NUL-terminate

    size_t len = (size_t)n;
    while (len > 0 && raw[len - 1] != '/')
        len--;
    if (len > 1)
        len--; // drop trailing slash, keep "/" for root

    if (len >= MAXPATH)
        return failure(FilesystemError::path_too_long);
    memcpy(result.str, raw, len);
    result.str[len] = '\0';
    result.length = (uint16_t)len;
    return result;
}

FilesystemResult<ano_fspath> ano::ano_fs_userpath(void)
{
    ano_fspath result = {0};

    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0')
        return failure(FilesystemError::unavailable);

    int len = snprintf(result.str, MAXPATH, "%s/." ANO_GAME_NAME, home);
    if (len < 0 || len >= MAXPATH)
        return failure(FilesystemError::path_too_long);

    if (fs_mkdir(result.str) != 0)
        return failure(FilesystemError::io);

    result.length = (uint16_t)len;
    return result;
}

FilesystemResult<> ano::ano_fs_chdir_gamepath(void)
{
    const auto dir = ano_fs_gamepath();
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

struct ano::ano_file {
    int fd;
};

static FilesystemResult<ano_file*> open_file(const char *path, int flags)
{
    if (path == NULL)
        return failure(FilesystemError::invalid_argument);

    const int fd = open(path, flags, 0644);
    if (fd < 0)
        return failure(FilesystemError::io);

    ano_file *file = mi_malloc_tp(ano_file);
    if (file == NULL) {
        close(fd);
        return failure(FilesystemError::out_of_memory);
    }
    file->fd = fd;
    return file;
}

FilesystemResult<ano_file*> ano::ano_fs_open_append(const char *path)
{
    return open_file(path, O_WRONLY | O_CREAT | O_APPEND);
}

FilesystemResult<ano_file*> ano::ano_fs_open_trunc(const char *path)
{
    return open_file(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND);
}

// 0 once all bytes are written. Loops past short writes and EINTR.
FilesystemResult<> ano::ano_fs_write(
    ano_file *file, const void *data, size_t length)
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

FilesystemResult<> ano::ano_fs_sync(ano_file *file)
{
    if (file == NULL)
        return failure(FilesystemError::invalid_argument);
    return result_if(fsync(file->fd) == 0, FilesystemError::io);
}

// Handle freed either way.
FilesystemResult<> ano::ano_fs_close(ano_file *file)
{
    if (file == NULL)
        return failure(FilesystemError::invalid_argument);
    const bool closed = close(file->fd) == 0;
    mi_free(file);
    return result_if(closed, FilesystemError::io);
}

#endif // __linux__
