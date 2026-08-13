/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory.h>

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

} // namespace

MemoryRegion *memory_region_create() noexcept
{
    MemoryRegion *region = mi_zalloc_tp(MemoryRegion);
    if (region == nullptr)
        return nullptr;
    region->heap = ano_heap_create();
    if (region->heap == nullptr) {
        mi_free(region);
        return nullptr;
    }
    return region;
}

void memory_region_destroy(MemoryRegion *region) noexcept
{
    if (region == nullptr)
        return;
    ano_heap_destroy(region->heap);
    mi_free(region);
}

bool memory_region_reset(MemoryRegion *region) noexcept
{
    if (region == nullptr)
        return false;
    mi_heap_t *replacement = ano_heap_create();
    if (replacement == nullptr)
        return false;
    mi_heap_t *retired = region->heap;
    region->heap = replacement;
    ano_heap_destroy(retired);
    return true;
}

void *memory_region_allocate(MemoryRegion *region, size_t size,
                             size_t alignment) noexcept
{
    if (region == nullptr || size == 0 || !valid_alignment(alignment))
        return nullptr;
    return mi_heap_malloc_aligned(region->heap, size, alignment);
}

void *memory_region_allocate_zero(MemoryRegion *region, size_t size,
                                  size_t alignment) noexcept
{
    if (region == nullptr || size == 0 || !valid_alignment(alignment))
        return nullptr;
    return mi_heap_zalloc_aligned(region->heap, size, alignment);
}

MemoryVolume *memory_volume_create(MemoryLayoutCursor layout) noexcept
{
    if (!layout.valid || !valid_alignment(layout.alignment))
        return nullptr;
    MemoryVolume *volume = mi_zalloc_tp(MemoryVolume);
    if (volume == nullptr)
        return nullptr;
    volume->region.heap = ano_heap_create();
    if (volume->region.heap == nullptr) {
        mi_free(volume);
        return nullptr;
    }
    if (layout.size != 0) {
        volume->data = static_cast<unsigned char *>(mi_heap_zalloc_aligned(
            volume->region.heap, layout.size, layout.alignment));
        if (volume->data == nullptr) {
            ano_heap_destroy(volume->region.heap);
            mi_free(volume);
            return nullptr;
        }
    }
    volume->size = layout.size;
    __atomic_store_n(&volume->references, size_t{1}, __ATOMIC_RELAXED);
    __atomic_store_n(&volume->sealed, 0u, __ATOMIC_RELAXED);
    return volume;
}

bool memory_volume_retain(MemoryVolume *volume) noexcept
{
    if (volume == nullptr)
        return false;
    size_t references = __atomic_load_n(&volume->references, __ATOMIC_RELAXED);
    do {
        if (references == 0 || references == SIZE_MAX)
            return false;
    } while (!__atomic_compare_exchange_n(
        &volume->references, &references, references + 1, true,
        __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    return true;
}

void memory_volume_release(MemoryVolume *volume) noexcept
{
    if (volume == nullptr)
        return;
    if (__atomic_fetch_sub(&volume->references, size_t{1},
                           __ATOMIC_ACQ_REL) != 1)
        return;
    ano_heap_destroy(volume->region.heap);
    mi_free(volume);
}

std::span<uint8_t> memory_volume_write(
    MemoryVolume *volume, MemoryReservation reservation) noexcept
{
    if (volume == nullptr
        || __atomic_load_n(&volume->sealed, __ATOMIC_ACQUIRE) != 0
        || !reservation_fits(volume, reservation))
        return {};
    return {reservation.size == 0
                ? nullptr : volume->data + reservation.offset,
            reservation.size};
}

void memory_volume_seal(MemoryVolume *volume) noexcept
{
    if (volume != nullptr)
        __atomic_store_n(&volume->sealed, 1u, __ATOMIC_RELEASE);
}

std::span<const uint8_t> memory_volume_view(
    const MemoryVolume *volume, MemoryReservation reservation) noexcept
{
    if (volume == nullptr
        || __atomic_load_n(&volume->sealed, __ATOMIC_ACQUIRE) == 0
        || !reservation_fits(volume, reservation))
        return {};
    return {reservation.size == 0
                ? nullptr : volume->data + reservation.offset,
            reservation.size};
}

} // namespace ano
