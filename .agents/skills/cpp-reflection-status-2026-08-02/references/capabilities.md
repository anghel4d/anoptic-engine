# Verified C++26 reflection capabilities - 2026-08-02

## Contents

1. Status and flags
2. Core reflection and identity
3. Structure, names, layout, and access
4. Annotations and parameters
5. Expansion, splicing, and generation
6. Reflected type computation
7. Known GCC 16.1 limits
8. Primary sources

## Status and flags

This snapshot reconciles adopted WG21 C++26 papers with GCC's status page, GCC 16.1's installed `<meta>` header, and local compiler probes.

```text
g++ (GCC) 16.1.0
-std=gnu++26 -freflection -fno-exceptions -fno-rtti -nostdlib++
```

The facilities below are standardized for C++26 and present in GCC 16.1 unless listed under limits.

## Core reflection and identity

| Capability | Illustrative spelling | Typical use |
| --- | --- | --- |
| Reflect declarations and types | `constexpr info r = ^^AudioFormat;` | Represent types, functions, variables, fields, enumerators, namespaces, templates, aliases, and type-ids as compile-time values. |
| Compare and pass reflections | `consteval bool valid(info r)` | Write ordinary compile-time algorithms over declarations. |
| Preserve or remove alias identity | `dealias(^^Rate)` | Distinguish declared aliases when useful, normalize when comparing representation. |
| Reflect current scope | `current_namespace()`, `current_class()`, `current_function()` | Scope-aware generators and diagnostics. |
| Reflect computed constants | `reflect_constant(48000u)` | Turn a constant into `info`, including annotation values. |
| Reflect objects and functions | `reflect_object(obj)`, `reflect_function(fn)` | Preserve resolved object or function identity. |
| Recover represented entities | `type_of`, `object_of`, `constant_of`, `extract<T>` | Move between declarations, types, constants, and typed values. |
| Classify entities | `is_type`, `is_variable`, `is_function`, `is_namespace`, `is_value`, `is_object` | Guard general generators. |
| Classify templates | `is_template`, `is_class_template`, `is_function_template`, `is_alias_template`, `is_variable_template` | Build generic registries and compute specializations. |
| Classify functions | `is_constructor`, `is_destructor`, `is_assignment`, `is_operator_function`, copy/move variants | Validate foreign callbacks and generated interfaces. |
| Inspect declaration properties | `is_deleted`, `is_defaulted`, `is_explicit`, `is_noexcept`, `is_virtual`, `is_final` | Enforce interface and C+Ultra subset contracts. |
| Inspect qualifiers, storage, and linkage | `is_const`, `is_volatile`, `has_static_storage_duration`, `has_external_linkage`, `has_c_language_linkage` | C ABI and storage-policy checks. |
| Navigate ownership | `has_parent`, `parent_of` | Confirm a reflected field or enumerator belongs to the intended schema. |
| Query names and locations | `identifier_of`, `display_string_of`, UTF-8 variants, `source_location_of` | Foreign names, diagnostics, documentation, registries. |
| Reflect operators | `operator_of`, `symbol_of`, `u8symbol_of` | Classify overloaded operators without string heuristics. |

## Structure, names, layout, and access

| Capability | Illustrative spelling | Typical use |
| --- | --- | --- |
| Enumerate members | `members_of(^^T, ctx)` | Inspect class or namespace contents. |
| Enumerate fields | `nonstatic_data_members_of(^^T, ctx)` | Parsing, serialization, hashing, projection, ABI generation. |
| Enumerate static fields | `static_data_members_of(^^T, ctx)` | Declaration-owned constants and registries. |
| Enumerate bases and subobjects | `bases_of`, `subobjects_of` | Structural visitors across direct bases and fields. |
| Enumerate enumerators | `enumerators_of(^^E)` | Enum maps, exhaustive mappings, generated tests. |
| Inspect access | `access_context::current/unprivileged/unchecked`, `.via(^^T)`, `is_accessible` | Respect or deliberately bypass language access for trusted schema machinery. |
| Detect inaccessible structure | `has_inaccessible_nonstatic_data_members`, `has_inaccessible_bases`, `has_inaccessible_subobjects` | Decide whether automatic processing is legal. |
| Query layout | `offset_of`, `size_of`, `alignment_of`, `bit_size_of` | Vulkan, std140/std430, binary formats, foreign ABIs. |
| Inspect bit fields | `is_bit_field`, `offset_of(m).total_bits()` | Packed protocols and binary structures. |
| Inspect representation | `is_standard_layout_type`, `is_trivially_copyable_type`, `is_implicit_lifetime_type`, `has_unique_object_representations` | C ABI, arenas, load-in-place, bytewise operations. |
| Inspect structural annotation eligibility | `is_structural_type(^^Tag)` | Prove an annotation or non-type template value is legal. |
| Inspect arrays | `rank`, `extent`, bounded/unbounded predicates | Foreign fixed layouts and generated loop bounds. |

## Annotations and parameters

| Capability | Illustrative spelling | Typical use |
| --- | --- | --- |
| Attach typed annotations | `[[=JsonName{"sampleRate"}]] uint32_t rate;` | Encode foreign names, ranges, requiredness, defaults, backend codes. |
| Annotate enumerators and parameters | `[[=AlsaCode{...}]] f32` | Platform maps and function argument schemas. |
| Read annotations | `annotations_of`, `annotations_of_with_type` | Typed metadata queries without string parsing. |
| Identify and extract annotations | `is_annotation`, `extract<Tag>(r)` | Validate and consume metadata. |
| Enumerate parameters | `parameters_of(^^fn)` | FFI, CLI, RPC, callback, and adapter generation. |
| Inspect parameters | `is_function_parameter`, `is_explicit_object_parameter`, `has_default_argument`, `is_vararg_function` | Function contract checks. |
| Recover parameter variables and return types | `variable_of`, `return_type_of` | Generated wrappers and in-function validation. |

