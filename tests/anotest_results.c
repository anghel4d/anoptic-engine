/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include "anoptic_compose.h"

using namespace ano;

namespace {

enum class ParseError {
    invalid,
    missing,
};

constexpr auto parse(bool valid) -> ano::Result<int, ParseError>
{
    if (valid)
        return 7;
    return ano::failure(ParseError::invalid);
}

constexpr auto increment(int value) -> ano::Result<int, ParseError>
{
    return value + 1;
}

constexpr auto double_value(int value) -> ano::Result<int, ParseError>
{
    return value * 2;
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
        .and_then(increment)
        .and_then(double_value)
        .transform([](int value) { return value + 3; });
    const auto value = path(true);
    const auto error = path(false);
    return value && *value == 19
        && ano::has_error(error, ParseError::invalid);
}

static_assert(composition_surface());

} // namespace

int main()
{
    return 0;
}
