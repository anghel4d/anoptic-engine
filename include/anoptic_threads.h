/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <anoptic_atomic.h>
#include <anoptic_results.h>
#include <time.h>     // struct timespec for thread_cond_timedwait

namespace ano {

enum class ThreadError : uint8_t {
    invalid_argument, unavailable, permission, deadlock,
    in_use, timed_out, out_of_memory, platform,
};

enum class ThreadWait : uint8_t { signaled, timed_out };
enum class BarrierRole : uint8_t { participant, serial };

template<class Value = void>
using ThreadResult = Result<Value, ThreadError>;

typedef pthread_t anothread_t;

typedef pthread_mutex_t anothread_mutex_t;
typedef pthread_mutexattr_t anothread_mutexattr_t;

typedef pthread_attr_t anothread_attr_t;

typedef pthread_cond_t anothread_cond_t;
typedef pthread_condattr_t anothread_condattr_t;

#if defined(__APPLE__)
// Darwin: atomic_int spin (0 free / 1 held). pshared ignored.
typedef atomic_int pthread_spinlock_t;
#endif
typedef pthread_spinlock_t anothread_spinlock_t;

typedef pthread_rwlock_t anothread_rwlock_t;
typedef pthread_rwlockattr_t anothread_rwlockattr_t;

#if defined(__APPLE__)
// Darwin: POSIX barrier stand-in using the engine's C++26 atomic substrate. attr ignored.
struct pthread_barrier_t {
    unsigned int count;       // cohort size, set at init
    atomic_uint  arrived;     // [phase : high half][arrivals : low half]
};
struct pthread_barrierattr_t {
    int pshared;              // accepted, unused
};
#endif
typedef pthread_barrier_t anothread_barrier_t;
typedef pthread_barrierattr_t anothread_barrierattr_t;

typedef pthread_key_t anothread_key_t;


/* Thread Management */

// Engine-thread stack reserve when attr is NULL. Win64: PE --stack sizes those threads and the main thread.
#define ANO_THREAD_STACK_SIZE ((size_t)8 << 20)

[[nodiscard]] ThreadResult<> thread_create(
    anothread_t *thread, const anothread_attr_t *attr,
    void *(*func)(void *), void *arg);

// Online logical processors, or one when unavailable.
uint32_t thread_concurrency(void);

// Initial-thread stack budget. POSIX: soft RLIMIT_STACK (SIZE_MAX if unlimited). Win64: PE reserve.
[[nodiscard]] ThreadResult<size_t> thread_main_stack(void);

[[nodiscard]] ThreadResult<> thread_join(anothread_t thread, void **res);

void thread_exit(void *res);

[[nodiscard]] ThreadResult<> thread_detach(anothread_t thread);

anothread_t thread_self(void);


/* Mutexes */

[[nodiscard]] ThreadResult<> mutex_init(
    anothread_mutex_t *mutex, const anothread_mutexattr_t *attr);

[[nodiscard]] ThreadResult<> mutex_lock(anothread_mutex_t *mutex);

[[nodiscard]] ThreadResult<> mutex_unlock(anothread_mutex_t *mutex);

[[nodiscard]] ThreadResult<> mutex_destroy(anothread_mutex_t *mutex);


/* Condition Variables */

[[nodiscard]] ThreadResult<> thread_cond_init(
    anothread_cond_t *conditionVariable, const anothread_condattr_t *attr);

[[nodiscard]] ThreadResult<> thread_cond_wait(
    anothread_cond_t *conditionVariable, anothread_mutex_t *external_mutex);

// Absolute deadline on the cond's clock (default CLOCK_REALTIME). 0 / ETIMEDOUT / errno. Default clock: timespec_get(TIME_UTC).
[[nodiscard]] ThreadResult<ThreadWait> thread_cond_timedwait(
    anothread_cond_t *conditionVariable, anothread_mutex_t *external_mutex,
    const struct timespec *abstime);

[[nodiscard]] ThreadResult<> thread_cond_signal(anothread_cond_t *conditionVariable);

[[nodiscard]] ThreadResult<> thread_cond_broadcast(anothread_cond_t *conditionVariable);

[[nodiscard]] ThreadResult<> thread_cond_destroy(anothread_cond_t *conditionVariable);


/* Spinlocks */

[[nodiscard]] ThreadResult<> thread_spin_init(anothread_spinlock_t *lock, int pshared);

[[nodiscard]] ThreadResult<> thread_spin_destroy(anothread_spinlock_t *lock);

[[nodiscard]] ThreadResult<> thread_spin_lock(anothread_spinlock_t *lock);

[[nodiscard]] ThreadResult<bool> thread_spin_trylock(anothread_spinlock_t *lock);

[[nodiscard]] ThreadResult<> thread_spin_unlock(anothread_spinlock_t *lock);


/* Read-Write Locks */

[[nodiscard]] ThreadResult<> thread_rwlock_init(
    anothread_rwlock_t *rwlock, const anothread_rwlockattr_t *attr);

[[nodiscard]] ThreadResult<> thread_rwlock_rdlock(anothread_rwlock_t *rwlock);

[[nodiscard]] ThreadResult<> thread_rwlock_wrlock(anothread_rwlock_t *rwlock);

[[nodiscard]] ThreadResult<> thread_rwlock_unlock(anothread_rwlock_t *rwlock);

[[nodiscard]] ThreadResult<> thread_rwlock_destroy(anothread_rwlock_t *rwlock);


/* Thread Attributes */

[[nodiscard]] ThreadResult<> thread_attr_init(anothread_attr_t *attr);

[[nodiscard]] ThreadResult<> thread_attr_setdetachstate(anothread_attr_t *attr, int flag);

[[nodiscard]] ThreadResult<> thread_attr_getstacksize(
    const anothread_attr_t *attr, size_t *size);

[[nodiscard]] ThreadResult<> thread_attr_setstacksize(anothread_attr_t *attr, size_t size);

[[nodiscard]] ThreadResult<> thread_attr_destroy(anothread_attr_t *attr);


/* Thread-Data */

[[nodiscard]] ThreadResult<> thread_key_create(
    anothread_key_t *key, void (*dest)(void *));

[[nodiscard]] ThreadResult<> thread_key_delete(anothread_key_t key);

[[nodiscard]] ThreadResult<> thread_setspecific(
    anothread_key_t key, const void *value);

void* thread_getspecific(anothread_key_t key);


/* Synchronization Barriers */

// EINVAL on count 0, and on Darwin past 65535 (arrival half of the state word). An ignored failure leaves the barrier unusable.
[[nodiscard]] ThreadResult<> thread_barrier_init(
    anothread_barrier_t *barrier, const anothread_barrierattr_t *attr,
    unsigned int count);

// 0 to every waiter but one. Exactly one waiter per cohort gets a platform-defined
// non-zero serial return (not exported). Test `!= 0`, never a literal.
[[nodiscard]] ThreadResult<BarrierRole> thread_barrier_wait(
    anothread_barrier_t *barrier);

[[nodiscard]] ThreadResult<> thread_barrier_destroy(anothread_barrier_t *barrier);

} // namespace ano
