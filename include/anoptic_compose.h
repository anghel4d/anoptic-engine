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
    std::meta::info domain{};
    std::meta::info result{};
    ResultShape carrier{};
    bool concrete{};
    bool supportedArity{};
    bool nullary{};
    bool nonthrowing{};
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

consteval OperationShape inspect_declaration(std::meta::info declaration)
{
    OperationShape shape{};
    shape.declaration = declaration;
    if (shape.declaration == std::meta::info{})
        return shape;
    shape.concrete = true;

    const auto parameters = std::meta::parameters_of(shape.declaration);
    const size_t first = !parameters.empty()
            && std::meta::is_function_parameter(parameters[0])
            && std::meta::is_explicit_object_parameter(parameters[0])
        ? 1
        : 0;
    if (parameters.size() - first > 1)
        return shape;
    shape.supportedArity = true;
    shape.nullary = parameters.size() == first;
    shape.parameter = shape.nullary
        ? ^^void
        : (std::meta::is_type(parameters[first])
               ? parameters[first]
               : std::meta::type_of(parameters[first]));
    shape.domain = std::meta::remove_cvref(shape.parameter);
    shape.result = std::meta::return_type_of(shape.declaration);
    shape.carrier = inspect_result(shape.result);
    shape.nonthrowing = std::meta::is_noexcept(shape.declaration);
    return shape;
}

consteval OperationShape inspect_operation(std::meta::info type)
{
    return inspect_declaration(callable_declaration(type));
}

template<class Type>
struct OperationInspector {
    static consteval OperationShape inspect()
    {
        return inspect_operation(^^Type);
    }
};

template<class Type>
inline constexpr OperationShape operationShape =
    OperationInspector<std::remove_cvref_t<Type>>::inspect();

