---
name: cpp-reflection-status-2026-08-02
description: Manual capability reference for adopted C++26 reflection on Anoptic's pinned GCC 17 toolchain. Invoke only when the user explicitly names $cpp-reflection-status-2026-08-02 or explicitly asks to load this status skill; do not infer invocation from mentions of reflection, std::meta, constexpr, consteval, annotations, generation, schemas, templates, registries, compiler support, or ordinary reflection implementation and review.
---

# C++26 Reflection Status - GCC 17

Use this skill as a dated capability snapshot and as a design posture. Treat adopted, implemented reflection as a serious compile-time programming substrate, not merely a serialization convenience.

## Programming model

Use ordinary C++ constant evaluation as the metaprogramming language:

```text
reflected declarations and typed annotations
    -> std::meta::info values
    -> consteval compiler or library boundary
    -> ordinary constexpr algorithms
    -> compile-time validation and transformation
    -> expansion, splicing, and static or aggregate generation
    -> direct ordinary C++
```

- `constexpr` functions own reusable computation. Use normal loops, locals, containers, ranges, graph algorithms, and value transformations instead of encoding computation in template instantiation.
- `consteval` functions and blocks require translation-time execution and host reflection-facing library interfaces. They invoke ordinary algorithms; they do not create a separate metaprogramming language.
- Reflection supplies program structure as values to those algorithms. It is not an orchestration veneer around template-owned kernels.
- Expansion statements and splicing reify computed results as direct typed code. `template for` is an expansion statement, not recursive template metaprogramming.
- Templates have no architectural ownership. Use them only when an interface genuinely needs type or value parameterization. A templated function body remains an ordinary algorithm.
- Runtime-only effects do not create a second source of structural truth. Reflect and compile the signatures, eligibility, selection, binding, and adaptation of concrete I/O, allocation, and device calls.

This follows the WG21 design direction recorded in [P3466R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3466r1.pdf): prefer direct `constexpr` computation over equivalent template metaprograms and prefer `consteval` libraries when they match a baked-in feature's usability, expressiveness, and performance. [P3437R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3437r1.pdf) treats reflection and generation as compile-time automation of the C++ a programmer would otherwise read and write manually. [P2996R13](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2996r13.html) demonstrates ordinary algorithms over reflection values and explicitly improves on hand-rolled template metaprogramming. Herb Sutter's [P0707R5](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p0707r5.pdf) layers generative abstractions on reflection and generation as `consteval` library functions.

## Default posture

- Prefer ordinary `constexpr` algorithms called through `consteval` reflection and generation boundaries before recursive template metaprogramming, typelists, X-macros, duplicated registries, or external generators.
- Be ambitious with the real facility. Use reflection to discover, validate, compute with, transform, and reify program structure; generate static tables and specialized code; and make inconsistent declarations fail compilation.
- Look especially for closed structural domains and foreign schemas: many declarations that are massively duplicated but only slightly different across glTF, Vulkan, shader ABIs, platform multimedia APIs, protocols, and file formats.
- Express domain semantics once as declarations, typed annotations, or `constexpr` data, then compile parsing, validation, mapping, routing, and direct invocation from them. Domain-specific semantics are not an excuse for a parallel handwritten structural implementation.
- Do not add runtime metadata traversal merely because reflection exists. Compile structural knowledge away unless a generated runtime table is genuinely needed.
- Do not claim unavailable facilities. Optimism means fully exploiting what is real, not inventing future syntax.

## Verified baseline

Assume this surface only after confirming the build selects Anoptic's pinned
GCC 17 toolchain:

```text
-std=gnu++26 -freflection
```

The 2026-08-16 probe corpus verified the important surface with:

```text
-std=gnu++26 -freflection -fno-exceptions -fno-rtti -nostdlib++
```

`<meta>` is a standard header, but its reflective work is compile-time. The verified probes linked without the C++ runtime library. Generated tables, strings, objects, or selected functions can still occupy runtime binary space when retained.

## Workflow

1. Identify the requested compiler and date.
2. If the user asks about "today," "current," a newer compiler snapshot, or work after 2026-08-16, verify status against primary sources before answering:
   - GCC C++ status: `https://gcc.gnu.org/projects/cxx-status.html`
   - GCC 17 snapshots: `https://gcc.gnu.org/pub/gcc/snapshots/LATEST-17/`
   - WG21 papers and adoption records: `https://www.open-std.org/jtc1/sc22/wg21/docs/papers/`
