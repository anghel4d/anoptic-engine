/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory_region.h>

struct AnoMemoryRegion final {
    unsigned char *data;
    size_t size;
    size_t alignment;
    size_t references;
    unsigned sealed;
};

static bool reservation_fits(const AnoMemoryRegion *region,
                             AnoMemoryReservation reservation)
{
    return reservation.offset <= region->size
        && reservation.size <= region->size - reservation.offset;
}

extern "C" AnoMemoryRegion *ano_memory_region_create(AnoMemoryLayout layout)
{
    if (!layout.valid || layout.alignment == 0
        || (layout.alignment & (layout.alignment - 1)) != 0)
        return nullptr;

    const size_t allocationAlignment = layout.alignment > alignof(AnoMemoryRegion)
        ? layout.alignment : alignof(AnoMemoryRegion);
    size_t dataOffset = 0;
    size_t allocationSize = 0;
    if (!ano_size_align(sizeof(AnoMemoryRegion), layout.alignment, &dataOffset)
        || !ano_size_add(dataOffset, layout.size, &allocationSize))
        return nullptr;
    AnoMemoryRegion *region = static_cast<AnoMemoryRegion *>(
        mi_zalloc_aligned(allocationSize, allocationAlignment));
    if (region == nullptr)
        return nullptr;

    region->data = layout.size == 0 ? nullptr
        : reinterpret_cast<unsigned char *>(region) + dataOffset;
    region->size = layout.size;
    region->alignment = layout.alignment;
    __atomic_store_n(&region->references, size_t{1}, __ATOMIC_RELAXED);
    __atomic_store_n(&region->sealed, 0u, __ATOMIC_RELAXED);
    return region;
}

extern "C" bool ano_memory_region_retain(AnoMemoryRegion *region)
{
    if (region == nullptr)
        return false;
    size_t references = __atomic_load_n(&region->references, __ATOMIC_RELAXED);
    do {
        if (references == 0 || references == SIZE_MAX)
            return false;
    } while (!__atomic_compare_exchange_n(
        &region->references, &references, references + 1, true,
        __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    return true;
}

extern "C" void ano_memory_region_release(AnoMemoryRegion *region)
{
    if (region == nullptr)
        return;
    if (__atomic_fetch_sub(&region->references, size_t{1},
                           __ATOMIC_ACQ_REL) != 1)
        return;
    mi_free(region);
}

extern "C" size_t ano_memory_region_size(const AnoMemoryRegion *region)
{
    return region == nullptr ? 0 : region->size;
}

extern "C" size_t ano_memory_region_alignment(const AnoMemoryRegion *region)
{
    return region == nullptr ? 0 : region->alignment;
}

extern "C" bool ano_memory_region_write(
    AnoMemoryRegion *region, AnoMemoryReservation reservation,
    AnoMemoryMutableView *view)
{
    if (view == nullptr)
        return false;
    *view = {};
    if (region == nullptr
        || __atomic_load_n(&region->sealed, __ATOMIC_ACQUIRE) != 0
        || !reservation_fits(region, reservation))
        return false;
    view->data = reservation.size == 0
        ? nullptr : region->data + reservation.offset;
    view->size = reservation.size;
    return true;
}

extern "C" bool ano_memory_region_seal(AnoMemoryRegion *region)
{
    if (region == nullptr)
        return false;
    __atomic_store_n(&region->sealed, 1u, __ATOMIC_RELEASE);
    return true;
}

extern "C" bool ano_memory_region_is_sealed(const AnoMemoryRegion *region)
{
    return region != nullptr
        && __atomic_load_n(&region->sealed, __ATOMIC_ACQUIRE) != 0;
}

extern "C" bool ano_memory_region_view(
    const AnoMemoryRegion *region, AnoMemoryReservation reservation,
    AnoMemoryView *view)
{
    if (view == nullptr)
        return false;
    *view = {};
    if (region == nullptr
        || __atomic_load_n(&region->sealed, __ATOMIC_ACQUIRE) == 0
        || !reservation_fits(region, reservation))
        return false;
    view->data = reservation.size == 0
        ? nullptr : region->data + reservation.offset;
    view->size = reservation.size;
    return true;
}
