/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// C atomic surface over compiler builtins in C++26. No standard-library runtime.

#ifndef ANOPTIC_ATOMIC_H
#define ANOPTIC_ATOMIC_H

#ifdef __cplusplus

namespace ano {

enum memory_order : int
{
    memory_order_relaxed = __ATOMIC_RELAXED,
    memory_order_consume = __ATOMIC_CONSUME,
    memory_order_acquire = __ATOMIC_ACQUIRE,
    memory_order_release = __ATOMIC_RELEASE,
    memory_order_acq_rel = __ATOMIC_ACQ_REL,
    memory_order_seq_cst = __ATOMIC_SEQ_CST,
};

// The __atomic *_n builtins admit exactly the integer, boolean, enum, and pointer scalars;
// anything else (structs, floats, over-wide types) must fail the contract, not the builtin.
template<class T>
concept atomic_builtin_admissible = requires(T *object, T value, T *expected) {
    __atomic_load_n(object, __ATOMIC_RELAXED);
    __atomic_store_n(object, value, __ATOMIC_RELAXED);
    __atomic_exchange_n(object, value, __ATOMIC_RELAXED);
    __atomic_compare_exchange_n(object, expected, value, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
};

template<atomic_builtin_admissible T>
struct Atomic final
{
    using value_type = T;

    alignas(T) T value{};

    constexpr Atomic() noexcept = default;
    constexpr Atomic(T desired) noexcept : value(desired) {}
    Atomic(const Atomic &) = delete;
    Atomic &operator=(const Atomic &) = delete;
};

// Layout contract, per instantiation: the wrapper must mirror T exactly.
template<class T>
consteval bool atomic_layout_contract()
{
    static_assert(__is_standard_layout(Atomic<T>), "Atomic<T> must stay standard-layout");
    static_assert(__is_trivially_copyable(Atomic<T>), "Atomic<T> must stay trivially copyable");
    static_assert(sizeof(Atomic<T>) == sizeof(T), "Atomic<T> must add no storage to T");
    static_assert(alignof(Atomic<T>) == alignof(T), "Atomic<T> must keep T's alignment");
    return true;
}

// Full accessor contract: admissible scalar + exact layout mirror. Checked on every instantiation.
template<class T>
concept atomic_contract = atomic_builtin_admissible<T> && atomic_layout_contract<T>();

// True when __atomic ops on T are lock-free at T's natural alignment. Lock-backed atomics stay
// legal in general; contention-sensitive users assert this predicate for their own types.
template<class T>
inline constexpr bool atomic_always_lock_free =
    __atomic_always_lock_free(sizeof(T), static_cast<const T *>(nullptr));

using atomic_bool = Atomic<bool>;
using atomic_int = Atomic<int>;
using atomic_uint = Atomic<unsigned>;

template<atomic_contract T>
inline void atomic_init(Atomic<T> *object, typename Atomic<T>::value_type desired) noexcept
{
    __atomic_store_n(&object->value, desired, __ATOMIC_RELAXED);
}

template<atomic_contract T>
[[nodiscard]] inline T atomic_load_explicit(const volatile Atomic<T> *object,
                                            memory_order order) noexcept
{
    return __atomic_load_n(&object->value, static_cast<int>(order));
}

template<atomic_contract T>
[[nodiscard]] inline T atomic_load(const volatile Atomic<T> *object) noexcept
{
    return atomic_load_explicit(object, memory_order_seq_cst);
}

template<atomic_contract T>
inline void atomic_store_explicit(volatile Atomic<T> *object,
                                  typename Atomic<T>::value_type desired,
                                  memory_order order) noexcept
{
    __atomic_store_n(&object->value, desired, static_cast<int>(order));
}

template<atomic_contract T>
inline void atomic_store(volatile Atomic<T> *object,
                         typename Atomic<T>::value_type desired) noexcept
{
    atomic_store_explicit(object, desired, memory_order_seq_cst);
}

template<atomic_contract T>
inline T atomic_exchange(volatile Atomic<T> *object,
                         typename Atomic<T>::value_type desired) noexcept
{
    return __atomic_exchange_n(&object->value, desired, __ATOMIC_SEQ_CST);
}

template<atomic_contract T>
inline T atomic_fetch_add_explicit(volatile Atomic<T> *object,
                                   typename Atomic<T>::value_type operand,
                                   memory_order order) noexcept
{
    return __atomic_fetch_add(&object->value, operand, static_cast<int>(order));
}

template<atomic_contract T>
inline T atomic_fetch_add(volatile Atomic<T> *object,
                          typename Atomic<T>::value_type operand) noexcept
{
    return atomic_fetch_add_explicit(object, operand, memory_order_seq_cst);
}

template<atomic_contract T>
inline T atomic_fetch_sub_explicit(volatile Atomic<T> *object,
                                   typename Atomic<T>::value_type operand,
                                   memory_order order) noexcept
{
    return __atomic_fetch_sub(&object->value, operand, static_cast<int>(order));
}

template<atomic_contract T>
[[nodiscard]] inline bool atomic_compare_exchange_strong_explicit(
    volatile Atomic<T> *object, T *expected, typename Atomic<T>::value_type desired,
    memory_order success, memory_order failure) noexcept
{
    return __atomic_compare_exchange_n(&object->value, expected, desired, false,
                                       static_cast<int>(success), static_cast<int>(failure));
}

template<atomic_contract T>
[[nodiscard]] inline bool atomic_compare_exchange_weak_explicit(
    volatile Atomic<T> *object, T *expected, typename Atomic<T>::value_type desired,
    memory_order success, memory_order failure) noexcept
{
    return __atomic_compare_exchange_n(&object->value, expected, desired, true,
                                       static_cast<int>(success), static_cast<int>(failure));
}

template<atomic_contract T>
[[nodiscard]] inline bool atomic_compare_exchange_strong(
    volatile Atomic<T> *object, T *expected, typename Atomic<T>::value_type desired) noexcept
{
    return atomic_compare_exchange_strong_explicit(
        object, expected, desired, memory_order_seq_cst, memory_order_seq_cst);
}

inline void atomic_thread_fence(memory_order order) noexcept
{
    __atomic_thread_fence(static_cast<int>(order));
}


} // namespace ano

#define ANO_ATOMIC(T) ::ano::Atomic<T>

using ano::atomic_bool;
using ano::atomic_compare_exchange_strong;
using ano::atomic_compare_exchange_strong_explicit;
using ano::atomic_compare_exchange_weak_explicit;
using ano::atomic_exchange;
using ano::atomic_fetch_add;
using ano::atomic_fetch_add_explicit;
using ano::atomic_fetch_sub_explicit;
using ano::atomic_init;
using ano::atomic_int;
using ano::atomic_load;
using ano::atomic_load_explicit;
using ano::atomic_store;
using ano::atomic_store_explicit;
using ano::atomic_thread_fence;
using ano::atomic_uint;
using ano::memory_order;
using ano::memory_order_acq_rel;
using ano::memory_order_acquire;
using ano::memory_order_consume;
using ano::memory_order_relaxed;
using ano::memory_order_release;
using ano::memory_order_seq_cst;

#else

#include <stdatomic.h>

#define ANO_ATOMIC(T) _Atomic(T)

#endif

#endif // ANOPTIC_ATOMIC_H
