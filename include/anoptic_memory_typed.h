/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Reflected contiguous-volume layouts and typed allocation for engine data.

#pragma once

#include <anoptic_memory.h>
#include <anoptic_meta.h>

#include <cstddef>
#include <meta>
#include <span>
#include <type_traits>

namespace ano {

namespace detail {

struct ArrayGrowth final { uint64_t capacity; size_t bytes; };

template<Data T>
[[nodiscard]] constexpr MemoryResult<ArrayGrowth> array_growth(
    uint64_t capacity, uint64_t required,
    uint64_t initialCapacity) noexcept
{
    if (required > SIZE_MAX / sizeof(T))
        return failure(MemoryError::overflow);
    uint64_t grown = capacity == 0
        ? (initialCapacity == 0 ? 1 : initialCapacity) : capacity;
    while (grown < required) {
        if (grown > (SIZE_MAX / sizeof(T)) / 2) {
            grown = required;
            break;
        }
        grown *= UINT64_C(2);
    }
    const auto allocation = checked_allocation_size(
        grown, uint64_t{sizeof(T)});
    if (!allocation)
        return failure(MemoryError::overflow);
    return ArrayGrowth{grown, *allocation};
}

} // namespace detail

template<Data T>
[[nodiscard]] MemoryResult<> reserve_array(
    T *&values, uint64_t& capacity, uint64_t required,
    uint64_t initialCapacity = 8) noexcept
{
    if (required <= capacity)
        return result_if(values != nullptr || capacity == 0,
                         MemoryError::invalid_argument);
    if (capacity != 0 && values == nullptr)
        return failure(MemoryError::invalid_argument);
    const auto growth = detail::array_growth<T>(
        capacity, required, initialCapacity);
    if (!growth)
        return failure(growth.error());
    void *allocation = mi_realloc(values, growth->bytes);
    if (allocation == nullptr)
        return failure(MemoryError::out_of_memory);
    values = static_cast<T *>(allocation);
    capacity = growth->capacity;
    return {};
}

template<Data T>
[[nodiscard]] MemoryResult<> reserve_zeroed_array(
    T *&values, uint64_t& capacity, uint64_t required,
    uint64_t initialCapacity = 8) noexcept
{
    const uint64_t previous = capacity;
    return reserve_array(values, capacity, required, initialCapacity)
        .transform([&] noexcept {
            if (capacity != previous)
                memset(values + previous, 0,
                       static_cast<size_t>(capacity - previous) * sizeof(T));
        });
}

template<Data T>
[[nodiscard]] MemoryResult<> reserve_region_array(
    MemoryRegion *region, T *&values, uint64_t count,
    uint64_t& capacity, uint64_t required,
    uint64_t initialCapacity = 8) noexcept
{
    if (required <= capacity)
        return result_if(count <= capacity
                         && (values != nullptr || capacity == 0),
                         MemoryError::invalid_argument);
    if (count > capacity || (capacity != 0 && values == nullptr))
        return failure(MemoryError::invalid_argument);
    const auto growth = detail::array_growth<T>(
        capacity, required, initialCapacity);
    if (!growth)
        return failure(growth.error());
    const auto allocation = memory_region_allocate_zero(
        region, growth->bytes, alignof(T));
    if (!allocation)
        return failure(allocation.error());
    T *replacement = static_cast<T *>(*allocation);
    if (count != 0)
        memcpy(replacement, values, static_cast<size_t>(count) * sizeof(T));
    values = replacement;
    capacity = growth->capacity;
    return {};
}

// mimalloc-backed allocator. Independent of the C++ runtime.
// Allocation failure is terminal.
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
            abort();
        T *result = mi_mallocn_tp(T, count);
        if (result == nullptr)
            abort();
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

// Writes each segment reservation from its runtime count. Reflection supplies
// field order, size, and alignment.
template<class Plan>
    requires (reflect_memory_plan<Plan>())
[[nodiscard]] constexpr ArithmeticResult<MemoryLayoutCursor> memory_layout(
    Plan& plan) noexcept
{
    MemoryLayoutCursor cursor{};
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^Plan, std::meta::access_context::current()));
    template for (constexpr auto member : members) {
        using Segment = [:std::meta::type_of(member):];
        using Value = typename Segment::Value;
        auto& segment = plan.[:member:];
        const auto bytes = checked_multiply(segment.count, sizeof(Value));
        if (!bytes)
            return failure(bytes.error());
        const auto reservation = cursor.reserve(*bytes, Segment::alignment);
        if (!reservation)
            return failure(reservation.error());
        segment.reservation = *reservation;
    }
    return cursor;
}

template<Data T, size_t Alignment>
[[nodiscard]] MemoryResult<std::span<T>> memory_volume_write(
    MemoryVolume *volume, MemorySegment<T, Alignment> segment) noexcept
{
    if ((segment.reservation.offset & (alignof(T) - 1)) != 0
        || segment.reservation.size % sizeof(T) != 0
        || segment.reservation.size / sizeof(T) != segment.count)
        return failure(MemoryError::invalid_layout);
    return memory_volume_write(volume, segment.reservation)
        .transform([=](std::span<uint8_t> view) noexcept {
            return std::span<T>{
                reinterpret_cast<T *>(view.data()), segment.count};
        });
}

template<Data T, size_t Alignment>
[[nodiscard]] MemoryResult<std::span<const T>> memory_volume_view(
    const MemoryVolume *volume, MemorySegment<T, Alignment> segment) noexcept
{
    if ((segment.reservation.offset & (alignof(T) - 1)) != 0
        || segment.reservation.size % sizeof(T) != 0
        || segment.reservation.size / sizeof(T) != segment.count)
        return failure(MemoryError::invalid_layout);
    return memory_volume_view(volume, segment.reservation)
        .transform([=](std::span<const uint8_t> view) noexcept {
            return std::span<const T>{
                reinterpret_cast<const T *>(view.data()), segment.count};
        });
}

template<Data T>
[[nodiscard]] MemoryResult<T *> memory_region_allocate(
    MemoryRegion *region, size_t count) noexcept
{
    const auto bytes = checked_multiply(count, sizeof(T));
    if (!bytes)
        return failure(MemoryError::overflow);
    return memory_region_allocate(region, *bytes, alignof(T))
        .transform([](void *value) { return static_cast<T *>(value); });
}

template<Data T>
[[nodiscard]] MemoryResult<T *> memory_region_allocate_zero(
    MemoryRegion *region, size_t count) noexcept
{
    const auto bytes = checked_multiply(count, sizeof(T));
    if (!bytes)
        return failure(MemoryError::overflow);
    return memory_region_allocate_zero(region, *bytes, alignof(T))
        .transform([](void *value) { return static_cast<T *>(value); });
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
