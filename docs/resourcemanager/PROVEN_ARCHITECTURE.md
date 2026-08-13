# Proven whole-engine C++26 factorization

## Status

This document records machine-checked facts. It is not a task list, migration
log, or implementation diary. The public boundary remains the types in
`include/`; their owners and interpreters remain the code in `src/`.

The central theorem is:

```text
forall interface in the 24-interface inventory,
    interface law
  x composition law
  x well-staged C++26 image
  x successful declaration compilation
  x associative target algebra
  x semantics-preserving lowering
```

In Lean this is
`Cxx26Mapping.allInterfacesMapToComposableCxx26`. The enumeration is closed:
adding an interface without adding its law, image, and proof case fails the
proof build.

The proof sources are:

| Fact | Lean authority |
|---|---|
| Abstract interface carriers and laws | `Anoptic.Coverage` and the domain modules it imports |
| Composition law of every interface | `Anoptic.CompositionCoverage` |
| Staged C++26 target language | `Anoptic.Cxx26` |
| Exhaustive interface lowering | `Anoptic.Cxx26Mapping` |
| Declared cross-module graph | `Anoptic.ArchitectureGraph` |
| Full cook/prepare/realize/interpret composition | `Anoptic.EngineComposition` |

## What “maps to C++26” means

The target is a small, typed subset of C++26 rather than an attempted formal
semantics for the entire language.

```text
reflected declarations
        |
        | reify, project, validate, expand, splice
        v
consteval compiler : Declaration -> CompileError + Plan
        |
        | successful plan only
        v
direct runtime values and calls
```

Translation operations and execution operations have different indexed Lean
types. A runtime plan therefore cannot contain reflection, immediate
evaluation, type instantiation, expansion, or splicing. This is a construction
rule, not a convention checked after the fact.

### Language facilities

| C++26 facility | Formal representation | Proven fact | Runtime image |
|---|---|---|---|
| Reflection | `Operation .translation.reflect`, `MetaInfo`, `reify`, `membersOf` | Reification preserves the complete declared member sequence | None; declarations are not retained as a registry |
| `consteval` | `Immediate Input Error Output` | Identity and associativity; failure short-circuits; successful schema compilation preserves the exact declaration | A successful immutable plan or a translation diagnostic |
| `constexpr` | `Constant Input Output` | Translation-time and execution-time evaluation have one meaning; composition is associative | Direct pure value operation when runtime inputs are supplied |
| Templates | `TypeTemplate.apply : List TypeExpr -> TypeExpr` | Template arguments are types by construction; there is no value/expression argument constructor | Typed carriers such as `AssetRef<T>`, `Result<T,E>`, and spans |
| Expansion statements | `expandMembers` | Expansion visits exactly the reflected member sequence | Straight-line generated operations |
| Splicing | `spliceType` and the emitted operation plan | Splicing reconstructs exactly the reflected field product | Concrete types and member accesses |
| Erasure | `Operation Stage` and `ExecutablePlan` | Every executable operation excludes translation-only features | Plain records, tagged values, state references, owner publication, and direct calls |

Templates provide type indexing. They do not own structural computation.
Reflection discovers structure; `consteval` validates and compiles it;
`constexpr` expresses pure operations that may execute at either stage; expansion
and splicing materialize direct code.

## Target composition algebras

There is no false claim that one global category explains every module. The
proof maps each interface to one of eight target algebras and proves the
appropriate operator.

| Algebra | C++26 value form | Composition | Proved by |
|---|---|---|---|
| Compiler | Immediate function `Witness -> Error + Plan` | Kleisli composition at translation time | `Immediate.compose_assoc` |
| Polynomial API | Reflected query sum with response family | Requests forward, responses backward | `API.Hom.trans_assoc` |
| Pure | Direct `A -> B` function | Ordinary function composition | `Runtime.Pure.compose_assoc` |
| Fallible | Direct `A -> Result(B,E)` function | Result bind | `Runtime.Fallible.compose_assoc` |
| Stateful | Direct `A x S -> B x S` function | Ordered state threading | `Runtime.Stateful.compose_assoc` |
| Transactional | Direct `A x S -> S x Result(B,E)` function | Failure-retaining candidate-state threading and publication | `Runtime.Transactional.compose_assoc` |
| Resource route | Typed dependent products of input and output ports | Many-input/many-output route composition | `ResourceRoute.Morphism.compose_assoc` |
| Relational interpreter | Relation between source and target observations | Existential intermediate observation | `Relation.compose_assoc` |

The corresponding lowering-preservation theorem is exhaustive as well:
`Cxx26Mapping.everyAlgebraLoweringPreservesComposition`. It proves that
composition before lowering and composition after lowering have the same
observable result for compiler projections, polynomial adaptors, pure,
fallible, stateful, transactional, and resource-route morphisms. Relational
composition is definitionally the same existential intermediate boundary.

## Exhaustive interface result

The following table is the evaluated content of
`Cxx26Mapping.interfaceImage`. Product rows lower to records, choice rows lower
to closed tagged values, and template applications carry types only.

