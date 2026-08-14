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
ano_fspath ano::ano_fs_gamepath(void) {

    ano_fspath result = {0};

    char pathBuffer[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, pathBuffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return result; // failed or truncated

    while (len > 0 && pathBuffer[len - 1] != '\\' && pathBuffer[len - 1] != '/')
        len--;
    // Drop trailing separator; keep for drive root ("C:" is drive-relative, "C:\" is root).
    if (len > 3)
        len--;

    if (len >= MAXPATH)
        return result;
    memcpy(result.str, pathBuffer, len);
    result.str[len] = '\0';
    result.length = (uint16_t)len;
    return result;
}

ano_fspath ano::ano_fs_userpath(void) {
    ano_fspath result = {0};

    const char *appdata = getenv("APPDATA");
    if (appdata == NULL || appdata[0] == '\0')
        return result;

    int len = snprintf(result.str, MAXPATH, "%s\\" ANO_GAME_NAME, appdata);
    if (len < 0 || len >= MAXPATH)
        return (ano_fspath){0};

    if (fs_mkdir(result.str) != 0)
        return (ano_fspath){0};

    result.length = (uint16_t)len;
    return result;
}

bool ano::ano_fs_chdir_gamepath(void)
{
    ano_fspath dir = ano_fs_gamepath();
    return dir.length > 0 && _chdir(dir.str) == 0;
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

struct ano::ano_file {
    HANDLE handle;
};

// FILE_SHARE_DELETE: POSIX unlink parity while open.
ano_file *ano::ano_fs_open_append(const char *path)
{
    if (path == NULL)
        return NULL;

    HANDLE handle = CreateFileA(path, FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return NULL;

    ano_file *file = mi_malloc_tp(ano_file);
    if (file == NULL) {
        CloseHandle(handle);
        return NULL;
    }
    file->handle = handle;
    return file;
}

// Truncate with a throwaway CREATE_ALWAYS (needs GENERIC_WRITE), then reopen FILE_APPEND_DATA.
ano_file *ano::ano_fs_open_trunc(const char *path)
{
    if (path == NULL)
        return NULL;

    HANDLE trunc = CreateFileA(path, GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (trunc == INVALID_HANDLE_VALUE)
        return NULL;
    CloseHandle(trunc);
    return ano_fs_open_append(path);
}

// written == 0 on TRUE is error (not retry).
int ano::ano_fs_write(ano_file *file, const void *data, size_t length)
{
    if (file == NULL || (data == NULL && length != 0))
        return -1;

    const char *cursor = static_cast<const char *>(data);
    size_t remaining = length;
    while (remaining > 0) {
        DWORD chunk = remaining > 0x7fffffff ? 0x7fffffff : (DWORD)remaining; // DWORD cap; 2 GiB-1 per call
        DWORD written = 0;
        if (!WriteFile(file->handle, cursor, chunk, &written, NULL) || written == 0)
            return -1;
        cursor += written;
        remaining -= written;
    }
    return 0;
}

int ano::ano_fs_sync(ano_file *file)
{
    if (file == NULL)
        return -1;
    return FlushFileBuffers(file->handle) ? 0 : -1;
}

// Handle freed either way.
int ano::ano_fs_close(ano_file *file)
{
    if (file == NULL)
        return -1;
    int rc = CloseHandle(file->handle) ? 0 : -1;
    mi_free(file);
    return rc;
}

#endif // _WIN32
