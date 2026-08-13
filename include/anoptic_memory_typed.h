/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Reflected contiguous-volume layouts and typed allocation for engine data.

#ifndef ANOPTICENGINE_ANOPTIC_MEMORY_TYPED_H
#define ANOPTICENGINE_ANOPTIC_MEMORY_TYPED_H

#include <anoptic_memory.h>
#include <anoptic_meta.h>

#include <cstddef>
#include <meta>
#include <span>
#include <type_traits>

namespace ano {

namespace detail {

template<Data T>
[[nodiscard]] bool array_growth(
    uint64_t capacity, uint64_t required, uint64_t initialCapacity,
    uint64_t& grown, size_t& bytes) noexcept
{
    if (required > SIZE_MAX / sizeof(T))
        return false;
    grown = capacity == 0
        ? (initialCapacity == 0 ? 1 : initialCapacity) : capacity;
    while (grown < required) {
        if (grown > (SIZE_MAX / sizeof(T)) / 2) {
            grown = required;
            break;
        }
        grown *= UINT64_C(2);
    }
    return checked_allocation_size(grown, uint64_t{sizeof(T)}, &bytes);
}

} // namespace detail

template<Data T>
[[nodiscard]] bool reserve_array(
    T *&values, uint64_t& capacity, uint64_t required,
    uint64_t initialCapacity = 8) noexcept
{
    if (required <= capacity)
        return values != nullptr || capacity == 0;
    uint64_t grown = 0;
    size_t bytes = 0;
    if ((capacity != 0 && values == nullptr)
        || !detail::array_growth<T>(
            capacity, required, initialCapacity, grown, bytes))
        return false;
    void *allocation = mi_realloc(values, bytes);
    if (allocation == nullptr)
        return false;
    values = static_cast<T *>(allocation);
    capacity = grown;
    return true;
}

template<Data T>
[[nodiscard]] bool reserve_zeroed_array(
    T *&values, uint64_t& capacity, uint64_t required,
    uint64_t initialCapacity = 8) noexcept
{
    const uint64_t previous = capacity;
    if (!reserve_array(values, capacity, required, initialCapacity))
        return false;
    if (capacity != previous)
        memset(values + previous, 0,
               static_cast<size_t>(capacity - previous) * sizeof(T));
    return true;
}

template<Data T>
[[nodiscard]] bool reserve_region_array(
    MemoryRegion *region, T *&values, uint64_t count,
    uint64_t& capacity, uint64_t required,
    uint64_t initialCapacity = 8) noexcept
{
    if (required <= capacity)
        return count <= capacity && (values != nullptr || capacity == 0);
    uint64_t grown = 0;
    size_t bytes = 0;
    if (count > capacity || (capacity != 0 && values == nullptr)
        || !detail::array_growth<T>(
            capacity, required, initialCapacity, grown, bytes))
        return false;
    T *replacement = static_cast<T *>(
        memory_region_allocate_zero(region, bytes, alignof(T)));
    if (replacement == nullptr)
        return false;
    if (count != 0)
        memcpy(replacement, values, static_cast<size_t>(count) * sizeof(T));
    values = replacement;
    capacity = grown;
    return true;
}

// Standard allocator surface backed directly by mimalloc. Containers using
// it remain independent of the C++ runtime; allocation failure is terminal in
// this no-exception engine process.
template<class T>
struct MimallocAllocator {
    using value_type = T;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using is_always_equal = std::true_type;

    template<class U>
    struct rebind final { using other = MimallocAllocator<U>; };

    constexpr MimallocAllocator() noexcept = default;
    template<class U>
    constexpr MimallocAllocator(const MimallocAllocator<U>&) noexcept {}

    [[nodiscard]] T *allocate(size_t count)
    {
        if (count > SIZE_MAX / sizeof(T))
            __builtin_trap();
        T *result = mi_mallocn_tp(T, count);
        if (result == nullptr)
            __builtin_trap();
        return result;
    }

    void deallocate(T *allocation, size_t) noexcept
    {
        mi_free(allocation);
    }

