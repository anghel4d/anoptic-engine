/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Per-platform primitives for filesystem.c.

#ifndef FILESYSTEM_INTERNAL_H
#define FILESYSTEM_INTERNAL_H


/* Directory */

// Create `path` if absent. Parents must already exist.
// EEXIST is success only when the existing entry is a directory.
// Output: 0 when `path` is a directory afterwards, -1 otherwise.
int fs_mkdir(const char *path);

#endif // FILESYSTEM_INTERNAL_H
