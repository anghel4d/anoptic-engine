# `src/` Directory

`src/` contains interpreters of the public types declared in `include/`.
In-tree `.c` files compile as C++26. The spelling preserves the engine's
C-shaped ABI and data style; it does not reduce the language model to C.

For a public interface with valued requests `Q` and response family `R(q)`, an
owner module implements

```text
handle : State x Q -> Effect(sum(r : R(q)) State)
```

Pure modules use the identity effect. Filesystem, worker, render-master,
audio-block, and crash-handler modules use distinct concrete effects. Source
code may choose any cache-optimal representation observationally equivalent to
the public algebra, but it may not enlarge the set of legal requests or expose
private state.

Reflected implementations consume the module's projection of the one normalized
declaration witness:

```text
D --reify--> S --rho_M--> S_M --compile_M--> Error_M + Plan_M
Plan_M --interpret_(M,p)--> implementation traces
```

Independent plans pair. Equality over a shared boundary uses a pullback; general
compatibility uses a refined dependent product. Runtime modules without a
reflected plan and static modules without a platform interpreter omit those
facets rather than manufacturing empty machinery.

The staged C++26 interpretation of these boundaries is machine-checked in
[`PROVEN_ARCHITECTURE.md`](../docs/resourcemanager/PROVEN_ARCHITECTURE.md):
reflection and `consteval` compile declarations, `constexpr` preserves one pure
meaning across stages, templates index types only, and executable plans contain
only direct value operations.

API adaptation, plan compatibility, resource-route composition, owner effects,
transport composition, and implementation refinement are distinct algebras.
Finite trace inclusion proves safety. Fairness, eventual retirement, bounded
backpressure, and starvation freedom require separate progress laws.

## Implementation laws

1. One module owns each mutable state machine and each foreign object family.
2. Cross-module calls use public types. A private header never becomes an
   accidental shared service locator.
3. Platform files are module-local interpretations of one public protocol into
   OS effects. Platform choice does not alter the protocol, and each
   implementation supplies the required observational refinement. Naturality
   or monoidal strength is claimed only where its diagrams are proved.
   Portability is a property of each module's interpretation, not a module of
   its own.
4. Reflection compiles a declared sum, product, schema, transform family, or
   ABI into direct code. Private code does not re-declare the same inventory as
   a switch table, callback registry, or parallel descriptor graph.
5. A tagged ABI record is validated/generated from its semantic sum. A status
   plus caller storage is also a valid lowering when inactive storage is
   unobservable. Internal code does not treat invalid combinations as ordinary
   states.
6. Temporal protocols use private or public typestate boundaries. Comments are
   not the sole enforcement of build/seal/publish/retire ordering.
7. Bulk execution is derived from scalar semantics unless atomicity, ownership,
   or layout supplies genuinely different behavior.
8. Allocation mechanism is shared through `anoptic_memory`; lifetime and layout
   policy remain with the owner module.
9. Tests observe public results, publication, foreign boundaries, and fuzzable
   parsers. They do not freeze private intermediate representations.
10. A source directory is not itself justification for an API. A semantic
    module exists only for a distinct algebra, state owner, effect owner, or
    reusable interpreter.
11. Alternatives within one ordered transport lane are a sum; independently
    ordered lanes are a product. Every multi-lane publication prevents mixed
    committed generations without prescribing one physical stamp format.
12. Failed cooker work may advance reusable private cache state but cannot
    change the published revision. Publication atomicity does not imply
    wholesale state rollback.

## Current implementation roots

The physical tree currently contains these owners:

