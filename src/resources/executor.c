/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include "parallel.h"

#include <anoptic_atomic.h>

using namespace ano;
#include <anoptic_memory_typed.h>
#include <anoptic_threads.h>

#include <mimalloc.h>

namespace ano::resource_detail {

struct Executor;

struct Worker final {
    Executor *executor;
    MemoryRegion *scratch;
    anothread_t thread;
    uint64_t observedGeneration;
};

struct Executor final {
    Worker *workers;
    uint32_t workerCount;
    MemoryRegion *callerScratch;
    anothread_mutex_t mutex;
    anothread_cond_t wake;
    anothread_cond_t complete;
    ParallelFunction function;
    void *context;
    uint64_t count;
    ANO_ATOMIC(uint64_t) next;
    uint64_t generation;
    uint32_t completed;
    bool active;
    bool failed;
    bool stop;
};

namespace {

void *executor_worker(void *opaque)
{
    mi_thread_set_in_threadpool();
    Worker& worker = *static_cast<Worker *>(opaque);
    Executor& executor = *worker.executor;
    worker.scratch = memory_region_create().value_or(nullptr);

    (void)mutex_lock(&executor.mutex);
    for (;;) {
        while (!executor.stop
               && worker.observedGeneration == executor.generation)
            (void)thread_cond_wait(&executor.wake, &executor.mutex);
        if (executor.stop)
            break;
        worker.observedGeneration = executor.generation;
        const bool scratchReady = worker.scratch != nullptr
            && !!memory_region_reset(worker.scratch);
        if (!scratchReady)
            executor.failed = true;
        ParallelFunction function = executor.function;
        void *context = executor.context;
        const uint64_t count = executor.count;
        (void)mutex_unlock(&executor.mutex);

        if (scratchReady)
            for (;;) {
                const uint64_t index = atomic_fetch_add_explicit(
                    &executor.next, UINT64_C(1), memory_order_relaxed);
                if (index >= count)
                    break;
                function(context, index, worker.scratch);
            }

        (void)mutex_lock(&executor.mutex);
        if (++executor.completed == executor.workerCount) {
            executor.active = false;
            (void)thread_cond_broadcast(&executor.complete);
        }
    }
    (void)mutex_unlock(&executor.mutex);
    memory_region_destroy(worker.scratch);
    return nullptr;
}

} // namespace

AnoResourceError executor_create(uint32_t requestedWorkers,
                                 Executor **output) noexcept
{
    if (output == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *output = nullptr;
    uint32_t count = requestedWorkers;
    if (count == 0) {
        count = thread_concurrency();
        if (count > 8)
            count = 8;
    }
    if (count == 0)
        count = 1;

    const uint32_t background = count > 1 ? count - 1 : 0;

    Executor *executor = mi_zalloc_tp(Executor);
    if (executor == nullptr)
        return ANO_RESOURCE_OUT_OF_MEMORY;
    executor->callerScratch = memory_region_create().value_or(nullptr);
    executor->workers = background == 0 ? nullptr
        : mi_calloc_tp(Worker, background);
    if (executor->callerScratch == nullptr
        || (background != 0 && executor->workers == nullptr)) {
        memory_region_destroy(executor->callerScratch);
        mi_free(executor->workers);
        mi_free(executor);
        return ANO_RESOURCE_OUT_OF_MEMORY;
    }
    if (!mutex_init(&executor->mutex, nullptr)) {
        memory_region_destroy(executor->callerScratch);
        mi_free(executor->workers);
        mi_free(executor);
        return ANO_RESOURCE_IO_ERROR;
    }
    if (!thread_cond_init(&executor->wake, nullptr)) {
        (void)mutex_destroy(&executor->mutex);
        memory_region_destroy(executor->callerScratch);
        mi_free(executor->workers);
        mi_free(executor);
        return ANO_RESOURCE_IO_ERROR;
    }
    if (!thread_cond_init(&executor->complete, nullptr)) {
        (void)thread_cond_destroy(&executor->wake);
        (void)mutex_destroy(&executor->mutex);
        memory_region_destroy(executor->callerScratch);
        mi_free(executor->workers);
        mi_free(executor);
        return ANO_RESOURCE_IO_ERROR;
    }
    executor->workerCount = background;
    atomic_init(&executor->next, UINT64_C(0));

    uint32_t started = 0;
    for (; started < background; ++started) {
        Worker& worker = executor->workers[started];
        worker.executor = executor;
        if (!thread_create(
                &worker.thread, nullptr, executor_worker, &worker))
            break;
    }
    if (started != background) {
        (void)mutex_lock(&executor->mutex);
        executor->stop = true;
        (void)thread_cond_broadcast(&executor->wake);
        (void)mutex_unlock(&executor->mutex);
        for (uint32_t i = 0; i < started; ++i)
            (void)thread_join(executor->workers[i].thread, nullptr);
        (void)thread_cond_destroy(&executor->complete);
        (void)thread_cond_destroy(&executor->wake);
        (void)mutex_destroy(&executor->mutex);
        memory_region_destroy(executor->callerScratch);
        mi_free(executor->workers);
        mi_free(executor);
        return ANO_RESOURCE_OUT_OF_MEMORY;
    }
    *output = executor;
    return ANO_RESOURCE_OK;
}

void executor_destroy(Executor *executor) noexcept
{
    if (executor == nullptr)
        return;
    (void)mutex_lock(&executor->mutex);
    while (executor->active)
        (void)thread_cond_wait(&executor->complete, &executor->mutex);
    executor->stop = true;
    ++executor->generation;
    (void)thread_cond_broadcast(&executor->wake);
    (void)mutex_unlock(&executor->mutex);
    for (uint32_t i = 0; i < executor->workerCount; ++i)
        (void)thread_join(executor->workers[i].thread, nullptr);
    (void)thread_cond_destroy(&executor->complete);
    (void)thread_cond_destroy(&executor->wake);
    (void)mutex_destroy(&executor->mutex);
    memory_region_destroy(executor->callerScratch);
    mi_free(executor->workers);
    mi_free(executor);
}

AnoResourceError parallel_for(Executor *executor, uint64_t count,
                              void *context,
                              ParallelFunction function) noexcept
{
    if (executor == nullptr || function == nullptr
        || (context == nullptr && count != 0))
        return ANO_RESOURCE_INVALID_ARGUMENT;
    if (count == 0)
        return ANO_RESOURCE_OK;
    (void)mutex_lock(&executor->mutex);
    while (executor->active)
        (void)thread_cond_wait(&executor->complete, &executor->mutex);
    executor->function = function;
    executor->context = context;
    executor->count = count;
    executor->completed = 0;
    executor->failed = !memory_region_reset(executor->callerScratch);
    executor->active = executor->workerCount != 0;
    atomic_store_explicit(&executor->next, UINT64_C(0), memory_order_relaxed);
    ++executor->generation;
    (void)thread_cond_broadcast(&executor->wake);
    ParallelFunction batchFunction = executor->function;
    void *batchContext = executor->context;
    MemoryRegion *scratch = executor->callerScratch;
    const bool callerReady = !executor->failed;
    (void)mutex_unlock(&executor->mutex);

    if (callerReady)
        for (;;) {
            const uint64_t index = atomic_fetch_add_explicit(
                &executor->next, UINT64_C(1), memory_order_relaxed);
            if (index >= count)
                break;
            batchFunction(batchContext, index, scratch);
        }

    (void)mutex_lock(&executor->mutex);
    while (executor->active)
        (void)thread_cond_wait(&executor->complete, &executor->mutex);
    const bool failed = executor->failed;
    (void)mutex_unlock(&executor->mutex);
    return failed ? ANO_RESOURCE_OUT_OF_MEMORY : ANO_RESOURCE_OK;
}

} // namespace ano::resource_detail
