/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_atomic.h>

using namespace ano;
#include <anoptic_memory_typed.h>
#include <anoptic_threads.h>

#include <stdint.h>
#include <stdio.h>

namespace {

int failures = 0;

#define CHECK(condition, message) do {                                      \
    if (!(condition)) {                                                     \
        printf("FAIL: %s (%s:%d)\n", (message), __FILE__, __LINE__);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

struct alignas(32) WideValue final {
    uint64_t words[4];
};

struct TestVolumePlan final {
    ano::MemorySegment<uint8_t> bytes;
    ano::MemorySegment<uint64_t> words;
    ano::MemorySegment<WideValue> wide;
};

static_assert([] consteval {
    const auto widened = ano::checked_add(uint8_t{255}, uint16_t{1});
    return widened && *widened == 256
        && !ano::checked_add(uint16_t{UINT16_MAX}, uint8_t{1});
}());

consteval bool layout_is_reflected()
{
    TestVolumePlan plan{
        .bytes = {.count = 3},
        .words = {.count = 2},
        .wide = {.count = 2},
    };
    const auto layout = ano::memory_layout(plan);
    return layout && plan.bytes.reservation.offset == 0
        && plan.bytes.reservation.size == 3
        && plan.words.reservation.offset == 8
        && plan.words.reservation.size == 16
        && plan.wide.reservation.offset == 32
        && plan.wide.reservation.size == 64
        && layout->size == 96 && layout->alignment == 32;
}

static_assert(layout_is_reflected());

struct ReaderContext final {
    ano::MemoryVolume *volume;
    ano::MemorySegment<uint64_t> words;
    atomic_bool start;
    atomic_bool passed;
};

void *read_retained_volume(void *opaque)
{
    auto& context = *static_cast<ReaderContext *>(opaque);
    while (!atomic_load_explicit(&context.start, memory_order_acquire))
        {}
    const auto words = ano::memory_volume_view(
        context.volume, context.words);
    atomic_store_explicit(
        &context.passed,
        words && words->size() == 2 && (*words)[0] == UINT64_C(0x1776)
            && (*words)[1] == UINT64_C(0x2323),
        memory_order_release);
    ano::memory_volume_release(context.volume);
    return nullptr;
}

struct ScratchContext final {
    ano::MemoryRegion *region;
    uint64_t *values;
};

void *allocate_from_scratch_region(void *opaque)
{
    auto& context = *static_cast<ScratchContext *>(opaque);
    context.values = ano::memory_region_allocate<uint64_t>(
        context.region, 256).value_or(nullptr);
    if (context.values != nullptr) {
        context.values[0] = UINT64_C(0x1776);
        context.values[255] = UINT64_C(0x7123);
    }
    return nullptr;
}

void test_scratch_region()
{
    ano::MemoryRegion *region = ano::memory_region_create().value_or(nullptr);
    CHECK(region != nullptr, "scratch region creation succeeds");
    if (region == nullptr)
        return;
    uint32_t *local = ano::memory_region_allocate_zero<uint32_t>(
        region, 64).value_or(nullptr);
    ScratchContext context{region, nullptr};
    anothread_t thread{};
    const bool started = thread_create(
        &thread, nullptr, allocate_from_scratch_region, &context).has_value();
    CHECK(started, "another worker can allocate from the region");
    if (started)
        CHECK(thread_join(thread, nullptr), "scratch worker joins");
    CHECK(local != nullptr && context.values != nullptr
              && context.values[0] == UINT64_C(0x1776)
              && context.values[255] == UINT64_C(0x7123),
          "region allocations remain valid until reset");
    CHECK(ano::memory_region_reset(region),
          "scratch allocation lifetime resets wholesale");
    CHECK(ano::memory_region_allocate<uint64_t>(region, 1),
          "a reset region accepts the next task");
    ano::memory_region_destroy(region);
}

void test_layout_failure()
{
    CHECK(!ano::checked_add(SIZE_MAX, size_t{1}),
          "checked addition rejects overflow");
    CHECK(!ano::checked_multiply(SIZE_MAX, size_t{2}),
          "checked multiplication rejects overflow");
    CHECK(!ano::checked_align(SIZE_MAX, 32),
          "checked alignment rejects overflow");

    ano::MemoryLayoutCursor layout{};
    CHECK(!layout.reserve(4, 3),
          "non-power-of-two alignment is rejected");

    struct OverflowPlan final {
        ano::MemorySegment<uint64_t> values;
    } plan{.values = {.count = SIZE_MAX}};
    CHECK(!ano::memory_layout(plan),
          "a reflected element-size overflow rejects the complete layout");
}

void test_typed_array_reservation()
{
    uint32_t *values = nullptr;
    uint64_t capacity = 0;
    CHECK(ano::reserve_zeroed_array(values, capacity, UINT64_C(3), 0)
              && capacity >= 3 && values[0] == 0 && values[2] == 0,
          "typed arrays accept a zero growth hint and initialize new storage");
    if (values != nullptr) {
        values[0] = 17;
        const uint64_t required = capacity + 1;
        CHECK(ano::reserve_zeroed_array(values, capacity, required)
                  && capacity >= required && values[0] == 17
                  && values[required - 1] == 0,
              "typed array growth preserves values and zeroes its new tail");
    }
    mi_free(values);

    ano::MemoryRegion *region = ano::memory_region_create().value_or(nullptr);
    uint64_t *regional = nullptr;
    uint64_t regionalCapacity = 0;
    CHECK(region != nullptr
              && ano::reserve_region_array(
                  region, regional, UINT64_C(0), regionalCapacity,
                  UINT64_C(2), 0),
          "region-backed typed arrays reserve through the same API");
    if (regional != nullptr) {
        regional[0] = UINT64_C(0x1776);
        regional[1] = UINT64_C(0x2323);
        CHECK(ano::reserve_region_array(
                  region, regional, UINT64_C(2), regionalCapacity,
                  regionalCapacity + 1)
                  && regional[0] == UINT64_C(0x1776)
                  && regional[1] == UINT64_C(0x2323),
              "region-backed growth preserves its live prefix");
    }
    ano::memory_region_destroy(region);
}

void test_volume_publication()
{
    TestVolumePlan plan{
        .bytes = {.count = 3},
        .words = {.count = 2},
        .wide = {.count = 2},
    };
    const auto layout = ano::memory_layout(plan);
    ano::MemoryVolume *volume = layout
        ? ano::memory_volume_create(*layout).value_or(nullptr) : nullptr;
    CHECK(volume != nullptr, "measured volume allocation succeeds");
    if (volume == nullptr)
        return;
    const auto unavailable = ano::memory_volume_view(volume, plan.words);
    CHECK(ano::has_error(unavailable, ano::MemoryError::unsealed),
          "immutable spans are unavailable during construction");
    auto bytes = ano::memory_volume_write(volume, plan.bytes);
    auto words = ano::memory_volume_write(volume, plan.words);
    auto wide = ano::memory_volume_write(volume, plan.wide);
    CHECK(bytes && words && wide && bytes->size() == 3
              && words->size() == 2 && wide->size() == 2,
          "typed segments resolve to exact mutable spans");
    CHECK((reinterpret_cast<uintptr_t>(wide->data()) & 31u) == 0,
          "the strongest reflected segment alignment is honored");
    (*bytes)[0] = 1;
    (*bytes)[1] = 2;
    (*bytes)[2] = 3;
    (*words)[0] = UINT64_C(0x1776);
    (*words)[1] = UINT64_C(0x2323);
    (*wide)[1].words[3] = UINT64_C(0x7123);

    CHECK(ano::memory_volume_seal(volume), "volume sealing succeeds");
    CHECK(ano::has_error(
              ano::memory_volume_write(volume, plan.bytes),
              ano::MemoryError::immutable),
          "sealed volumes reject mutable spans");
    const auto published = ano::memory_volume_view(volume, plan.wide);
    CHECK(published && published->size() == 2
              && (*published)[1].words[3] == UINT64_C(0x7123),
          "immutable spans borrow the retained volume without copying");

    ReaderContext context{
        .volume = volume,
        .words = plan.words,
        .start = false,
        .passed = false,
    };
    CHECK(ano::memory_volume_retain(volume),
          "a volume owner may cross a thread boundary");
    anothread_t reader{};
    const bool started = thread_create(
        &reader, nullptr, read_retained_volume, &context).has_value();
    CHECK(started, "retained reader starts");
    atomic_store_explicit(&context.start, true, memory_order_release);
    if (started)
        CHECK(thread_join(reader, nullptr), "retained reader joins");
    CHECK(atomic_load_explicit(&context.passed, memory_order_acquire),
          "retained immutable spans survive publication to another thread");
    if (!started)
        ano::memory_volume_release(volume);
    ano::memory_volume_release(volume);
}

} // namespace

int main()
{
    test_scratch_region();
    test_layout_failure();
    test_typed_array_reservation();
    test_volume_publication();
    if (failures != 0)
        printf("%d memory substrate checks failed\n", failures);
    return failures == 0 ? 0 : 1;
}
