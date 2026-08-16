/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0-only
 * Anoptic targets ISO C++26. */

#pragma once

#include "anoptic_results.h"

#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace ano {

namespace detail {

struct CallableShape final {
    std::meta::info declaration{};
    std::meta::info first{};
    std::meta::info second{};
    std::meta::info result{};
    ResultShape carrier{};
    size_t arity{static_cast<size_t>(-1)};
    bool nonthrowing{};

    friend constexpr bool operator==(const CallableShape&,
                                     const CallableShape&) = default;

    [[nodiscard]] consteval bool has_arity(size_t expected) const noexcept
    {
        return declaration != std::meta::info{} && arity == expected;
    }
};

static_assert(std::regular<CallableShape>);
static_assert(std::is_trivially_copyable_v<CallableShape>);

consteval std::meta::info callable_declaration(std::meta::info type)
{
    type = std::meta::dealias(std::meta::remove_cvref(type));
    if (std::meta::is_function_type(type))
        return type;
    if (std::meta::is_pointer_type(type)) {
        const auto pointee = std::meta::remove_pointer(type);
        return std::meta::is_function_type(pointee)
            ? pointee
            : std::meta::info{};
    }
    if (!std::meta::is_class_type(type))
        return {};

    std::meta::info call{};
    for (const auto member : std::meta::members_of(
             type, std::meta::access_context::unchecked())) {
        if (std::meta::is_operator_function_template(member)
            && std::meta::operator_of(member) == std::meta::op_parentheses)
            return {};
        if (!std::meta::is_operator_function(member)
            || std::meta::operator_of(member) != std::meta::op_parentheses)
            continue;
        if (call != std::meta::info{})
            return {};
        call = member;
    }
    return call;
}

consteval std::meta::info parameter_type(std::meta::info parameter)
{
    return std::meta::is_type(parameter)
        ? parameter
        : std::meta::type_of(parameter);
}

consteval CallableShape inspect_callable(std::meta::info type)
{
    CallableShape shape{};
    shape.first = ^^void;
    shape.second = ^^void;
    shape.declaration = callable_declaration(type);
    if (shape.declaration == std::meta::info{})
        return shape;

    const auto parameters = std::meta::parameters_of(shape.declaration);
    const size_t first = !parameters.empty()
            && std::meta::is_function_parameter(parameters[0])
            && std::meta::is_explicit_object_parameter(parameters[0])
        ? 1
        : 0;
    shape.arity = parameters.size() - first;
    if (shape.arity > 0)
        shape.first = parameter_type(parameters[first]);
    if (shape.arity > 1)
        shape.second = parameter_type(parameters[first + 1]);
    shape.result = std::meta::return_type_of(shape.declaration);
    shape.carrier = inspect_result(shape.result);
    shape.nonthrowing = std::meta::is_noexcept(shape.declaration);
    return shape;
}

template<class Type>
inline constexpr CallableShape callableShape =
    inspect_callable(^^std::remove_cvref_t<Type>);

template<class Operation, class Return, class... Arguments>
consteval bool exact_invocation()
{
    if constexpr (!std::is_invocable_v<const Operation&, Arguments...>)
        return false;
    else
        return std::is_nothrow_invocable_v<
                   const Operation&, Arguments...>
            && std::same_as<
                std::invoke_result_t<const Operation&, Arguments...>,
                Return>;
}

template<class Type>
consteval bool declared_invocation()
{
    using Operation = std::remove_cvref_t<Type>;
    constexpr auto shape = callableShape<Operation>;
    if constexpr (shape.declaration == std::meta::info{}
                  || shape.arity > 2) {
        return false;
    } else {
        using Return = [:shape.result:];
        if constexpr (shape.arity == 0)
            return exact_invocation<Operation, Return>();
        else if constexpr (shape.arity == 1) {
            using First = [:shape.first:];
            return exact_invocation<Operation, Return, First>();
        } else {
            using First = [:shape.first:];
            using Second = [:shape.second:];
            return exact_invocation<Operation, Return, First, Second>();
        }
    }
}

template<class Stored, class Source>
concept NothrowStorable = std::move_constructible<Stored>
    && std::is_nothrow_move_constructible_v<Stored>
    && NothrowConstructibleFrom<Stored, Source>;

template<class Value>
consteval bool nothrow_value()
{
    if constexpr (std::is_void_v<Value>)
        return true;
    else
        return NothrowConstructibleFrom<Value, Value&&>;
}

} // namespace detail

