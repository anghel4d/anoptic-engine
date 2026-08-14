/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#ifndef ANOPTIC_RESULTS_H
#define ANOPTIC_RESULTS_H

#ifdef __cplusplus

#include <expected>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ano {

template<class Value, class Error>
using Result = std::expected<Value, Error>;

namespace detail {

struct ResultShape final {
    std::meta::info value;
    std::meta::info error;
    bool valid;
};

consteval ResultShape result_shape(std::meta::info type)
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
concept ResultInstance = result_shape(^^Type).valid;

template<class Type>
consteval bool unit_result()
{
    static_assert(ResultInstance<Type>);
    return result_shape(^^Type).value == ^^void;
}

template<class Status, class Code>
consteval bool status_contract()
{
    if (!std::meta::is_class_type(^^Status)
        || !std::meta::is_enum_type(^^Code)
        || !std::is_standard_layout_v<Status>
        || !std::is_trivially_copyable_v<Status>)
        return false;

    const auto members = std::meta::nonstatic_data_members_of(
        ^^Status, std::meta::access_context::current());
    if (members.size() != 1
        || std::meta::identifier_of(members[0]) != std::string_view("code")
        || std::meta::type_of(members[0]) != ^^Code)
        return false;

    static constexpr auto enumerators = std::define_static_array(
        std::meta::enumerators_of(^^Code));
    using Raw = std::underlying_type_t<Code>;
    Raw values[enumerators.size()] = {};
    unsigned count = 0;
    template for (constexpr auto enumerator : enumerators)
        values[count++] = static_cast<Raw>([:enumerator:]);

    bool hasSuccess = false;
    for (unsigned i = 0; i < count; ++i) {
        hasSuccess = hasSuccess || values[i] == Raw{};
        for (unsigned j = 0; j < i; ++j)
            if (values[i] == values[j])
                return false;
    }
    return hasSuccess && sizeof(Status) == sizeof(Code)
        && alignof(Status) == alignof(Code);
}

template<class Status, class Code>
[[nodiscard]] constexpr Status make_status(Code code) noexcept
{
    static_assert(status_contract<Status, std::remove_cvref_t<Code>>(),
        "ANO_RESULT requires the code enum belonging to its status type");
    return Status{code};
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
requires requires(Self&& self, Function&& function) {
    std::forward<Self>(self).transform(std::forward<Function>(function));
}
[[nodiscard]] constexpr auto map(Self&& self, Function&& function)
    noexcept(noexcept(std::forward<Self>(self).transform(
        std::forward<Function>(function))))
{
    return std::forward<Self>(self).transform(
        std::forward<Function>(function));
}

template<detail::ResultInstance Self, class Function>
requires requires(Self&& self, Function&& function) {
    std::forward<Self>(self).and_then(std::forward<Function>(function));
}
[[nodiscard]] constexpr auto and_then(Self&& self, Function&& function)
    noexcept(noexcept(std::forward<Self>(self).and_then(
        std::forward<Function>(function))))
{
    return std::forward<Self>(self).and_then(
        std::forward<Function>(function));
}

template<detail::ResultInstance Self, class Function>
requires requires(Self&& self, Function&& function) {
    std::forward<Self>(self).or_else(std::forward<Function>(function));
}
[[nodiscard]] constexpr auto or_else(Self&& self, Function&& function)
    noexcept(noexcept(std::forward<Self>(self).or_else(
        std::forward<Function>(function))))
{
    return std::forward<Self>(self).or_else(
        std::forward<Function>(function));
}

template<detail::ResultInstance Self, class Observer>
requires (!detail::unit_result<Self>())
    && requires(Self&& self, Observer&& observer) {
        std::forward<Observer>(observer)(*std::forward<Self>(self));
    }
constexpr void inspect(Self&& self, Observer&& observer)
    noexcept(noexcept(std::forward<Observer>(observer)(
        *std::forward<Self>(self))))
{
    if (self.has_value())
        std::forward<Observer>(observer)(*std::forward<Self>(self));
}

template<detail::ResultInstance Self, class Observer>
requires (detail::unit_result<Self>())
    && requires(Observer&& observer) {
        std::forward<Observer>(observer)();
    }
constexpr void inspect(Self&& self, Observer&& observer)
    noexcept(noexcept(std::forward<Observer>(observer)()))
{
    if (self.has_value())
        std::forward<Observer>(observer)();
}

template<detail::ResultInstance Self, class Observer>
requires requires(Self&& self, Observer&& observer) {
    std::forward<Observer>(observer)(std::forward<Self>(self).error());
}
constexpr void inspect_error(Self&& self, Observer&& observer)
    noexcept(noexcept(std::forward<Observer>(observer)(
        std::forward<Self>(self).error())))
{
    if (!self.has_value())
        std::forward<Observer>(observer)(
            std::forward<Self>(self).error());
}

} // namespace ano

#define ANO_RESULT_TYPE(name, ...)                                      \
    typedef enum name##Code { __VA_ARGS__ } name##Code;                 \
    typedef struct [[nodiscard]] name { name##Code code; } name;        \
    static_assert(::ano::detail::status_contract<name, name##Code>(),   \
        "ANO_RESULT_TYPE requires one unique zero success code and one code member")

#define ANO_RESULT(name, value) \
    (::ano::detail::make_status<name>((value)))

#else

#define ANO_RESULT_TYPE(name, ...)                          \
    typedef enum name##Code { __VA_ARGS__ } name##Code;     \
    typedef struct name { name##Code code; } name

#define ANO_RESULT(name, value) ((name){ .code = (value) })

#endif

#endif /* ANOPTIC_RESULTS_H */