| Ideal module | Target algebra | Reflected request | Reflected response |
|---|---|---|---|
| Structural compiler | Compiler | Declaration | `Result<NormalizedWitness, CompileError>` |
| Outcomes | Fallible | Input | `Result<Value, Error>` |
| Polynomial collections | Polynomial API | `QueryA + QueryB` | `Response<SelectedQuery>` |
| Linear algebra | Pure | `Vector x Vector` | Vector |
| Memory | Fallible | `Cursor x Span<Segment>` | `Result<Layout, MemoryError>` |
| Concurrency/publication | Transactional | Publication request | `Result<Generation, TransportError>` |
| Time | Pure | `Instant x Duration` | Instant |
| Filesystem | Fallible | File request | `Result<FileValue, FileError>` |
| Strings | Fallible | `StringView x StringView` | `Result<OwnedString, AllocationError>` |
| Diagnostics | Stateful | Diagnostic command | Diagnostic state |
| glTF/GLB | Fallible | `JsonBytes + GlbBytes` | `Result<BoundGltf, GltfError>` |
| Mesh | Pure | Canonical mesh | `Span<MeshLod>` |
| Resource language | Resource route | Typed input handles and settings | Typed output handles and provenance |
| Render resources | Pure | Portable cell handle and render owner | Render slot |
| Render protocol | Stateful | `Draw + Upload + Present` | Render state |
| Input/display | Stateful | `InputEvent + DisplayEvent` | Display state |
| Vulkan interpreter | Stateful | Render commands | `Result<Frame, DeviceError>` |
| Text | Fallible | Font-bake handle and string view | `Result<Span<Glyph>, TextError>` |
| UI | Pure | UI description | Packed UI scene |
| Audio | Stateful | `Play + Stop + SetListener` | Audio block |
| Music | Stateful | Music control | Music bar |
| Synth | Stateful | Synth input | Audio bus |
| World/ECS/save | Stateful | World input | Demand, render commands, audio commands, and packed UI scene |
| Engine root | Transactional | Frame request | `Result<Frame x AudioBlock, EngineError>` |

For every row, Lean proves all of the following together:

1. The abstract interface law holds.
2. Its own composition law holds.
3. Its declaration contains a reflection, `consteval`, `constexpr`, type-only
   template, expansion, and splice stage.
4. Its declaration compiles successfully to an executable plan.
5. No translation-only operation occurs in that executable plan.
6. Its target algebra is associative.
7. Lowering preserves its source composition.

## Whole-graph composition

`ArchitectureGraph.EdgeName` exhaustively names 31 dependency edges. Each edge
has an indexed source and target cell, so an ill-typed intermediate cell cannot
form a path. `Cxx26Mapping.allArchitectureEdgesMapToComposableCxx26` proves that
every named edge is typed, compiles, is well staged, and uses a composable pure
target arrow.

For arbitrary declared paths:

```text
lower(run(first ; second))
    observationally equals
lower(run(first)) ; lower(run(second))
```

This is `Cxx26Mapping.lowerPath_compose_preserves`. Target-side regrouping is
`Cxx26Mapping.loweredPathCompositionAssociates`; source-side regrouping is
`ArchitectureGraph.Path.run_assoc`.

The joins and forks at the engine root are covered separately.
`Cxx26Mapping.engineFullPathPreserved` identifies the lowered four-part
cook/prepare/realize/interpret arrow with `EngineComposition.run`. Therefore
the full public path retains the already-proved demand routing, shared
revision-indexed owner generation, render/audio/music/synth routing,
cook/pack/open identity, and failed-reload publication isolation.

## Executable compiler witness

Lean proves the staged subset and the lowering. The repository also contains a
real C++26 witness at `proofs/cpp/cxx26_mapping.cpp`. GCC 16.2 compiles and runs
it with:

```text
-std=gnu++26 -freflection -fno-exceptions -fno-rtti -nostdlib++
```

The witness exercises the same boundary:

- class templates whose parameters are types;
- reflection of complete record members;
- a `consteval` record validator and compile-time member count;
- expansion statements and type splicing;
- one stage-coherent `constexpr` pure composition;
- fallible, stateful, transactional, and typed resource-route compositions;
- compile-time rejection of an empty reflected record;
- transactional failure preserving the preceding publication.

`nix build .#proofs` first builds the complete Lean kernel, then compiles,
links, and executes this witness. The proof target cannot pass by checking only
one side of the bridge.

## Present-tense architectural consequences

1. The API is the type. A hand-maintained descriptor graph is not a second
   authority.
2. C++26 reflection compiles declarations into module projections and direct
   operations at translation time.
3. `consteval` owns structural validation, plan construction, and rejection of
   incomplete or illegal declarations.
4. `constexpr` owns reusable pure value computation at either stage.
5. Templates index semantic types only. Structural iteration and code
   generation use reflection, expansion, and splicing.
6. Runtime code contains no reflection registry, generic metadata interpreter,
   or runtime template dispatch.
7. Products, sums, failures, state transitions, transactions, resource routes,
   and platform refinements retain their distinct composition laws.
8. A lowered composition must preserve the observations of the abstract
   composition; merely compiling is insufficient.
9. `include/include.md` defines the public type boundaries. `src/src.md`
   defines their owner and interpreter boundaries.

## Trust boundary

The Lean kernel proves the abstract laws, staged target grammar, exhaustive
interface and edge mappings, algebraic composition, and lowering preservation.
The Nix-pinned GCC witness proves that the required standard C++26 spelling is
accepted, links without a C++ runtime, and executes the checked examples.

The compiler implementation and its generated object code remain trusted, as
does the Lean kernel itself. Foreign codecs, operating systems, drivers, and
devices require module-local observational refinements; the language proof does
not invent codec correctness, driver progress, or device fairness. A production
implementation satisfies an ideal interface only when its concrete bridge
supplies the corresponding refinement witness.