template<class Type>
concept NonthrowingOperation = detail::callableShape<Type>.declaration
        != std::meta::info{}
    && detail::callableShape<Type>.arity <= 1
    && detail::callableShape<Type>.nonthrowing
    && detail::declared_invocation<Type>();

template<class Type>
concept ResultOperation = NonthrowingOperation<Type>
    && detail::callableShape<Type>.carrier.valid
    && [] consteval {
        constexpr auto shape = detail::callableShape<Type>;
        using Error = [:shape.carrier.error:];
        return ResultError<Error>;
    }();

template<class Type>
concept PureOperation = NonthrowingOperation<Type>
    && !detail::callableShape<Type>.carrier.valid;

template<NonthrowingOperation Type>
struct OperationAlgebra {
    static constexpr auto shape = detail::callableShape<Type>;
    static constexpr bool nullary = shape.arity == 0;

    using Operation = std::remove_cvref_t<Type>;
    using Parameter = [:shape.first:];
    using Domain = std::remove_cvref_t<Parameter>;
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
    static_assert(std::same_as<typename Base::Domain,
                  std::remove_cvref_t<typename Base::Parameter>>);
    static_assert(std::is_void_v<Value> || std::is_object_v<Value>);
    static_assert(ResultError<Error>);
    static_assert(Base::nullary == std::is_void_v<typename Base::Domain>);
};

