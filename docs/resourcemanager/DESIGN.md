# Reflection-Compiled Residency Graph

## Contract

**Types and laws are compile-time. Asset instances and schedules are runtime.**

Reflection compiles the resource language. The cooker evaluates asset-instance
graphs. The runtime reconciles residency goals. ECS supplies those goals and
consumes immutable bindings.

| Stage | Input | Output |
|---|---|---|
| C++ compilation | Resource types, fields, transform functions, annotations | Schemas, validators, transform type graph, direct typed operations, legal-route tables |
| Asset cooking | Source assets, build settings, platform profiles | Content-addressed artifact DAG, manifest, packs, dependency graph |
| Runtime | ECS and world demand, hardware capabilities, current residency | I/O and transform schedule, immutable residency epochs |

C++ compilation never evaluates an asset-instance graph. The resource language
uses reflected declarations directly and has no parallel template typelist or
marker hierarchy.

## Resource language

Artifact types and transformation functions are the declarations reflected by
the resource compiler. Their annotations state storage class, execution stage,
determinism, streaming granularity, capabilities, and other resource semantics.

One mandatory `consteval` operation scans the shared `ano::asset_schema`
namespace and produces the compiled resource language:

```cpp
consteval auto compile_resource_language();

inline constexpr auto resource_language =
    compile_resource_language();
```

`compile_resource_language` is the mandatory translation-phase boundary, not a
separate algorithm language. It inspects artifact types, fields, transform
functions, annotations, parameters, and return types, then invokes ordinary
`constexpr` functions using normal loops, local values, containers, ranges, and
graph algorithms. Those functions compute schemas, mappings, validation,
dependency closure, route selection, capability matrices, and retained products.
The actual semantic declarations are the sole source of structural truth.

The resource compiler is one ordinary C++ constant-evaluation program.
Templates parameterize a type or value only where an interface requires it;
they do not own resource algorithms or encode a parallel metaprogram. An
ordinary algorithm is `constexpr` whenever its operations permit constant
evaluation. A `consteval` entry point requires translation-time execution.
Expansion statements and splicing reify the result as direct ordinary C++.

This programming model follows [P2996R13](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2996r13.html),
[P3437R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3437r1.pdf),
[P3466R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3466r1.pdf),
and Herb Sutter's [P0707R5](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p0707r5.pdf):
compile-time computation is ordinary C++, reflection automates reading program
structure, and generation automates writing the direct C++ otherwise maintained
by hand.

### Generated artifact operations

For every artifact type, reflection, ordinary `constexpr` algorithms, and
expansion produce:

- Stable semantic type identity.
- Field inventory and wire-field descriptors.
- Schema fingerprint.
- Pointer-free and load-in-place classification.
- Endian conversion.
- Bounds and overflow validation.
- Dependency extraction.
- Streaming-atom inventory.
- Typed views.
- Retained debug field names.

Generated field operations contain direct member accesses. Runtime validation
does not interpret a reflection database or iterate a generic field registry.

### Generated transform operations

For every transform function, reflection, ordinary `constexpr` algorithms, and
expansion produce:

- Input and output representations.
- Executor domain.
- Invocation-signature validation.
- `noexcept` validation.
- Scratch requirements.
- Capability requirements.
- Offline and runtime legality.
- Determinism and cacheability.
- Direct specialized dispatch.

The compiled representation graph rejects cycles and invalid stage transitions,
computes reachable source-to-resident routes, derives platform capability
matrices, retains the Pareto-optimal transform paths, and emits the runtime
dispatch tables and required executor bridges. Runtime selects among retained
paths from hardware capabilities, current residency, and deadlines; it does not
perform general graph search during gameplay.

### Division of labor

