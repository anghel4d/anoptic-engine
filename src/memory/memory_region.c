/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory.h>

using namespace ano;

namespace ano {

struct MemoryRegion final {
    mi_heap_t *heap;
};

struct MemoryVolume final {
    MemoryRegion region;
    unsigned char *data;
    size_t size;
    size_t references;
    unsigned sealed;
};

namespace {

bool valid_alignment(size_t alignment)
{
    return alignment != 0 && (alignment & (alignment - 1)) == 0;
}

bool reservation_fits(const MemoryVolume *volume,
                      MemoryReservation reservation)
{
    return reservation.offset <= volume->size
        && reservation.size <= volume->size - reservation.offset;
}

template<bool Zero>
MemoryResult<void *> region_allocate(
    MemoryRegion *region, size_t size, size_t alignment)
{
    if (region == nullptr || size == 0)
        return failure(MemoryError::invalid_argument);
    if (!valid_alignment(alignment))
        return failure(MemoryError::invalid_layout);
    void *allocation = Zero
        ? mi_heap_zalloc_aligned(region->heap, size, alignment)
        : mi_heap_malloc_aligned(region->heap, size, alignment);
    return result_if(allocation != nullptr, allocation,
                     MemoryError::out_of_memory);
}

template<class Byte, class Volume>
MemoryResult<std::span<Byte>> volume_span(
    Volume *volume, MemoryReservation reservation, bool sealed)
{
    if (volume == nullptr)
        return failure(MemoryError::invalid_argument);
    if ((__atomic_load_n(&volume->sealed, __ATOMIC_ACQUIRE) != 0) != sealed)
        return failure(sealed ? MemoryError::unsealed : MemoryError::immutable);
    if (!reservation_fits(volume, reservation))
        return failure(MemoryError::out_of_bounds);
    return std::span<Byte>{
        reservation.size == 0 ? nullptr
            : static_cast<Byte *>(volume->data + reservation.offset),
        reservation.size};
}

} // namespace

MemoryResult<MemoryRegion *> memory_region_create() noexcept
{
    MemoryRegion *region = mi_zalloc_tp(MemoryRegion);
    if (region == nullptr)
        return failure(MemoryError::out_of_memory);
    region->heap = heap_create();
    if (region->heap == nullptr) {
        mi_free(region);
        return failure(MemoryError::out_of_memory);
    }
    return region;
}

void memory_region_destroy(MemoryRegion *region) noexcept
{
    if (region == nullptr)
        return;
    heap_destroy(region->heap);
    mi_free(region);
}

MemoryResult<> memory_region_reset(MemoryRegion *region) noexcept
{
    if (region == nullptr)
        return failure(MemoryError::invalid_argument);
    mi_heap_t *replacement = heap_create();
    if (replacement == nullptr)
        return failure(MemoryError::out_of_memory);
    mi_heap_t *retired = region->heap;
    region->heap = replacement;
    heap_destroy(retired);
    return {};
}

MemoryResult<void *> memory_region_allocate(
    MemoryRegion *region, size_t size, size_t alignment) noexcept
{
    return region_allocate<false>(region, size, alignment);
}

MemoryResult<void *> memory_region_allocate_zero(
    MemoryRegion *region, size_t size, size_t alignment) noexcept
{
    return region_allocate<true>(region, size, alignment);
}

MemoryResult<MemoryVolume *> memory_volume_create(
    MemoryLayoutCursor layout) noexcept
{
    if (!valid_alignment(layout.alignment))
        return failure(MemoryError::invalid_layout);
    MemoryVolume *volume = mi_zalloc_tp(MemoryVolume);
    if (volume == nullptr)
        return failure(MemoryError::out_of_memory);
    volume->region.heap = heap_create();
    if (volume->region.heap == nullptr) {
        mi_free(volume);
        return failure(MemoryError::out_of_memory);
    }
    if (layout.size != 0) {
        volume->data = static_cast<unsigned char *>(mi_heap_zalloc_aligned(
            volume->region.heap, layout.size, layout.alignment));
        if (volume->data == nullptr) {
            heap_destroy(volume->region.heap);
            mi_free(volume);
            return failure(MemoryError::out_of_memory);
        }
    }
    volume->size = layout.size;
    __atomic_store_n(&volume->references, size_t{1}, __ATOMIC_RELAXED);
    __atomic_store_n(&volume->sealed, 0u, __ATOMIC_RELAXED);
    return volume;
}

MemoryResult<> memory_volume_retain(MemoryVolume *volume) noexcept
{
    if (volume == nullptr)
        return failure(MemoryError::invalid_argument);
    size_t references = __atomic_load_n(&volume->references, __ATOMIC_RELAXED);
    do {
        if (references == 0 || references == SIZE_MAX)
            return failure(MemoryError::overflow);
    } while (!__atomic_compare_exchange_n(
        &volume->references, &references, references + 1, true,
        __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    return {};
}

void memory_volume_release(MemoryVolume *volume) noexcept
{
    if (volume == nullptr)
        return;
    if (__atomic_fetch_sub(&volume->references, size_t{1},
                           __ATOMIC_ACQ_REL) != 1)
        return;
    heap_destroy(volume->region.heap);
    mi_free(volume);
}

MemoryResult<std::span<uint8_t>> memory_volume_write(
    MemoryVolume *volume, MemoryReservation reservation) noexcept
{
    return volume_span<uint8_t>(volume, reservation, false);
}

MemoryResult<> memory_volume_seal(MemoryVolume *volume) noexcept
{
    if (volume == nullptr)
        return failure(MemoryError::invalid_argument);
    __atomic_store_n(&volume->sealed, 1u, __ATOMIC_RELEASE);
    return {};
}

MemoryResult<std::span<const uint8_t>> memory_volume_view(
    const MemoryVolume *volume, MemoryReservation reservation) noexcept
{
    return volume_span<const uint8_t>(volume, reservation, true);
}

} // namespace ano
