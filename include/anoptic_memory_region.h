/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Measured contiguous storage with bulk lifetime and immutable publication.

#ifndef ANOPTICENGINE_ANOPTIC_MEMORY_REGION_H
#define ANOPTICENGINE_ANOPTIC_MEMORY_REGION_H

#include "anoptic_memory.h"

typedef struct AnoMemoryLayout {
    size_t size;
    size_t alignment;
    bool valid;
} AnoMemoryLayout;

typedef struct AnoMemoryReservation {
    size_t offset;
    size_t size;
} AnoMemoryReservation;

typedef struct AnoMemoryView {
    const void *data;
    size_t size;
} AnoMemoryView;

typedef struct AnoMemoryMutableView {
    void *data;
    size_t size;
} AnoMemoryMutableView;

typedef struct AnoMemoryRegion AnoMemoryRegion;

#ifdef __cplusplus
#define ANO_MEMORY_REGION_CONSTEXPR constexpr
#else
#define ANO_MEMORY_REGION_CONSTEXPR
#endif

static inline ANO_MEMORY_REGION_CONSTEXPR AnoMemoryLayout
ano_memory_layout(void)
{
    return (AnoMemoryLayout){ .size = 0, .alignment = 1, .valid = true };
}

// Appends an aligned byte range. Overflow or an invalid alignment poisons the
// layout so it cannot later be allocated accidentally.
static inline ANO_MEMORY_REGION_CONSTEXPR bool ano_memory_layout_reserve(
    AnoMemoryLayout *layout, size_t size, size_t alignment,
    AnoMemoryReservation *reservation)
{
    if (layout == NULL || reservation == NULL)
        return false;
    reservation->offset = 0;
    reservation->size = 0;
    if (!layout->valid || alignment == 0
        || (alignment & (alignment - 1)) != 0) {
        layout->valid = false;
        return false;
    }
    if (size == 0) {
        reservation->offset = layout->size;
        return true;
    }

    size_t offset = 0;
    size_t end = 0;
    if (!ano_size_align(layout->size, alignment, &offset)
        || !ano_size_add(offset, size, &end)) {
        layout->valid = false;
        return false;
    }

    reservation->offset = offset;
    reservation->size = size;
    layout->size = end;
    if (alignment > layout->alignment)
        layout->alignment = alignment;
    return true;
}

#undef ANO_MEMORY_REGION_CONSTEXPR

#ifdef __cplusplus
extern "C" {
#endif

// The returned region starts mutable with one reference. Its one backing
// allocation is explicitly contiguous and aligned to layout.alignment.
AnoMemoryRegion *ano_memory_region_create(AnoMemoryLayout layout);

// Retention is cross-thread. The final release winks out the complete region.
bool ano_memory_region_retain(AnoMemoryRegion *region);
void ano_memory_region_release(AnoMemoryRegion *region);

size_t ano_memory_region_size(const AnoMemoryRegion *region);
size_t ano_memory_region_alignment(const AnoMemoryRegion *region);

// Construction views exist only before sealing. Concurrent writers may fill
// disjoint reservations; they must finish before the region is sealed.
bool ano_memory_region_write(AnoMemoryRegion *region,
                             AnoMemoryReservation reservation,
                             AnoMemoryMutableView *view);

// Sealing publishes all completed writes. It is idempotent. Read views exist
// only after sealing and remain valid while any region reference is retained.
bool ano_memory_region_seal(AnoMemoryRegion *region);
bool ano_memory_region_is_sealed(const AnoMemoryRegion *region);
bool ano_memory_region_view(const AnoMemoryRegion *region,
                            AnoMemoryReservation reservation,
                            AnoMemoryView *view);

#ifdef __cplusplus
}

#include "anoptic_memory_typed.h"

namespace ano {

template<class T>
struct MemoryRegionSpan final {
    T *values;
    std::size_t count;

    [[nodiscard]] constexpr T *data() const noexcept { return values; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return count; }
    [[nodiscard]] constexpr bool empty() const noexcept { return count == 0; }
    [[nodiscard]] constexpr T &operator[](std::size_t index) const noexcept
    {
        assume(index < count);
        return values[index];
    }
};

template<Data T>
[[nodiscard]] constexpr bool memory_layout_reserve(
    AnoMemoryLayout *layout, std::size_t count,
    AnoMemoryReservation *reservation) noexcept
{
    std::size_t bytes = 0;
    if (!ano_size_multiply(count, sizeof(T), &bytes)) {
        if (layout != nullptr)
            layout->valid = false;
        return false;
    }
    return ano_memory_layout_reserve(layout, bytes, alignof(T), reservation);
}

template<Data T>
[[nodiscard]] MemoryRegionSpan<T> memory_region_write(
    AnoMemoryRegion *region, AnoMemoryReservation reservation) noexcept
{
    if ((reservation.offset & (alignof(T) - 1)) != 0
        || reservation.size % sizeof(T) != 0)
        return {};
    AnoMemoryMutableView view{};
    if (!ano_memory_region_write(region, reservation, &view))
        return {};
    return { static_cast<T *>(view.data), view.size / sizeof(T) };
}

template<Data T>
[[nodiscard]] MemoryRegionSpan<const T> memory_region_view(
    const AnoMemoryRegion *region, AnoMemoryReservation reservation) noexcept
{
    if ((reservation.offset & (alignof(T) - 1)) != 0
        || reservation.size % sizeof(T) != 0)
        return {};
    AnoMemoryView view{};
    if (!ano_memory_region_view(region, reservation, &view))
        return {};
    return { static_cast<const T *>(view.data), view.size / sizeof(T) };
}

} // namespace ano
#endif

#endif // ANOPTICENGINE_ANOPTIC_MEMORY_REGION_H
