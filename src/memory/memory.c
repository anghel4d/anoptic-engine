/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory.h>

namespace std {

// Weak terminate: no-runtime, no-exception containers require this path.
[[gnu::weak, noreturn]] void terminate() noexcept
{
    __builtin_trap();
}

} // namespace std

mi_heap_t *ano_heap_create(void)
{
    return mi_heap_new();
}

void ano_heap_destroy(mi_heap_t *heap)
{
    if (heap == NULL)
        return;
    // v3 caches the last first-class heap's theap. Move this thread's cache
    // while the retiring heap still exists; no allocation is required.
    (void)mi_heap_theap(mi_heap_main());
    mi_heap_destroy(heap);
}

void ano_heap_cleanup(mi_heap_t **heap)
{
    if (heap != NULL)
        ano_heap_destroy(*heap);
}