3. Read [references/capabilities.md](references/capabilities.md) when the task needs exact APIs, syntax, limitations, or examples.
4. Select the strongest supported mechanism. Prefer a direct reflection solution over emulating reflection with traits or macros.
5. For implementation work, compile a minimal probe before restructuring a module. Use the production flags, including `-nostdlib++`, `-fno-exceptions`, and `-fno-rtti` when those are project policy.
6. Stabilize transient query results with `std::define_static_array(...)` before `template for`. This is the C++26 lifetime model for expansion over reflection vectors, not a GCC 16 workaround.
7. Turn structural omissions into compile-time failures with `static_assert` or `std::meta::exception`.
8. Inspect emitted data/code when generating large per-type specializations. Reflection itself has no runtime traversal cost, but its products may affect code size and layout.

## High-value pattern

Use one semantic declaration plus typed annotations:

```cpp
enum class SampleType {
    [[=WasapiFormat{WAVE_FORMAT_IEEE_FLOAT, 32}]]
    [[=AlsaFormat{SND_PCM_FORMAT_FLOAT_LE}]]
    [[=CoreAudioFormat{kAudioFormatLinearPCM, FloatFlag}]]
    f32,
};
```

Then reflect the enumerators, validate that every required backend annotation exists, compute foreign descriptors and capability matrices with ordinary `constexpr` algorithms under a `consteval` boundary, and materialize only the tables or dispatch required at runtime.

## Supported conceptual stack

Use the complete stack together:

```text
^^ declaration/type/value
    -> std::meta::info
    -> typed annotations
    -> consteval compiler boundary
    -> ordinary constexpr inspection, transformation, and validation
    -> template for expansion
    -> [: splice :] into types, members, bases, functions, and values
    -> define_static_* or define_aggregate when persistent products are needed
```

This is reflective metaprogramming in ordinary C++ control flow. Do not reduce it to "loop over fields," and do not rebuild a template metaprogram beside it.

## GCC 17 coverage that changes engine code

- `members_of` exposes closure `operator()` declarations and omits undeclared
  global builtins. Inspect callables and namespaces directly; do not maintain
  a parallel trait registry or builtin denylist.
- A reflected member-function template can be specialized with
  `can_substitute` and `substitute`, then spliced through its object or as an
  address. A callable wrapper may therefore remain a real member template
  while its concrete public arrow is recovered at translation time.
- Reflection queries instantiate an otherwise unused specialization before
  answering type, deduced return, deferred `noexcept`, base, and substitution
  questions. Do not ODR-use a declaration merely to make reflection truthful.
- `std::meta::info` has pointer size and pointer alignment in every Anoptic GCC
  17 lane. Never mix a translation unit using that layout with GCC 16-built
  reflection objects.

These are implementation corrections to the adopted language, not new Anoptic
extensions. The headers should express the standard design directly.

## Hard boundaries in GCC 17

- No arbitrary token injection or arbitrary generated function bodies.
- `define_aggregate` generates aggregate data members, alignment, bit fields, `no_unique_address`, and annotations; it is not a general AST builder.
- No runtime `std::meta::info`; reflections are consteval-only values.
- No arbitrary legacy attribute reflection; use C++26 annotations `[[=value]]` on declarations you control.
- No reflected array-element subobjects.
- No P3598 reflection inside contract assertions.
- Do not assume proposed or renamed reflection APIs merely because an older
  paper used them. Confirm the adopted spelling in the pinned `<meta>` header.
- Foreign semantics enter the compile-time program as typed declarations, annotations, or `constexpr` data. Reflection-driven algorithms compile JSON, GLSL, SPIR-V, multimedia, and Vulkan structure from that source; never use domain specificity to justify duplicated registries or routes.

## Decision rule

Reach for reflection when at least one is true:

- Program behavior depends on declarations, types, members, functions, annotations, or other program structure.
- One structural fact has more than one hand-maintained representation.
- A foreign specification defines a large family of structurally similar records or enumerations.
- A new field or enum value should force all affected adapters to update before the build succeeds.
- Compile-time computation can replace manual registries, traits tables, X-macros, switch forests, or generated source files.
- The output can be specialized completely and removed from runtime introspection.

Algorithms with no program-structure input remain ordinary functions, `constexpr` whenever their operations permit it. Reflect them only when the compile-time program must inspect, validate, select, bind, or reify them; this phase distinction gives templates no ownership over their implementation.
