/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0-only
 * Anoptic targets ISO C++26. */

#pragma once

#include "anoptic_results.h"

#include <concepts>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace ano {

namespace detail {

struct OperationShape final {
    std::meta::info declaration{};
    std::meta::info parameter{};
    std::meta::info domain{};
    std::meta::info result{};
    ResultShape carrier{};
    bool concrete{};
    bool supportedArity{};
    bool nullary{};
    bool nonthrowing{};

    friend constexpr bool operator==(const OperationShape&,
                                     const OperationShape&) = default;

    [[nodiscard]] consteval bool coherent() const noexcept
    {
        return !nullary || supportedArity;
    }
};

static_assert(std::regular<OperationShape>);

struct FoldShape final {
    std::meta::info declaration{};
    std::meta::info stateParameter{};
    std::meta::info itemParameter{};
    std::meta::info stateDomain{};
    std::meta::info itemDomain{};
    std::meta::info result{};
    ResultShape carrier{};
    bool concrete{};
    bool supportedArity{};
    bool nonthrowing{};

    friend constexpr bool operator==(const FoldShape&,
                                     const FoldShape&) = default;
};

static_assert(std::regular<FoldShape>);

consteval std::meta::info callable_declaration(std::meta::info type)
{
    type = std::meta::dealias(std::meta::remove_cvref(type));
    if (std::meta::is_function_type(type))
        return type;
    if (std::meta::is_pointer_type(type)) {
        const std::meta::info pointee = std::meta::remove_pointer(type);
        return std::meta::is_function_type(pointee)
            ? pointee
            : std::meta::info{};
    }
    if (!std::meta::is_class_type(type))
        return {};

    std::meta::info call{};
    for (const std::meta::info declaration : std::meta::members_of(
             type, std::meta::access_context::unchecked())) {
        if (std::meta::is_operator_function_template(declaration)
            && std::meta::operator_of(declaration)
                == std::meta::op_parentheses)
            return {};
        if (!std::meta::is_operator_function(declaration)
            || std::meta::operator_of(declaration)
                != std::meta::op_parentheses)
            continue;
        if (call != std::meta::info{})
            return {};
        call = declaration;
    }
    return call;
}

consteval std::meta::info parameter_type(std::meta::info parameter)
{
    return std::meta::is_type(parameter)
        ? parameter
        : std::meta::type_of(parameter);
}

consteval OperationShape inspect_operation(std::meta::info type)
{
    OperationShape shape{};
    shape.declaration = callable_declaration(type);
    if (shape.declaration == std::meta::info{})
        return shape;

    shape.concrete = true;
    shape.nonthrowing = std::meta::is_noexcept(shape.declaration);

    const auto parameters = std::meta::parameters_of(shape.declaration);
    const size_t first = !parameters.empty()
            && std::meta::is_function_parameter(parameters[0])
            && std::meta::is_explicit_object_parameter(parameters[0])
        ? 1
        : 0;
    const size_t arity = parameters.size() - first;
    if (arity > 1)
        return shape;

    shape.supportedArity = true;
    shape.nullary = arity == 0;
    shape.parameter = shape.nullary
        ? ^^void
        : parameter_type(parameters[first]);
    shape.domain = std::meta::remove_cvref(shape.parameter);
    shape.result = std::meta::return_type_of(shape.declaration);
    shape.carrier = inspect_result(shape.result);
    return shape;
}

consteval FoldShape inspect_fold(std::meta::info type)
{
    FoldShape shape{};
    shape.declaration = callable_declaration(type);
    if (shape.declaration == std::meta::info{})
        return shape;

    shape.concrete = true;
    shape.nonthrowing = std::meta::is_noexcept(shape.declaration);

    const auto parameters = std::meta::parameters_of(shape.declaration);
    const size_t first = !parameters.empty()
            && std::meta::is_function_parameter(parameters[0])
            && std::meta::is_explicit_object_parameter(parameters[0])
        ? 1
        : 0;
    if (parameters.size() - first != 2)
        return shape;

    shape.supportedArity = true;
    shape.stateParameter = parameter_type(parameters[first]);
    shape.itemParameter = parameter_type(parameters[first + 1]);
    shape.stateDomain = std::meta::remove_cvref(shape.stateParameter);
    shape.itemDomain = std::meta::remove_cvref(shape.itemParameter);
    shape.result = std::meta::return_type_of(shape.declaration);
    shape.carrier = inspect_result(shape.result);
    return shape;
}

template<class Type>
inline constexpr OperationShape operationShape =
    inspect_operation(^^std::remove_cvref_t<Type>);

template<class Type>
inline constexpr FoldShape foldShape =
    inspect_fold(^^std::remove_cvref_t<Type>);

template<class Type>
consteval bool declared_invocation()
{
    using Operation = std::remove_cvref_t<Type>;
    constexpr auto shape = operationShape<Operation>;
    if constexpr (!shape.concrete || !shape.supportedArity) {
        return false;
    } else if constexpr (shape.nullary) {
        using Return = [:shape.result:];
        if constexpr (std::meta::is_class_member(shape.declaration))
            return requires(const Operation& operation) {
                { operation.[:shape.declaration:]() }
                    noexcept -> std::same_as<Return>;
            };
        else
            return requires(const Operation& operation) {
                { operation() } noexcept -> std::same_as<Return>;
            };
    } else {
        using Parameter = [:shape.parameter:];
        using Return = [:shape.result:];
        if constexpr (std::meta::is_class_member(shape.declaration))
            return requires(const Operation& operation, Parameter argument) {
                { operation.[:shape.declaration:](
                      std::forward<Parameter>(argument)) }
                    noexcept -> std::same_as<Return>;
            };
        else
            return requires(const Operation& operation, Parameter argument) {
                { operation(std::forward<Parameter>(argument)) }
                    noexcept -> std::same_as<Return>;
            };
    }
}

template<class Type>
consteval bool nothrow_error()
{
    constexpr auto shape = operationShape<Type>;
    if constexpr (!shape.carrier.valid)
        return false;
    else {
        using Error = [:shape.carrier.error:];
        return ResultError<Error>;
    }
}

template<class Step, class State, class Item>
consteval bool fold_step()
{
    using Operation = std::remove_cvref_t<Step>;
    using StoredState = std::remove_cvref_t<State>;
    constexpr auto shape = foldShape<Operation>;
    if constexpr (!shape.concrete || !shape.supportedArity
                  || !shape.nonthrowing || shape.carrier.valid) {
        return false;
    } else {
        using StateDomain = [:shape.stateDomain:];
        using ItemDomain = [:shape.itemDomain:];
        using Return = [:shape.result:];
        return std::movable<StoredState>
            && std::same_as<StateDomain, StoredState>
            && std::same_as<ItemDomain, std::remove_cvref_t<Item>>
            && std::same_as<Return, StoredState>
            && std::is_nothrow_invocable_r_v<
                StoredState, const Operation&, StoredState, Item>;
    }
}

template<class Step, class State, class Item, class Error>
consteval bool kleisli_fold_step()
{
    using Operation = std::remove_cvref_t<Step>;
    using StoredState = std::remove_cvref_t<State>;
    using StoredError = std::remove_cvref_t<Error>;
    constexpr auto shape = foldShape<Operation>;
    if constexpr (!shape.concrete || !shape.supportedArity
                  || !shape.nonthrowing || !shape.carrier.valid) {
        return false;
    } else {
        using StateDomain = [:shape.stateDomain:];
        using ItemDomain = [:shape.itemDomain:];
        using ReflectedCarrier = [:shape.result:];
        using Carrier = std::remove_cvref_t<ReflectedCarrier>;
        using Value = [:shape.carrier.value:];
        using ActualError = [:shape.carrier.error:];
        return std::movable<StoredState>
            && std::same_as<StateDomain, StoredState>
            && std::same_as<ItemDomain, std::remove_cvref_t<Item>>
            && std::same_as<Value, StoredState>
            && std::same_as<ActualError, StoredError>
            && ResultError<StoredError>
            && std::is_nothrow_invocable_r_v<
                Carrier, const Operation&, StoredState, Item>;
    }
}

} // namespace detail

