/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#pragma once

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

} // namespace detail

template<class Error>
[[nodiscard]] constexpr auto failure(Error&& error)
    -> std::unexpected<std::remove_cvref_t<Error>>
{
    return std::unexpected<std::remove_cvref_t<Error>>(
        std::forward<Error>(error));
}

template<class Error>
[[nodiscard]] constexpr auto result_if(bool condition, Error&& error)
    -> Result<void, std::remove_cvref_t<Error>>
{
    if (!condition)
        return failure(std::forward<Error>(error));
    return {};
}

template<class Value, class Error>
[[nodiscard]] constexpr auto result_if(
    bool condition, Value&& value, Error&& error)
    -> Result<std::remove_cvref_t<Value>, std::remove_cvref_t<Error>>
{
    if (!condition)
        return failure(std::forward<Error>(error));
    return std::forward<Value>(value);
}

template<detail::ResultInstance Self, class Error>
[[nodiscard]] constexpr bool has_error(const Self& self,
                                       const Error& error)
{
    return !self && self.error() == error;
}

} // namespace ano
