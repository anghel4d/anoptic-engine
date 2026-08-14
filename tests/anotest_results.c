/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include "anoptic_results.h"

namespace {

enum class ParseError {
    invalid,
    missing,
};

constexpr auto parse(bool valid) -> ano::Result<int, ParseError>
{
    if (valid)
        return ano::success<ParseError>(7);
    return ano::failure<int>(ParseError::invalid);
}

consteval bool result_surface()
{
    auto mapped = ano::map(parse(true), [](int value) {
        return value * 2;
    });
    if (!mapped || *mapped != 14)
        return false;

    unsigned calls = 0;
    auto failedMap = ano::map(parse(false), [&](int) {
        ++calls;
        return 0;
    });
    if (failedMap || failedMap.error() != ParseError::invalid || calls != 0)
        return false;

    auto chained = ano::and_then(parse(true), [](int value) {
        return ano::success<ParseError>(value + 5);
    });
    if (!chained || *chained != 12)
        return false;

    auto recovered = ano::or_else(parse(false), [](ParseError error) {
        return error == ParseError::invalid
            ? ano::success<ParseError>(11)
            : ano::failure<int>(error);
    });
    if (!recovered || *recovered != 11)
        return false;

    int observations = 0;
    ano::inspect(parse(true), [&](int value) {
        observations += value;
    });
    ano::inspect_error(parse(true), [&](ParseError) {
        observations = -100;
    });
    ano::inspect(parse(false), [&](int) {
        observations = -200;
    });
    ano::inspect_error(parse(false), [&](ParseError error) {
        if (error == ParseError::invalid)
            ++observations;
    });

    auto unit = ano::success<ParseError>();
    ano::inspect(unit, [&] {
        ++observations;
    });
    auto failedUnit = ano::failure<void>(ParseError::missing);
    ano::inspect_error(failedUnit, [&](ParseError error) {
        if (error == ParseError::missing)
            ++observations;
    });
    auto unitMap = ano::map(unit, [] {
        return 3;
    });

    return observations == 10 && unitMap && *unitMap == 3;
}

ANO_RESULT_TYPE(AnoProbeResult,
    ANO_PROBE_ACCEPTED = 0,
    ANO_PROBE_REJECTED);

static_assert(result_surface());
static_assert(ANO_RESULT(AnoProbeResult, ANO_PROBE_ACCEPTED).code
    == ANO_PROBE_ACCEPTED);

} // namespace

int main()
{
    const AnoProbeResult rejected =
        ANO_RESULT(AnoProbeResult, ANO_PROBE_REJECTED);
    return rejected.code == ANO_PROBE_REJECTED ? 0 : 1;
}