template<class Type>
concept NonthrowingOperation = detail::operationShape<Type>.concrete
    && detail::operationShape<Type>.supportedArity
    && detail::operationShape<Type>.nonthrowing
    && detail::declared_invocation<Type>();

template<class Type>
concept ResultOperation = NonthrowingOperation<Type>
    && detail::operationShape<Type>.carrier.valid
    && detail::nothrow_error<Type>();

template<class Type>
concept PureOperation = NonthrowingOperation<Type>
    && !detail::operationShape<Type>.carrier.valid;

template<NonthrowingOperation Type>
struct OperationAlgebra {
    static constexpr auto shape = detail::operationShape<Type>;
    static constexpr bool nullary = shape.nullary;

    using Operation = std::remove_cvref_t<Type>;
    using Parameter = [:shape.parameter:];
    using Domain = [:shape.domain:];
    using Return = [:shape.result:];
};

template<ResultOperation Type>
struct ResultOperationAlgebra final : OperationAlgebra<Type> {
    using Base = OperationAlgebra<Type>;
    using Carrier = std::remove_cvref_t<typename Base::Return>;
    using CarrierAlgebra = ResultAlgebra<Carrier>;
    using Value = typename CarrierAlgebra::Value;
    using Error = typename CarrierAlgebra::Error;

