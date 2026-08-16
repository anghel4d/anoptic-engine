/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0-only
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

template<class Error>
concept ResultError = std::is_nothrow_move_constructible_v<
    std::remove_cvref_t<Error>>;

namespace detail {

struct ResultShape final {
    std::meta::info value{};
    std::meta::info error{};
    bool valid{};

    friend constexpr bool operator==(const ResultShape&,
                                     const ResultShape&) = default;
};

static_assert(std::regular<ResultShape>);
static_assert(std::is_trivially_copyable_v<ResultShape>);

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

template<class Target, class Source>
concept NothrowConstructibleFrom =
    std::constructible_from<Target, Source>
    && std::is_nothrow_constructible_v<Target, Source>;

template<class Left, class Right>
concept NothrowEqualityComparableWith =
    std::equality_comparable_with<Left, Right>
    && requires(const Left& left, const Right& right) {
        { static_cast<bool>(left == right) } noexcept -> std::same_as<bool>;
    };

} // namespace detail

template<class Type>
concept ResultCarrier = detail::resultShape<Type>.valid;

template<ResultCarrier Type>
struct ResultAlgebra final {
    static constexpr auto shape = detail::resultShape<Type>;

    using Carrier = std::remove_cvref_t<Type>;
    using Value = [:shape.value:];
    using Error = [:shape.error:];

    static_assert(std::same_as<Carrier, std::expected<Value, Error>>,
                  "carrier is not the expected<Value, Error> it decomposes to");
    static_assert(ResultError<Error>,
                  "ano::Result error types must be nothrow-movable");
};

template<class Error>
    requires (!std::is_array_v<std::remove_reference_t<Error>>)
        && ResultError<Error>
        && detail::NothrowConstructibleFrom<
            std::remove_cvref_t<Error>, Error>
[[nodiscard]] constexpr auto failure(Error&& error) noexcept
    -> std::unexpected<std::remove_cvref_t<Error>>
{
    using StoredError = std::remove_cvref_t<Error>;
    return std::unexpected<StoredError>(
        std::in_place, std::forward<Error>(error));
}

template<class Error>
    requires ResultError<Error>
        && detail::NothrowConstructibleFrom<
            std::remove_cvref_t<Error>, Error>
[[nodiscard]] constexpr auto result_if(bool condition, Error&& error) noexcept
    -> Result<void, std::remove_cvref_t<Error>>
{
    using Output = Result<void, std::remove_cvref_t<Error>>;
    if (!condition)
        return Output(std::unexpect, std::forward<Error>(error));
    return Output{};
}

template<class Value, class Error>
    requires ResultError<Error>
        && detail::NothrowConstructibleFrom<
            std::remove_cvref_t<Value>, Value>
        && detail::NothrowConstructibleFrom<
            std::remove_cvref_t<Error>, Error>
[[nodiscard]] constexpr auto result_if(
    bool condition, Value&& value, Error&& error) noexcept
    -> Result<std::remove_cvref_t<Value>, std::remove_cvref_t<Error>>
{
    using Output = Result<
        std::remove_cvref_t<Value>, std::remove_cvref_t<Error>>;
    if (!condition)
        return Output(std::unexpect, std::forward<Error>(error));
    return Output(std::in_place, std::forward<Value>(value));
}

template<ResultCarrier Self, class Error>
    requires detail::NothrowEqualityComparableWith<
        typename ResultAlgebra<Self>::Error, Error>
[[nodiscard]] constexpr bool has_error(const Self& self,
                                       const Error& error) noexcept
{
    return !self && static_cast<bool>(self.error() == error);
}

} // namespace ano
