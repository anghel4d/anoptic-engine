# Anoptic — corrected surface

Full blocks, grouped by header.

| | meaning |
|---|---|
| ✅ | free, or enforces a rule already agreed |
| ⚠️ | works, but it is a design commitment |
| ‼️ | corrected from something that could not compile |

**Before** blocks are marked by provenance:

- **shipped** — literal from `anoptic_results.h` / `anoptic_compose.h` at HEAD, or from the `cf31d66c → b14efec9` excerpts
- **drafted** — written during design and found to be ill-formed; kept so the failure mode is visible
- no Before block — the construct is new

Two conformance rules govern everything below:

1. **P3687** removed splice-template-arguments from C++26. `[:R:]` in a template-argument list is a splice-*expression* and matches only a **constant** template parameter. Splice into an alias first.
2. **Non-transient allocation** remains barred. `std::vector` inside a `consteval` body is transient and fine; a `constexpr` variable holding one is not. Freeze with `std::define_static_array`.

Unverified against the final `<meta>` synopsis: `is_operator_function`, `is_operator_function_template`, `operator_of`, `op_parentheses`, `is_noexcept`, `extract`. `dealias` may have been renamed `underlying_entity_of`.

---

# `anoptic_results.h`

## ✅ ResultShape — carrier cut, value semantics added

An alias is not a distinct type: `ResourceResult<T>` **is** `std::expected<T, E>`. `[:shape.carrier:]` and `std::remove_cvref_t<Type>` always denote the same type. `dealias` is still load-bearing — it normalizes the reflection so `template_of` compares equal — but storing the normalized result is what is redundant. A shape describes a decomposition and should not contain its own subject.

**Before** (shipped)

```cpp
struct ResultShape final {
    std::meta::info carrier{};
    std::meta::info value{};
    std::meta::info error{};
    bool valid{};
};
```

**After**

```cpp
namespace detail {

struct ResultShape final {
    std::meta::info value{};
    std::meta::info error{};
    bool valid{};

    friend constexpr bool operator==(const ResultShape&,
                                     const ResultShape&) = default;
};

static_assert(std::regular<ResultShape>);
static_assert(std::is_trivially_copyable_v<ResultShape>);

} // namespace detail
```

## ⚠️ inspect_result — one fewer field returned; `dealias` may need renaming

**Before** (shipped)

```cpp
consteval ResultShape inspect_result(std::meta::info type)
{
    type = std::meta::dealias(std::meta::remove_cvref(type));
    if (!std::meta::has_template_arguments(type)
        || std::meta::template_of(type) != ^^std::expected)
        return {};

    const auto arguments = std::meta::template_arguments_of(type);
    if (arguments.size() != 2 || !std::meta::is_type(arguments[0])
        || !std::meta::is_type(arguments[1]))
        return {};

    return {type, arguments[0], arguments[1], true};
}

template<class Type>
consteval ResultShape inspect_result()
{
    return inspect_result(^^Type);
}

template<class Type>
inline constexpr ResultShape resultShape = inspect_result<Type>();
```

**After**

```cpp
consteval ResultShape inspect_result(std::meta::info type)
{
    type = std::meta::underlying_entity_of(std::meta::remove_cvref(type));
    if (!std::meta::has_template_arguments(type)
        || std::meta::template_of(type) != ^^std::expected)
        return {};

    const auto arguments = std::meta::template_arguments_of(type);
    if (arguments.size() != 2 || !std::meta::is_type(arguments[0])
        || !std::meta::is_type(arguments[1]))
        return {};

    return {arguments[0], arguments[1], true};
}

template<class Type>
inline constexpr ResultShape resultShape = inspect_result(^^Type);
```

The `<Type>()` overload had exactly one caller. `operationShape` already uses the `inspect_operation(^^Type)` form directly; results.h was the odd one out.

## ✅ Witnesses — normalization proven, not assumed

```cpp
static_assert(inspect_result(^^Result<int, ParseError>).valid);
static_assert(!inspect_result(^^int).valid);
static_assert(inspect_result(^^const Result<int, ParseError>&)
              == inspect_result(^^Result<int, ParseError>));
```

The third one is the reason `operator==` was added: it proves cvref-stripping and alias normalization actually converge.

## ✅ ResultCarrier — unchanged; `result_type` deleted

`result_type` was never called. The concept goes through `resultShape<Type>.valid`.

**Before** (shipped)

```cpp
consteval bool result_type(std::meta::info type)
{
    return inspect_result(type).valid;
}

template<class Type>
concept ResultCarrier = detail::resultShape<Type>.valid;
```

**After**

```cpp
template<class Type>
concept ResultCarrier = detail::resultShape<Type>.valid;
```

## ⚠️ ResultAlgebra — carrier reconstructed; the nothrow assert is a surface-wide commitment

**Before** (shipped)

```cpp
template<ResultCarrier Type>
struct ResultAlgebra final {
    static constexpr auto shape = detail::resultShape<Type>;

    using Carrier = [:shape.carrier:];
    using Value = [:shape.value:];
    using Error = [:shape.error:];
};
```

**After**

```cpp
template<ResultCarrier Type>
struct ResultAlgebra final {
    static constexpr auto shape = detail::resultShape<Type>;

    using Carrier = std::remove_cvref_t<Type>;
    using Value   = [:shape.value:];
    using Error   = [:shape.error:];

    static_assert(std::same_as<Carrier, std::expected<Value, Error>>,
                  "carrier is not the expected<Value, Error> it decomposes to");
    static_assert(std::is_nothrow_move_constructible_v<Error>,
                  "ano::Result error types must be nothrow-movable");
};
```

The second assert forbids allocating error types across the whole `Result` surface, including raw `expected` uses that never enter a chain. Narrower alternative: drop it here and keep it only in `BoundComposition`.

## ✅ failure — constructibility stated, array decay rejected

**Before** (shipped)

```cpp
template<class Error>
[[nodiscard]] constexpr auto failure(Error&& error)
    -> std::unexpected<std::remove_cvref_t<Error>>
{
    return std::unexpected<std::remove_cvref_t<Error>>(
        std::forward<Error>(error));
}
```

**After**

```cpp
template<class Error>
    requires std::constructible_from<std::remove_cvref_t<Error>, Error>
[[nodiscard]] constexpr auto failure(Error&& error)
    noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<Error>, Error>)
    -> std::unexpected<std::remove_cvref_t<Error>>
{
    static_assert(!std::is_array_v<std::remove_reference_t<Error>>,
                  "array errors decay unexpectedly; wrap it");
    return std::unexpected<std::remove_cvref_t<Error>>(
        std::forward<Error>(error));
}
```

Every `return failure(...)` inside a `noexcept` function now depends on that `noexcept` evaluating true. With enum errors it always does.

## ✅ result_if — both overloads

**Before** (shipped)