| Facility | Responsibility |
|---|---|
| Reflection | Supplies types, fields, functions, annotations, and relationships as compile-time values |
| `consteval` | Requires the resource compiler and reflection-facing library boundary to execute during translation |
| `constexpr` | Owns ordinary reusable schema, graph, layout, hashing, validation, mapping, and pure parsing, decoding, and transformation algorithms |
| Expansion statements | Produce one concrete operation per reflected field or function |
| Splicing | Performs direct member access and direct transform invocation |
| Templates | Parameterize types or values only where an interface requires it; never encode resource algorithms or a parallel metaprogram |
| `define_static_*` | Retains only final tables, strings, and descriptors |
| `std::meta::exception` | Reports errors at the responsible declaration |
| Ordinary runtime functions | Perform irreducible I/O, allocation, foreign-library calls, and owner-device effects through signatures selected and bound by the compile-time program |

### Compile-time and instance graphs

The type graph is closed and small enough for constant evaluation. It contains
artifact representations and their transforms. The asset-instance graph contains
project content and belongs to the cooker and manifest.

Adding an asset does not recompile the engine. Changing the meaning of an
artifact or transform does.

The cooker consumes compiler-produced schema descriptions and direct typed
operations. It emits the content-addressed artifact DAG, dependency graph,
manifest, and physical packs.

### Module boundary

Each module exposes its artifact and transform declarations through its public
compile-time interface:

```text
anoptic_render.h    scene, mesh, and texture representations
anoptic_audio.h     PCM, bank, and impulse representations
anoptic_text.h      font and font-atlas representations
anoptic_resources.h common resource language
```

A resource-universe translation unit includes those public interfaces and
reflects the shared schema namespace. It never includes another module's private
`src/` headers.

## ECS boundary

The ECS graph and resource graph remain separate:

```text
ECS graph                              Resource graph

Entity                                 AssetId
  `- MeshRenderer                        `- semantic artifact
      |- AssetRef<Mesh>                     |- dependencies
      `- AssetRef<Material>                 |- representations
                                               `- resident bindings
```

The bridge is a typed stable reference:

```cpp
template<class SemanticAsset>
struct AssetRef final {
    AssetId id;
};
```

Persistent ECS components store `AssetRef<T>`. They do not store file paths,
resource pointers, content digests, backend handles, cache-entry addresses, or
snapshot-local pointers.

The resource system owns immutable shared facts such as mesh geometry, skeletons,
animation clips, audio samples, material definitions, prefab blueprints, and
navigation data. ECS owns mutable simulation state such as transforms, poses,
playback cursors, instance overrides, spawned entities, and agent state.

### Demand extraction

Reflection recognizes `AssetRef<T>` fields directly. Optional field annotations
state representation and quality policy. Generated component-specific functions
emit structural residency goals.

Structural extraction runs when:

- A component is inserted.
- An asset-reference field changes.
- A world cell is instantiated.
- A prefab is expanded.

It does not scan every entity every frame. Systems with behavioral information
produce dynamic refinement goals: rendering supplies visibility and projected
size, audio supplies proximity, world streaming supplies cell demand, and UI
supplies atlas and font demand.

Reflection discovers structural references. Explicit systems calculate
behavioral demand.

## Residency epochs

The runtime product is a structurally shared residency epoch containing immutable
binding tables:

