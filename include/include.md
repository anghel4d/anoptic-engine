**2026-08-14 — not current!**

# `include/` Directory

`include/` contains the public type of the Anoptic engine.

An API is not merely a list of callable symbols. It is a family of requests and
the response type belonging to each request. Records are products, tagged
alternatives are sums, optional values are `1 + T`, failures are `T + E`, and
opaque handles index state owned by another module. Function signatures expose
the morphisms between those types.

For an interface `A` with valued requests `Q_A` and response family `R_A`, the
public interaction shape is

```text
P_A(X) = sum(q : Q_A) X ^ R_A(q)
```

The shape does not determine its meaning. For carrier `X_A` and effect functor
`F_A`, a semantic algebra supplies

```text
alpha_A : P_A(X_A) -> F_A(X_A)
```

An API adaptor is only a morphism of shapes. It preserves meaning only when it
also has a carrier map, a natural map between effects, and a commuting proof.
The implementation in `src/` interprets the selected algebra. Foreign ABI records,
calling conventions, queues, and memory layouts are physical representations
of it, not additional semantic authorities.

The public surface does not pretend that every boundary belongs to one global
category. API adaptors, reflected plan compilers, resource routes, owner state
machines, transports, and implementation refinements retain their own
composition laws. Their common compile-time structural authority factors as:

```text
D --reify--> W --rho_M--> W_M --C_M--> CompileError_M + Plan_M
```

`W` is one normalized witness for the closed declaration universe. Each public
module compiler consumes only its projection `W_M`; no runtime registry retains
`W`. Independent plans pair. Plans that must agree on a shared projection use a
pullback, and arbitrary compatibility uses a proof-carrying refined product.

For API adaptors, the unique terminal map is `A -> 1`, not `1 -> A`. Polynomial
zero has no request; polynomial unit has one request with no response positions.

Every first-party public interface is C++26 and lives in namespace `ano`.
Reflection may generate an `extern "C"` projection only at a genuine foreign
boundary; C23 source compatibility never constrains the engine API. Reflected
resource-cell declarations live in `ano::asset_schema`. Platform types,
foreign-library types, private allocation policy, and backend objects do not
cross this directory.
Portability is a property of each module's interpretation, not a module of its
own.

## Fallible composition

For fallible arrows

```text
f : A -> Result<B, E>
g : B -> Result<C, E>
```

their composition is

```text
(f >=> g)(value) = f(value).and_then(g)
```

`anoptic_compose.h` represents that arrow directly as
`function(f).and_then(g)`. `transform(h)` lifts a direct `h : B -> C` through
the same result. Both operations are `constexpr`, retain concrete callable
types, allocate nothing, and compile to direct calls. The reflected `Result`
classifier rejects a stage outside the `T + E` algebra during instantiation.
This interface does not imply a scheduler: owner-thread transfer, transactional
state, and publication remain explicit effects supplied by their modules.

## Public type laws

Every public header obeys the following rules:

1. A product contains only values that exist together.
2. Alternatives use one closed sum; a tag plus alternative storage is an ABI
   encoding generated at a boundary. Alternative payloads within one ordered
   lane are sums; independently ordered lanes are products.
3. A fallible response is one domain-local outcome. A generated foreign status
   plus caller storage may lower that outcome only when the contract makes the
   payload observable solely on success.
4. Distinct protocol states use distinct capabilities when sequencing affects
   legality. Building, sealed, published, and retired values are not one
   universally mutable handle.
5. Ownership is visible in the type: value, immutable borrow with lifetime
   owner, unique builder, stable identity, or opaque owner-issued slot.
6. A bulk interface derives from its scalar semantics unless it has distinct
   layout or atomic-publication semantics; an optimized lowering proves the
   same observations rather than merely reusing an implementation.
7. Public dependencies follow signature types only. A producer and consumer of
   an intermediate value both depend on that value's header; neither depends on
   the other's operational API.