```cpp
template<class Error>
[[nodiscard]] constexpr auto result_if(bool condition, Error&& error)
    -> Result<void, std::remove_cvref_t<Error>>
{
    if (!condition)
        return failure(std::forward<Error>(error));
    return {};
}

template<class Value, class Error>
[[nodiscard]] constexpr auto result_if(
    bool condition, Value&& value, Error&& error)
    -> Result<std::remove_cvref_t<Value>, std::remove_cvref_t<Error>>
{
    if (!condition)
        return failure(std::forward<Error>(error));
    return std::forward<Value>(value);
}
```

**After**

```cpp
template<class Error>
    requires std::constructible_from<std::remove_cvref_t<Error>, Error>
[[nodiscard]] constexpr auto result_if(bool condition, Error&& error) noexcept
    -> Result<void, std::remove_cvref_t<Error>>
{
    if (!condition)
        return failure(std::forward<Error>(error));
    return {};
}

template<class Value, class Error>
    requires std::move_constructible<std::remove_cvref_t<Value>>
        && std::constructible_from<std::remove_cvref_t<Error>, Error>
[[nodiscard]] constexpr auto result_if(
    bool condition, Value&& value, Error&& error)
    noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<Value>, Value>)
    -> Result<std::remove_cvref_t<Value>, std::remove_cvref_t<Error>>
{
    if (!condition)
        return failure(std::forward<Error>(error));
    return std::forward<Value>(value);
}
```

Note that `result_if` evaluates both arms at the call site — `result_if(ok, expensive(), make_error())` computes the error on the success path. A plain `if (!cond) return failure(...);` does not. Unchanged behaviour, worth knowing.

## ✅ has_error — comparability stated, `noexcept` added, `<concepts>` earns its include

The comparison was unconstrained: `has_error` silently required `Error` to be equality-comparable with the carrier's error type, with nothing expressing it.

**Before** (shipped)

```cpp
template<ResultCarrier Self, class Error>
[[nodiscard]] constexpr bool has_error(const Self& self,
                                       const Error& error)
{
    return !self && self.error() == error;
}
```

**After**

```cpp
template<ResultCarrier Self, class Error>
    requires std::equality_comparable_with<
        typename ResultAlgebra<Self>::Error, Error>
[[nodiscard]] constexpr bool has_error(const Self& self,
                                       const Error& error) noexcept
{
    static_assert(noexcept(self.error() == error),
                  "error comparison must not throw");
    return !self && self.error() == error;
}
```

## ✅ SPDX — bare `LGPL-3.0` is deprecated

Retired in favour of the explicit forms. `reuse lint` flags it, and the bare form is genuinely ambiguous about whether later LGPL versions apply.

**Before** (shipped)

```cpp
/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
```

**After**

```cpp
/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0-only
 * Anoptic targets ISO C++26. */
```

---

# `anoptic_compose.h`

## ✅ OperationShape — value semantics plus a flag invariant

**Before** (shipped)

```cpp
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
```

**After**

```cpp
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

    constexpr bool coherent() const noexcept
    { return !nullary || supportedArity; }
};

static_assert(std::regular<OperationShape>);

template<class Type>
inline constexpr OperationShape operationShape = inspect_operation(^^Type);

static_assert(operationShape<decltype(parse)>.coherent());
static_assert(operationShape<decltype(parse)>.carrier.valid);
```

## ‼️ ResultOperation — the splice in the constraint was ill-formed

`[:...:]` as a template argument to a type parameter is deferred to C++29. The check moves to a helper where the type can be named.

**Before** (drafted)

```cpp
template<class Type>
concept ResultOperation = NonthrowingOperation<Type>
    && detail::operationShape<Type>.carrier.valid
    && std::is_nothrow_move_constructible_v<
           [:detail::operationShape<Type>.carrier.error:]>;   // ill-formed
```

**After**

```cpp
namespace detail {

template<class Type>
consteval bool nothrow_error()
{
    constexpr auto shape = operationShape<Type>;
    if constexpr (!shape.carrier.valid) {
        return false;
    } else {
        using E = [:shape.carrier.error:];
        return std::is_nothrow_move_constructible_v<E>;
    }
}

} // namespace detail

template<class Type>
concept ResultOperation = NonthrowingOperation<Type>
    && detail::operationShape<Type>.carrier.valid
    && detail::nothrow_error<Type>();
```

The `if constexpr` guard matters: without it, `shape.carrier.error` is a null `info` when the carrier is invalid and the splice is a hard error rather than a failed constraint.

## ✅ ResultOperationAlgebra — carrier reconstructed, five invariants asserted

**Before** (shipped)

```cpp
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
```

**After**

```cpp
template<ResultOperation Type>
struct ResultOperationAlgebra final {
    static constexpr auto shape = detail::operationShape<Type>;
    static constexpr bool nullary = shape.nullary;

    using Operation = std::remove_cvref_t<Type>;
    using Parameter = [:shape.parameter:];
    using Domain    = [:shape.domain:];
    using Carrier   = std::remove_cvref_t<[:shape.result:]>;
    using Value     = [:shape.carrier.value:];
    using Error     = [:shape.carrier.error:];

    static_assert(std::same_as<Carrier, std::expected<Value, Error>>);
    static_assert(std::same_as<Domain, std::remove_cvref_t<Parameter>>);
    static_assert(std::is_void_v<Value> || std::is_object_v<Value>,
                  "Value must be void or an object type");
    static_assert(std::is_nothrow_move_constructible_v<Error>);
    static_assert(nullary == std::is_void_v<Domain>);
};
```

`remove_cvref_t` on the spliced result preserves the one divergent case: a function declared to return a cv-qualified class type.

## ✅ KleisliComposable — logic unchanged, witnesses added

```cpp
template<class Left, class Right>
concept KleisliComposable = SameResultError<Left, Right>
    && ((std::same_as<typename ResultOperationAlgebra<Left>::Value, void>
         && ResultOperationAlgebra<Right>::nullary)
        || (!std::same_as<typename ResultOperationAlgebra<Left>::Value, void>
            && !ResultOperationAlgebra<Right>::nullary
            && std::same_as<typename ResultOperationAlgebra<Left>::Value,
                            typename ResultOperationAlgebra<Right>::Domain>));

static_assert(KleisliComposable<decltype(parse), decltype(increment)>);
static_assert(!KleisliComposable<decltype(parse), decltype(consume_float)>);
```

## ⚠️ BoundComposition — `noexcept` was unconditional but `and_then` is not

`ResultOperation` guarantees `first` and `next` do not throw. It says nothing about `Error`. On the failure path `std::expected::and_then` **move-constructs the error** into the returned `expected`; a throwing move escapes a function marked unconditionally `noexcept`, and that calls `std::terminate` — no unwinding, no handler, nothing to write recovery for.

Latent today, since every current `Error` is an enum. Live the day an error type carries an `anostr_t`.

**Before** (shipped)

```cpp
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
```

**After**