    static_assert(std::same_as<Carrier, std::expected<Value, Error>>);
    static_assert(std::same_as<
                  typename Base::Domain,
                  std::remove_cvref_t<typename Base::Parameter>>);
    static_assert(std::is_void_v<Value> || std::is_object_v<Value>,
                  "Result value must be void or an object type");
    static_assert(ResultError<Error>);
    static_assert(Base::nullary == std::is_void_v<typename Base::Domain>);
};

namespace detail {

template<class Value, class Next>
concept Accepts = (Next::nullary && std::same_as<Value, void>)
    || (!Next::nullary && std::same_as<Value, typename Next::Domain>);

template<class Value>
consteval bool nothrow_value()
{
    if constexpr (std::is_void_v<Value>)
        return true;
    else
        return NothrowConstructibleFrom<Value, Value&&>;
}

template<class Left, class Mapper>
consteval bool error_mappable()
{
    using First = std::remove_cvref_t<Left>;
    using Map = std::remove_cvref_t<Mapper>;
    if constexpr (!ResultOperation<First> || !PureOperation<Map>) {
        return false;
    } else {
        using FirstAlgebra = ResultOperationAlgebra<First>;
        using MapAlgebra = OperationAlgebra<Map>;
        using OldError = typename FirstAlgebra::Error;
        if constexpr (MapAlgebra::nullary
                      || !std::same_as<OldError,
                                       typename MapAlgebra::Domain>
                      || !std::is_nothrow_invocable_v<
                          const Map&, OldError&&>) {
            return false;
        } else {
            using Mapped = std::invoke_result_t<const Map&, OldError&&>;
            using NewError = std::remove_cvref_t<Mapped>;
            using Value = typename FirstAlgebra::Value;
            return std::is_object_v<NewError>
                && ResultError<NewError>
                && NothrowConstructibleFrom<NewError, Mapped>
                && nothrow_value<Value>();
        }
    }
}

} // namespace detail

template<class Left, class Right>
concept SameResultError = ResultOperation<Left> && ResultOperation<Right>
    && std::same_as<typename ResultOperationAlgebra<Left>::Error,
                    typename ResultOperationAlgebra<Right>::Error>;

template<class Left, class Right>
concept KleisliComposable = SameResultError<Left, Right>
    && detail::Accepts<typename ResultOperationAlgebra<Left>::Value,
                       ResultOperationAlgebra<Right>>;

template<class Left, class Mapper>
concept ResultTransformable = ResultOperation<Left> && PureOperation<Mapper>
    && detail::Accepts<typename ResultOperationAlgebra<Left>::Value,
                       OperationAlgebra<Mapper>>;

template<class Left, class Mapper>
concept ErrorMappable = detail::error_mappable<Left, Mapper>();

template<class Left, class Right>
concept KleisliPairable = SameResultError<Left, Right>
    && (ResultOperationAlgebra<Left>::nullary
        == ResultOperationAlgebra<Right>::nullary)
    && std::is_object_v<typename ResultOperationAlgebra<Left>::Value>
    && std::is_object_v<typename ResultOperationAlgebra<Right>::Value>
    && detail::NothrowConstructibleFrom<
        typename ResultOperationAlgebra<Left>::Value,
        typename ResultOperationAlgebra<Left>::Value&&>
    && detail::NothrowConstructibleFrom<
        typename ResultOperationAlgebra<Right>::Value,
        typename ResultOperationAlgebra<Right>::Value&&>
    && (ResultOperationAlgebra<Left>::nullary
        || (std::same_as<typename ResultOperationAlgebra<Left>::Domain,
                         typename ResultOperationAlgebra<Right>::Domain>
            && std::is_object_v<
                typename ResultOperationAlgebra<Left>::Domain>
            && std::is_nothrow_invocable_v<
                const Left&,
                const typename ResultOperationAlgebra<Left>::Domain&>
            && std::is_nothrow_invocable_v<
                const Right&,
                const typename ResultOperationAlgebra<Right>::Domain&>));