namespace detail {

template<class Value, class Next>
concept Accepts = (Next::nullary && std::same_as<Value, void>)
    || (!Next::nullary && std::same_as<Value, typename Next::Domain>);

template<class Left, class Mapper>
consteval bool transformable()
{
    if constexpr (!ResultOperation<Left> || !PureOperation<Mapper>)
        return false;
    else {
        using First = ResultOperationAlgebra<Left>;
        using Map = OperationAlgebra<Mapper>;
        return Accepts<typename First::Value, Map>
            && (std::is_void_v<typename Map::Return>
                || std::is_object_v<typename Map::Return>)
            && nothrow_value<typename Map::Return>();
    }
}

template<class Left, class Mapper>
consteval bool error_mappable()
{
    if constexpr (!ResultOperation<Left> || !PureOperation<Mapper>) {
        return false;
    } else {
        using First = ResultOperationAlgebra<Left>;
        using Map = OperationAlgebra<Mapper>;
        using OldError = typename First::Error;
        if constexpr (Map::nullary
                      || !std::same_as<OldError, typename Map::Domain>
                      || !std::is_nothrow_invocable_v<
                          const Mapper&, OldError&&>) {
            return false;
        } else {
            using Mapped = std::invoke_result_t<const Mapper&, OldError&&>;
            using NewError = std::remove_cvref_t<Mapped>;
            return std::is_object_v<NewError>
                && ResultError<NewError>
                && NothrowConstructibleFrom<NewError, Mapped>
                && nothrow_value<typename First::Value>();
        }
    }
}

template<bool Fallible, class Step, class State, class Item, class Error>
consteval bool fold_step()
{
    using Operation = std::remove_cvref_t<Step>;
    using StoredState = std::remove_cvref_t<State>;
    using StoredError = std::remove_cvref_t<Error>;
    constexpr auto shape = callableShape<Operation>;
    if constexpr (!shape.has_arity(2) || !shape.nonthrowing
                  || !declared_invocation<Operation>()) {
        return false;
    } else {
        using StateParameter = [:shape.first:];
        using ItemParameter = [:shape.second:];
        using Return = [:shape.result:];
        if constexpr (!std::same_as<
                          std::remove_cvref_t<StateParameter>, StoredState>
                      || !std::same_as<
                          std::remove_cvref_t<ItemParameter>,
                          std::remove_cvref_t<Item>>
                      || !std::movable<StoredState>
                      || !std::is_nothrow_invocable_v<
                          const Operation&, StoredState, Item>) {
            return false;
        } else if constexpr (Fallible) {
            if constexpr (!shape.carrier.valid)
                return false;
            else {
                using Carrier = std::remove_cvref_t<Return>;
                using Algebra = ResultAlgebra<Carrier>;
                return std::same_as<typename Algebra::Value, StoredState>
                    && std::same_as<typename Algebra::Error, StoredError>
                    && ResultError<StoredError>;
            }
        } else {
            return !shape.carrier.valid
                && std::same_as<Return, StoredState>;
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
concept ResultTransformable = detail::transformable<Left, Mapper>();

template<class Left, class Mapper>
concept ErrorMappable = detail::error_mappable<Left, Mapper>();

template<class Left, class Right>
concept KleisliPairable = SameResultError<Left, Right>
    && (ResultOperationAlgebra<Left>::nullary
        == ResultOperationAlgebra<Right>::nullary)
    && std::is_object_v<typename ResultOperationAlgebra<Left>::Value>
    && std::is_object_v<typename ResultOperationAlgebra<Right>::Value>
    && detail::nothrow_value<
        typename ResultOperationAlgebra<Left>::Value>()
    && detail::nothrow_value<
        typename ResultOperationAlgebra<Right>::Value>()
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
concept FoldStep = detail::fold_step<
    false, Step, State, Item, void>();

template<class Step, class State, class Item, class Error>
concept KleisliFoldStep = detail::fold_step<
    true, Step, State, Item, Error>();

template<class Range>
concept ScanRange = std::is_object_v<Range>
    && std::ranges::contiguous_range<const Range>
    && std::ranges::sized_range<const Range>
    && requires(const Range& range) {
        { std::ranges::data(range) } noexcept;
        { std::ranges::size(range) } noexcept;
    };

namespace detail {

template<class Source, class State, class Step>
consteval bool scannable()
{
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
            if constexpr (callableShape<Fold>.carrier.valid)
                return safeState && KleisliFoldStep<
                    Fold, StoredState, Item, typename Algebra::Error>;
            else
                return safeState && FoldStep<
                    Fold, StoredState, Item>;
        }
    }
}

enum class CompositionKind {
    bind,
    transform,
    mapError,
};

template<CompositionKind Kind, class First, class Next>
struct Composition final {
    using Algebra = ResultOperationAlgebra<First>;
    using Argument = std::conditional_t<
        Algebra::nullary, int, typename Algebra::Parameter>;

    [[no_unique_address]] First first;
    [[no_unique_address]] Next next;

    [[nodiscard]] constexpr auto operator()() const noexcept
        requires Algebra::nullary
    {
        return apply(first());
    }

    [[nodiscard]] constexpr auto operator()(Argument input) const noexcept
        requires (!Algebra::nullary)
    {
        return apply(first(std::forward<Argument>(input)));
    }

private:
    [[nodiscard]] constexpr auto apply(
        typename Algebra::Carrier result) const noexcept
    {
        if constexpr (Kind == CompositionKind::bind)
            return std::move(result).and_then(next);
        else if constexpr (Kind == CompositionKind::transform)
            return std::move(result).transform(next);
        else {
            using Value = typename Algebra::Value;
            using Mapped = std::invoke_result_t<
                const Next&, typename Algebra::Error&&>;
            using Error = std::remove_cvref_t<Mapped>;
            using Carrier = Result<Value, Error>;
            if (result) {
                if constexpr (std::is_void_v<Value>)
                    return Carrier{};
                else
                    return Carrier(
                        std::in_place, std::move(result).value());
            }
            return Carrier(
                std::unexpect, next(std::move(result).error()));
        }
    }
};

template<class First, class Source>
concept StorableChain = NothrowStorable<First, const First&>
    && NothrowStorable<std::decay_t<Source>, Source>;

template<class Domain, bool Nullary>
struct PairArgument final {
    using type = const Domain&;
};

template<class Domain>
struct PairArgument<Domain, true> final {
    using type = int;
};

template<class Left, class Right>
struct PairedComposition final {
    using LeftAlgebra = ResultOperationAlgebra<Left>;
    using RightAlgebra = ResultOperationAlgebra<Right>;
    static constexpr bool nullary = LeftAlgebra::nullary;

    using Domain = typename LeftAlgebra::Domain;
    using Argument = typename PairArgument<Domain, nullary>::type;
    using LeftValue = typename LeftAlgebra::Value;
    using RightValue = typename RightAlgebra::Value;
    using Carrier = Result<
        std::pair<LeftValue, RightValue>, typename LeftAlgebra::Error>;

    [[no_unique_address]] Left left;
    [[no_unique_address]] Right right;

    [[nodiscard]] constexpr Carrier operator()() const noexcept
        requires nullary
    {
        return invoke();
    }

    [[nodiscard]] constexpr Carrier operator()(Argument witness) const noexcept
        requires (!nullary)
    {
        return invoke(witness);
    }

private:
    template<class... Arguments>
    [[nodiscard]] constexpr Carrier invoke(Arguments&&... arguments) const
        noexcept
    {
        auto first = left(std::forward<Arguments>(arguments)...);
        if (!first)
            return Carrier(std::unexpect, std::move(first).error());
        auto second = right(std::forward<Arguments>(arguments)...);
        if (!second)
            return Carrier(std::unexpect, std::move(second).error());
        return Carrier(std::in_place,
            std::move(first).value(), std::move(second).value());
    }
};

template<class Source, class State, class Step>
struct ScanComposition final {
    using Algebra = ResultOperationAlgebra<Source>;
    using Range = typename Algebra::Value;
    using Error = typename Algebra::Error;
    static constexpr bool fallible = callableShape<Step>.carrier.valid;

    State initial;
    [[no_unique_address]] Step step;
    std::span<State> output;

    [[nodiscard]] constexpr auto operator()(const Range& values) const noexcept
    {
        State state = initial;
        const size_t available = std::ranges::size(values);
        const size_t count = available < output.size()
            ? available
            : output.size();
        const auto data = std::ranges::data(values);
        for (size_t i = 0; i < count; ++i) {
            if constexpr (fallible) {
                auto next = step(std::move(state), data[i]);
                if (!next)
                    return Result<std::span<State>, Error>(
                        std::unexpect, std::move(next).error());
                state = std::move(next).value();
            } else {
                state = step(std::move(state), data[i]);
            }
            output[i] = state;
        }
        const std::span<State> written{output.data(), count};
        if constexpr (fallible)
            return Result<std::span<State>, Error>(written);
        else
            return written;
    }
};

} // namespace detail

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
            && detail::StorableChain<Operation, Next>
    [[nodiscard]] constexpr auto and_then(
        this const Function& self, Next&& next) noexcept
    {
        using Stored = std::decay_t<Next>;
        using Node = detail::Composition<
            detail::CompositionKind::bind, Operation, Stored>;
        return Function<Node>{Node{
            self.operation, Stored(std::forward<Next>(next))}};
    }

    template<class Mapper>
        requires ResultTransformable<Operation, std::decay_t<Mapper>>
            && detail::StorableChain<Operation, Mapper>
    [[nodiscard]] constexpr auto transform(
        this const Function& self, Mapper&& mapper) noexcept
    {
        using Stored = std::decay_t<Mapper>;
        using Node = detail::Composition<
            detail::CompositionKind::transform, Operation, Stored>;
        return Function<Node>{Node{
            self.operation, Stored(std::forward<Mapper>(mapper))}};
    }

    template<class Mapper>
        requires ErrorMappable<Operation, std::decay_t<Mapper>>
            && detail::StorableChain<Operation, Mapper>
    [[nodiscard]] constexpr auto map_error(
        this const Function& self, Mapper&& mapper) noexcept
    {
        using Stored = std::decay_t<Mapper>;
        using Node = detail::Composition<
            detail::CompositionKind::mapError, Operation, Stored>;
        return Function<Node>{Node{
            self.operation, Stored(std::forward<Mapper>(mapper))}};
    }

    template<class State, class Step>
        requires (detail::scannable<
                      Operation, State, std::decay_t<Step>>())
            && detail::NothrowStorable<std::decay_t<Step>, Step>
    [[nodiscard]] constexpr auto scan_into(
        this const Function& self, State initial, Step&& step,
        std::span<State> output) noexcept
    {
        using Fold = std::decay_t<Step>;
        using Scan = detail::ScanComposition<Operation, State, Fold>;
        auto scan = Scan{
            std::move(initial), Fold(std::forward<Step>(step)), output};
        if constexpr (Scan::fallible)
            return self.and_then(std::move(scan));
        else
            return self.transform(std::move(scan));
    }
};

template<NonthrowingOperation Operation>
Function(Operation) -> Function<Operation>;

template<ResultOperation Operation>
using ResultMorphism = Function<Operation>;

namespace detail {

template<std::meta::info Declaration>
consteval bool liftable()
{
    if constexpr (!std::meta::is_function(Declaration))
        return false;
    else {
        constexpr auto shape = inspect_callable(
            std::meta::type_of(Declaration));
        return shape.declaration != std::meta::info{}
            && shape.arity <= 1
            && shape.nonthrowing
            && shape.carrier.valid;
    }
}

} // namespace detail

template<std::meta::info Declaration>
    requires (detail::liftable<Declaration>())
struct Lifted final {
    static constexpr auto shape = detail::inspect_callable(
        std::meta::type_of(Declaration));
    using Return = [:shape.result:];
    using Parameter = [:shape.first:];
    using Argument = std::conditional_t<
        shape.arity == 0, int, Parameter>;