```cpp
template<ResultOperation First, ResultOperation Next>
    requires KleisliComposable<First, Next>
struct BoundComposition<First, Next, false> final {
    [[no_unique_address]] First first;
    [[no_unique_address]] Next next;

    using Parameter = typename ResultOperationAlgebra<First>::Parameter;
    using Error     = typename ResultOperationAlgebra<First>::Error;

    static_assert(std::is_nothrow_move_constructible_v<Error>,
                  "composed error must be nothrow-movable; "
                  "and_then moves it on the failure path");

    [[nodiscard]] constexpr auto operator()(Parameter input) const
        noexcept(std::is_nothrow_move_constructible_v<Error>)
    {
        return first(std::forward<Parameter>(input)).and_then(next);
    }
};
```

Identical treatment for `MappedComposition` and both nullary specializations.

This does **not** change error propagation. `and_then` on a failed input already short-circuits and forwards; that is correct and untouched.

## ⚠️ ResultMorphism — does not satisfy `ResultOperation`

`callable_declaration` returns `{}` when it finds `is_operator_function_template`. Deducing-this makes `operator()` a template, so `ConcreteOperation<ResultMorphism<Op>>` is false and the wrapper sits outside the category it defines. `WrappedOperation`, `OperationSource`, `OperationSourceType` and `unwrap_operation` exist only to undo that before the concepts can see through it.

Second consequence: `ANO_LET(name, some_morphism)` fails both `function` overloads, because `function` does not route through `OperationSourceType` the way `compose` does.

**Before** (shipped)

```cpp
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

    // transform: same shape
};
```

**After**

```cpp
template<ResultOperation Operation>
struct [[nodiscard]] ResultMorphism final {
    using AnopticOperation = Operation;
    using Algebra   = ResultOperationAlgebra<Operation>;
    using Carrier   = typename Algebra::Carrier;
    using Parameter = typename Algebra::Parameter;

    [[no_unique_address]] Operation operation;

    [[nodiscard]] constexpr Carrier operator()(Parameter input) const noexcept
        requires (!Algebra::nullary)
    { return operation(std::forward<Parameter>(input)); }

    [[nodiscard]] constexpr Carrier operator()() const noexcept
        requires Algebra::nullary
    { return operation(); }

    template<class Next>
        requires KleisliComposable<Operation, std::decay_t<Next>>
    [[nodiscard]] constexpr auto and_then(Next&& next) const;

    template<class Mapper>
        requires ResultTransformable<Operation, std::decay_t<Mapper>>
    [[nodiscard]] constexpr auto transform(Mapper&& mapper) const;

    template<class Mapper>
        requires ErrorMappable<Operation, std::decay_t<Mapper>>
    [[nodiscard]] constexpr auto map_error(Mapper&& mapper) const;
};

static_assert(ResultOperation<ResultMorphism<decltype(parse)>>,
              "morphism must satisfy the concept it composes over");
```

Losing the `same_as<remove_cvref_t<Input>, Domain>` guard permits implicit conversion at the call. **`BoundComposition::operator()(Parameter)` already permits it** — so today a bare morphism is strict and the same morphism after one `and_then` is not. This removes an inconsistency rather than introducing looseness.

## ✅ The unwrapping layer, deleted

The four entities exist only to work around the previous item. They go away with it.

**Before** (shipped)

```cpp
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
```

**After** — deleted. `and_then` / `transform` / `map_error` require `KleisliComposable<Operation, std::decay_t<Next>>` directly.

## ‼️ ErrorMapped — the missing arrow; the "no-op" assert was wrong

`SameResultError` means a route can never widen its error, so every boundary between error domains is written by hand. The excerpts show this four times: the `decode` early return, the mesh `resolved` check, the texture ternary, and the trailing `ResourceResult<...>(failure(result.error()))` in `cook_startup_revision`.

**Before** (shipped — the hand-written lift, `resources.c`)

```cpp
const auto pixels = ano::checked_multiply(texture.width, texture.height);
const auto bytes = pixels
    ? ano::checked_multiply(*pixels, UINT64_C(4))
    : ano::ArithmeticResult<uint64_t>(ano::failure(pixels.error()));
```

**After**

```cpp
template<class Left, class Mapper>
concept ErrorMappable = ResultOperation<Left> && PureOperation<Mapper>
    && std::invocable<const Mapper&, typename ResultOperationAlgebra<Left>::Error>
    && std::is_nothrow_move_constructible_v<
           std::invoke_result_t<const Mapper&,
                                typename ResultOperationAlgebra<Left>::Error>>;

template<ResultOperation First, PureOperation Mapper>
    requires ErrorMappable<First, Mapper>
struct ErrorMapped final {
    [[no_unique_address]] First first;
    [[no_unique_address]] Mapper mapper;

    using Parameter = typename ResultOperationAlgebra<First>::Parameter;
    using Value     = typename ResultOperationAlgebra<First>::Value;
    using Error     = std::invoke_result_t<
        const Mapper&, typename ResultOperationAlgebra<First>::Error>;
    using Carrier   = Result<Value, Error>;

    // No same_as<Error, First::Error> assert. Remapping an enum onto itself
    // with added context is a legitimate use, and the common one.

    [[nodiscard]] constexpr Carrier operator()(Parameter input) const noexcept
    {
        auto result = first(std::forward<Parameter>(input));
        if (result)
            return static_cast<Carrier>(std::move(result));
        return failure(mapper(std::move(result).error()));
    }
};
```

Call site becomes:

```cpp
const auto bytes = ano::lift<^^pixel_count>
    .and_then(scale_by_four)
    .map_error(to_resource_error)(texture);
```

## ✅ Lifted — declaration carried instead of decayed to a pointer

`std::decay_t` on a function reference yields a function pointer: eight bytes per stage that `[[no_unique_address]]` cannot collapse, and an indirect call the optimizer must prove away. `sizeof(route) == 1` is false for the `compose` form. The route object is local and non-escaping so constant propagation very likely devirtualizes — but if inlining is the performance story, this is the one place worth reading the asm rather than assuming.

**Before** (shipped — `resources.c`)

```cpp
const auto route = ano::compose(ano::decode<Input>)
    .and_then([&](const ano::ArtifactView<Input>& decoded) noexcept {
        return [:Declaration:](decoded.value, context);
    })
    .transform([&](const Output& output) noexcept {
        publish_output(target, output);
    });
```

**After**

```cpp
template<std::meta::info Declaration>
struct Lifted final {
    static_assert(std::meta::is_function(Declaration),
                  "lift requires a reflection of a function");

    static constexpr auto shape =
        detail::inspect_operation(std::meta::type_of(Declaration));

    static_assert(shape.concrete && shape.supportedArity && shape.nonthrowing);
    static_assert(shape.carrier.valid, "lifted function must return a Result");

    using Parameter = [:shape.parameter:];
    using Return    = [:shape.result:];

    [[nodiscard]] constexpr Return operator()(Parameter input) const noexcept
    { return [:Declaration:](std::forward<Parameter>(input)); }
};

template<std::meta::info Declaration>
inline constexpr auto lift = ResultMorphism{Lifted<Declaration>{}};

static_assert(std::is_empty_v<Lifted<^^parse>>);
static_assert(std::is_empty_v<ResultMorphism<Lifted<^^parse>>>);
```