template<class Step, class State, class Item>
concept FoldStep = detail::fold_step<Step, State, Item>();

template<class Step, class State, class Item, class Error>
concept KleisliFoldStep =
    detail::kleisli_fold_step<Step, State, Item, Error>();

template<class Range>
concept ScanRange = std::is_object_v<Range>
    && std::ranges::contiguous_range<const Range>
    && std::ranges::sized_range<const Range>
    && requires(const Range& range) {
        { std::ranges::data(range) } noexcept;
        { std::ranges::size(range) } noexcept;
    };

template<class Range>
    requires ScanRange<Range>
using RangeValue = std::ranges::range_value_t<const Range>;

namespace detail {

template<class Operation, class State, class Step, bool Fallible>
consteval bool scannable()
{
    using Source = std::remove_cvref_t<Operation>;
    using StoredState = std::remove_cvref_t<State>;
    using Fold = std::remove_cvref_t<Step>;
    if constexpr (!ResultOperation<Source>) {
        return false;
    } else {
        using Algebra = ResultOperationAlgebra<Source>;
        using Range = typename Algebra::Value;
        if constexpr (!ScanRange<Range>) {
            return false;
        } else {
            using Item = std::ranges::range_reference_t<const Range>;
            constexpr bool safeState =
                std::is_trivially_copyable_v<StoredState>
                && std::is_nothrow_copy_constructible_v<StoredState>
                && std::is_nothrow_move_constructible_v<StoredState>
                && std::is_nothrow_copy_assignable_v<StoredState>
                && std::is_nothrow_move_assignable_v<StoredState>;
            if constexpr (Fallible)
                return safeState && KleisliFoldStep<
                    Fold, StoredState, Item, typename Algebra::Error>;
            else
                return safeState
                    && FoldStep<Fold, StoredState, Item>;
        }
    }
}

} // namespace detail

namespace detail {

template<ResultOperation First, ResultOperation Next>
    requires KleisliComposable<First, Next>
struct BoundComposition final {
    using Algebra = ResultOperationAlgebra<First>;
    using Error = typename Algebra::Error;
    using Argument = std::conditional_t<
        Algebra::nullary, int, typename Algebra::Parameter>;

    static_assert(ResultError<Error>,
                  "composed error must be nothrow-movable");

    [[no_unique_address]] First first;
    [[no_unique_address]] Next next;

    [[nodiscard]] constexpr auto operator()() const
        noexcept(std::is_nothrow_move_constructible_v<Error>)
        requires Algebra::nullary
    {
        return first().and_then(next);
    }

    [[nodiscard]] constexpr auto operator()(Argument input) const
        noexcept(std::is_nothrow_move_constructible_v<Error>)
        requires (!Algebra::nullary)
    {
        return first(std::forward<Argument>(input)).and_then(next);
    }
};

template<ResultOperation First, PureOperation Mapper>
    requires ResultTransformable<First, Mapper>
struct MappedComposition final {
    using Algebra = ResultOperationAlgebra<First>;
    using Error = typename Algebra::Error;
    using Argument = std::conditional_t<
        Algebra::nullary, int, typename Algebra::Parameter>;

    static_assert(ResultError<Error>,
                  "transformed error must be nothrow-movable");

    [[no_unique_address]] First first;
    [[no_unique_address]] Mapper mapper;

    [[nodiscard]] constexpr auto operator()() const
        noexcept(std::is_nothrow_move_constructible_v<Error>)
        requires Algebra::nullary
    {
        return first().transform(mapper);
    }

