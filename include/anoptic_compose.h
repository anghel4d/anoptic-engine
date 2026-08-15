/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#pragma once

#include "anoptic_results.h"

#include <type_traits>
#include <utility>

namespace ano {

namespace detail {

consteval std::meta::info callable_declaration(std::meta::info type)
{
    type = std::meta::dealias(std::meta::remove_cvref(type));
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

template<class Operation>
consteval bool concrete_function()
{
    return callable_declaration(^^Operation) != std::meta::info{};
}

template<class Operation>
consteval bool nonthrowing_function()
{
    const std::meta::info declaration = callable_declaration(^^Operation);
    return declaration != std::meta::info{}
        && std::meta::is_noexcept(declaration);
}

template<class First, class Next>
struct BoundComposition final {
    [[no_unique_address]] First first;
    [[no_unique_address]] Next next;

    template<class Self, class Input>
    [[nodiscard]] constexpr auto operator()(this Self&& self, Input&& input)
    {
        auto outcome = std::forward<Self>(self).first(
            std::forward<Input>(input));
        static_assert(ResultInstance<decltype(outcome)>,
                      "fallible composition stages return ano::Result");
        auto result = std::move(outcome).and_then(
            std::forward<Self>(self).next);
        static_assert(ResultInstance<decltype(result)>,
                      "and_then stages return ano::Result");
        return result;
    }
};

template<class First, class Next>
struct MappedComposition final {
    [[no_unique_address]] First first;
    [[no_unique_address]] Next next;

    template<class Self, class Input>
    [[nodiscard]] constexpr auto operator()(this Self&& self, Input&& input)
    {
        auto outcome = std::forward<Self>(self).first(
            std::forward<Input>(input));
        static_assert(ResultInstance<decltype(outcome)>,
                      "fallible composition stages return ano::Result");
        auto result = std::move(outcome).transform(
            std::forward<Self>(self).next);
        static_assert(ResultInstance<decltype(result)>);
        return result;
    }
};

} // namespace detail

// A named, concrete function value. The wrapper preserves the callable's
// signature and adds no sequencing semantics of its own.
template<class Operation>
struct [[nodiscard]] Function final {
    static_assert(detail::concrete_function<Operation>(),
                  "ANO_LET requires one concrete function signature");
    static_assert(detail::nonthrowing_function<Operation>(),
                  "ANO_LET functions must be noexcept");

    [[no_unique_address]] Operation operation;

    template<class Self, class... Arguments>
    [[nodiscard]] constexpr decltype(auto) operator()(
        this Self&& self, Arguments&&... arguments) noexcept(
        noexcept(std::forward<Self>(self).operation(
            std::forward<Arguments>(arguments)...)))
    {
        return std::forward<Self>(self).operation(
            std::forward<Arguments>(arguments)...);
    }
};

template<class Operation>
[[nodiscard]] constexpr auto function(Operation&& operation)
{
    using Stored = std::decay_t<Operation>;
    return Function<Stored>{std::forward<Operation>(operation)};
}

// A reusable fallible morphism. Composition is direct and allocation-free.
template<class Operation>
struct [[nodiscard]] FallibleComposition final {
    [[no_unique_address]] Operation operation;

    template<class Self, class Input>
    [[nodiscard]] constexpr auto operator()(this Self&& self, Input&& input)
    {
        auto result = std::forward<Self>(self).operation(
            std::forward<Input>(input));
        static_assert(detail::ResultInstance<decltype(result)>,
                      "fallible composition stages return ano::Result");
        return result;
    }

    template<class Self, class Next>
    [[nodiscard]] constexpr auto and_then(this Self&& self, Next&& next)
    {
        using NextType = std::decay_t<Next>;
        using Bound = detail::BoundComposition<Operation, NextType>;
        return FallibleComposition<Bound>{
            Bound{std::forward<Self>(self).operation,
                  std::forward<Next>(next)}};
    }

    template<class Self, class Next>
    [[nodiscard]] constexpr auto transform(this Self&& self, Next&& next)
    {
        using NextType = std::decay_t<Next>;
        using Mapped = detail::MappedComposition<Operation, NextType>;
        return FallibleComposition<Mapped>{
            Mapped{std::forward<Self>(self).operation,
                   std::forward<Next>(next)}};
    }
};

template<class Operation>
[[nodiscard]] constexpr auto compose(Operation&& operation)
{
    using Stored = std::decay_t<Operation>;
    return FallibleComposition<Stored>{
        std::forward<Operation>(operation)};
}

} // namespace ano

#define ANO_LET(name, ...) \
    const auto name = ::ano::function(__VA_ARGS__)