## Expansion, splicing, and generation

| Capability | Illustrative spelling | Typical use |
| --- | --- | --- |
| Stabilize reflection ranges | `std::define_static_array(members_of(^^T, ctx))` | GCC 16.1-safe input to expansion statements. |
| Expand heterogeneous statements | `template for (constexpr info m : members) { ... }` | Replace index sequences and recursive template iteration. |
| Use normal compile-time control flow | `if constexpr`, loops, local variables, containers | Compute over program structure in ordinary C++. |
| Splice types and values | `using T = [:type_info:];`, `[:constant_info:]` | Reify computed types and constants. |
| Splice members | `object.[:member:]` | Generated parsers, serializers, projections, structural algorithms. |
| Splice base subobjects | `object.[:base:]` | Uniform direct-base and field traversal. |
| Splice functions | `[:fn:](args...)`, `object.[:method:](args...)` | Compile-time-generated dispatch. |
| Form reflected member pointers | `&[:member:]` | Generated member tables. |
| Inspect and compute template specializations | `template_of`, `template_arguments_of`, `can_substitute`, `substitute` | Select generic implementations using ordinary consteval logic. |
| Materialize static data | `std::define_static_string`, `define_static_array`, `define_static_object` | Persistent names, registries, capability matrices, descriptors. |
| Reflect constant arrays and strings | `reflect_constant_array`, `reflect_constant_string` | Low-level static-data generation. |
| Run declaration-level compile-time work | `consteval { validate(^^T); }` | Execute validation without a dummy variable. |
| Describe generated data members | `data_member_spec(^^int, {.name="x", .alignment=16})` | Compute aggregate field layouts. |
| Complete generated aggregates | `define_aggregate(^^Generated, specs)` | Generate structs/unions with names, alignments, bit widths, `no_unique_address`, and annotations. |
| Report and recover compile-time errors | `std::meta::exception`, `try`/`catch` in constant evaluation | Structured failures with operation and source information. |

## Reflected type computation

GCC 16.1 exposes reflected equivalents of the normal type-trait families. Operate on `info` values and splice results back into code.

| Family | Available operations | Typical use |
| --- | --- | --- |
| Primary/composite categories | void, integral, floating, array, pointer, reference, member pointer, enum, union, class, function, arithmetic, scalar, compound | Select generated logic by representation. |
| Type properties | const/volatile, standard-layout, trivially-copyable, empty, polymorphic, abstract, final, aggregate, structural, signedness, scoped enum | Enforce schema and safety contracts. |
| Construction/assignment/destruction | constructible, assignable, swappable, destructible; default/copy/move; trivial and nothrow variants | Prove generated operations are legal and exception-free. |
| Relations | same, base-of, virtual-base-of, convertible, layout-compatible, pointer-interconvertible | Foreign ABI and adapter correctness. |
| Invocation | invocable, invocable-result-constrained, nothrow variants, `invoke_result` | Validate generated dispatch. |
| CV/ref transforms | add/remove const, volatile, cv, lvalue/rvalue reference, `remove_cvref` | Compute exact generated signatures. |
| Sign/array/pointer transforms | make signed/unsigned, remove extents, add/remove pointer | Foreign numeric and layout adaptation. |
| Higher transforms | decay, common type/reference, underlying type, unwrap reference/decay | Generic compile-time projection. |
| Tuple/variant structure | `tuple_size`, `tuple_element`, `variant_size`, `variant_alternative` | Heterogeneous structural generation. |
| Deterministic ordering | `type_order` | Stable compile-time registries. |

## Known GCC 16.1 limits

- Missing `std::meta::apply_result`.
- Missing `std::meta::is_applicable_type`.
- Missing `std::meta::is_nothrow_applicable_type`.
- P3598 contract splicing is not implemented; a recovered probe caused an ICE.
- Expanding directly over a temporary reflection vector failed; stabilize with `std::define_static_array`.
- Arbitrary token injection, arbitrary function-body synthesis, enum synthesis, arbitrary legacy attribute reflection, runtime reflections, and reflected array-element subobjects are unavailable.
- `define_aggregate` is deliberately limited to generated aggregate data members.

## Primary sources

- P0707R5, Metaclass functions for generative C++: `https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p0707r5.pdf`
- P3437R1, Reflect C++, generate C++ (by default): `https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3437r1.pdf`
- P3466R1, (Re)affirm design principles for future C++ evolution: `https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3466r1.pdf`
- P2996R13, Reflection for C++26: `https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2996r13.html`
- P3394R4, Annotations for Reflection: `https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3394r4.html`
- P1306R5, Expansion Statements: `https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p1306r5.html`
- P3096R12, Function Parameter Reflection: `https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3096r12.html`
- P3293R3, Splicing a Base Class Subobject: `https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3293r3.html`
- P3491R3, `define_static_*`: `https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3491r3.html`
- P3560R2, Error Handling in Reflection: `https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3560r2.html`
- P3795R2, Miscellaneous Reflection Cleanup: `https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p3795r2.html`
- GCC C++ status: `https://gcc.gnu.org/projects/cxx-status.html`
- GCC 16 changes: `https://gcc.gnu.org/gcc-16/changes.html`
