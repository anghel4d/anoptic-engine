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

struct AdversarialError final {
    AdversarialError() = default;
    AdversarialError(const AdversarialError&) noexcept(false) {}
    AdversarialError(AdversarialError&&) noexcept = default;

    friend bool operator==(const AdversarialError&,
                           const AdversarialError&) noexcept(false)
    {
        return true;
    }
};

struct ThrowingMoveError final {
    ThrowingMoveError() = default;
    ThrowingMoveError(const ThrowingMoveError&) noexcept = default;
    ThrowingMoveError(ThrowingMoveError&&) noexcept(false) {}
};

struct MoveOnlyValue final {
    MoveOnlyValue() = default;
    MoveOnlyValue(const MoveOnlyValue&) = delete;
    MoveOnlyValue(MoveOnlyValue&&) noexcept = default;
};

template<class Error>
concept FailureAvailable = requires(Error&& error) {
    ano::failure(std::forward<Error>(error));
};

template<class Error>
concept UnitResultIfAvailable = requires(Error&& error) {
    ano::result_if(true, std::forward<Error>(error));
};

template<class Value, class Error>
concept ValueResultIfAvailable = requires(Value&& value, Error&& error) {
    ano::result_if(true, std::forward<Value>(value),
                   std::forward<Error>(error));
};