`^^X` as a constant template argument is the **allowed** splice form and unaffected by P3687. `compose` stays for lambdas and non-reflectable callables; `lift` is the named-function path.

## ✅ function / compose — `ANO_LET` now accepts an existing morphism

`compose` called `unwrap_operation`; `function` did not.

**Before** (shipped)

```cpp
template<class Operation>
    requires ResultOperation<std::decay_t<Operation>>
[[nodiscard]] constexpr auto function(Operation&& operation)
{
    using Stored = std::decay_t<Operation>;
    return ResultMorphism{Stored(std::forward<Operation>(operation))};
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
```

**After**

```cpp
template<class Operation>
    requires ResultOperation<std::decay_t<Operation>>
[[nodiscard]] constexpr auto function(Operation&& operation)
{
    using Stored = std::decay_t<Operation>;
    static_assert(std::move_constructible<Stored>);
    return ResultMorphism{Stored(std::forward<Operation>(operation))};
}

template<class Operation>
    requires ResultOperation<std::decay_t<Operation>>
[[nodiscard]] constexpr auto compose(Operation&& operation)
{
    using Stored = std::decay_t<Operation>;
    static_assert(std::move_constructible<Stored>);
    return ResultMorphism{Stored(std::forward<Operation>(operation))};
}

#define ANO_LET(name, ...) \
    const auto name = ::ano::function(__VA_ARGS__)
```

Both are now the same function because `ResultMorphism` satisfies `ResultOperation`; keep both names for intent.

## ✅ KleisliPairable / PairedComposition — new

Left-error precedence is the early return, not a comment. Taking `const Domain&` rather than two independent parameters puts the noninterference precondition in the signature: both compilers read one shared witness. The short-circuit is why this is fail-fast Kleisli and not a categorical product.

```cpp
template<class Left, class Right>
concept KleisliPairable = SameResultError<Left, Right>
    && ResultOperationAlgebra<Left>::nullary
        == ResultOperationAlgebra<Right>::nullary
    && (ResultOperationAlgebra<Left>::nullary
        || (std::same_as<typename ResultOperationAlgebra<Left>::Domain,
                         typename ResultOperationAlgebra<Right>::Domain>
            && std::is_object_v<typename ResultOperationAlgebra<Left>::Domain>));

template<ResultOperation Left, ResultOperation Right>
    requires KleisliPairable<Left, Right>
struct PairedComposition final {
    [[no_unique_address]] Left left;
    [[no_unique_address]] Right right;

    using Domain     = typename ResultOperationAlgebra<Left>::Domain;
    using LeftValue  = typename ResultOperationAlgebra<Left>::Value;
    using RightValue = typename ResultOperationAlgebra<Right>::Value;
    using Value      = std::pair<LeftValue, RightValue>;
    using Error      = typename ResultOperationAlgebra<Left>::Error;

    static_assert(std::move_constructible<LeftValue>);
    static_assert(std::move_constructible<RightValue>);
    static_assert(std::is_nothrow_move_constructible_v<Error>);

    [[nodiscard]] constexpr Result<Value, Error> operator()(
        const Domain& witness) const
        noexcept(std::is_nothrow_move_constructible_v<Error>)
    {
        auto first = left(witness);
        if (!first)
            return failure(std::move(first).error());
        auto second = right(witness);
        if (!second)
            return failure(std::move(second).error());
        return Value{*std::move(first), *std::move(second)};
    }
};

static_assert(PairwiseCompatible<decltype(check_format),
                                 decltype(check_extent)>);
```

`all(...)` is the variadic form. Evaluation order is fixed left-to-right and that fact belongs in the header comment, because it is the same fact as Lean's `pair_both_error_selects_left`.

## ✅ FoldStep / KleisliFoldStep / scan_into — new

Fold is not a Kleisli arrow; it is `(State, Item) → State` over a sequence. Keeping it a separate shape avoids a category error later.

```cpp
template<class Step, class State, class Item>
concept FoldStep = PureOperation<Step>
    && std::movable<State>
    && std::invocable<const Step&, State, Item>
    && std::same_as<std::invoke_result_t<const Step&, State, Item>, State>;

template<class Step, class State, class Item, class Error>
concept KleisliFoldStep = ResultOperation<Step>
    && std::movable<State>
    && std::same_as<typename ResultOperationAlgebra<Step>::Value, State>
    && std::same_as<typename ResultOperationAlgebra<Step>::Error, Error>;

template<class Self, class State, class Step>
    requires FoldStep<Step, State, RangeValue<typename Self::Algebra::Value>>
        && std::is_trivially_copyable_v<State>
[[nodiscard]] constexpr auto scan_into(
    this Self&& self, State init, Step step, std::span<State> out) noexcept
    -> Result<std::span<State>, typename Self::Algebra::Error>;
```

---

# `anoptic_meta.h`

## ‼️ MaskableError — `enumerators_of` returns a container

It cannot appear directly in a constraint-expression. Wrap it in a `consteval` helper that consumes the container and returns a `bool`.

**Before** (drafted)

```cpp
template<class E>
concept MaskableError = std::is_enum_v<E>
    && std::meta::enumerators_of(^^E).size() <= 64      // non-transient alloc
    && contiguous_from_zero(^^E);
```

**After**

```cpp
namespace detail {

consteval bool maskable_enum(std::meta::info type)
{
    if (!std::meta::is_enum_type(type))
        return false;
    const auto enumerators = std::meta::enumerators_of(type);   // transient
    if (enumerators.size() > 64)
        return false;
    uint64_t seen = 0;
    for (const auto e : enumerators) {
        const auto value = std::meta::extract<uint64_t>(e);
        if (value >= 64)
            return false;
        seen |= uint64_t{1} << value;
    }
    return seen == ((uint64_t{1} << enumerators.size()) - 1);
}

} // namespace detail

template<class E>
concept MaskableError = std::is_enum_v<E>
    && std::is_unsigned_v<std::underlying_type_t<E>>
    && detail::maskable_enum(^^E);

template<MaskableError E>
struct ErrorMask final {
    uint64_t bits{};
    friend constexpr bool operator==(ErrorMask, ErrorMask) = default;
};

static_assert(MaskableError<ResourceError>);
static_assert(!MaskableError<int>);
```

Reflection verifies the precondition that makes the branchless accumulating fold sound. The optimization is not a trick to remember; it is a concept that fires when it is legal.

## ✅ checked_add — out-param and nullptr check gone

**Before** (shipped — `include/anoptic_meta.h`)

