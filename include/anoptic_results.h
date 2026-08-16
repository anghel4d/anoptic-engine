/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#pragma once

#include <concepts>
#include <expected>
#include <meta>
#include <type_traits>
#include <utility>

namespace ano {

static_assert(sizeof(std::meta::info) == sizeof(void*));
static_assert(alignof(std::meta::info) == alignof(void*));

template<class Value, class Error>
using Result = std::expected<Value, Error>;

namespace detail {

struct ResultShape final {
    std::meta::info value{};
    std::meta::info error{};
    bool valid{};
};

consteval ResultShape inspect_result(std::meta::info type)
{
    type = std::meta::dealias(std::meta::remove_cvref(type));
    if (!std::meta::has_template_arguments(type)
        || std::meta::template_of(type) != ^^std::expected)
        return {};

    const auto arguments = std::meta::template_arguments_of(type);
    if (arguments.size() != 2 || !std::meta::is_type(arguments[0])
        || !std::meta::is_type(arguments[1]))
        return {};

    return {arguments[0], arguments[1], true};
}

template<class Type>
inline constexpr ResultShape resultShape = inspect_result(^^Type);

} // namespace detail

template<class Type>
concept ResultCarrier = detail::resultShape<Type>.valid;

template<ResultCarrier Type>
struct ResultAlgebra final {
    static constexpr auto shape = detail::resultShape<Type>;

    using Carrier = std::remove_cvref_t<Type>;
    using Value = [:shape.value:];
    using Error = [:shape.error:];

    static_assert(std::same_as<Carrier, std::expected<Value, Error>>);
};

template<class Error>
    requires std::constructible_from<std::remove_cvref_t<Error>, Error>
[[nodiscard]] constexpr auto failure(Error&& error)
    noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<Error>, Error>)
    -> std::unexpected<std::remove_cvref_t<Error>>
{
    return std::unexpected<std::remove_cvref_t<Error>>(
        std::forward<Error>(error));
}

template<class Error>
    requires std::constructible_from<std::remove_cvref_t<Error>, Error>
[[nodiscard]] constexpr auto result_if(bool condition, Error&& error)
    noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<Error>, Error>)
    -> Result<void, std::remove_cvref_t<Error>>
{
    if (!condition)
        return failure(std::forward<Error>(error));
    return {};
}

template<class Value, class Error>
    requires std::constructible_from<std::remove_cvref_t<Value>, Value>
        && std::constructible_from<std::remove_cvref_t<Error>, Error>
[[nodiscard]] constexpr auto result_if(
    bool condition, Value&& value, Error&& error)
    noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<Value>, Value>
             && std::is_nothrow_constructible_v<std::remove_cvref_t<Error>, Error>)
    -> Result<std::remove_cvref_t<Value>, std::remove_cvref_t<Error>>
{
    if (!condition)
        return failure(std::forward<Error>(error));
    return std::forward<Value>(value);
}

template<ResultCarrier Self, class Error>
    requires std::equality_comparable_with<
        typename ResultAlgebra<Self>::Error, Error>
[[nodiscard]] constexpr bool has_error(const Self& self,
                                       const Error& error)
    noexcept(noexcept(self.error() == error))
{
    return !self && self.error() == error;
}

} // namespace ano
