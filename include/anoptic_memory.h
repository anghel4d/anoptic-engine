/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTICENGINE_ANOPTIC_MEMORY_H
#define ANOPTICENGINE_ANOPTIC_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h> // before mimalloc-override.h (MinGW _msize/_aligned_msize)
#include <string.h> // before mimalloc-override.h (MinGW strdup)
#include <mimalloc.h>
#if MI_MALLOC_VERSION < 30405 || MI_MALLOC_VERSION >= 40000
#error "Anoptic requires mimalloc 3.4.5 or newer from the v3 API series"
#endif
#if defined(__APPLE__)
#include <unistd.h> // before mimalloc-override.h (Darwin valloc)
#else
#include <malloc.h>
#endif
#include <mimalloc-override.h>
#if defined(__linux__) || defined(__APPLE__)
#include <alloca.h>
#endif

#ifdef __cplusplus
#define ANO_MEMORY_CONSTEXPR constexpr
#else
#define ANO_MEMORY_CONSTEXPR
#endif

// Checked size arithmetic. Failure leaves *result untouched.
static inline ANO_MEMORY_CONSTEXPR bool ano_size_add(
    size_t lhs, size_t rhs, size_t *result)
{
    if (result == NULL || rhs > SIZE_MAX - lhs)
        return false;
    *result = lhs + rhs;
    return true;
}

static inline ANO_MEMORY_CONSTEXPR bool ano_size_multiply(
    size_t lhs, size_t rhs, size_t *result)
{
    if (result == NULL || (lhs != 0 && rhs > SIZE_MAX / lhs))
        return false;
    *result = lhs * rhs;
    return true;
}

static inline ANO_MEMORY_CONSTEXPR bool ano_size_align(
    size_t value, size_t alignment, size_t *result)
{
    if (result == NULL || alignment == 0
        || (alignment & (alignment - 1)) != 0)
        return false;
    const size_t mask = alignment - 1;
    if (value > SIZE_MAX - mask)
        return false;
    *result = (value + mask) & ~mask;
    return true;
}

#undef ANO_MEMORY_CONSTEXPR

#ifdef __cplusplus
extern "C" {
#endif

// Hardware interference sizes. Compile-time only (alignas / struct layout); not a runtime query.
// ANO_CACHE_LINE: true coherency grain for packing / line-granular reservation (128 Apple aarch64, else 64).
// ANO_THREAD_LINE: false-sharing isolation for hot per-thread atomics. Align cursors to it.
// Default ANO_THREAD_LINE is 128 on every target.
// Override: -DANO_CACHE_LINE=N / -DANO_THREAD_LINE=N.
#ifndef ANO_CACHE_LINE
#if defined(__APPLE__) && defined(__aarch64__)
#define ANO_CACHE_LINE 128
#else
#define ANO_CACHE_LINE 64       // x86-64 and generic arm64
#endif
#endif
#ifndef ANO_THREAD_LINE
#define ANO_THREAD_LINE 128
#endif

// First-class lifetime heaps accept allocations from every thread. Destroying
// one winks out all of its live allocations after its users have stopped.
mi_heap_t *ano_heap_create(void);
void ano_heap_destroy(mi_heap_t *heap);
void ano_heap_cleanup(mi_heap_t **heap);

// Scoped lifetime heap: mi_heap_t *heap ANO_SCOPED_HEAP = ano_heap_create();
#define ANO_SCOPED_HEAP __attribute__((__cleanup__(ano_heap_cleanup)))

// Stack alloc. Overflow risk.
#define ano_salloc(bytes) alloca((size_t)bytes)

// Allocates size bytes aligned to alignment (power of 2). mi_malloc_aligned wrapper.
// Returns pointer, or NULL on failure. NULL if size or alignment is 0.
void* ano_aligned_malloc(size_t size, size_t alignment);

// Frees a block from ano_aligned_malloc. Else UB.
void ano_aligned_free(void* ptr);

#ifdef __cplusplus
}
#endif

#endif //ANOPTICENGINE_ANOPTIC_MEMORY_H
