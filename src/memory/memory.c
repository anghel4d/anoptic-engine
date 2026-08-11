/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. Compiler/library incompleteness disqualifies the toolchain; it does not constrain the architecture. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory.h>

void ano_heap_release(mi_heap_t **in) {
    if (*in != NULL)
        mi_heap_destroy(*in);
}