template<class Carrier, class Error>
concept HasErrorAvailable = requires(const Carrier& carrier,
                                     const Error& error) {
    { ano::has_error(carrier, error) } -> std::same_as<bool>;
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

constexpr OtherError to_other_error(ParseError) noexcept
{
    return OtherError::invalid;
}

constexpr auto check_format(int witness) noexcept
    -> ano::Result<int, ParseError>
{
    if (witness < 0)
        return ano::failure(ParseError::missing);
    return witness + 1;
}

constexpr auto check_extent(int witness) noexcept
    -> ano::Result<unsigned, ParseError>
{
    if (witness < 0)
        return ano::failure(ParseError::invalid);
    return static_cast<unsigned>(witness + 2);
}

constexpr auto check_version(int witness) noexcept
    -> ano::Result<long, ParseError>
{
    return static_cast<long>(witness + 3);
}

constexpr int positiveSamples[] = {1, 2, 3};
constexpr int mixedSamples[] = {1, -2, 3};

constexpr auto sample_range(int selection) noexcept
    -> ano::Result<std::span<const int>, ParseError>
{
    if (selection == 0)
        return ano::failure(ParseError::invalid);
    return selection == 1
        ? std::span<const int>{positiveSamples}
        : std::span<const int>{mixedSamples};
}

constexpr int sum_step(int state, const int& item) noexcept
{
    return state + item;
}

constexpr auto checked_sum_step(int state, const int& item) noexcept
    -> ano::Result<int, ParseError>
{
    if (item < 0)
        return ano::failure(ParseError::missing);
    return state + item;
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

auto unsafe_error_operation(int value) noexcept
    -> ano::Result<int, ThrowingMoveError>
{
    return value;
}

using ParseResult = decltype(parse(true));

static_assert(ano::detail::operationShape<decltype(&parse)>.coherent());
static_assert(ano::detail::operationShape<decltype(&parse)>.concrete);
static_assert(ano::detail::operationShape<decltype(&parse)>.supportedArity);
static_assert(!ano::detail::operationShape<decltype(&parse)>.nullary);
static_assert(ano::detail::operationShape<decltype(&parse)>.nonthrowing);
static_assert(ano::detail::operationShape<decltype(&parse)>.carrier.valid);
static_assert(ano::detail::inspect_result(
                  ^^ano::Result<int, ParseError>).valid);
static_assert(!ano::detail::inspect_result(^^int).valid);
static_assert(ano::detail::inspect_result(
                  ^^const ano::Result<int, ParseError>&)
              == ano::detail::inspect_result(
                  ^^ano::Result<int, ParseError>));
static_assert(ano::ResultCarrier<ParseResult>);
static_assert(ano::ResultCarrier<const ParseResult&>);
static_assert(!ano::ResultCarrier<int>);
static_assert(!ano::ResultError<ThrowingMoveError>);
static_assert(ano::ResultCarrier<
              ano::Result<int, ThrowingMoveError>>);
static_assert(!ano::ResultOperation<decltype(&unsafe_error_operation)>);
static_assert(!FailureAvailable<const char (&)[2]>);
static_assert(UnitResultIfAvailable<AdversarialError>);
static_assert(!UnitResultIfAvailable<AdversarialError&>);
static_assert(ValueResultIfAvailable<MoveOnlyValue, ParseError>);
static_assert(!ValueResultIfAvailable<MoveOnlyValue&, ParseError>);
static_assert(!ValueResultIfAvailable<int, AdversarialError&>);
static_assert(!HasErrorAvailable<
              ano::Result<int, AdversarialError>, AdversarialError>);
static_assert(noexcept(ano::result_if(
                  true, 1, ParseError::invalid)));
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
static_assert(ano::ErrorMappable<decltype(&parse),
                                 decltype(&to_other_error)>);
static_assert(ano::KleisliPairable<decltype(&check_format),
                                   decltype(&check_extent)>);
static_assert(!ano::KleisliPairable<decltype(&check_format),
                                    decltype(&wrong_domain)>);
static_assert(ano::detail::foldShape<decltype(&sum_step)>.concrete);
static_assert(ano::detail::foldShape<decltype(&sum_step)>.supportedArity);
static_assert(ano::detail::foldShape<decltype(&sum_step)>.nonthrowing);
static_assert(ano::FoldStep<decltype(&sum_step), int, const int&>);
static_assert(ano::KleisliFoldStep<
              decltype(&checked_sum_step), int, const int&, ParseError>);
static_assert(!ano::PureOperation<decltype(&sum_step)>);
static_assert(!ano::ResultOperation<decltype(&checked_sum_step)>);
static_assert(std::same_as<
              ano::ResultOperationAlgebra<decltype(&parse)>::Domain, bool>);
static_assert(std::same_as<
              ano::ResultOperationAlgebra<decltype(&parse)>::Value, int>);
static_assert(std::same_as<
              ano::ResultOperationAlgebra<decltype(&parse)>::Error,
              ParseError>);
static_assert(std::same_as<
              ano::ResultOperationAlgebra<decltype(&parse)>::CarrierAlgebra,
              ano::ResultAlgebra<ParseResult>>);
static_assert(ano::ResultOperation<decltype(ano::lift<^^parse>)>);
static_assert(std::is_empty_v<decltype(ano::lift<^^parse>)>);
static_assert(std::is_empty_v<ano::ResultMorphism<ano::Lifted<^^parse>>>);

consteval bool result_surface()
{
    const auto value = parse(true);
    const auto error = parse(false);
    const ano::Result<void, ParseError> unit{};
    const ano::Result<void, ParseError> failed =
        ano::failure(ParseError::missing);
    return value && *value == 7
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
    constexpr auto path = ano::lift<^^parse>
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

consteval bool error_mapping_surface()
{
    constexpr auto mapped = ano::lift<^^parse>
        .map_error(to_other_error);
    static_assert(ano::ResultOperation<decltype(mapped)>);
    static_assert(std::same_as<
                  typename ano::ResultOperationAlgebra<
                      decltype(mapped)>::Error,
                  OtherError>);
    const auto value = mapped(true);
    const auto error = mapped(false);
    return value && *value == 7
        && ano::has_error(error, OtherError::invalid);
}

static_assert(error_mapping_surface());

consteval bool pairing_surface()
{
    constexpr auto paired = ano::pair(
        ano::lift<^^check_format>, ano::lift<^^check_extent>);
    constexpr auto gathered = ano::all(
        ano::lift<^^check_format>,
        ano::lift<^^check_extent>,
        ano::lift<^^check_version>);
    static_assert(ano::ResultOperation<decltype(paired)>);
    static_assert(ano::AllPairable<
                  decltype(ano::lift<^^check_format>),
                  decltype(ano::lift<^^check_extent>),
                  decltype(ano::lift<^^check_version>)>);

    const auto pairValue = paired(4);
    const auto pairError = paired(-1);
    const auto allValue = gathered(4);
    return pairValue
        && pairValue->first == 5
        && pairValue->second == 6u
        && ano::has_error(pairError, ParseError::missing)
        && allValue
        && allValue->first.first == 5
        && allValue->first.second == 6u
        && allValue->second == 7L;
}

static_assert(pairing_surface());

consteval bool scan_surface()
{
    int pureOutput[2]{};
    int fallibleOutput[3]{};
    const auto pure = ano::lift<^^sample_range>.scan_into(
        0, sum_step, std::span<int>{pureOutput});
    const auto fallible = ano::lift<^^sample_range>.scan_into(
        0, checked_sum_step, std::span<int>{fallibleOutput});

    const auto prefix = pure(1);
    const auto sourceError = pure(0);
    const auto foldError = fallible(2);
    return prefix
        && prefix->size() == 2
        && (*prefix)[0] == 1
        && (*prefix)[1] == 3
        && ano::has_error(sourceError, ParseError::invalid)
        && ano::has_error(foldError, ParseError::missing);
}

static_assert(scan_surface());

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
    constexpr auto existing = ano::lift<^^parse>;
    ANO_LET(rebound, existing);

    static_assert(noexcept(operation(true)));
    static_assert(ano::ResultCarrier<decltype(operation(true))>);
    static_assert(ano::ResultOperation<decltype(rebound)>);
    const auto direct = operation(true)
        .and_then(named_increment)
        .and_then(double_value)
        .and_then(publish);
    const auto path = ano::lift<^^parse>
        .and_then(named_increment)
        .and_then(double_value)
        .and_then(publish)
        .transform(project);
    const auto composed = path(true);
    const auto failed = path(false);
    const auto reboundValue = rebound(true);
    return direct && *direct == 19
        && composed && *composed == 361
        && reboundValue && *reboundValue == 7
        && ano::has_error(failed, ParseError::invalid);
}

static_assert(named_function_surface());

consteval bool unit_composition_surface()
{
    constexpr auto throughUnit = ano::lift<^^accept>.and_then(ano::lift<^^produce>);
    constexpr auto fromUnit = ano::lift<^^produce>.and_then(ano::lift<^^increment>);
    const auto first = throughUnit(1);
    const auto second = fromUnit();
    return first && *first == 11 && second && *second == 12;
}

static_assert(unit_composition_surface());

} // namespace

int main()
{
    if (ano::result_if(false, AdversarialError{}))
        return 1;
    if (!ano::result_if(true, MoveOnlyValue{}, ParseError::invalid))
        return 1;

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