template<class Type>
consteval bool declared_invocation()
{
    using Operation = std::remove_cvref_t<Type>;
    constexpr auto shape = operationShape<Operation>;
    if constexpr (!shape.supportedArity) {
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
consteval bool nothrow_result_error()
{
    constexpr auto shape = operationShape<Type>;
    if constexpr (!shape.carrier.valid) {
        return false;
    } else {
        using Error = [:shape.carrier.error:];
        return std::is_nothrow_move_constructible_v<Error>;
    }
}

} // namespace detail

template<class Type>
concept ConcreteOperation = detail::operationShape<Type>.concrete;

template<class Type>
concept SupportedOperation = ConcreteOperation<Type>
    && detail::operationShape<Type>.supportedArity;

template<class Type>
concept NonthrowingOperation = SupportedOperation<Type>
    && detail::operationShape<Type>.nonthrowing
    && detail::declared_invocation<Type>();

template<class Type>
concept ResultOperation = NonthrowingOperation<Type>
    && detail::operationShape<Type>.carrier.valid
    && detail::nothrow_result_error<Type>();

template<class Type>
concept PureOperation = NonthrowingOperation<Type>
    && !detail::operationShape<Type>.carrier.valid;

template<SupportedOperation Type>
struct OperationAlgebra final {
    static constexpr auto shape = detail::operationShape<Type>;
    static constexpr bool nullary = shape.nullary;

    using Operation = std::remove_cvref_t<Type>;
    using Parameter = [:shape.parameter:];
    using Domain = [:shape.domain:];
    using Return = [:shape.result:];
};

template<ResultOperation Type>
struct ResultOperationAlgebra final {
    static constexpr auto shape = detail::operationShape<Type>;
    static constexpr bool nullary = shape.nullary;

    using Operation = std::remove_cvref_t<Type>;
    using Parameter = [:shape.parameter:];
    using Domain = [:shape.domain:];
    using ReflectedResult = [:shape.result:];
    using Carrier = std::remove_cvref_t<ReflectedResult>;
    using Value = [:shape.carrier.value:];
    using Error = [:shape.carrier.error:];

    static_assert(std::same_as<Carrier, std::expected<Value, Error>>);
    static_assert(std::same_as<Domain, std::remove_cvref_t<Parameter>>);
    static_assert(std::is_void_v<Value> || std::is_object_v<Value>);
    static_assert(nullary == std::is_void_v<Domain>);
};

template<class Left, class Right>
concept SameResultError = ResultOperation<Left> && ResultOperation<Right>
    && std::same_as<typename ResultOperationAlgebra<Left>::Error,
                    typename ResultOperationAlgebra<Right>::Error>;

template<class Left, class Right>
concept KleisliComposable = SameResultError<Left, Right>
    && ((std::same_as<typename ResultOperationAlgebra<Left>::Value, void>
         && ResultOperationAlgebra<Right>::nullary)
        || (!std::same_as<typename ResultOperationAlgebra<Left>::Value, void>
            && !ResultOperationAlgebra<Right>::nullary
            && std::same_as<typename ResultOperationAlgebra<Left>::Value,
                            typename ResultOperationAlgebra<Right>::Domain>));

template<class Left, class Mapper>
concept ResultTransformable = ResultOperation<Left> && PureOperation<Mapper>
    && ((std::same_as<typename ResultOperationAlgebra<Left>::Value, void>
         && OperationAlgebra<Mapper>::nullary)
        || (!std::same_as<typename ResultOperationAlgebra<Left>::Value, void>
            && !OperationAlgebra<Mapper>::nullary
            && std::same_as<typename ResultOperationAlgebra<Left>::Value,
                            typename OperationAlgebra<Mapper>::Domain>));

template<NonthrowingOperation Operation>
struct Function;

namespace detail {

template<ResultOperation First, ResultOperation Next,
         bool = ResultOperationAlgebra<First>::nullary>
    requires KleisliComposable<First, Next>
struct BoundComposition;

template<ResultOperation First, ResultOperation Next>
    requires KleisliComposable<First, Next>
struct BoundComposition<First, Next, false> final {
    [[no_unique_address]] First first;
    [[no_unique_address]] Next next;

    using Parameter = typename ResultOperationAlgebra<First>::Parameter;

    [[nodiscard]] constexpr auto operator()(Parameter input) const noexcept
    {
        return first(std::forward<Parameter>(input)).and_then(next);
    }
};

template<ResultOperation First, ResultOperation Next>
    requires KleisliComposable<First, Next>
struct BoundComposition<First, Next, true> final {
    [[no_unique_address]] First first;
    [[no_unique_address]] Next next;

    [[nodiscard]] constexpr auto operator()() const noexcept
    {
        return first().and_then(next);
    }
};

template<ResultOperation First, PureOperation Mapper,
         bool = ResultOperationAlgebra<First>::nullary>
    requires ResultTransformable<First, Mapper>
struct MappedComposition;

template<ResultOperation First, PureOperation Mapper>
    requires ResultTransformable<First, Mapper>
struct MappedComposition<First, Mapper, false> final {
    [[no_unique_address]] First first;
    [[no_unique_address]] Mapper mapper;

    using Parameter = typename ResultOperationAlgebra<First>::Parameter;

    [[nodiscard]] constexpr auto operator()(Parameter input) const noexcept
    {
        return first(std::forward<Parameter>(input)).transform(mapper);
    }
};

template<ResultOperation First, PureOperation Mapper>
    requires ResultTransformable<First, Mapper>
struct MappedComposition<First, Mapper, true> final {
    [[no_unique_address]] First first;
    [[no_unique_address]] Mapper mapper;

    [[nodiscard]] constexpr auto operator()() const noexcept
    {
        return first().transform(mapper);
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

    template<class Self>
    [[nodiscard]] constexpr Return operator()(this Self&& self) noexcept
        requires Algebra::nullary
    {
        return std::forward<Self>(self).operation();
    }

    template<class Self>
    [[nodiscard]] constexpr Return operator()(
        this Self&& self, Argument input) noexcept
        requires (!Algebra::nullary)
    {
        return std::forward<Self>(self).operation(
            std::forward<Argument>(input));
    }

    template<class Self, class Next>
        requires KleisliComposable<Operation, std::decay_t<Next>>
    [[nodiscard]] constexpr auto and_then(
        this Self&& self, Next&& next)
    {
        using NextOperation = std::decay_t<Next>;
        using Bound = detail::BoundComposition<Operation, NextOperation>;
        return Function<Bound>{Bound{
            std::forward<Self>(self).operation,
            NextOperation(std::forward<Next>(next))}};
    }

    template<class Self, class Mapper>
        requires ResultTransformable<Operation, std::decay_t<Mapper>>
    [[nodiscard]] constexpr auto transform(
        this Self&& self, Mapper&& mapper)
    {
        using MapOperation = std::decay_t<Mapper>;
        using Mapped = detail::MappedComposition<Operation, MapOperation>;
        return Function<Mapped>{Mapped{
            std::forward<Self>(self).operation,
            MapOperation(std::forward<Mapper>(mapper))}};
    }
};

namespace detail {

template<NonthrowingOperation Operation>
consteval OperationShape inspect_function()
{
    constexpr std::meta::info type = ^^Function<Operation>;
    constexpr std::meta::info self = std::meta::add_lvalue_reference(
        std::meta::add_const(type));
    std::meta::info call{};
    for (const std::meta::info declaration : std::meta::members_of(
             type, std::meta::access_context::unchecked())) {
        if (!std::meta::is_operator_function_template(declaration)
            || std::meta::operator_of(declaration)
                != std::meta::op_parentheses
            || !std::meta::can_substitute(declaration, {self}))
            continue;
        if (call != std::meta::info{})
            return {};
        call = std::meta::substitute(declaration, {self});
    }
    return inspect_declaration(call);
}

template<NonthrowingOperation Operation>
struct OperationInspector<Function<Operation>> {
    static consteval OperationShape inspect()
    {
        return inspect_function<Operation>();
    }
};

} // namespace detail

template<NonthrowingOperation Operation>
Function(Operation) -> Function<Operation>;

template<ResultOperation Operation>
using ResultMorphism = Function<Operation>;

template<std::meta::info Declaration>
struct Lifted final {
    static_assert(std::meta::is_function(Declaration));
    static constexpr auto shape =
        detail::inspect_operation(std::meta::type_of(Declaration));
    static_assert(shape.supportedArity && shape.nonthrowing
                  && shape.carrier.valid);

    using Return = [:shape.result:];
    using Parameter = [:shape.parameter:];
    using Argument = std::conditional_t<
        shape.nullary, int, Parameter>;

    [[nodiscard]] constexpr Return operator()() const noexcept
        requires (shape.nullary)
    {
        return [:Declaration:]();
    }

    [[nodiscard]] constexpr Return operator()(Argument input) const noexcept
        requires (!shape.nullary)
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
    using Stored = std::decay_t<Operation>;
    return Function{Stored(std::forward<Operation>(operation))};
}

} // namespace ano

#define ANO_LET(name, ...) \
    const auto name = ::ano::function(__VA_ARGS__)
