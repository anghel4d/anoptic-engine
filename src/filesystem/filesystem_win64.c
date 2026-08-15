/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(_WIN32)

#include "anoptic_filesystem.h"

using namespace ano;
#include "filesystem/filesystem_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <errno.h>
#include <windows.h>
#include <libloaderapi.h>
#include <mimalloc.h>


/* Paths */

// GetModuleFileNameA (not TCHAR). -A mangles paths outside the active codepage.
// Debt: GetModuleFileNameW + UTF-8.
FilesystemResult<fspath> ano::fs_gamepath(void) {

    fspath result = {0};

    char pathBuffer[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, pathBuffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return failure(len == 0 ? FilesystemError::unavailable
                                : FilesystemError::path_too_long);

    while (len > 0 && pathBuffer[len - 1] != '\\' && pathBuffer[len - 1] != '/')
        len--;
    // Drop trailing separator; keep for drive root ("C:" is drive-relative, "C:\" is root).
    if (len > 3)
        len--;

    if (len >= MAXPATH)
        return failure(FilesystemError::path_too_long);
    memcpy(result.str, pathBuffer, len);
    result.str[len] = '\0';
    result.length = (uint16_t)len;
    return result;
}

FilesystemResult<fspath> ano::fs_userpath(void) {
    fspath result = {0};

    const char *appdata = getenv("APPDATA");
    if (appdata == NULL || appdata[0] == '\0')
        return failure(FilesystemError::unavailable);

    int len = snprintf(result.str, MAXPATH, "%s\\" ANO_GAME_NAME, appdata);
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
    return result_if(_chdir(dir->str) == 0, FilesystemError::io);
}

// 0 when path is a directory afterwards. EEXIST succeeds only if it is a real directory.
int fs_mkdir(const char *path)
{
    if (_mkdir(path) == 0)
        return 0;
    if (errno != EEXIST)
        return -1;

    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) ? 0 : -1;
}


/* Append-Only File */

struct ano::fs_file {
    HANDLE handle;
};

// FILE_SHARE_DELETE: POSIX unlink parity while open.
FilesystemResult<fs_file*> ano::fs_open_append(const char *path)
{
    if (path == NULL)
        return failure(FilesystemError::invalid_argument);

    HANDLE handle = CreateFileA(path, FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return failure(FilesystemError::io);

    fs_file *file = mi_malloc_tp(fs_file);
    if (file == NULL) {
        CloseHandle(handle);
        return failure(FilesystemError::out_of_memory);
    }
    file->handle = handle;
    return file;
}

// Truncate with a throwaway CREATE_ALWAYS (needs GENERIC_WRITE), then reopen FILE_APPEND_DATA.
FilesystemResult<fs_file*> ano::fs_open_trunc(const char *path)
{
    if (path == NULL)
        return failure(FilesystemError::invalid_argument);

    HANDLE trunc = CreateFileA(path, GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (trunc == INVALID_HANDLE_VALUE)
        return failure(FilesystemError::io);
    CloseHandle(trunc);
    return fs_open_append(path);
}

// written == 0 on TRUE is error (not retry).
FilesystemResult<> ano::fs_write(
    fs_file *file, const void *data, size_t length)
{
    if (file == NULL || (data == NULL && length != 0))
        return failure(FilesystemError::invalid_argument);

    const char *cursor = static_cast<const char *>(data);
    size_t remaining = length;
    while (remaining > 0) {
        DWORD chunk = remaining > 0x7fffffff ? 0x7fffffff : (DWORD)remaining; // DWORD cap; 2 GiB-1 per call
        DWORD written = 0;
        if (!WriteFile(file->handle, cursor, chunk, &written, NULL) || written == 0)
            return failure(FilesystemError::io);
        cursor += written;
        remaining -= written;
    }
    return {};
}

FilesystemResult<> ano::fs_sync(fs_file *file)
{
    if (file == NULL)
        return failure(FilesystemError::invalid_argument);
    return result_if(FlushFileBuffers(file->handle), FilesystemError::io);
}

// Handle freed either way.
FilesystemResult<> ano::fs_close(fs_file *file)
{
    if (file == NULL)
        return failure(FilesystemError::invalid_argument);
    const bool closed = CloseHandle(file->handle);
    mi_free(file);
    return result_if(closed, FilesystemError::io);
}

#endif // _WIN32
