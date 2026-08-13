/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#ifndef ANOPTICENGINE_RESOURCES_PARALLEL_H
#define ANOPTICENGINE_RESOURCES_PARALLEL_H

#include <anoptic_atomic.h>
#include <anoptic_threads.h>
#include <mimalloc.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ano::resource_detail {

using ParallelFunction = void (*)(void *, uint64_t);

struct ParallelBatch final {
    void *context;
    ParallelFunction function;
    uint64_t count;
    ANO_ATOMIC(uint64_t) next;
};

inline void *parallel_worker(void *argument)
{
    ParallelBatch& batch = *static_cast<ParallelBatch *>(argument);
    for (;;) {
        const uint64_t index = atomic_fetch_add_explicit(
            &batch.next, UINT64_C(1), memory_order_relaxed);
        if (index >= batch.count)
            return nullptr;
        batch.function(batch.context, index);
    }
}

inline void *parallel_pool_worker(void *argument)
{
    mi_thread_set_in_threadpool();
    return parallel_worker(argument);
}

inline void parallel_for(uint64_t count, void *context,
                         ParallelFunction function)
{
    if (count == 0)
        return;
    ParallelBatch batch = {context, function, count};
    atomic_init(&batch.next, UINT64_C(0));

    constexpr uint32_t maximumWorkers = 16;
    uint32_t workerCount = ano_thread_concurrency();
    if (workerCount > maximumWorkers)
        workerCount = maximumWorkers;
    if (workerCount > count)
        workerCount = static_cast<uint32_t>(count);

    anothread_t workers[maximumWorkers - 1] = {};
    uint32_t spawned = 0;
    for (; spawned + 1 < workerCount; ++spawned)
        if (ano_thread_create(
                &workers[spawned], nullptr, parallel_pool_worker, &batch) != 0)
            break;
    (void)parallel_worker(&batch);
    for (uint32_t worker = 0; worker < spawned; ++worker)
        (void)ano_thread_join(workers[worker], nullptr);
}

struct CopyBatch final {
    uint8_t *destination;
    const uint8_t *source;
    uint64_t size;
};

inline void copy_chunk(void *context, uint64_t index)
{
    constexpr uint64_t chunkSize = UINT64_C(4) << 20;
    const CopyBatch& copy = *static_cast<const CopyBatch *>(context);
    const uint64_t offset = index * chunkSize;
    uint64_t size = copy.size - offset;
    if (size > chunkSize)
        size = chunkSize;
    memcpy(copy.destination + offset, copy.source + offset,
           static_cast<size_t>(size));
}

inline void parallel_copy(uint8_t *destination, const uint8_t *source,
                          uint64_t size)
{
    constexpr uint64_t chunkSize = UINT64_C(4) << 20;
    if (size < chunkSize) {
        memcpy(destination, source, static_cast<size_t>(size));
        return;
    }
    CopyBatch copy = {destination, source, size};
    const uint64_t chunks = size / chunkSize
        + (size % chunkSize != 0 ? 1 : 0);
    parallel_for(chunks, &copy, copy_chunk);
}

} // namespace ano::resource_detail

#endif // ANOPTICENGINE_RESOURCES_PARALLEL_H
