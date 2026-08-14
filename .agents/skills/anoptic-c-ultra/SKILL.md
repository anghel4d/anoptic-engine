---
name: anoptic-c-ultra
description: >-
  Architectural reference for when a task creates new Anoptic modules: a public API under include/, its implementation boundary, and its ownership model. Use implicitly only when both conditions hold: (1) the task creates a new top-level engine module with a new public header and implementation unit, and (2) its API, ownership, data layout, or foreign boundary must be (re)designed from scratch.
---

# Doctrine

Every first-party source and header compiles as ISO C++26 or later, including files named `.c` and `.h`. C23 is limited to external dependencies and generated `extern "C"` foreign-ABI projections; it never constrains an engine source API.

Use safe, modern C++26 with ordinary `constexpr`/`consteval` computation, concepts, and reflection-driven generation. Prefer plain data, value semantics, namespaces, strong types, compile-time validation, immutable generated results, and foreign ABI projections only where an external boundary requires them. Avoid OOP cruft, inheritance, virtual polymorphism, RTTI, exceptions, and unnecessary library ownership abstractions. Preserve hot-path layout and measured performance.

## Compile-time programming

Treat C++26 reflection as the replacement for template metaprogramming and handwritten structural duplication, not as a serialization helper or an orchestration veneer around template-owned kernels.

```text
reflected declarations and typed annotations
    -> std::meta::info values
    -> consteval compiler entry point
    -> ordinary constexpr algorithms
    -> compile-time validation and transformation
    -> expansion, splicing, and static or aggregate generation
    -> direct typed runtime code
```

- `constexpr` functions own reusable algorithms. Use ordinary loops, local variables, containers, ranges, graph algorithms, and value transformations during constant evaluation.
- `consteval` functions and blocks enforce the translation-time phase. They invoke `constexpr` algorithms; they are not a separate template language.
- Reflection supplies declarations, types, functions, members, annotations, and values as data to those algorithms.
- Expansion statements and splicing reify computed structure as direct member access, calls, types, values, and specialized statements. `template for` is an expansion statement, not template metaprogramming.
- Templates have no architectural ownership. Use them only when an interface genuinely needs type or value parameterization. A templated function body remains an ordinary algorithm; do not build recursive TMP, typelists, traits registries, specialization forests, or index-sequence machinery when reflection can express the work directly.
- Materialize only the immutable tables, objects, strings, aggregates, or specialized code required by runtime. Do not move compile-time structural knowledge into runtime metadata traversal.

## One structural authority

Express each semantic fact once in reflected declarations or typed annotations. Compile schemas, foreign-format mappings, validation, migrations, dependency closure, route selection, ABI adaptation, serialization, and dispatch from that source.

Reject designs that add a parallel handwritten registry, tag switch, type-erased route table, schema mirror, adapter inventory, or backend mapping. New declarations and unsupported combinations fail during compilation rather than falling through at runtime.

Runtime-only effects do not create a second architectural ownership layer. Parsing, decoding, and transformation remain ordinary `constexpr` algorithms whenever their operations permit constant evaluation. I/O, allocation, and device API calls execute in small concrete functions, but their signatures, eligibility, selection, binding, and structural adaptation remain inputs to the reflected compile-time program. Do not call those runtime effects template-owned kernels or let them become another source of schema truth.

Use the ISO C++26 facilities required by the architecture and select a compiler that implements them. Compose expansion statements, spliced direct calls, generated aggregates, and retained static data without restoring duplicated structural machinery as a compatibility layer.

## Safety and runtime style

Rust proved strict compiler judgment viable; WG21 answered with SG23, Safe C++, profiles, hardening, contracts, and diagnosable initialization. Consult only when necessary: [safety](https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2024/p3390r0.html), [profiles](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3081r1.pdf), [hardening](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3471r2.html), [contracts](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2900r14.pdf), [initialization](https://www9.open-std.org/JTC1/SC22/WG21/docs/papers/2024/p2795r5.html), [reflection](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2996r13.html).

The compile-time programming model follows [P2996R13](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2996r13.html), [P1306R5](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p1306r5.html), [P3394R4](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3394r4.html), and Herb Sutter's [P0707R5](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p0707r5.pdf), [P3437R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3437r1.pdf), and [P3466R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3466r1.pdf).
