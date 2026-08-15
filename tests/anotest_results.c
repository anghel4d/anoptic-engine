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

enum class OtherError {
    invalid,
};

constexpr auto parse(bool valid) noexcept -> ano::Result<int, ParseError>
{
    if (valid)
        return 7;
    return ano::failure(ParseError::invalid);
}

constexpr auto increment(int value) noexcept
    -> ano::Result<int, ParseError>
{
    return value + 1;
}

constexpr auto accept(int) noexcept -> ano::Result<void, ParseError>
{
    return {};
}

constexpr auto produce() noexcept -> ano::Result<int, ParseError>
{
    return 11;
}

constexpr auto wrong_domain(float value) noexcept
    -> ano::Result<int, ParseError>
{
    return static_cast<int>(value);
}

constexpr auto wrong_error(int value) noexcept
    -> ano::Result<int, OtherError>
{
    return value;
}

constexpr int square(int value) noexcept
{
    return value * value;
}

constexpr auto potentially_throwing(int value)
    -> ano::Result<int, ParseError>
{
    return value;
}

using ParseResult = decltype(parse(true));

static_assert(ano::ResultCarrier<ParseResult>);
static_assert(ano::ResultCarrier<const ParseResult&>);
static_assert(!ano::ResultCarrier<int>);
static_assert(std::same_as<ano::ResultAlgebra<ParseResult>::Carrier,
                           ParseResult>);
static_assert(std::same_as<ano::ResultAlgebra<ParseResult>::Value, int>);
static_assert(std::same_as<ano::ResultAlgebra<ParseResult>::Error,
                           ParseError>);
static_assert(ano::ResultOperation<decltype(&parse)>);
static_assert(ano::PureOperation<decltype(&square)>);
static_assert(!ano::NonthrowingOperation<decltype(&potentially_throwing)>);
static_assert(ano::KleisliComposable<decltype(&parse),
                                     decltype(&increment)>);
static_assert(ano::KleisliComposable<decltype(&accept),
                                     decltype(&produce)>);
static_assert(!ano::KleisliComposable<decltype(&parse),
                                      decltype(&wrong_domain)>);
static_assert(!ano::KleisliComposable<decltype(&parse),
                                      decltype(&wrong_error)>);
static_assert(std::same_as<
              ano::ResultOperationAlgebra<decltype(&parse)>::Domain, bool>);
static_assert(std::same_as<
              ano::ResultOperationAlgebra<decltype(&parse)>::Value, int>);
static_assert(std::same_as<
              ano::ResultOperationAlgebra<decltype(&parse)>::Error,
              ParseError>);

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
        .and_then([](int value) noexcept -> ano::Result<int, ParseError> {
            return value + 1;
        })
        .and_then([](int value) noexcept -> ano::Result<int, ParseError> {
            return value * 2;
        })
        .transform([](int value) noexcept { return value + 3; });
    const auto value = path(true);
    const auto error = path(false);
    return value && *value == 19
        && ano::has_error(error, ParseError::invalid);
}

static_assert(composition_surface());

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
    ANO_LET(project, square);

    static_assert(noexcept(operation(true)));
    static_assert(ano::ResultCarrier<decltype(operation(true))>);
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

consteval bool unit_composition_surface()
{
    constexpr auto throughUnit = ano::compose(accept).and_then(produce);
    constexpr auto fromUnit = ano::compose(produce).and_then(increment);
    const auto first = throughUnit(1);
    const auto second = fromUnit();
    return first && *first == 11 && second && *second == 12;
}

static_assert(unit_composition_surface());

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