    [[nodiscard]] constexpr Return operator()() const noexcept
        requires (shape.arity == 0)
    {
        return [:Declaration:]();
    }

    [[nodiscard]] constexpr Return operator()(Argument input) const noexcept
        requires (shape.arity == 1)
    {
        return [:Declaration:](std::forward<Argument>(input));
    }
};

template<std::meta::info Declaration>
    requires (detail::liftable<Declaration>())
inline constexpr auto lift = Function{Lifted<Declaration>{}};

template<class Operation>
    requires NonthrowingOperation<std::decay_t<Operation>>
        && detail::NothrowStorable<std::decay_t<Operation>, Operation>
[[nodiscard]] constexpr auto function(Operation&& operation)
    noexcept
{
    using Stored = std::decay_t<Operation>;
    return Function{Stored(std::forward<Operation>(operation))};
}

template<class Operation>
    requires ResultOperation<std::decay_t<Operation>>
        && detail::NothrowStorable<std::decay_t<Operation>, Operation>
[[nodiscard]] constexpr auto compose(Operation&& operation)
    noexcept
{
    return function(std::forward<Operation>(operation));
}

template<class Left, class Right>
    requires KleisliPairable<std::decay_t<Left>, std::decay_t<Right>>
        && detail::NothrowStorable<std::decay_t<Left>, Left>
        && detail::NothrowStorable<std::decay_t<Right>, Right>
[[nodiscard]] constexpr auto pair(Left&& left, Right&& right)
    noexcept
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
    if constexpr (!KleisliPairable<Left, Right>)
        return false;
    else if constexpr (sizeof...(Rest) == 0)
        return true;
    else {
        using Paired = Function<PairedComposition<Left, Right>>;
        return all_pairable<Paired, Rest...>();
    }
}

template<class Operation>
[[nodiscard]] constexpr auto all_impl(Operation&& operation)
    noexcept
{
    return function(std::forward<Operation>(operation));
}

template<class Left, class Right, class... Rest>
[[nodiscard]] constexpr auto all_impl(
    Left&& left, Right&& right, Rest&&... rest) noexcept
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

template<class... Operations>
    requires AllPairable<Operations...>
        && (detail::NothrowStorable<
                std::decay_t<Operations>, Operations> && ...)
[[nodiscard]] constexpr auto all(Operations&&... operations)
    noexcept
{
    return detail::all_impl(
        std::forward<Operations>(operations)...);
}

} // namespace ano

#define ANO_LET(name, ...) \
    const auto name = ::ano::function(__VA_ARGS__)