```text
Residency epoch
|- artifact bindings
|- CPU representation slots
|- GPU representation slots
|- audio representation slots
`- manifest root
```

An `AssetId` indexes a dense typed binding table:

```cpp
GpuMeshSlot slot = epoch.mesh_bindings[renderer.mesh.id.index()];
```

Hot paths therefore use predictable array lookups rather than strings, hash-map
lookups, ownership graphs, or reflection metadata.

An ephemeral derived component can cache resolved bindings together with the
epoch that produced them:

```cpp
struct ResidentRenderable final {
    ResidencyEpochId epoch;
    GpuMeshSlot mesh;
    GpuMaterialSlot material;
};
```

When bindings change, the resource system publishes a new epoch and a compact
changed-ID list. Only affected derived bindings refresh. Persistent components
remain unchanged.

### Commit groups

Residency goals belong to commit groups. Each group has a hard floor and optional
refinements. The floor publishes atomically when ready; refinements join later
epochs without delaying the floor.

Commit groups include world cells, prefab spawn batches, UI screens, audio scenes,
cutscenes, and renderer pipeline families.

### World-cell instantiation

A streamed world cell is an immutable asset containing archetype descriptions,
component columns, local entity references, stable `AssetRef<T>` fields, and a
dependency floor.

World-cell publication follows this sequence:

1. Spatial streaming emits the cell's residency goal.
2. The resource runtime materializes its minimum artifact set.
3. The commit group becomes ready.
4. ECS bulk-instantiates the archetype columns.
5. ECS remaps local entity indices to live entity IDs.
6. Asset references remain stable manifest IDs.
7. The coordinated ECS and resource state becomes visible.

Reflection generates component-column schemas, load-in-place validation,
bulk-copy operations, local-reference patching, asset-reference extraction, and
persistent-field handling.

## Publication boundary

A rendered frame consumes a consistent ECS and resource pair:

```cpp
struct FrameWorld final {
    EcsEpoch ecs;
    ResidencyEpoch resources;
};
```

The logic/render bridge publishes this pair at a synchronization point. Commands
that refer to resources carry stable asset IDs or resident slots tagged with the
residency epoch that produced them.

Transform declarations assign execution ownership:

```text
I/O executor           reads extents
Worker executor        validates, decodes, and conditions
Render-master executor uploads GPU representations
Audio-master executor  adopts audio banks
```

The scheduler submits work through each module's public bridge. The renderer owns
Vulkan objects. The audio module owns audio objects. Residency epochs contain only
opaque slots issued by those owners. I/O workers never invoke owner APIs directly.

## Unloading

Entity deletion removes the entity's demand contribution. It does not invoke an
`unload()` operation.

After reconciliation:

- Assets demanded elsewhere remain resident.
- Assets with no reachable demand enter hysteresis.
- Retired CPU representations wait for read epochs.
- Retired GPU representations wait for fences.
- Whole world-cell arenas retire together where appropriate.

Demand aggregation can use counts or bitsets. These calculate desired state; they
do not confer resource ownership.

## Hot reload

A stable `AssetRef<T>` remains unchanged while a new manifest maps its `AssetId`
to replacement content. The runtime materializes the replacement, builds new
bindings, publishes a new residency epoch, refreshes affected derived bindings,
and retires the previous representation safely.

Mutable ECS state remains intact. Semantic migration of mutable state belongs to
the responsible ECS system rather than the resource manager.

## Persistence

Persistent ECS state stores stable `AssetId` values, semantic component values,
and entity-reference identities appropriate to the save domain.

Persistent state does not store residency epoch IDs, dense resident slots, GPU
handles, or artifact addresses. Replay and deterministic simulation can pin a
manifest root so stable asset IDs resolve to the same content versions.

## System shape

```text
C++26 compile-time layer
|- reflected artifact schemas
|- reflected transform functions
|- reflected AssetRef fields
|- consteval type and transform graph
|- generated validators and dependency extractors
`- generated direct typed operations

Offline content layer
|- source assets
|- cooker
|- instance dependency DAG
|- CAS
|- manifest
`- physical packs

Runtime resource layer
|- residency goals
|- commit groups
|- materialization scheduler
|- execution-domain bridges
|- immutable binding tables
`- residency epochs

ECS layer
|- stable AssetRef<T> components
|- mutable simulation state
|- world-cell and prefab instantiation
|- dynamic quality-demand systems
`- transient resolved bindings
```

C++26 reflection compiles the closed language of artifact types,
transformations, and ECS asset references. The offline cooker evaluates that
language over content. ECS declares the desired semantic world. Runtime
incrementally materializes and atomically publishes the cheapest residency
epochs satisfying that world.