    [[nodiscard]] constexpr auto operator()(Argument input) const
        noexcept(std::is_nothrow_move_constructible_v<Error>)
        requires (!Algebra::nullary)
    {
        return first(std::forward<Argument>(input)).transform(mapper);
    }
};

template<ResultOperation First, PureOperation Mapper>
    requires ErrorMappable<First, Mapper>
struct ErrorMapped final {
    using Algebra = ResultOperationAlgebra<First>;
    using Argument = std::conditional_t<
        Algebra::nullary, int, typename Algebra::Parameter>;
    using Value = typename Algebra::Value;
    using OldError = typename Algebra::Error;
    using Mapped = std::invoke_result_t<const Mapper&, OldError&&>;
    using Error = std::remove_cvref_t<Mapped>;
    using Carrier = Result<Value, Error>;

    [[no_unique_address]] First first;
    [[no_unique_address]] Mapper mapper;

    static_assert(ResultError<Error>);
    static_assert(nothrow_value<Value>());

    [[nodiscard]] constexpr Carrier operator()() const noexcept
        requires Algebra::nullary
    {
        return remap(first());
    }

    [[nodiscard]] constexpr Carrier operator()(Argument input) const noexcept
        requires (!Algebra::nullary)
    {
        return remap(first(std::forward<Argument>(input)));
    }

private:
    [[nodiscard]] constexpr Carrier remap(
        typename Algebra::Carrier result) const noexcept
    {
        if (result) {
            if constexpr (std::is_void_v<Value>)
                return Carrier{};
            else
                return Carrier(
                    std::in_place, std::move(result).value());
        }
        return Carrier(
            std::unexpect,
            mapper(std::move(result).error()));
    }
};

template<class Domain, bool Nullary>
struct PairArgument final {
    using type = const Domain&;
};

template<class Domain>
struct PairArgument<Domain, true> final {
    using type = int;
};

// Pairing is fail-fast and left-biased: left runs first, and its error wins
// when both operations would fail for the same witness.
template<ResultOperation Left, ResultOperation Right>
    requires KleisliPairable<Left, Right>
struct PairedComposition final {
    using LeftAlgebra = ResultOperationAlgebra<Left>;
    using RightAlgebra = ResultOperationAlgebra<Right>;
    static constexpr bool nullary = LeftAlgebra::nullary;

    using Domain = typename LeftAlgebra::Domain;
    using Argument = typename PairArgument<Domain, nullary>::type;
    using LeftValue = typename LeftAlgebra::Value;
    using RightValue = typename RightAlgebra::Value;
    using Value = std::pair<LeftValue, RightValue>;
    using Error = typename LeftAlgebra::Error;
    using Carrier = Result<Value, Error>;

    [[no_unique_address]] Left left;
    [[no_unique_address]] Right right;

    static_assert(NothrowConstructibleFrom<LeftValue, LeftValue&&>);
    static_assert(NothrowConstructibleFrom<RightValue, RightValue&&>);
    static_assert(ResultError<Error>);

    [[nodiscard]] constexpr Carrier operator()() const noexcept
        requires nullary
    {
        auto first = left();
        if (!first)
            return Carrier(
                std::unexpect, std::move(first).error());
        auto second = right();
        if (!second)
            return Carrier(
                std::unexpect, std::move(second).error());
        return Carrier(
            std::in_place,
            std::move(first).value(),
            std::move(second).value());
    }

    [[nodiscard]] constexpr Carrier operator()(Argument witness) const noexcept
        requires (!nullary)
    {
        auto first = left(witness);
        if (!first)
            return Carrier(
                std::unexpect, std::move(first).error());
        auto second = right(witness);
        if (!second)
            return Carrier(
                std::unexpect, std::move(second).error());
        return Carrier(
            std::in_place,
            std::move(first).value(),
            std::move(second).value());
    }
};

} // namespace detail

// A named function value. Result-valued functions additionally expose checked
// Kleisli and functor composition.
template<NonthrowingOperation Operation>
struct [[nodiscard]] Function final {
    using Algebra = OperationAlgebra<Operation>;
    using Return = typename Algebra::Return;
    using Argument = std::conditional_t<
        Algebra::nullary, int, typename Algebra::Parameter>;

    [[no_unique_address]] Operation operation;

    [[nodiscard]] constexpr Return operator()(
        this const Function& self) noexcept
        requires Algebra::nullary
    {
        return self.operation();
    }

    [[nodiscard]] constexpr Return operator()(
        this const Function& self, Argument input) noexcept
        requires (!Algebra::nullary)
    {
        return self.operation(std::forward<Argument>(input));
    }

