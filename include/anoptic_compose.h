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