```cpp
template<class Left, class Right, class Result>
    requires (std::is_unsigned_v<Left> && std::is_unsigned_v<Right>
              && std::is_unsigned_v<Result>)
[[nodiscard]] constexpr bool checked_add(
    Left lhs, Right rhs, Result *result) noexcept
{
    if (result == nullptr)
        return false;
    Result value{};
    if (__builtin_add_overflow(lhs, rhs, &value))
        return false;
    *result = value;
    return true;
}
```

**After**

```cpp
template<class Left, class Right>
    requires (std::is_unsigned_v<Left> && std::is_unsigned_v<Right>)
[[nodiscard]] constexpr auto checked_add(Left lhs, Right rhs) noexcept
    -> ArithmeticResult<std::common_type_t<Left, Right>>
{
    using Value = std::common_type_t<Left, Right>;
    static_assert(std::is_unsigned_v<Value>);
    Value value{};
    if (__builtin_add_overflow(lhs, rhs, &value))
        return failure(ArithmeticError::overflow);
    return value;
}

static_assert(MaskableError<ArithmeticError>);
static_assert(!checked_add(UINT64_MAX, UINT64_C(1)));
```

The out-param is why this is a codegen win, not only an ergonomic one: `&out` forces the compiler to assume aliasing and materialize the store. Two registers back, no memory touched.

## ✅ checked_align — `checked_add` then a pure transform

New in the migration; no prior single-function form.

```cpp
[[nodiscard]] constexpr ArithmeticResult<size_t> checked_align(
    size_t value, size_t alignment) noexcept
{
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        return failure(ArithmeticError::invalid_alignment);
    return checked_add(value, alignment - 1).transform(
        [=](size_t padded) noexcept { return padded & ~(alignment - 1); });
}

static_assert(*checked_align(13, 8) == 16);
```

---

# `anoptic_memory.h`

## ✅ MemoryLayout::reserve — the sticky `valid` flag is gone

This is the change most likely to be the measured codegen win. `bool valid` was read and conditionally written on every `reserve`, so no two reservations could be reordered or held in registers across each other — a chain of dependent memory ops where the arithmetic is independent. Without it the cursor is `{size, alignment}` and can stay in registers across a run of reservations.

**Before** (shipped — `include/anoptic_memory.h`)

```cpp
bool valid = true;

[[nodiscard]] constexpr bool reserve(
    size_t bytes, size_t requestedAlignment,
    MemoryReservation& reservation) noexcept
{
    reservation = {};
    if (!valid || requestedAlignment == 0
        || (requestedAlignment & (requestedAlignment - 1)) != 0) {
        valid = false;
        return false;
    }
    if (bytes == 0) {
        reservation.offset = size;
        return true;
    }
    size_t offset = 0;
    size_t end = 0;
    if (!ano_size_align(size, requestedAlignment, &offset)
        || !ano_size_add(offset, bytes, &end)) {
        valid = false;
        return false;
    }
    reservation = {offset, bytes};
    size = end;
    return true;
}
```

**After**

```cpp
[[nodiscard]] constexpr ArithmeticResult<MemoryReservation> reserve(
    size_t bytes, size_t requestedAlignment) noexcept
{
    if (requestedAlignment == 0
        || (requestedAlignment & (requestedAlignment - 1)) != 0)
        return failure(ArithmeticError::invalid_alignment);
    if (bytes == 0)
        return MemoryReservation{size, 0};

    const auto offset = checked_align(size, requestedAlignment);
    const auto end = offset.and_then(
        [&](size_t aligned) noexcept { return checked_add(aligned, bytes); });
    if (!end)
        return failure(end.error());

    const MemoryReservation reservation{*offset, bytes};
    size = *end;
    if (requestedAlignment > alignment)
        alignment = requestedAlignment;
    return reservation;
}

static_assert(std::is_trivially_copyable_v<MemoryReservation>);
```

Being `constexpr` and free of sticky state is also what lets the same function lay out ECS chunks in the frontend.

---

# `anoptic_bits.h`

New header. Erlang's bit syntax, with the pattern derived from the struct rather than written.

## ✅ Shapes hold spans — `define_static_array` freezes the scratch

```cpp
struct FieldShape final {
    std::meta::info member{};
    std::meta::info type{};
    size_t width{};
    size_t offset{};
    std::meta::info count{};
    Endian endian{Endian::little};
    bool variable{};
    bool valid{};

    friend constexpr bool operator==(const FieldShape&,
                                     const FieldShape&) = default;
};

struct BitShape final {
    std::meta::info record{};
    std::span<const FieldShape> fields{};     // NOT std::vector
    size_t fixedWidth{};
    bool wholeBytes{};
    bool variable{};
    bool valid{};
};

consteval BitShape inspect_bits(std::meta::info type)
{
    type = std::meta::underlying_entity_of(std::meta::remove_cvref(type));
    if (!std::meta::is_class_type(type))
        return {};

    std::vector<FieldShape> scratch;                    // transient
    size_t cursor = 0;
    bool variable = false;
    for (const auto member : std::meta::nonstatic_data_members_of(
             type, std::meta::access_context::unchecked())) {
        const FieldShape field = field_from_annotations(member, cursor);
        if (!field.valid)
            return {};
        variable |= field.variable;
        cursor += field.variable ? 0 : field.width;
        scratch.push_back(field);
    }
    return {type, std::define_static_array(scratch), cursor,
            (cursor % 8) == 0, variable, true};
}

template<class Type>
inline constexpr BitShape bitShape = inspect_bits(^^Type);
```

A `std::vector` member here would be ill-formed in the `inline constexpr` variable template. Inside the `consteval` body it is transient and fine.

## ✅ The declaration stays an ordinary struct

```cpp
struct PackHeader final {
    [[=bits(32)]]               uint32_t magic;
    [[=bits(8)]]                uint8_t  version;
    [[=bits(8)]]                uint8_t  flags;
    [[=bits(16), =big]]         uint16_t entryCount;
    [[=counted_by(entryCount)]] std::span<const Entry> entries;
};

template<class Type>
concept BitRecord = detail::bitShape<Type>.valid
    && std::is_standard_layout_v<Type>
    && std::equality_comparable<Type>;

template<class Type>
concept FixedBitRecord = BitRecord<Type>
    && !detail::bitShape<Type>.variable;

static_assert(FixedBitRecord<PackHeader>);
static_assert(detail::bitShape<PackHeader>.wholeBytes,
              "fixed record must end on a byte boundary");
static_assert(detail::bitShape<PackHeader>.fixedWidth == 64);
```

`equality_comparable` is in the concept because the round-trip proof needs `==`.

## ‼️ parse — the field type must be named before use

**Before** (drafted)

```cpp
const auto read = read_field<[:Field.type:], Field>(bytes, cursor);   // ill-formed
```

**After**

