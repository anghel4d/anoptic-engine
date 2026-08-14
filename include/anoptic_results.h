/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#ifndef ANOPTIC_RESULTS_H
#define ANOPTIC_RESULTS_H

#include <expected>
#include <meta>
#include <type_traits>
#include <utility>

namespace ano {

template<class Value, class Error>
using Result = std::expected<Value, Error>;

namespace detail {

consteval bool result_type(std::meta::info type)
{
    type = std::meta::dealias(std::meta::remove_cvref(type));
    if (!std::meta::has_template_arguments(type)
        || std::meta::template_of(type) != ^^std::expected)
        return false;

    const auto arguments = std::meta::template_arguments_of(type);
    return arguments.size() == 2 && std::meta::is_type(arguments[0])
        && std::meta::is_type(arguments[1]);
}

template<class Type>
concept ResultInstance = result_type(^^Type);

template<class Type>
consteval bool unit_result()
{
    static_assert(ResultInstance<Type>);
    const auto type = std::meta::dealias(std::meta::remove_cvref(^^Type));
    return std::meta::template_arguments_of(type)[0] == ^^void;
}

} // namespace detail

template<class Error, class Value>
[[nodiscard]] constexpr auto success(Value&& value)
    -> Result<std::remove_cvref_t<Value>, Error>
{
    return Result<std::remove_cvref_t<Value>, Error>(
        std::in_place, std::forward<Value>(value));
}

template<class Error>
[[nodiscard]] constexpr auto success() -> Result<void, Error>
{
    return {};
}

template<class Value, class Error>
[[nodiscard]] constexpr auto failure(Error&& error)
    -> Result<Value, std::remove_cvref_t<Error>>
{
    return std::unexpected<std::remove_cvref_t<Error>>(
        std::forward<Error>(error));
}

template<detail::ResultInstance Self, class Function>
[[nodiscard]] constexpr decltype(auto) map(Self&& self, Function&& function)
    noexcept(noexcept(std::forward<Self>(self).transform(
        std::forward<Function>(function))))
{
    return std::forward<Self>(self).transform(
        std::forward<Function>(function));
}

template<detail::ResultInstance Self, class Function>
[[nodiscard]] constexpr decltype(auto) and_then(Self&& self, Function&& function)
    noexcept(noexcept(std::forward<Self>(self).and_then(
        std::forward<Function>(function))))
{
    return std::forward<Self>(self).and_then(
        std::forward<Function>(function));
}

template<detail::ResultInstance Self, class Function>
[[nodiscard]] constexpr decltype(auto) or_else(Self&& self, Function&& function)
    noexcept(noexcept(std::forward<Self>(self).or_else(
        std::forward<Function>(function))))
{
    return std::forward<Self>(self).or_else(
        std::forward<Function>(function));
}

template<detail::ResultInstance Self, class Observer>
constexpr void inspect(Self&& self, Observer&& observer)
{
    if (!self.has_value())
        return;
    if constexpr (detail::unit_result<Self>())
        std::forward<Observer>(observer)();
    else
        std::forward<Observer>(observer)(*std::forward<Self>(self));
}

template<detail::ResultInstance Self, class Observer>
constexpr void inspect_error(Self&& self, Observer&& observer)
{
    if (!self.has_value())
        std::forward<Observer>(observer)(
            std::forward<Self>(self).error());
}

} // namespace ano

#endif /* ANOPTIC_RESULTS_H */
