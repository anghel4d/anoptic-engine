/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// C++ typed allocation for plain engine data. Header-only; no ownership surface.

#ifndef ANOPTICENGINE_ANOPTIC_MEMORY_TYPED_H
#define ANOPTICENGINE_ANOPTIC_MEMORY_TYPED_H

#include <anoptic_memory.h>
#include <anoptic_meta.h>

#include <cstddef>

namespace ano {

template<Data T>
[[nodiscard]] T* allocate(std::size_t count) noexcept
{
    return mi_mallocn_tp(T, count);
}

template<Data T>
[[nodiscard]] T* allocate_zero(std::size_t count) noexcept
{
    return mi_calloc_tp(T, count);
}

template<Data T>
[[nodiscard]] T* reallocate(T* data, std::size_t count) noexcept
{
    return mi_reallocn_tp(T, data, count);
}

template<Data T>
[[nodiscard]] T* heap_allocate(mi_heap_t* heap, std::size_t count) noexcept
{
    return mi_heap_mallocn_tp(T, heap, count);
}

template<Data T>
[[nodiscard]] T* heap_allocate_zero(mi_heap_t* heap, std::size_t count) noexcept
{
    return mi_heap_calloc_tp(T, heap, count);
}

template<Data T>
[[nodiscard]] T* heap_reallocate(mi_heap_t* heap, T* data, std::size_t count) noexcept
{
    return mi_heap_reallocn_tp(T, heap, data, count);
}

} // namespace ano

#endif // ANOPTICENGINE_ANOPTIC_MEMORY_TYPED_H
