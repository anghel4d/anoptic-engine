/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#ifndef ANOPTICENGINE_RESOURCES_PARALLEL_H
#define ANOPTICENGINE_RESOURCES_PARALLEL_H

#include <anoptic_memory.h>

using namespace ano;
#include <anoptic_resources.h>

#include <stdint.h>

namespace ano::resource_detail {

struct Executor;
using ParallelFunction = void (*)(void *, uint64_t, MemoryRegion *);

AnoResourceError executor_create(uint32_t requestedWorkers,
                                 Executor **executor) noexcept;
void executor_destroy(Executor *executor) noexcept;
AnoResourceError parallel_for(Executor *executor, uint64_t count,
                              void *context,
                              ParallelFunction function) noexcept;

} // namespace ano::resource_detail

#endif // ANOPTICENGINE_RESOURCES_PARALLEL_H
