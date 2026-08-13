/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#ifdef _WIN64

#include <anoptic_memory.h>
#include <mimalloc.h>

// mi_malloc_aligned returns a live block for size 0.
void* ano_aligned_malloc(size_t size, size_t alignment) {
    if (size == 0 || alignment == 0) return NULL;
    return mi_malloc_aligned(size, alignment);
}

void ano_aligned_free(void* ptr) {
    mi_free(ptr);
}

#endif