```cpp
template<BitRecord Type>
[[nodiscard]] constexpr auto parse(ByteView bytes) noexcept -> BitResult<Type>
{
    constexpr auto shape = detail::bitShape<Type>;
    if (bytes.size() * 8 < shape.fixedWidth)
        return failure(BitError::truncated);

    Type record{};
    size_t cursor = 0;
    template for (constexpr auto Field : shape.fields) {
        using F = [:Field.type:];
        const auto read = read_field<F, Field>(bytes, cursor);
        if (!read)
            return failure(read.error());
        record.[:Field.member:] = *read;
        cursor += width_of<Field>(record);
    }
    return record;
}
```

Splicing into `record.[:Field.member:]` is a member-access context and remains legal. Only the template-argument position needed the alias.

## ✅ round_trips — the proof, on a literal witness

```cpp
template<FixedBitRecord Type>
consteval bool round_trips(const Type& witness)
{
    std::array<std::byte, encoded_size<Type>()> buffer{};
    return emit(witness, buffer) && parse<Type>(buffer)
        .transform([&](const Type& back) { return back == witness; })
        .value_or(false);
}

static_assert(round_trips(canonicalPackHeader));
static_assert(encoded_size<PackHeader>() * 8
              == detail::bitShape<PackHeader>.fixedWidth);
```

The existing Texture codec already executes one witness at compile time. This makes it the default for every wire format, derived rather than hand-written. Variable-length payloads need a fixed-size witness or a bounded buffer.

---

# `anoptic_ecs.h`

New header.

## ⚠️ Component — `!is_empty_v` rejects tag components

```cpp
inline constexpr size_t chunkAlignment = 64;

template<class C>
concept Component = std::is_trivially_copyable_v<C>
    && std::is_standard_layout_v<C>
    && std::is_default_constructible_v<C>
    && (alignof(C) <= chunkAlignment)
    && !std::is_empty_v<C>;

static_assert(Component<Position>);
static_assert(Component<Velocity>);
```

Most ECS designs want `struct Player {};` as a zero-size marker in the archetype mask. Deliberate if tags route through a separate mechanism; a bug if they do not. `chunkAlignment` is one named constant rather than two literals that can drift.

## ‼️ layout_of — `capacity` is not a constant expression inside the body

A `consteval` function's parameters are not constant expressions within it. Bounds checks on `capacity` must be a plain `if` returning an invalid layout, or `capacity` becomes a template parameter.

**Before** (drafted)

```cpp
consteval ChunkLayout layout_of(size_t capacity)
{
    static_assert(capacity > 0);          // not a constant expression here
    // ...
}
```

**After**

```cpp
template<Component... Cs>
consteval ChunkLayout layout_of(size_t capacity)
{
    static_assert(sizeof...(Cs) > 0);
    static_assert(distinct_types<Cs...>(), "duplicate component in archetype");

    MemoryLayout cursor{};
    std::array<size_t, sizeof...(Cs)> offsets{};
    size_t i = 0;
    (..., (offsets[i++] = *cursor.reserve(sizeof(Cs) * capacity,
                                          chunkAlignment)));
    return {offsets, cursor.size};
}

inline constexpr auto combatLayout =
    layout_of<Transform, Velocity, Health, Race, TwoHanded, Gold>(4096);

static_assert(combatLayout.total <= 16384, "chunk overflows the target size");
static_assert(all_of(combatLayout.offsets,
                     [](size_t o) { return o % chunkAlignment == 0; }));
```

This is `MemoryLayout::reserve` run in the frontend. Lane offsets become compile-time immediates, 64-byte alignment is guaranteed by construction, and chunk overflow is a build error naming the archetype.

## ✅ AccessSet — the parameter list is the declaration

A system can only touch what it takes as a parameter, so the signature **is** the read/write set. No body introspection, no annotation to drift. Bevy computes this at startup from `Query<&mut Position, &Velocity>` type parameters; this computes it at compile time from an ordinary C++ signature.

```cpp
struct AccessSet final {
    ComponentMask reads{};
    ComponentMask writes{};

    friend constexpr bool operator==(AccessSet, AccessSet) = default;

    constexpr bool disjoint() const noexcept { return !(reads & writes); }
};

consteval AccessSet access_of(std::meta::info fn)
{
    AccessSet acc{};
    for (const auto p : std::meta::parameters_of(fn)) {
        const auto t = std::meta::type_of(p);
        const auto c = std::meta::remove_cvref(t);
        if (std::meta::is_lvalue_reference_type(t)
            && !std::meta::is_const_type(std::meta::remove_reference(t)))
            acc.writes |= bit_of(c);
        else
            acc.reads |= bit_of(c);
    }
    return acc;
}

consteval bool conflicts(AccessSet a, AccessSet b)
{ return (a.writes & (b.reads | b.writes)) | (b.writes & a.reads); }

static_assert(access_of(^^integrate)
    == AccessSet{.reads = mask<Velocity>, .writes = mask<Transform>});
static_assert(conflicts(access_of(^^integrate), access_of(^^integrate)));
static_assert(!conflicts(access_of(^^integrate), access_of(^^cull)));
static_assert(conflicts(access_of(^^integrate), access_of(^^physics))
              == conflicts(access_of(^^physics), access_of(^^integrate)));
```

The symmetry assert needs real witnesses — free variables `a` and `b` at namespace scope do not compile.

The soundness rule is one line: **systems take components, never the world.** That is checkable by rejecting pointer parameters.

## ✅ build_waves — an ordinary graph algorithm, frozen on return

`define_static_array` is what makes this a normal algorithm rather than a template metaprogram. Greedy coloring with `std::sort` and `std::vector`, running in the frontend, shipping a span.

```cpp
consteval auto build_waves(std::span<const std::meta::info> systems)
{
    std::vector<AccessSet> access;                  // transient
    std::vector<Wave> waves;                        // transient
    // greedy coloring over the conflict graph
    return std::define_static_array(waves);
}

inline constexpr auto frameWaves = build_waves(
    std::define_static_array(std::vector{^^integrate, ^^cull, ^^animate}));

consteval bool waves_are_sound(std::span<const Wave> waves)
{
    for (const Wave& w : waves)
        for (size_t i = 0; i < w.systems.size(); ++i)
            for (size_t j = i + 1; j < w.systems.size(); ++j)
                if (conflicts(w.systems[i].access, w.systems[j].access))
                    return false;
    return true;
}

static_assert(waves_are_sound(frameWaves));
static_assert(frameWaves.size() >= 1);
```

`parallel_for` over a wave needs no lock and no runtime access check, because the disjointness was proven here.

## ✅ DeterministicSystem — signature-level closure

```cpp
consteval bool closed_over_components(std::meta::info fn)
{
    for (const auto p : std::define_static_array(std::meta::parameters_of(fn))) {
        const auto t = std::meta::type_of(p);
        if (std::meta::is_pointer_type(t)) return false;    // no reach-through
        const auto c = std::meta::remove_cvref(std::meta::remove_reference(t));
        if (!registered_component(c) && !plain_data(c)) return false;
    }
    return true;
}

template<class Type>
concept DeterministicSystem = NonthrowingOperation<Type>
    && detail::systemShape<Type>.closed
    && detail::systemShape<Type>.access.disjoint();

static_assert(DeterministicSystem<decltype(integrate)>);
```

