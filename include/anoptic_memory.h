/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <span>
#include "anoptic_meta.h"
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

namespace ano {

// Hardware interference sizes. Compile-time only (alignas / struct layout); not a runtime query.
// ANO_CACHE_LINE: coherency grain for packing (128 Apple aarch64, else 64).
// ANO_THREAD_LINE: false-sharing isolation for hot per-thread atomics. Default 128.
// Override: -DANO_CACHE_LINE=N / -DANO_THREAD_LINE=N.
#ifndef ANO_CACHE_LINE
#if defined(__APPLE__) && defined(__aarch64__)
#define ANO_CACHE_LINE 128
#else
#define ANO_CACHE_LINE 64
#endif
#endif
#ifndef ANO_THREAD_LINE
#define ANO_THREAD_LINE 128
#endif

// First-class heap: any thread may allocate. Destroy winks out every live
// allocation after users have stopped.
mi_heap_t *heap_create(void);
void heap_destroy(mi_heap_t *heap);
void heap_cleanup(mi_heap_t **heap);

// Scoped lifetime heap: mi_heap_t *heap ANO_SCOPED_HEAP = heap_create();
#define ANO_SCOPED_HEAP __attribute__((__cleanup__(heap_cleanup)))

// Stack alloc. Overflow risk.
#define ano_salloc(bytes) alloca((size_t)bytes)

// mi_malloc_aligned wrapper. Power-of-2 alignment. NULL if size or alignment is 0.
void* aligned_malloc(size_t size, size_t alignment);

// Only a pointer from aligned_malloc. Else UB.
void aligned_free(void* ptr);

struct MemoryReservation final { size_t offset = 0; size_t size = 0; };

struct MemoryLayoutCursor final {
    size_t size = 0;
    size_t alignment = 1;

    [[nodiscard]] constexpr ArithmeticResult<MemoryReservation> reserve(
        size_t bytes, size_t requestedAlignment) noexcept
    {
        if (requestedAlignment == 0
            || (requestedAlignment & (requestedAlignment - 1)) != 0) {
            return failure(ArithmeticError::invalid_alignment);
        }
        if (bytes == 0)
            return MemoryReservation{size, 0};
        const auto offset = checked_align(size, requestedAlignment);
        const auto end = offset.and_then(
            [&](size_t aligned) { return checked_add(aligned, bytes); });
        if (!end)
            return failure(end.error());
        const MemoryReservation reservation{*offset, bytes};
        size = *end;
        if (requestedAlignment > alignment)
            alignment = requestedAlignment;
        return reservation;
    }
};

struct MemoryRegion;
struct MemoryVolume;

enum class MemoryError : uint8_t {
    invalid_argument, invalid_layout, overflow, out_of_memory,
    immutable, unsealed, out_of_bounds,
};

template<class Value = void>
using MemoryResult = Result<Value, MemoryError>;

// Unique mimalloc lifetime domain. Reset and destroy only after every
// allocation is unreachable.
[[nodiscard]] MemoryResult<MemoryRegion *> memory_region_create() noexcept;
void memory_region_destroy(MemoryRegion *region) noexcept;
[[nodiscard]] MemoryResult<> memory_region_reset(MemoryRegion *region) noexcept;
[[nodiscard]] MemoryResult<void *> memory_region_allocate(
    MemoryRegion *region, size_t size, size_t alignment) noexcept;
[[nodiscard]] MemoryResult<void *> memory_region_allocate_zero(
    MemoryRegion *region, size_t size, size_t alignment) noexcept;

// Exclusive region plus one contiguous payload. Write before seal; view after.
// Last release winks out the region.
[[nodiscard]] MemoryResult<MemoryVolume *> memory_volume_create(
    MemoryLayoutCursor layout) noexcept;
[[nodiscard]] MemoryResult<> memory_volume_retain(MemoryVolume *volume) noexcept;
void memory_volume_release(MemoryVolume *volume) noexcept;
[[nodiscard]] MemoryResult<std::span<uint8_t>> memory_volume_write(
    MemoryVolume *volume, MemoryReservation reservation) noexcept;
[[nodiscard]] MemoryResult<> memory_volume_seal(MemoryVolume *volume) noexcept;
[[nodiscard]] MemoryResult<std::span<const uint8_t>> memory_volume_view(
    const MemoryVolume *volume, MemoryReservation reservation) noexcept;

} // namespace ano
