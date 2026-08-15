/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_threads.h>

using namespace ano;
#include <anoptic_log_crash.h>
#include <anoptic_memory.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>    // PE header walk for thread_main_stack
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

static constexpr ThreadError thread_error(int status)
{
    switch (status) {
    case EINVAL: return ThreadError::invalid_argument;
    case EAGAIN: case ESRCH: return ThreadError::unavailable;
    case EPERM: return ThreadError::permission;
    case EDEADLK: return ThreadError::deadlock;
    case EBUSY: return ThreadError::in_use;
    case ETIMEDOUT: return ThreadError::timed_out;
    case ENOMEM: return ThreadError::out_of_memory;
    default: return ThreadError::platform;
    }
}

static constexpr ThreadResult<> thread_status(int status)
{
    return result_if(status == 0, thread_error(status));
}

/* Thread Management */

// Spawn shim: arms crash stack before user code. Cleanup disarms on return / pthread_exit / thread_exit.
typedef struct {
    void *(*func)(void *);
    void   *arg;
} thread_tramp_t;

static void tramp_disarm(void *unused)
{
    (void)unused;
    log_crash_thread_disarm();
}

static void *thread_trampoline(void *p)
{
    thread_tramp_t t = *(thread_tramp_t *)p;
    mi_free(p);
    (void)log_crash_thread_arm();   // best effort: an unarmed thread still runs
    void *ret;
    pthread_cleanup_push(tramp_disarm, NULL);
    ret = t.func(t.arg);
    pthread_cleanup_pop(1);
    return ret;
}

ThreadResult<> ano::thread_create(
    anothread_t *thread, const anothread_attr_t *attr,
    void *(*func)(void *), void *arg) {
    // NULL attr: ANO_THREAD_STACK_SIZE, lazily committed. Win64 uses PE --stack instead.
#if !defined(_WIN32)
    pthread_attr_t engineAttr;
    bool engineOwned = attr == NULL && pthread_attr_init(&engineAttr) == 0;
    if (engineOwned) {
        if (pthread_attr_setstacksize(&engineAttr, ANO_THREAD_STACK_SIZE) == 0) {
            attr = &engineAttr;
        } else {
            pthread_attr_destroy(&engineAttr);    // libc default beats no thread
            engineOwned = false;
        }
    }
#endif

    int rc;
    thread_tramp_t *t = mi_malloc_tp(thread_tramp_t);
    if (t == NULL) {
        rc = pthread_create(thread, attr, func, arg);    // no shim beats no thread
    } else {
        *t = (thread_tramp_t){ .func = func, .arg = arg };
        rc = pthread_create(thread, attr, thread_trampoline, t);
        if (rc != 0)
            mi_free(t);
    }

#if !defined(_WIN32)
    if (engineOwned)
        pthread_attr_destroy(&engineAttr);
#endif
    return thread_status(rc);
}

uint32_t ano::thread_concurrency(void) {

#if defined(_WIN32)
    const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return count == 0 ? 1 : (uint32_t)count;
#else
    const long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count <= 0 ? 1 : (count > UINT32_MAX ? UINT32_MAX : (uint32_t)count);
#endif
}

ThreadResult<> ano::thread_join(anothread_t thread, void **res) {
    return thread_status(pthread_join(thread, res));
}

void ano::thread_exit(void *res) {
    pthread_exit(res);
}

ThreadResult<> ano::thread_detach(anothread_t thread) {
    return thread_status(pthread_detach(thread));
}

anothread_t ano::thread_self(void) {
    return pthread_self();
}

ThreadResult<size_t> ano::thread_main_stack(void) {

#if defined(_WIN32)
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)GetModuleHandleW(NULL);
    if (dos == nullptr)
        return failure(ThreadError::platform);
    const IMAGE_NT_HEADERS *nt  = (const IMAGE_NT_HEADERS *)((const char *)dos + dos->e_lfanew);
    return (size_t)nt->OptionalHeader.SizeOfStackReserve;
#else
    struct rlimit rl;
    if (getrlimit(RLIMIT_STACK, &rl) != 0)
        return failure(ThreadError::platform);
    return rl.rlim_cur == RLIM_INFINITY ? SIZE_MAX : (size_t)rl.rlim_cur;
#endif
}


/* Mutexes */

ThreadResult<> ano::mutex_init(anothread_mutex_t *mutex, const anothread_mutexattr_t *attr) {
    return thread_status(pthread_mutex_init(mutex, attr));
}

ThreadResult<> ano::mutex_lock(anothread_mutex_t *mutex) {
    return thread_status(pthread_mutex_lock(mutex));
}

ThreadResult<> ano::mutex_unlock(anothread_mutex_t *mutex) {
    return thread_status(pthread_mutex_unlock(mutex));
}

ThreadResult<> ano::mutex_destroy(anothread_mutex_t *mutex) {
    return thread_status(pthread_mutex_destroy(mutex));
}


/* Condition Variables */

ThreadResult<> ano::thread_cond_init(anothread_cond_t *conditionVariable, const anothread_condattr_t *attr) {
    return thread_status(pthread_cond_init(conditionVariable, attr));
}

ThreadResult<> ano::thread_cond_wait(anothread_cond_t *conditionVariable, anothread_mutex_t *external_mutex) {
    return thread_status(pthread_cond_wait(conditionVariable, external_mutex));
}

