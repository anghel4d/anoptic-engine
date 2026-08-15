/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include "anoptic_compose.h"

namespace {

enum class ParseError {
    invalid,
    missing,
};

constexpr auto parse(bool valid) noexcept -> ano::Result<int, ParseError>
{
    if (valid)
        return 7;
    return ano::failure(ParseError::invalid);
}

consteval bool result_surface()
{
    const auto value = parse(true);
    const auto error = parse(false);
    const ano::Result<void, ParseError> unit{};
    const ano::Result<void, ParseError> failed =
        ano::failure(ParseError::missing);
    return ano::detail::result_type(^^decltype(value))
        && value && *value == 7
        && ano::has_error(error, ParseError::invalid)
        && unit && ano::has_error(failed, ParseError::missing)
        && ano::result_if(true, ParseError::invalid)
        && ano::has_error(ano::result_if(false, ParseError::missing),
                          ParseError::missing)
        && *ano::result_if(true, 9, ParseError::invalid) == 9;
}

static_assert(result_surface());

consteval bool composition_surface()
{
    constexpr auto path = ano::compose(parse)
        .and_then([](int value) -> ano::Result<int, ParseError> {
            return value + 1;
        })
        .and_then([](int value) -> ano::Result<int, ParseError> {
            return value * 2;
        })
        .transform([](int value) { return value + 3; });
    const auto value = path(true);
    const auto error = path(false);
    return value && *value == 19
        && ano::has_error(error, ParseError::invalid);
}

static_assert(composition_surface());

constexpr auto increment(int value) noexcept
    -> ano::Result<int, ParseError>
{
    return value + 1;
}

consteval bool named_function_surface()
{
    ANO_LET(operation, parse);
    ANO_LET(named_increment, increment);
    ANO_LET(double_value,
            [](int value) constexpr noexcept
                -> ano::Result<int, ParseError> {
                return value * 2;
            });
    ANO_LET(publish,
            [](int value) constexpr noexcept
                -> ano::Result<int, ParseError> {
                return value + 3;
            });
    ANO_LET(project,
            [](int value) constexpr noexcept { return value * value; });

    static_assert(noexcept(operation(true)));
    static_assert(ano::detail::ResultInstance<decltype(operation(true))>);
    const auto direct = operation(true)
        .and_then(named_increment)
        .and_then(double_value)
        .and_then(publish);
    const auto path = ano::compose(parse)
        .and_then(named_increment)
        .and_then(double_value)
        .and_then(publish)
        .transform(project);
    const auto composed = path(true);
    const auto failed = path(false);
    return direct && *direct == 19
        && composed && *composed == 361
        && ano::has_error(failed, ParseError::invalid);
}

static_assert(named_function_surface());

} // namespace

int main()
{
    int calls = 0;
    ANO_LET(observe,
            [&calls](int value) noexcept
                -> ano::Result<int, ParseError> {
                ++calls;
                return value;
            });

    const auto failed = parse(false)
        .and_then(observe)
        .and_then(observe);
    if (!ano::has_error(failed, ParseError::invalid) || calls != 0)
        return 1;

    const auto value = parse(true)
        .and_then(observe)
        .and_then(observe);
    return value && *value == 7 && calls == 2 ? 0 : 1;
}
