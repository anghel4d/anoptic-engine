/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_atomic.h>
#include <anoptic_memory_region.h>
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

consteval bool layout_is_constexpr()
{
    AnoMemoryLayout layout = ano_memory_layout();
    AnoMemoryReservation bytes{};
    AnoMemoryReservation words{};
    AnoMemoryReservation wide{};
    return ano::memory_layout_reserve<uint8_t>(&layout, 3, &bytes)
        && ano::memory_layout_reserve<uint64_t>(&layout, 2, &words)
        && ano::memory_layout_reserve<WideValue>(&layout, 2, &wide)
        && bytes.offset == 0 && bytes.size == 3
        && words.offset == 8 && words.size == 16
        && wide.offset == 32 && wide.size == 64
        && layout.size == 96 && layout.alignment == 32 && layout.valid;
}

static_assert(layout_is_constexpr());

struct ReaderContext final {
    AnoMemoryRegion *region;
    AnoMemoryReservation reservation;
    atomic_bool start;
    atomic_bool passed;
};

void *read_on_retaining_thread(void *opaque)
{
    auto *context = static_cast<ReaderContext *>(opaque);
    while (!atomic_load_explicit(&context->start, memory_order_acquire))
        {}
    const ano::MemoryRegionSpan<const uint64_t> words =
        ano::memory_region_view<uint64_t>(context->region,
                                          context->reservation);
    atomic_store_explicit(
        &context->passed,
        words.size() == 2 && words[0] == UINT64_C(0x1776)
            && words[1] == UINT64_C(0x2323),
        memory_order_release);
    ano_memory_region_release(context->region);
    return nullptr;
}

struct HeapContext final {
    mi_heap_t *heap;
    uint64_t *values;
};

void *allocate_from_shared_heap(void *opaque)
{
    auto *context = static_cast<HeapContext *>(opaque);
    context->values = mi_heap_mallocn_tp(uint64_t, context->heap, 256);
    if (context->values != nullptr) {
        context->values[0] = UINT64_C(0x1776);
        context->values[255] = UINT64_C(0x7123);
    }
    return nullptr;
}

void test_first_class_heap()
{
    mi_heap_t *heap = ano_heap_create();
    CHECK(heap != nullptr, "first-class heap creation succeeds");
    if (heap == nullptr)
        return;
    uint32_t *local = mi_heap_calloc_tp(uint32_t, heap, 64);
    HeapContext context{ .heap = heap, .values = nullptr };
    anothread_t thread{};
    const bool started = ano_thread_create(
        &thread, nullptr, allocate_from_shared_heap, &context) == 0;
    CHECK(started, "a second thread can use the same first-class heap");
    if (started)
        CHECK(ano_thread_join(thread, nullptr) == 0, "heap worker joins");
    CHECK(local != nullptr && context.values != nullptr
              && context.values[0] == UINT64_C(0x1776)
              && context.values[255] == UINT64_C(0x7123),
          "cross-thread heap allocations preserve their values");
    CHECK(mi_heap_contains(heap, local)
              && mi_heap_contains(heap, context.values),
          "both threads allocate into the requested heap");
    ano_heap_destroy(heap);
}

void test_layout_failure()
{
    size_t result = 17;
    CHECK(!ano_size_add(SIZE_MAX, 1, &result) && result == 17,
          "checked addition leaves output untouched on overflow");
    CHECK(!ano_size_multiply(SIZE_MAX, 2, &result) && result == 17,
          "checked multiplication leaves output untouched on overflow");
    CHECK(!ano_size_align(SIZE_MAX, 32, &result) && result == 17,
          "checked alignment leaves output untouched on overflow");

    AnoMemoryLayout layout = ano_memory_layout();
    AnoMemoryReservation reservation{};
    CHECK(!ano_memory_layout_reserve(&layout, 4, 3, &reservation),
          "non-power-of-two alignment is rejected");
    CHECK(!layout.valid && ano_memory_region_create(layout) == nullptr,
          "a failed layout cannot be allocated");
}

void test_region_publication()
{
    AnoMemoryLayout layout = ano_memory_layout();
    AnoMemoryReservation bytes{};
    AnoMemoryReservation words{};
    AnoMemoryReservation wide{};
    CHECK(ano::memory_layout_reserve<uint8_t>(&layout, 3, &bytes)
              && ano::memory_layout_reserve<uint64_t>(&layout, 2, &words)
              && ano::memory_layout_reserve<WideValue>(&layout, 2, &wide),
          "typed reservations produce one valid layout");

    AnoMemoryRegion *region = ano_memory_region_create(layout);
    CHECK(region != nullptr, "region allocation succeeds");
    if (region == nullptr)
        return;
    CHECK(ano_memory_region_size(region) == 96
              && ano_memory_region_alignment(region) == 32,
          "region preserves measured extent and alignment");

    AnoMemoryView unavailable{};
    CHECK(!ano_memory_region_view(region, words, &unavailable),
          "immutable views are unavailable during construction");

    ano::MemoryRegionSpan<uint8_t> byteView =
        ano::memory_region_write<uint8_t>(region, bytes);
    ano::MemoryRegionSpan<uint64_t> wordView =
        ano::memory_region_write<uint64_t>(region, words);
    ano::MemoryRegionSpan<WideValue> wideView =
        ano::memory_region_write<WideValue>(region, wide);
    CHECK(byteView.size() == 3 && wordView.size() == 2 && wideView.size() == 2,
          "reservations resolve to their exact typed spans");
    CHECK((reinterpret_cast<uintptr_t>(wideView.data()) & 31u) == 0,
          "the strongest reservation is correctly aligned");

    byteView[0] = 1;
    byteView[1] = 2;
    byteView[2] = 3;
    wordView[0] = UINT64_C(0x1776);
    wordView[1] = UINT64_C(0x2323);
    wideView[1].words[3] = UINT64_C(0x7123);

    CHECK(ano_memory_region_seal(region)
              && ano_memory_region_is_sealed(region),
          "sealing publishes the complete region");
    AnoMemoryMutableView forbidden{};
    CHECK(!ano_memory_region_write(region, bytes, &forbidden),
          "sealed regions reject mutable views");
    const ano::MemoryRegionSpan<const WideValue> published =
        ano::memory_region_view<WideValue>(region, wide);
    CHECK(published.size() == 2
              && published[1].words[3] == UINT64_C(0x7123),
          "published typed data is retained without copying");

    ReaderContext context{
        .region = region,
        .reservation = words,
        .start = false,
        .passed = false,
    };
    CHECK(ano_memory_region_retain(region),
          "region can be retained for another thread");
    anothread_t reader{};
    if (ano_thread_create(&reader, nullptr, read_on_retaining_thread,
                          &context) != 0) {
        CHECK(false, "reader thread starts");
        ano_memory_region_release(region);
        ano_memory_region_release(region);
        return;
    }

    ano_memory_region_release(region);
    atomic_store_explicit(&context.start, true, memory_order_release);
    CHECK(ano_thread_join(reader, nullptr) == 0, "reader thread joins");
    CHECK(atomic_load_explicit(&context.passed, memory_order_acquire),
          "the final retaining thread reads and winks out the region");

}

} // namespace

int main()
{
    test_layout_failure();
    test_first_class_heap();
    test_region_publication();
    if (failures == 0) {
        printf("anotest_memory_region: all checks passed\n");
        return 0;
    }
    printf("anotest_memory_region: %d check(s) failed\n", failures);
    return 1;
}