ThreadResult<ThreadWait> ano::thread_cond_timedwait(
    anothread_cond_t *conditionVariable, anothread_mutex_t *external_mutex,
    const struct timespec *abstime) {
    const int status = pthread_cond_timedwait(conditionVariable, external_mutex, abstime);
    if (status == 0) return ThreadWait::signaled;
    if (status == ETIMEDOUT) return ThreadWait::timed_out;
    return failure(thread_error(status));
}

ThreadResult<> ano::thread_cond_signal(anothread_cond_t *conditionVariable) {
    return thread_status(pthread_cond_signal(conditionVariable));
}

ThreadResult<> ano::thread_cond_broadcast(anothread_cond_t *conditionVariable) {
    return thread_status(pthread_cond_broadcast(conditionVariable));
}

ThreadResult<> ano::thread_cond_destroy(anothread_cond_t *conditionVariable) {
    return thread_status(pthread_cond_destroy(conditionVariable));
}


/* Spinlocks */

#if !defined(__APPLE__)   // macOS: provided by threads_macos.c
ThreadResult<> ano::thread_spin_init(anothread_spinlock_t *lock, int pshared) {
    return thread_status(pthread_spin_init(lock, pshared));
}

ThreadResult<> ano::thread_spin_destroy(anothread_spinlock_t *lock) {
    return thread_status(pthread_spin_destroy(lock));
}

ThreadResult<> ano::thread_spin_lock(anothread_spinlock_t *lock) {
    return thread_status(pthread_spin_lock(lock));
}

ThreadResult<bool> ano::thread_spin_trylock(anothread_spinlock_t *lock) {
    const int status = pthread_spin_trylock(lock);
    if (status == 0) return true;
    if (status == EBUSY) return false;
    return failure(thread_error(status));
}

ThreadResult<> ano::thread_spin_unlock(anothread_spinlock_t *lock) {
    return thread_status(pthread_spin_unlock(lock));
}
#endif


/* Read-Write Locks */

ThreadResult<> ano::thread_rwlock_init(anothread_rwlock_t *rwlock, const anothread_rwlockattr_t *attr) {
    return thread_status(pthread_rwlock_init(rwlock, attr));
}

ThreadResult<> ano::thread_rwlock_rdlock(anothread_rwlock_t *rwlock) {
    return thread_status(pthread_rwlock_rdlock(rwlock));
}

ThreadResult<> ano::thread_rwlock_wrlock(anothread_rwlock_t *rwlock) {
    return thread_status(pthread_rwlock_wrlock(rwlock));
}

ThreadResult<> ano::thread_rwlock_unlock(anothread_rwlock_t *rwlock) {
    return thread_status(pthread_rwlock_unlock(rwlock));
}

ThreadResult<> ano::thread_rwlock_destroy(anothread_rwlock_t *rwlock) {
    return thread_status(pthread_rwlock_destroy(rwlock));
}


/* Thread Attributes */

ThreadResult<> ano::thread_attr_init(anothread_attr_t *attr) {
    return thread_status(pthread_attr_init(attr));
}

ThreadResult<> ano::thread_attr_setdetachstate(anothread_attr_t *attr, int flag) {
    return thread_status(pthread_attr_setdetachstate(attr, flag));
}

ThreadResult<> ano::thread_attr_getstacksize(const anothread_attr_t *attr, size_t *size) {
    return thread_status(pthread_attr_getstacksize(attr, size));
}

ThreadResult<> ano::thread_attr_setstacksize(anothread_attr_t *attr, size_t size) {
    return thread_status(pthread_attr_setstacksize(attr, size));
}

ThreadResult<> ano::thread_attr_destroy(anothread_attr_t *attr) {
    return thread_status(pthread_attr_destroy(attr));
}


/* Thread-Data */

ThreadResult<> ano::thread_key_create(anothread_key_t *key, void (*dest)(void *)) {
    return thread_status(pthread_key_create(key, dest));
}

ThreadResult<> ano::thread_key_delete(anothread_key_t key) {
    return thread_status(pthread_key_delete(key));
}

ThreadResult<> ano::thread_setspecific(anothread_key_t key, const void *value) {
    return thread_status(pthread_setspecific(key, value));
}

void* ano::thread_getspecific(anothread_key_t key) {
    return pthread_getspecific(key);
}


/* Synchronization Barriers */

#if !defined(__APPLE__)   // macOS: provided by threads_macos.c

ThreadResult<> ano::thread_barrier_init(anothread_barrier_t *barrier, const anothread_barrierattr_t *attr, unsigned int count) {
    return thread_status(pthread_barrier_init(barrier, attr, count));
}

ThreadResult<BarrierRole> ano::thread_barrier_wait(anothread_barrier_t *barrier) {
    const int status = pthread_barrier_wait(barrier);
    if (status == 0) return BarrierRole::participant;
    if (status == PTHREAD_BARRIER_SERIAL_THREAD) return BarrierRole::serial;
    return failure(thread_error(status));
}

ThreadResult<> ano::thread_barrier_destroy(anothread_barrier_t *barrier) {
    return thread_status(pthread_barrier_destroy(barrier));
}
#endif