8. Reflection reads the declared products, sums, functions, and annotations.
   It generates direct operations and transport layouts; no runtime reflection
   interpreter or parallel registration inventory is public.
9. Header-only `constexpr`, `consteval`, expansion, and splicing definitions are
   implementation only when they are required to compile the public type. All
   other implementation lives in `src/<owner>/`.
10. A reflected transform exposes typed input and output port products. Producer
    identity is `(transform, output-port)`, so one invocation may produce
    several independently addressable cells.
11. A public multi-lane protocol states one committed-generation law but does
    not require every physical message to repeat a transaction identifier.
12. An absent module is omitted. An empty header alone establishes neither the
    zero polynomial nor the tensor unit.

## Required dependency shape

The shared resource value `Revision` is the center of three sibling protocols:

```text
                         anoptic_resources_typed.h
                                    |
                                    v
                         reflected cell declarations
                                    |
              +---------------------+---------------------+
              |                                           |
              v                                           v
 anoptic_resources_cook.h  ----produces---->  anoptic_resources_revision.h
                                                   ^              |
                                                   |              v
 anoptic_resources_pack.h  <---serializes/opens----+    anoptic_resources_runtime.h
```

Cook does not include pack merely to name a revision. Runtime does not include
pack merely to consume one. A live cook and an authenticated opened pack expose
the same immutable revision abstraction.

Owner resource extensions declare cells and transforms without importing
orchestration:

```text
anoptic_resources_typed.h
    |- anoptic_render_resources.h
    |- anoptic_audio_resources.h
    |- anoptic_text_resources.h
    `- anoptic_resources_ecs.h
