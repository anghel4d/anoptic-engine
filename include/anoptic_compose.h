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

consteval OperationShape inspect_operation(std::meta::info type)
{
    OperationShape shape{};
    shape.declaration = callable_declaration(type);
    if (shape.declaration == std::meta::info{})
        return shape;
    shape.concrete = true;

    const auto parameters = std::meta::parameters_of(shape.declaration);
    if (parameters.size() > 1)
        return shape;
    shape.supportedArity = true;
    shape.nullary = parameters.empty();
    shape.parameter = shape.nullary
        ? ^^void
        : (std::meta::is_type(parameters[0])
               ? parameters[0]
               : std::meta::type_of(parameters[0]));
    shape.domain = std::meta::remove_cvref(shape.parameter);
    shape.result = std::meta::return_type_of(shape.declaration);
    shape.carrier = inspect_result(shape.result);
    shape.nonthrowing = std::meta::is_noexcept(shape.declaration);
    return shape;
}

template<class Type>
inline constexpr OperationShape operationShape = inspect_operation(^^Type);

template<class Type>
consteval bool declared_invocation()
{
    using Operation = std::remove_cvref_t<Type>;
    constexpr auto shape = operationShape<Operation>;
    if constexpr (!shape.supportedArity) {
        return false;
    } else if constexpr (shape.nullary) {
        using Return = [:shape.result:];
        return requires(const Operation& operation) {
            { operation() } noexcept -> std::same_as<Return>;
        };
    } else {
        using Parameter = [:shape.parameter:];
        using Return = [:shape.result:];
        return requires(const Operation& operation, Parameter argument) {
            { operation(std::forward<Parameter>(argument)) }
                noexcept -> std::same_as<Return>;
        };
    }
}

template<class Type>
concept WrappedOperation = requires {
    typename std::remove_cvref_t<Type>::AnopticOperation;
};

template<class Type, bool = WrappedOperation<Type>>
struct OperationSource final {
    using type = std::remove_cvref_t<Type>;
};

template<class Type>
struct OperationSource<Type, true> final {
    using type = typename std::remove_cvref_t<Type>::AnopticOperation;
};

template<class Type>
using OperationSourceType = typename OperationSource<Type>::type;

template<class Type>
[[nodiscard]] constexpr decltype(auto) unwrap_operation(Type&& operation)
{
    if constexpr (WrappedOperation<Type>)
        return std::forward<Type>(operation).operation;
    else
        return std::forward<Type>(operation);
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
    && detail::operationShape<Type>.carrier.valid;

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
    using Carrier = [:shape.carrier.carrier:];
    using Value = [:shape.carrier.value:];
    using Error = [:shape.carrier.error:];
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

template<ResultOperation Operation>
struct ResultMorphism;

template<PureOperation Operation>
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

// A named, concrete pure function value. It carries no sequencing semantics.
template<PureOperation Operation>
struct [[nodiscard]] Function final {
    using AnopticOperation = Operation;
    using Algebra = OperationAlgebra<Operation>;

    [[no_unique_address]] Operation operation;

    template<class Self>
        requires Algebra::nullary
    [[nodiscard]] constexpr auto operator()(this Self&& self) noexcept
        -> typename Algebra::Return
    {
        return std::forward<Self>(self).operation();
    }

    template<class Self, class Input>
        requires (!Algebra::nullary
                  && std::same_as<std::remove_cvref_t<Input>,
                                  typename Algebra::Domain>)
    [[nodiscard]] constexpr auto operator()(
        this Self&& self, Input&& input) noexcept -> typename Algebra::Return
    {
        return std::forward<Self>(self).operation(
            std::forward<Input>(input));
    }
};

template<PureOperation Operation>
Function(Operation) -> Function<Operation>;

// A Result-valued arrow. Its fluent operations are the checked Kleisli and
// functor compositions of the reflected domain, value and error types.
template<ResultOperation Operation>
struct [[nodiscard]] ResultMorphism final {
    using AnopticOperation = Operation;
    using Algebra = ResultOperationAlgebra<Operation>;

    [[no_unique_address]] Operation operation;

    template<class Self>
        requires Algebra::nullary
    [[nodiscard]] constexpr auto operator()(this Self&& self) noexcept
        -> typename Algebra::Carrier
    {
        return std::forward<Self>(self).operation();
    }

    template<class Self, class Input>
        requires (!Algebra::nullary
                  && std::same_as<std::remove_cvref_t<Input>,
                                  typename Algebra::Domain>)
    [[nodiscard]] constexpr auto operator()(
        this Self&& self, Input&& input) noexcept -> typename Algebra::Carrier
    {
        return std::forward<Self>(self).operation(
            std::forward<Input>(input));
    }

    template<class Self, class Next>
        requires KleisliComposable<
            Operation, detail::OperationSourceType<Next>>
    [[nodiscard]] constexpr auto and_then(this Self&& self, Next&& next)
    {
        auto&& nextOperation = detail::unwrap_operation(
            std::forward<Next>(next));
        using NextOperation = std::decay_t<decltype(nextOperation)>;
        using Bound = detail::BoundComposition<Operation, NextOperation>;
        return ResultMorphism<Bound>{
            Bound{std::forward<Self>(self).operation,
                  std::forward<decltype(nextOperation)>(nextOperation)}};
    }

    template<class Self, class Mapper>
        requires ResultTransformable<
            Operation, detail::OperationSourceType<Mapper>>
    [[nodiscard]] constexpr auto transform(this Self&& self, Mapper&& mapper)
    {
        auto&& mapOperation = detail::unwrap_operation(
            std::forward<Mapper>(mapper));
        using MapOperation = std::decay_t<decltype(mapOperation)>;
        using Mapped = detail::MappedComposition<Operation, MapOperation>;
        return ResultMorphism<Mapped>{
            Mapped{std::forward<Self>(self).operation,
                   std::forward<decltype(mapOperation)>(mapOperation)}};
    }
};

template<ResultOperation Operation>
ResultMorphism(Operation) -> ResultMorphism<Operation>;

template<class Operation>
    requires ResultOperation<std::decay_t<Operation>>
[[nodiscard]] constexpr auto function(Operation&& operation)
{
    using Stored = std::decay_t<Operation>;
    return ResultMorphism{Stored(std::forward<Operation>(operation))};
}

template<class Operation>
    requires PureOperation<std::decay_t<Operation>>
[[nodiscard]] constexpr auto function(Operation&& operation)
{
    using Stored = std::decay_t<Operation>;
    return Function{Stored(std::forward<Operation>(operation))};
}

template<class Operation>
    requires ResultOperation<detail::OperationSourceType<Operation>>
[[nodiscard]] constexpr auto compose(Operation&& operation)
{
    auto&& source = detail::unwrap_operation(
        std::forward<Operation>(operation));
    using Stored = std::decay_t<decltype(source)>;
    return ResultMorphism{Stored(
        std::forward<decltype(source)>(source))};
}

} // namespace ano

#define ANO_LET(name, ...) \
    const auto name = ::ano::function(__VA_ARGS__)