    template<class Next>
        requires KleisliComposable<Operation, std::decay_t<Next>>
            && detail::NothrowConstructibleFrom<
                Operation, const Operation&>
            && detail::NothrowConstructibleFrom<
                std::decay_t<Next>, Next>
    [[nodiscard]] constexpr auto and_then(
        this const Function& self, Next&& next)
    {
        using NextOperation = std::decay_t<Next>;
        using Bound = detail::BoundComposition<Operation, NextOperation>;
        return Function<Bound>{Bound{
            self.operation,
            NextOperation(std::forward<Next>(next))}};
    }

    template<class Mapper>
        requires ResultTransformable<Operation, std::decay_t<Mapper>>
            && detail::NothrowConstructibleFrom<
                Operation, const Operation&>
            && detail::NothrowConstructibleFrom<
                std::decay_t<Mapper>, Mapper>
    [[nodiscard]] constexpr auto transform(
        this const Function& self, Mapper&& mapper)
    {
        using MapOperation = std::decay_t<Mapper>;
        using Mapped = detail::MappedComposition<Operation, MapOperation>;
        return Function<Mapped>{Mapped{
            self.operation,
            MapOperation(std::forward<Mapper>(mapper))}};
    }

    template<class Mapper>
        requires ResultOperation<Operation>
            && ErrorMappable<Operation, std::decay_t<Mapper>>
            && detail::NothrowConstructibleFrom<
                Operation, const Operation&>
            && detail::NothrowConstructibleFrom<
                std::decay_t<Mapper>, Mapper>
    [[nodiscard]] constexpr auto map_error(
        this const Function& self, Mapper&& mapper)
    {
        using MapOperation = std::decay_t<Mapper>;
        using Mapped = detail::ErrorMapped<Operation, MapOperation>;
        return Function<Mapped>{Mapped{
            self.operation,
            MapOperation(std::forward<Mapper>(mapper))}};
    }

    // Scan a contiguous result range into caller-owned storage. If the output
    // is shorter than the input, the written prefix is returned.
    template<class State, class Step>
        requires (detail::scannable<
            Operation, State, std::decay_t<Step>, false>())
    [[nodiscard]] constexpr auto scan_into(
        this const Function& self,
        State initial,
        Step&& step,
        std::span<State> output)
    {
        using ResultAlgebra = ResultOperationAlgebra<Operation>;
        using Range = typename ResultAlgebra::Value;
        using Fold = std::decay_t<Step>;
        auto scan = [initial,
                     fold = Fold(std::forward<Step>(step)),
                     output](const Range& values) noexcept {
            State state = initial;
            const size_t available = std::ranges::size(values);
            const size_t count = available < output.size()
                ? available
                : output.size();
            const auto data = std::ranges::data(values);
            for (size_t i = 0; i < count; ++i) {
                state = fold(std::move(state), data[i]);
                output[i] = state;
            }
            return std::span<State>{output.data(), count};
        };
        return self.transform(std::move(scan));
    }

    template<class State, class Step>
        requires (detail::scannable<
            Operation, State, std::decay_t<Step>, true>())
    [[nodiscard]] constexpr auto scan_into(
        this const Function& self,
        State initial,
        Step&& step,
        std::span<State> output)
    {
        using ResultAlgebra = ResultOperationAlgebra<Operation>;
        using Range = typename ResultAlgebra::Value;
        using Error = typename ResultAlgebra::Error;
        using Fold = std::decay_t<Step>;
        auto scan = [initial,
                     fold = Fold(std::forward<Step>(step)),
                     output](const Range& values) noexcept
            -> Result<std::span<State>, Error> {
            State state = initial;
            const size_t available = std::ranges::size(values);
            const size_t count = available < output.size()
                ? available
                : output.size();
            const auto data = std::ranges::data(values);
            for (size_t i = 0; i < count; ++i) {
                auto next = fold(std::move(state), data[i]);
                if (!next)
                    return Result<std::span<State>, Error>(
                        std::unexpect, std::move(next).error());
                state = std::move(next).value();
                output[i] = state;
            }
            return std::span<State>{output.data(), count};
        };
        return self.and_then(std::move(scan));
    }
};

template<NonthrowingOperation Operation>
Function(Operation) -> Function<Operation>;

template<ResultOperation Operation>
using ResultMorphism = Function<Operation>;