    template<class U>
    constexpr bool operator==(const MimallocAllocator<U>&) const noexcept
    {
        return true;
    }
};

template<class T, size_t Alignment = alignof(T)>
struct MemorySegment final {
    using Value = T;
    static constexpr size_t alignment = Alignment;

    static_assert(Alignment >= alignof(T) && (Alignment & (Alignment - 1)) == 0,
                  "segment alignment must be a power of two valid for T");

    size_t count = 0;
    MemoryReservation reservation{};
};

template<class Segment>
concept MemorySegmentDeclaration = requires(Segment segment) {
    typename Segment::Value;
    segment.count;
    segment.reservation;
} && Data<typename Segment::Value>
  && std::is_same_v<decltype(Segment::count), size_t>
  && std::is_same_v<decltype(Segment::reservation), MemoryReservation>;

template<class Plan>
consteval bool reflect_memory_plan()
{
    static_assert(std::is_class_v<Plan> && Data<Plan>,
                  "a memory plan must be a plain data record");
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^Plan, std::meta::access_context::current()));
    static_assert(!members.empty(), "a memory plan requires at least one segment");
    template for (constexpr auto member : members) {
        using Segment = [:std::meta::type_of(member):];
        static_assert(MemorySegmentDeclaration<Segment>,
                      "every memory-plan field must be ano::MemorySegment<T>");
    }
    return true;
}

// Runtime counts enter through the reflected plan record. Reflection supplies
// field order, element size, and alignment; this constexpr pass supplies the
// checked prefix sum and writes each reservation back into the record.
template<class Plan>
    requires (reflect_memory_plan<Plan>())
[[nodiscard]] constexpr MemoryLayoutCursor memory_layout(Plan& plan) noexcept
{
    MemoryLayoutCursor cursor{};
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^Plan, std::meta::access_context::current()));
    template for (constexpr auto member : members) {
        using Segment = [:std::meta::type_of(member):];
        using Value = typename Segment::Value;
        auto& segment = plan.[:member:];
        size_t bytes = 0;
        if (!ano_size_multiply(segment.count, sizeof(Value), &bytes)) {
            cursor.valid = false;
            break;
        }
        if (!cursor.reserve(bytes, Segment::alignment,
                            segment.reservation))
            break;
    }
    return cursor;
}

template<Data T, size_t Alignment>
[[nodiscard]] std::span<T> memory_volume_write(
    MemoryVolume *volume, MemorySegment<T, Alignment> segment) noexcept
{
    if ((segment.reservation.offset & (alignof(T) - 1)) != 0
        || segment.reservation.size % sizeof(T) != 0
        || segment.reservation.size / sizeof(T) != segment.count)
        return {};
    const std::span<uint8_t> view = memory_volume_write(
        volume, segment.reservation);
    if (view.size() != segment.reservation.size)
        return {};
    return {reinterpret_cast<T *>(view.data()), segment.count};
}

template<Data T, size_t Alignment>
[[nodiscard]] std::span<const T> memory_volume_view(
    const MemoryVolume *volume, MemorySegment<T, Alignment> segment) noexcept
{
    if ((segment.reservation.offset & (alignof(T) - 1)) != 0
        || segment.reservation.size % sizeof(T) != 0
        || segment.reservation.size / sizeof(T) != segment.count)
        return {};
    const std::span<const uint8_t> view = memory_volume_view(
        volume, segment.reservation);
    if (view.size() != segment.reservation.size)
        return {};
    return {reinterpret_cast<const T *>(view.data()), segment.count};
}

template<Data T>
[[nodiscard]] T *memory_region_allocate(
    MemoryRegion *region, size_t count) noexcept
{
    size_t bytes = 0;
    if (!ano_size_multiply(count, sizeof(T), &bytes))
        return nullptr;
    return static_cast<T *>(memory_region_allocate(region, bytes, alignof(T)));
}

template<Data T>
[[nodiscard]] T *memory_region_allocate_zero(
    MemoryRegion *region, size_t count) noexcept
{
    size_t bytes = 0;
    if (!ano_size_multiply(count, sizeof(T), &bytes))
        return nullptr;
    return static_cast<T *>(
        memory_region_allocate_zero(region, bytes, alignof(T)));
}

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