This stops parameter-based reach-through, not a system calling `rand()` in its body. Reflection cannot read bodies. Closing that fully needs a sanctioned-call whitelist or a linter — the one place in the design where the guarantee is a convention rather than a type.

## ‼️ Lane iteration — the splice must be named

**Before** (drafted)

```cpp
for (const auto& cell : chunk.lane<[:Lane:]>())          // ill-formed
```

**After**

```cpp
template for (constexpr auto Lane :
              std::define_static_array(chunk.archetype.lanes)) {
    using L = [:Lane:];
    for (const auto& cell : chunk.template lane<L>()) { /* ... */ }
}
```

The kernel this produces is a straight loop over two contiguous arrays at constant offsets. Non-aliasing is not a `restrict` promise; the offsets are consteval constants from `layout_of`, so the compiler can see the lanes are disjoint.

---

# `anoptic_render_types.h`

New header. The strongest single result in the design: a missing barrier becomes a compile error at zero runtime cost.

## ✅ Image<Layout> — phantom index, erasure asserted

```cpp
enum class ImageLayout : uint8_t {
    undefined, transfer_dst, transfer_src, shader_ro, color_attachment,
    depth_attachment, present_src,
};

template<ImageLayout Layout>
struct Image final {
    VkImage handle{};
    static constexpr ImageLayout layout = Layout;
};

static_assert(sizeof(Image<ImageLayout::shader_ro>) == sizeof(VkImage),
              "layout index must be free");
static_assert(std::is_trivially_copyable_v<Image<ImageLayout::shader_ro>>);
static_assert(std::is_standard_layout_v<Image<ImageLayout::shader_ro>>);
```

**Before** — the C++17 shape, for contrast. `oldLayout` is an assertion the programmer makes and nothing checks:

```cpp
vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
    &(VkImageMemoryBarrier){
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ... });
```

Get it wrong and you get a validation error, or worse, correct behaviour on one vendor and corruption on another.

## ⚠️ LegalTransition — `From != To` rejects a same-layout barrier

```cpp
template<ImageLayout From, ImageLayout To>
concept LegalTransition = (From != To)
    && legal_transition_table()[ordinal_of(From) * layoutCount
                                + ordinal_of(To)];

template<ImageLayout From, ImageLayout To>
    requires LegalTransition<From, To>
[[nodiscard]] constexpr auto transition(Image<From> image) noexcept
    -> RenderResult<Image<To>>;

static_assert(LegalTransition<ImageLayout::undefined,
                              ImageLayout::transfer_dst>);
static_assert(!LegalTransition<ImageLayout::undefined,
                               ImageLayout::present_src>);
```

`shader_ro → shader_ro` purely as a read-after-write barrier is a real Vulkan pattern. If you need it, drop `From != To` and add an explicit `barrier<L>` overload instead.

The route then reads:

```cpp
constexpr auto upload = ano::lift<^^stage_texture>
    .and_then(transition<ImageLayout::undefined, ImageLayout::transfer_dst>)
    .and_then(copy_from_staging)       // requires transfer_dst
    .and_then(transition<ImageLayout::transfer_dst, ImageLayout::shader_ro>)
    .and_then(bind_descriptor);        // requires shader_ro
```

Feeding `copy_from_staging` an `Image<undefined>` fails `KleisliComposable` by name.

---

# `anoptic_commands.h`

New header. The tick-boundary command buffer is already an event system; input, gameplay, and network are three origins on one stream.

## ✅ Stamp — total order asserted, not assumed

```cpp
struct Tick final {
    uint64_t value{};
    friend constexpr auto operator<=>(Tick, Tick) = default;
};

enum class Origin : uint8_t { input, simulation, network, replay };

struct Stamp final {
    Tick tick{};
    Origin origin{};
    uint32_t sequence{};
    friend constexpr auto operator<=>(const Stamp&, const Stamp&) = default;
};

static_assert(std::totally_ordered<Stamp>);
static_assert(std::is_trivially_copyable_v<Stamp>);
static_assert(Stamp{Tick{1}, Origin::input, 0}
              < Stamp{Tick{1}, Origin::simulation, 0});
```

Without a total order the apply sort is not stable and replay diverges — the kind of thing otherwise found at hour six of a desync hunt.

Because everything is deferred to the tick boundary, `add(Burning)` and `Gold += 1000` are the same kind of thing. No two-tier effect system, no `,` that means different things depending on the verb.

Two consequences to decide deliberately: write-write conflicts need a rule (source order, with `+=` classified as commutative from the AST), and the tick-start snapshot is the semantics — worth a named invariant rather than behaviour people infer.

---

# `anoptic_input.h`

New header.

## ✅ SampleMonoid — the second factor is a monoid, not specifically "latest"

`FIFO ⊗ Latest` was almost right. Parameterizing on the monoid fixes a bug that would otherwise ship: a gamepad stick is `LastWrite`, but a **mouse delta is `Additive`**. Take the latest mouse delta instead of the sum and you silently drop rotation whenever two motion events land in one tick — the classic frame-rate-dependent sensitivity bug.

```cpp
template<class Combine, class Value>
concept SampleMonoid = requires (Value a, Value b) {
    { Combine::combine(a, b) } noexcept -> std::same_as<Value>;
    { Combine::identity() }    noexcept -> std::same_as<Value>;
};

template<class Value, class Combine>
    requires SampleMonoid<Combine, Value>
struct Sampled final {
    Value accumulated{Combine::identity()};

    constexpr void observe(Value next) noexcept
    { accumulated = Combine::combine(accumulated, next); }
};

template<class Edge, class Level, class Motion>
struct InputTensor final {
    SpscRing<Edge> edges;                          // FIFO: a dropped jump is a bug
    Sampled<Level,  LastWrite<Level>>  level;      // stick
    Sampled<Motion, Additive<Motion>>  motion;     // mouse delta
};

static_assert(SampleMonoid<Additive<Axis2>, Axis2>);
static_assert(Additive<int>::combine(Additive<int>::identity(), 5) == 5);
static_assert(Additive<int>::combine(1, Additive<int>::combine(2, 3))
              == Additive<int>::combine(Additive<int>::combine(1, 2), 3));
```

`threads_typed.h` already had the FIFO/Latest distinction before there was a use case for it.

---

# `anoptic_match.h`

New header. Erlang pattern matching with non-linear binders, plus archetype presence matching, which Erlang cannot express.

## ‼️ Guard_ — a field that does not exist, and a splice in a template argument

Two problems in one line. `GuardShape` has no `result` member, and the splice is in a template-argument position. The return-type check already belongs inside `inspect_guard`.

**Before** (drafted)

```cpp
template<class Type>
concept Guard_ = detail::guardShape<Type>.valid
    && detail::guardShape<Type>.pure
    && std::same_as<[:detail::guardShape<Type>.result:], bool>;   // both problems
```

