/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#pragma once

#include "anoptic_results.h"

#include <concepts>
#include <type_traits>
#include <utility>

namespace ano {

namespace detail {

struct OperationShape final {
    std::meta::info declaration{};
    std::meta::info parameter{};
    std::meta::info result{};
    ResultShape carrier{};
};

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

consteval OperationShape inspect_operation(std::meta::info type)
{
    OperationShape shape{};
    shape.declaration = callable_declaration(type);
    if (shape.declaration == std::meta::info{})
        return shape;

    const auto parameters = std::meta::parameters_of(shape.declaration);
    const size_t first = !parameters.empty()
            && std::meta::is_function_parameter(parameters[0])
            && std::meta::is_explicit_object_parameter(parameters[0])
        ? 1
        : 0;
    if (parameters.size() - first > 1)
        return shape;
    shape.parameter = parameters.size() == first
        ? ^^void
        : (std::meta::is_type(parameters[first])
               ? parameters[first]
               : std::meta::type_of(parameters[first]));
    shape.result = std::meta::return_type_of(shape.declaration);
    shape.carrier = inspect_result(shape.result);
    return shape;
}

template<class Type>
inline constexpr OperationShape operationShape =
    inspect_operation(^^std::remove_cvref_t<Type>);

template<class Type>
consteval bool declared_invocation()
{
    using Operation = std::remove_cvref_t<Type>;
    constexpr auto shape = operationShape<Operation>;
    if constexpr (shape.parameter == std::meta::info{}) {
        return false;
    } else if constexpr (shape.parameter == ^^void) {
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
consteval bool nothrow_result_error()
{
    constexpr auto shape = operationShape<Type>;
    if constexpr (!shape.carrier.valid)
        return false;
    else {
        using Error = [:shape.carrier.error:];
        return ResultError<Error>;
    }
}

} // namespace detail

template<class Type>
concept NonthrowingOperation = detail::operationShape<Type>.declaration
        != std::meta::info{}
    && detail::operationShape<Type>.parameter != std::meta::info{}
    && std::meta::is_noexcept(detail::operationShape<Type>.declaration)
    && detail::declared_invocation<Type>();

template<class Type>
concept ResultOperation = NonthrowingOperation<Type>
    && detail::operationShape<Type>.carrier.valid
    && detail::nothrow_result_error<Type>();

template<class Type>
concept PureOperation = NonthrowingOperation<Type>
    && !detail::operationShape<Type>.carrier.valid;

template<NonthrowingOperation Type>
struct OperationAlgebra {
    static constexpr auto shape = detail::operationShape<Type>;
    static constexpr bool nullary = shape.parameter == ^^void;

    using Operation = std::remove_cvref_t<Type>;
    using Parameter = [:shape.parameter:];
    using Domain = [:std::meta::remove_cvref(shape.parameter):];
    using Return = [:shape.result:];
};

template<ResultOperation Type>
struct ResultOperationAlgebra final : OperationAlgebra<Type> {
    using Base = OperationAlgebra<Type>;
    using CarrierAlgebra = ResultAlgebra<typename Base::Return>;
    using Carrier = typename CarrierAlgebra::Carrier;
    using Value = typename CarrierAlgebra::Value;
    using Error = typename CarrierAlgebra::Error;
};

namespace detail {

template<class Value, class Next>
concept Accepts = (Next::nullary && std::same_as<Value, void>)
    || (!Next::nullary && std::same_as<Value, typename Next::Domain>);

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

template<NonthrowingOperation Operation>
struct Function;

namespace detail {

template<ResultOperation First, ResultOperation Next>
    requires KleisliComposable<First, Next>
struct BoundComposition final {
    using Algebra = ResultOperationAlgebra<First>;
    using Argument = std::conditional_t<
        Algebra::nullary, int, typename Algebra::Parameter>;

    [[no_unique_address]] First first;
    [[no_unique_address]] Next next;

    [[nodiscard]] constexpr auto operator()() const noexcept
        requires Algebra::nullary
    {
        return first().and_then(next);
    }

    [[nodiscard]] constexpr auto operator()(Argument input) const noexcept
        requires (!Algebra::nullary)
    {
        return first(std::forward<Argument>(input)).and_then(next);
    }
};

template<ResultOperation First, PureOperation Mapper>
    requires ResultTransformable<First, Mapper>
struct MappedComposition final {
    using Algebra = ResultOperationAlgebra<First>;
    using Argument = std::conditional_t<
        Algebra::nullary, int, typename Algebra::Parameter>;

    [[no_unique_address]] First first;
    [[no_unique_address]] Mapper mapper;

    [[nodiscard]] constexpr auto operator()() const noexcept
        requires Algebra::nullary
    {
        return first().transform(mapper);
    }

    [[nodiscard]] constexpr auto operator()(Argument input) const noexcept
        requires (!Algebra::nullary)
    {
        return first(std::forward<Argument>(input)).transform(mapper);
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
    [[nodiscard]] constexpr auto transform(
        this const Function& self, Mapper&& mapper)
    {
        using MapOperation = std::decay_t<Mapper>;
        using Mapped = detail::MappedComposition<Operation, MapOperation>;
        return Function<Mapped>{Mapped{
            self.operation,
            MapOperation(std::forward<Mapper>(mapper))}};
    }
};

template<NonthrowingOperation Operation>
Function(Operation) -> Function<Operation>;

template<ResultOperation Operation>
using ResultMorphism = Function<Operation>;

template<std::meta::info Declaration>
struct Lifted final {
    static_assert(std::meta::is_function(Declaration));
    static constexpr auto shape =
        detail::inspect_operation(std::meta::type_of(Declaration));
    static_assert(shape.parameter != std::meta::info{}
                  && std::meta::is_noexcept(shape.declaration)
                  && shape.carrier.valid);

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
[[nodiscard]] constexpr auto function(Operation&& operation)
{
    using Stored = std::decay_t<Operation>;
    return Function{Stored(std::forward<Operation>(operation))};
}

template<class Operation>
    requires ResultOperation<std::decay_t<Operation>>
[[nodiscard]] constexpr auto compose(Operation&& operation)
{
    return function(std::forward<Operation>(operation));
}

} // namespace ano

#define ANO_LET(name, ...) \
    const auto name = ::ano::function(__VA_ARGS__)