template<std::meta::info Declaration>
struct Lifted final {
    static_assert(std::meta::is_function(Declaration),
                  "lift requires a reflection of a function");
    static constexpr auto shape =
        detail::inspect_operation(std::meta::type_of(Declaration));
    static_assert(shape.concrete && shape.supportedArity
                  && shape.nonthrowing);
    static_assert(shape.carrier.valid,
                  "lifted function must return a Result");

    using Return = [:shape.result:];
    using Parameter = [:shape.parameter:];
    using Argument = std::conditional_t<
        shape.parameter == ^^void, int, Parameter>;

    [[nodiscard]] constexpr Return operator()() const noexcept
        requires (shape.parameter == ^^void)
    {
        return [:Declaration:]();
    }

    [[nodiscard]] constexpr Return operator()(Argument input) const noexcept
        requires (shape.parameter != ^^void)
    {
        return [:Declaration:](std::forward<Argument>(input));
    }
};

template<std::meta::info Declaration>
inline constexpr auto lift = Function{Lifted<Declaration>{}};

template<class Operation>
    requires NonthrowingOperation<std::decay_t<Operation>>
        && detail::NothrowConstructibleFrom<
            std::decay_t<Operation>, Operation>
[[nodiscard]] constexpr auto function(Operation&& operation)
{
    using Stored = std::decay_t<Operation>;
    static_assert(std::move_constructible<Stored>);
    return Function{Stored(std::forward<Operation>(operation))};
}

template<class Operation>
    requires ResultOperation<std::decay_t<Operation>>
        && detail::NothrowConstructibleFrom<
            std::decay_t<Operation>, Operation>
[[nodiscard]] constexpr auto compose(Operation&& operation)
{
    using Stored = std::decay_t<Operation>;
    static_assert(std::move_constructible<Stored>);
    return function(std::forward<Operation>(operation));
}

template<class Left, class Right>
    requires KleisliPairable<std::decay_t<Left>, std::decay_t<Right>>
        && detail::NothrowConstructibleFrom<std::decay_t<Left>, Left>
        && detail::NothrowConstructibleFrom<std::decay_t<Right>, Right>
[[nodiscard]] constexpr auto pair(Left&& left, Right&& right)
{
    using LeftOperation = std::decay_t<Left>;
    using RightOperation = std::decay_t<Right>;
    using Paired = detail::PairedComposition<
        LeftOperation, RightOperation>;
    return Function<Paired>{Paired{
        LeftOperation(std::forward<Left>(left)),
        RightOperation(std::forward<Right>(right))}};
}

namespace detail {

template<class First>
consteval bool all_pairable()
{
    return ResultOperation<std::decay_t<First>>;
}

template<class First, class Second, class... Rest>
consteval bool all_pairable()
{
    using Left = std::decay_t<First>;
    using Right = std::decay_t<Second>;
    if constexpr (!KleisliPairable<Left, Right>) {
        return false;
    } else if constexpr (sizeof...(Rest) == 0) {
        return true;
    } else {
        using Paired = Function<PairedComposition<Left, Right>>;
        return all_pairable<Paired, Rest...>();
    }
}

template<class Operation>
[[nodiscard]] constexpr auto all_impl(Operation&& operation)
{
    return function(std::forward<Operation>(operation));
}

template<class Left, class Right, class... Rest>
[[nodiscard]] constexpr auto all_impl(
    Left&& left, Right&& right, Rest&&... rest)
{
    auto paired = ::ano::pair(
        std::forward<Left>(left), std::forward<Right>(right));
    if constexpr (sizeof...(Rest) == 0)
        return paired;
    else
        return all_impl(
            std::move(paired), std::forward<Rest>(rest)...);
}

} // namespace detail

template<class... Operations>
concept AllPairable = sizeof...(Operations) > 0
    && detail::all_pairable<Operations...>();

// all(f, g, h) is the left-associated variadic pairing of one shared witness.
// Evaluation is always f, then g, then h; the first error is returned.
template<class... Operations>
    requires AllPairable<Operations...>
[[nodiscard]] constexpr auto all(Operations&&... operations)
{
    return detail::all_impl(
        std::forward<Operations>(operations)...);
}

} // namespace ano

#define ANO_LET(name, ...) \
    const auto name = ::ano::function(__VA_ARGS__)