```text
src/
|- meta/            C++26 reflection interface target
|- memory/          mimalloc heaps, lifetime regions, sealed volumes
|- threads/         OS thread and synchronization adaptation
|- time/            monotonic/civil clocks and waits
|- strings/         immutable strings, interning, UTF and collation
|- filesystem/      environment paths and append-file effects
|- resources/       resource compiler boundary, cooker, pack, runtime, import
|- log/             asynchronous logger and crash blackbox
|- mesh/            pure mesh conditioning kernels
|- text/            FreeType ownership, font baking, shaping
|- ui/              packed UI scene builder, reference evaluator, tiling
|- render_bridge/   logic/render transport interpreter
|- audio/           mixer, transport, DSP, file codec and device backends
|- synth/           score/bar-to-audio transducer
|- music/           deterministic musical state machine
|- vulkan_backend/  private Vulkan/GLFW render interpreter
`- engine/          process composition root and current demo world
```

There is no current `src/render/` implementation root. Render support is split
between `render_bridge/`, `vulkan_backend/`, `text/`, `ui/`, and reflected
resource code. Documentation and includes do not refer to a nonexistent render
directory.

## Factored semantic owners

Physical directories may be renamed or merged when the algebra demands it.
The required final ownership is:

| Semantic owner | Current source | Factored algebra and boundary |
|---|---|---|
| Structural compiler | `meta/` plus header-only definitions | One normalization of reflected records, sums, enums, functions, and annotations into `S`, followed by typed module projections; domain policy stays in its domain |
| Memory | `memory/` | Ordered layout monoid, unique construction state, immutable retained volume state, and scratch-region lifetime |
| Concurrency | `threads/` plus `anoptic_atomic.h` and typed headers | Platform-neutral threads internally; typed dual channel endpoints, latest-value publication, and bounded worker execution publicly |
| Time | `time/` | Typed clocks, instants, durations, conversion, and scheduler waits |
| Storage I/O | `filesystem/` | Environment/path queries and file/source/sink effects; logging and resource policy compose these rather than living inside them |
| Strings | `strings/` | Immutable byte-sequence monoid, explicit borrow/ownership, UTF interpretation, collation, and owner-thread interning |
| Diagnostics | `log/` | A monoid homomorphism from record sequences to sinks; crash blackbox remains a separate restricted interpreter |
| Foreign glTF schema | `resources/import/anogltf.c`, `include/anogltf.h` | Reflection-derived parse/validate/projection over explicit JSON/GLB source values; I/O and allocation are supplied by resource owners |
| Mesh transforms | `mesh/` | Pure canonical-mesh morphisms declared as resource transforms; numerical kernels remain private |
| Resource language | `resources/resource_universe.c` and typed header | One `consteval` closure of the reflected many-input/many-output route language, output-port provenance, and direct operations |
| Cooker | `resources/cooker.c`, `executor.c`, import sources | Stateful incremental interpretation from source-instance graph to immutable revision |
| Revision | currently split across `cooker_internal.h`, cook, and pack | Shared immutable value implemented independently of which producer created it |
| Pack | `resources/pack.c` | Deterministic serialize/open interpretation around revision; not the owner of revision semantics |
| Residency | `resources/runtime.c` | Demand/edit transition system producing immutable epochs and changed-ID values |
| Render resource owner | resource declarations plus `vulkan_backend/resources/` | Effectful capability-indexed interpretation from portable render cells to opaque fence-retired GPU slots, proved by observational refinement |
| Render protocol owner | `render_bridge/` plus platform-neutral parts of `vulkan_backend/` | Closed render-command/event sums and frame publication; transport remains private |
| Input | GLFW callbacks and input records currently inside render | Independent closed input-event sum; the GLFW adapter may remain physically beside the window owner |
| Vulkan interpreter | `vulkan_backend/` | Private effectful interpretation of render frames, resource jobs, and reflected pass/ABI schemas |
| Text owner | `text/` | FreeType/face and bake ownership separated from pure shaping; resource cells preserve borrowed font-source lifetime |
| UI | `ui/` | Free algebra of UI primitives folded into packed tables, then interpreted by CPU reference or GPU render paths |
| Audio owner | `audio/` | Mixer coalgebra and device interpreters over actual command/event sums; no music protocol or path-based asset import |
| Music | `music/` | Deterministic Mealy transition from state and typed controls to state and musical bars |
| Synth | `synth/` | Deterministic transducer from score/bar stream and transport to bus samples, automation, and synth events |
| ECS/world | current procedural state in `engine/main.c` | Archetype/column state owner producing demand and render/audio/UI values; persistence is a typed projection |
| Process composition | `engine/main.c` | Selects concrete interpreters and composes only public protocols; owns no backend, importer, or module-internal policy |

## Private-header ownership

Private headers remain inside their current owner until the corresponding
factorization moves them:

- `resources/`: `cooker_internal.h`, `parallel.h`.
- `render_bridge/`: `render_bridge.h`, `render_command_contract.h`,
  `bulk_update_plan.h`.
- `audio/`: `audio_bridge.h`, `audio_command_contract.h`, `audio_format.h`,
  `audio_fx.h`, `audio_internal.h`, `audio_pull.h`, `audio_source.h`, and
  `dsp/*.h`.
- `synth/`: `synth_internal.h`.
- `music/`: `music_*.h`; these are one owner's internal vocabulary, not public
  module interfaces.
- `text/`: `text_internal.h`.
- `ui/`: `ui_path.h`.
- `strings/`: `ano_strings_internal.h`, generated Unicode/collation tables.
- `filesystem/`: `filesystem_internal.h`.
- `log/`: `log_core.h`, `log_ring.h`, crash internals and POSIX adaptation.
  `log_old.h` has no final owner and is removed with its obsolete source.
- `mesh/`: `ano_meshoptimizer.h`; mesh algorithms remain private resource
  transform kernels.
- `vulkan_backend/`: all headers under the root and its `bridge/`, `frame/`,
  `instance/`, `resources/`, `shadow/`, `texture/`, and `vertex/` subtrees.
  None is included by `engine/main.c` or another public consumer.

## Resource implementation composition

The resource source tree implements sibling interpretations around a shared
immutable revision:

```text
reflected declarations --compile--> direct resource operations
          |                                  |
          v                                  v
source providers -------> cooker --------> Revision
                                            /   \
                              serialize/open     demand/edit
                                          /       \
                                       pack       residency
                                                    |
                                                    v
                                                  Epoch
                                           /         |        \
                                      render       audio      text
                                       owner       owner      owner
```

`resource_universe.c` is the sole translation unit that closes the reflected
language visible to one executable or tool. Import files declare/implement
ordinary transforms and source effects; they do not mutate a second type graph.
One transform invocation may publish several addressable output cells; revision
provenance identifies each by `(transform, output-port)` while sharing the same
typed input handles. Several instance edges may name one immutable handle
without copying the artifact value.
`cooker.c` may retain a persistent instance DAG and worker group, but its public
codomain is `Revision`. `pack.c` authenticates and constructs the same revision
abstraction. `runtime.c` consumes revisions and publishes epochs. Owner modules
alone realize and retire device values.

If a cook fails, verified hashes, parsing results, discovered dependencies, and
other reusable private cache state may remain in the long-lived cooker. The
previous revision remains published. A successful attempt alone changes the
published revision.

## Composition root

`engine/main.c` is an interpreter selector, not a super-module. Its final form:

- initializes process services through public headers;
- selects platform render, audio, filesystem, clock, and input interpreters;
- creates the world/ECS state owner;
- submits typed source roots or opens a revision;
- feeds world demand into residency;
- publishes coherent ECS/resource pairs to render and audio owners;
- drives the frame loop and orderly teardown.

It does not include `vulkan_backend/*.h`, Vulkan, GLFW, resource cooker
internals, or owner-private headers. Demo scenes, material permutations, and hot
reload exercises are game/tool clients of the same public resource API rather
than special branches in the process root.