```

Cook, runtime, renderer publication, mixer publication, and text-owner
publication consume the compiled products through their own headers. An owner
extension does not include cooker/runtime headers simply to declare a transform.
The one resource-universe translation unit closes the relevant projection of
`W`; each transform contributes all reflected input and output ports.

## Factored public surface

The following is the required semantic surface. Lean checks the abstract
algebras used to state it, but this table is not generated from the current
headers and is not itself an implementation certificate. The reflected
resource-schema subset has a mechanical C++26-to-Lean bridge; exact status is in
[`PROOF_STATUS.md`](../docs/resourcemanager/PROOF_STATUS.md). A compatibility
header may currently implement more than one row, but it does not merge their
semantic ownership or composition laws.

| Algebraic module | Public header boundary | Current disposition |
|---|---|---|
| Structural reflection and compile-time values | `anoptic_meta.h` | Normalize one shared witness and expose typed module projections; fold `anoptic_meta_types.h` into this authority; domain command policy leaves meta |
| Domain outcomes | `anoptic_results.h` | Retain as the C++26 `T + E` algebra; generate any C-linkage projection beside the concrete foreign boundary |
| Fallible composition | `anoptic_compose.h` | Reusable `constexpr` Kleisli composition over typed results; no allocation, erasure, scheduler, or runtime route registry |
| Atomic values | `anoptic_atomic.h` | Retain as the no-runtime compiler-builtin boundary |
| Linear algebra and ABI values | `anoptic_math.h` | Retain; layout and coordinate semantics become reflected type facts |
| Runtime memory ownership | `anoptic_memory.h` | Retain regions, sealed volumes, and raw C allocation boundary |
| Reflected memory layouts | `anoptic_memory_typed.h` | Retain as the C++26 product-to-layout interpretation |
| OS concurrency | `anoptic_threads.h` | Retain the name; replace public `pthread_*` aliases with platform-neutral capabilities |
| Typed transport | `anoptic_threads_typed.h` | Retain typed SPSC and latest-value protocols; byte-stride owner specializations leave this header |
| Time values and clock effects | `anoptic_time.h` | Retain; use typed instants and durations rather than unit-bearing scalar names |
| Immutable byte strings | `anoptic_strings.h` | Retain; separate borrowed and owned lifetime types and typed allocation failure |
| UTF-8 and collation extension | `anoptic_strings_utf.h` | Retain the base-plus-extension boundary |
| Filesystem values and effects | `anoptic_filesystem.h` | Retain; separate environment/path policy from file operations within the type surface |
| Asynchronous diagnostics | `anoptic_log.h` | Retain records and ordinary sink control |
| Crash blackbox | `anoptic_log_crash.h` | Retain as a distinct async-signal-safe interpreter |
| Reflected glTF/GLB schema | `anogltf.h` | Retain the schema and required compile-time derivation; filesystem/allocation callbacks leave the semantic parse type |
| Resource identities | `anoptic_resources.h` | Retain stable IDs, content IDs, byte views, errors, and common values |
| Resource language | `anoptic_resources_typed.h` | Retain cells, many-input/many-output transforms, output-port provenance, `AssetRef<T>`, navigation, and generated direct operations |
| Immutable cooked revision | `anoptic_resources_revision.h` | New shared boundary extracted from `anoptic_resources_pack.h` and `anoptic_resources_cook.h` |
| Source import and cooking | `anoptic_resources_cook.h` | Retain as a producer of revisions; remove callback-shaped artifact semantics |
| Manifest and pack interpretation | `anoptic_resources_pack.h` | Retain as serialization/opening around revisions |
| Demand, edit, and residency | `anoptic_resources_runtime.h` | Retain as the epoch and transaction owner; depend on revision, not pack |
| ECS/world resource integration | `anoptic_resources_ecs.h` | New reflected demand, world-cell, and coordinated-publication extension |
| Render resource routes | `anoptic_render_resources.h` | Retain portable/render-resident cells and transforms only; move reload orchestration to its owner boundary |
| Audio resource routes | `anoptic_audio_resources.h` | New source, decoded, streaming, and mixer-resident cell extension |
| Text resource routes | `anoptic_text_resources.h` | New font-source, face/bake, atlas, and text-owner cell extension |
| Render protocol | `anoptic_render.h` | Retain frame/world protocol; remove backend names, input, font/resource lookup, capture file I/O, and optional UI/text protocol fields |
| Input protocol | `anoptic_input.h` | New closed input-event sum extracted from `anoptic_render.h` |
| Rendered text extension | `anoptic_render_text.h` | New text submission protocol depending on `anoptic_text.h` |
| Rendered UI extension | `anoptic_render_ui.h` | New UI submission protocol depending on `anoptic_ui.h` |
| Text shaping | `anoptic_text.h` | Retain immutable bake values and pure shaping; path-based face ownership moves to text resources |
| UI scene algebra | `anoptic_ui.h` | Retain packed scene construction and interpretation; demo generation is not public API |
| Audio mixer protocol | `anoptic_audio.h` | Retain mixer/device request and response sums; remove music tunneling and path-based WAV import |
| Musical state machine | `anoptic_music.h` | Retain typed controls, deterministic state transition, bars, and canonical snapshots |
| Synthesis transducer | `anoptic_synth.h` | Retain score/bar-to-audio transformation; remove ownership of music and audio-module callbacks |
| ECS state | `anoptic_ecs.h` | New archetype/column state interface; resource integration remains in `anoptic_resources_ecs.h` |

`anoptic_collections.h` has no semantic API and is deleted. `anoptic_hive.h` is
an implementation-selection alias and is deleted when the selected C++26
standard library provides `std::hive`; modules use the standard type directly.
Neither header contributes a zero or unit API merely by being empty or
identity-shaped; the final factorization contains no collections module.

## Header composition

Focused extension headers follow the pattern already proven by
`anoptic_strings.h` and `anoptic_strings_utf.h`: the base declares the smaller
algebra, and the extension depends on it without making every base consumer pay
for the extension.

A public header may be split because two protocol sums have different
dependencies even when one implementation owner handles both. Conversely,
headers are not split merely to mirror source files. The irreducible criterion
is whether the request/response family, state index, ownership law, or required
dependency differs.