**After**

```cpp
consteval GuardShape inspect_guard(std::meta::info type)
{
    const auto declaration = callable_declaration(type);
    if (declaration == std::meta::info{})
        return {};
    if (!std::meta::is_noexcept(declaration))
        return {};
    if (std::meta::remove_cvref(
            std::meta::return_type_of(declaration)) != ^^bool)
        return {};
    const auto access = access_of(declaration);
    return {declaration, access, access.writes == ComponentMask{}, true};
}

template<class Type>
inline constexpr GuardShape guardShape = inspect_guard(^^Type);

template<class Type>
concept Guard_ = detail::guardShape<Type>.valid
    && detail::guardShape<Type>.pure;

static_assert(Guard_<decltype(lethal)>);
static_assert(!Guard_<decltype(mutating_guard)>);
```

Purity is derived from parameter constness — a guard taking `Health&` has a non-empty write set and fails by name. Erlang enforces this with a hardcoded BIF whitelist because its runtime cannot trust user code; this is open to any pure function.

## ✅ PatternShape — span, not vector

```cpp
enum class BindKind : uint8_t { bind, repeat, pinned, literal };

struct BindingShape final {
    std::string_view name{};
    std::meta::info member{};
    std::meta::info type{};
    size_t firstUse{};
    BindKind kind{BindKind::bind};
};

struct PatternShape final {
    std::meta::info alternative{};
    std::span<const BindingShape> bindings{};      // NOT std::vector
    bool valid{};
};

consteval PatternShape inspect_pattern(
    std::meta::info alternative, std::span<const std::string_view> specs)
{
    std::vector<BindingShape> scratch;              // transient
    // classify bind / repeat / pinned / literal; resolve member_named
    return {alternative, std::define_static_array(scratch), true};
}
```

Binding by reflected member name means a renamed field is a compile error at the match site, not a silently-wrong positional bind.

## ✅ pattern_holds — comparability asserted where non-linear bindings need it

```cpp
template<class Alt, PatternShape Shape>
[[nodiscard]] constexpr bool pattern_holds(
    const Alt& value, const auto& pins) noexcept
{
    bool holds = true;
    template for (constexpr auto B : Shape.bindings) {
        using T = [:B.type:];
        if constexpr (B.kind == BindKind::repeat) {
            static_assert(std::equality_comparable<T>,
                          "repeated binding needs operator==");
            static_assert(noexcept(std::declval<const T&>()
                                   == std::declval<const T&>()));
            holds = holds && value.[:B.member:]
                == value.[:Shape.bindings[B.firstUse].member:];
        } else if constexpr (B.kind == BindKind::pinned) {
            static_assert(std::equality_comparable<T>,
                          "pinned binding needs operator==");
            holds = holds && value.[:B.member:] == get<B.firstUse>(pins);
        } else if constexpr (B.kind == BindKind::literal) {
            holds = holds && value.[:B.member:] == literal_of<B>();
        }
    }
    return holds;
}
```

`ANO_CASE(Collision, a, a)` is self-collision as a pattern rather than an `if` someone forgot. A pure-`bind` pattern compiles to nothing, because `if constexpr` erases the untaken arms.

## ⚠️ ExhaustiveMatch — stricter than Erlang

```cpp
template<class Sum, class... Clauses>
concept ExhaustiveMatch = (Clause_<Clauses> && ...)
    && (sizeof...(Clauses) > 0)
    && detail::covers_all_alternatives<Sum, Clauses...>();

static_assert(ExhaustiveMatch<Command, DamageClause, HealClause, DespawnClause>);
```

Guards cannot be proven total, so a guard-only alternative is rejected at compile time rather than becoming a runtime `CaseClauseError`. Correct, and occasionally annoying: a fallback is required even when guards provably cover the space.

---

# `anoptic_determinism.h`

New header.

## ⚠️ replay — sound for integer and fixed-point simulation only

```cpp
consteval uint64_t replay(std::span<const InputFrame> frames)
{
    World world = fixture_world();
    for (const InputFrame& frame : frames)
        world = step(std::move(world), frame);
    return world_hash(world);
}

static_assert(DeterministicSystem<decltype(step)>);
static_assert(replay(recordedCombatFrames) == 0x9f2c4a1de8730b56);
```

Transcendentals are not `constexpr`, and constant-evaluated float need not match runtime float bit-for-bit. The assert therefore proves determinism for the integer and fixed-point subset — which is exactly what rollback netcode wants anyway. The technique is not so much limited by this as revealing the design pressure early.

If a system reads a global, `DeterministicSystem` fails and names it. If behaviour changes, the hash moves and the build breaks with a recorded input stream as the witness.

---

# Erasure postconditions

Wherever the route is defined. This is the paper's actual claim — stronger than a framerate, because nobody argues with a `static_assert`.

```cpp
static_assert(std::is_empty_v<decltype(route)>);
static_assert(std::is_empty_v<decltype(bounty)>);
static_assert(std::is_empty_v<decltype(spawn_enemy)>);
static_assert(std::is_trivially_copyable_v<decltype(route)>);
static_assert(stage_count(^^decltype(route)) == 2);
static_assert(sizeof(frameWaves) == sizeof(WaveTable));
```

`is_empty_v` is the stronger statement. `sizeof == 1` is what an empty class happens to measure; `is_empty_v` is the property you mean.

---

# `tests/compile_fail/`

CMake greps for the concept name. Two of these exist at `b14efec9`; the rest follow the same shape.

```
results_mismatched_composition.c   → "KleisliComposable"
results_throwing_operation.c       → "NonthrowingOperation"
results_throwing_error_move.c      → "nothrow-movable"
match_nonexhaustive.c              → "ExhaustiveMatch"
match_impure_guard.c               → "Guard_"
bits_unaligned_record.c            → "BitRecord"
ecs_conflicting_wave.c             → "waves_are_sound"
ecs_duplicate_component.c          → "duplicate component in archetype"
vulkan_missing_barrier.c           → "LegalTransition"
```

The reason every mismatch is designed to fail at a *named concept* rather than inside a library template: the failure has to be greppable. That is also, incidentally, what makes the diagnostics readable — and the P3687 splice restriction helps here, since naming every intermediate via `using X = [:r:];` means errors print `X` rather than an anonymous instantiation.

---

# Build order

1. `anoptic_render_types.h` — `Image<Layout>` and `LegalTransition`. Smallest, and a missing barrier becoming a compile error is the result Vulkan people feel immediately.
2. `anoptic_ecs.h` — `layout_of` and `access_of`. The codegen delta is undeniable and easy to graph.

Everything else follows from those two working.

Before either: check `is_operator_function`, `operator_of`, `op_parentheses`, `is_noexcept`, `extract`, and whether `dealias` still exists under that name. Operator reflection is the kind of surface that gets trimmed late, and if it was, `callable_declaration` needs a different strategy for class types — most likely `members_of` filtered by `identifier_of` against `operator()`.

