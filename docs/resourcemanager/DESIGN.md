# Reflection-Compiled Residency Graph

## Introduction

### What a game-engine resource manager is

A game does not execute authoring files directly. Models, images, fonts, audio,
shaders, scenes, and other source assets must be interpreted, validated,
converted into engine-defined representations, packaged, loaded, realized by
the responsible device module, and eventually retired. The same logical asset
often has several representations: glTF source data, canonical mesh artifacts,
packed bytes, CPU views, and renderer-owned GPU buffers can all describe one
mesh at different points in its lifetime.

A resource manager is the engine subsystem that preserves the identity and
dependency relationships of those logical assets while moving their typed
representations through the offline and runtime pipeline.

| Term | Meaning |
|---|---|
| Source asset | Authoring input such as glTF/GLB, PNG/JPEG, OpenType, WAV, GLSL, an include file, or SPIR-V |
| Artifact | A canonical typed product produced by import, migration, cooking, or transformation |
| Semantic asset | The stable game-facing identity represented by `AssetRef<T>` independently of its current bytes or residency |
| Resident representation | A CPU view or an opaque renderer-, audio-, or text-owned slot ready for runtime use |
| Residency goal | A request for a semantic asset at a representation, quality, and priority required by the current world |
| Residency epoch | An immutable publication mapping stable asset identities to one mutually consistent generation of resident bindings |

The resource manager therefore spans two connected domains:

- Offline content processing imports source formats, validates and migrates
  typed data, incrementally cooks dependency graphs, stores exact products by
  content identity, and produces manifests and shipping packs.
- Runtime residency accepts demand from ECS and behavioral systems, reads and
  transforms artifacts, asks owner modules to realize device objects, publishes
  immutable bindings, and retires replaced or unneeded representations safely.

It is not merely a file cache or asynchronous loader. A complete resource
manager also answers which transformations are legal, which dependencies form
an atomic floor, which representation satisfies a platform, when a replacement
generation may become visible, and when the previous generation is safe to
destroy. It preserves stable semantic references while content, packaging,
memory location, device objects, and quality levels change.

The resource manager does not absorb every subsystem that touches an asset.
ECS owns mutable simulation state. The renderer owns Vulkan objects. The audio
module owns mixer and device objects. The text module owns font realization.
The resource manager owns identity, typed transformation, demand reconciliation,
publication, and retirement across those boundaries.

### Existing solutions

Existing engines solve substantial parts of this problem with different centers
of gravity. The following systems are representative rather than exhaustive.

| System | Architectural center | Established strengths | Boundary changed by RCRG |
|---|---|---|---|
| [Unreal Asset Manager](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-management-in-unreal-engine) and [Asset Registry](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-registry-in-unreal-engine) | A global Asset Manager, asynchronously populated Asset Registry, primary and secondary asset IDs, bundles, configuration, and streamable handles around the `UObject` ecosystem | Mature discovery, cooking, chunking, auditing, asynchronous loading, and project customization | RCRG makes reflected artifact and transform declarations, rather than a global object plus configuration and registration, the structural authority for every pipeline stage. |
| [Unity Addressables](https://docs.unity3d.com/Packages/com.unity.addressables@1.21/manual/index.html) | Addresses and labels resolve through content catalogs to dependency-aware AssetBundles and asynchronous operation handles | Local or remote delivery, catalog updates, grouping, bundle construction, dependency download, and runtime loading | RCRG's manifest locates content, but its reflected type graph additionally compiles import, validation, migration, representation routes, owner realization, and ECS demand. |
| [Godot Resources](https://docs.godotengine.org/en/stable/tutorials/scripting/resources.html) and [import pipeline](https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/import_process.html) | Path-addressed, cached, reference-counted `Resource` objects with engine serialization and editor-managed source import | Tight editor integration, shared loaded objects, automatic serialization, custom resource types, and straightforward scene composition | RCRG separates stable semantic identity from paths and object lifetime, then publishes immutable cross-module residency epochs instead of exposing one mutable cached object generation. |
| [Bevy Assets](https://docs.rs/bevy/latest/bevy/asset/) | `AssetServer`, registered asset types and loaders, strongly typed `Assets<T>` collections, and `Handle<T>` references integrated with ECS | Typed handles, asynchronous loading, dependency tracking, asset events, and development hot reload | RCRG preserves the typed ECS reference but compiles the asset inventory and legal transformations from declarations; demand, liveness, replacement, and owner bindings resolve through immutable epochs rather than handle-owned residency. |
| [O3DE Asset Pipeline](https://docs.o3de.org/docs/user-guide/assets/pipeline/) | Asset Processor, registered Asset Builders, processing jobs, source and product assets, an Asset Cache, database, catalog, and runtime asset system | End-to-end source processing, platform products, dependency tracking, incremental jobs, caching, catalogs, and hot-reload notifications | RCRG unifies the builder, product schema, runtime representation, ECS reference, and owner route in one reflected language instead of coordinating separate builder descriptors, products, catalogs, and runtime registrations. |

These systems demonstrate that typed handles, asynchronous loading, import
pipelines, dependency catalogs, incremental products, packaging, and hot reload
are proven requirements. RCRG adopts those requirements. Its departure is where
the system's structural truth lives and when inconsistencies are rejected.

### Anoptic's reflection-compiled approach

RCRG treats the complete resource domain as a language embedded in ordinary
C++. Public module interfaces declare artifact types, transformation functions,
`AssetRef<T>` fields, and typed annotations. A resource-universe translation
unit reflects those declarations and closes one type graph spanning source
import, cooking, validation, migration, packing, loading, device realization,
ECS demand, residency, reload, and retirement.

C++26 reflection makes the declarations themselves available as compile-time
values. Typed annotations add semantic facts without creating a stringly typed
side channel. A mandatory `consteval` compiler invokes ordinary `constexpr`
algorithms over the reflected graph to compute schemas, fingerprints,
dependencies, migration closure, legal representation routes, capability
matrices, executor ownership, and retained dispatch. Expansion statements and
splicing then emit direct member operations, exact types, and direct transform
calls. Only final immutable tables and diagnostics remain when runtime needs
them.

This produces one structural authority instead of a stack of synchronized
descriptions:

```text
public C++ declarations + typed annotations
                    |
                    v
      reflected compile-time type graph
                    |
       +------------+-------------+
       |            |             |
       v            v             v
 offline typed   runtime typed   ECS demand
 operations      routes/bindings extractors
       |            |             |
       +------------+-------------+
                    |
                    v
       direct ordinary generated C++
```

Within the one-language, no-parallel-IDL, no-external-generator contract, this
combination is enabled specifically by C++26 reflection and generation. A field
or transform is declared once and all affected importers, validators, encoders,
migrations, dependency extractors, routes, owner bridges, and ECS demand
extractors follow from it. Missing coverage is a compile error at the responsible
declaration rather than a runtime fallback or a human synchronization task.

Reflection does not replace algorithms with metadata. Reflection supplies
program structure; ordinary `constexpr` C++ performs parsing, hashing, graph,
validation, decoding, and transformation work; `consteval` requires language
closure during translation; expansion and splicing reify the result as direct
code. Templates parameterize inherently typed interfaces but own no parallel
metaprogram. Runtime still performs instance-dependent I/O, allocation,
scheduling, and owner-device effects, using signatures and routes already
compiled from the reflected language.

The result is a resource manager whose offline and runtime halves share one
typed definition of every resource, whose hot paths contain no reflection
interpreter, and whose unsupported structural combinations cannot enter a
successful build.

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

The complete data flow is:

```text
 Public reflected declarations
 (artifacts, transforms, AssetRef fields, typed annotations)
                         |
                         v
 C++26 resource compiler: consteval boundary + constexpr algorithms
                         |
             +-----------+-------------+
             |                         |
             v                         v
 schemas, fingerprints,          legal typed routes,
 validators, migrations          direct generated calls
             |                         |
             +-----------+-------------+
                         |
                         v
 source bytes -> import -> canonical artifacts -> cook/CAS -> manifest + packs
                                                            |
 ECS AssetRef<T> + behavioral demand ------------------------+
                                                            |
                                                            v
                                            residency reconciliation
                                                            |
                                  +-------------------------+------------------+
                                  |                         |                  |
                                  v                         v                  v
                         renderer bridge             audio bridge         text bridge
                         (GPU objects)               (audio objects)      (font objects)
                                  |                         |                  |
                                  +-------------------------+------------------+
                                                            |
                                                            v
                                           immutable residency epoch
                                                            |
                                                            v
                                             ECS and frame consumers
```

The arrows carry typed values or immutable data. They do not cross through a
runtime reflection interpreter, an untyped service locator, or backend handles
owned by the resource manager.

## Resource language

Artifact types and transformation functions are the declarations reflected by
the resource compiler. An empty `Artifact` annotation marks a canonical type;
its reflected qualified identifier, member identifiers, declaration order, and
exact member types define its identity and schema. Transform annotations carry
only irreducible execution semantics such as executor, determinism, streaming
granularity, and capabilities. Their input and output types come from the
reflected function signature.

One mandatory `consteval` operation scans and closes the supplied resource
namespace:

```cpp
consteval bool compile_resource_language(std::meta::info schema_namespace);

static_assert(compile_resource_language(^^ano::asset_schema));
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

- Semantic type identity derived from the reflected qualified declaration.
- Canonical declaration-order field traversal.
- Schema fingerprint.
- Pointer-free and load-in-place classification.
- Endian conversion.
- Bounds and overflow validation.
- Dependency extraction.
- Streaming-atom inventory.
- Typed views.
- Field names in compile-time diagnostics.

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

### Reflection-specific closure

Under the one-language, no-parallel-IDL, no-external-generator contract, the
architecture depends on C++26 reflection for all of the following at once:

- Namespace reflection discovers the artifact and transform inventory exposed
  by independently owned public module extensions.
- Type, field, parameter, return-type, layout, and annotation reflection turns
  those declarations into the closed representation graph without a manual
  registry.
- Ordinary `constexpr` graph algorithms compute fingerprints, migrations,
  dependency closure, legal routes, capabilities, and retained paths over that
  reflected structure.
- The `consteval` boundary rejects duplicate identities, malformed schemas,
  illegal ownership edges, missing migrations, cycles, and incomplete routes
  during translation.
- Expansion statements and splicing emit direct field operations and exact
  transform calls from the computed result.
- Reflection over ECS components emits typed `AssetRef<T>` demand extractors
  from the same resource language.

Reflection is consumed while compiling the language. Runtime retains only the
immutable schemas, dispatch products, and diagnostic names it needs; it does not
retain a general reflection interpreter or reconstruct the type graph.

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

The resource API follows the engine's base-plus-extension convention. The base
header is C-compatible; the typed header and owner extensions are C++26
compile-time interfaces.

| Public header | Boundary |
|---|---|
| `anoptic_resources.h` | Stable IDs, content IDs, schema fingerprints, byte/range values, errors, and opaque runtime views |
| `anoptic_resources_typed.h` | Resource annotations, `AssetRef<T>`, relative wire types, the reflection compiler, and direct typed operations |
| `anoptic_resources_cook.h` | Import and cook requests, build profiles, diagnostics, incremental results, and CAS control |
| `anoptic_resources_pack.h` | Vendor-neutral manifests, packs, queries, and range reads |
| `anoptic_resources_runtime.h` | Residency goals, commit groups, manifest transactions, epoch acquisition, resolution, and retirement |
| `anoptic_resources_ecs.h` | Reflected component demand, native prefab/world-cell schemas, bulk instantiation, and coordinated publication |
| `anoptic_render_resources.h` | Render artifacts and transforms, opaque GPU slots, and the renderer-owned realization bridge |
| `anoptic_audio_resources.h` | Audio artifacts and transforms, opaque audio slots, streaming adoption, and the mixer-owned realization bridge |
| `anoptic_text_resources.h` | Font artifacts and transforms, opaque text slots, and the text-owned realization bridge |

Runtime C ABI functions begin with `ano_`. C++26 compile-time facilities live in
namespace `ano`; reflected semantic declarations live in the shared
`ano::asset_schema` namespace. C-compatible headers expose neither templates nor
`std::meta::info`. The base header includes no owner extension.

Each owner extension declares its artifact types and transformation functions.
The owning module implements device or library effects behind that public
interface and issues opaque resident slots. The resource runtime schedules those
operations but never owns Vulkan, audio, or font-library objects.

One resource-universe translation unit includes every public resource extension
before invoking `compile_resource_language(^^ano::asset_schema, profile)`. It is
the only place that closes the type graph. Public headers and that translation
unit never include another module's private `src/` headers. Private
implementations consume compiler-produced schemas and operations; they do not
redeclare artifact inventories, transform routes, or owner maps.

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
    AnoAssetId id;
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

## Sources and prior art

These sources define language capability, external data contracts, and the
systems patterns adapted by this architecture. They are inputs to the design,
not substitute specifications for Anoptic's public API.

### C++26 language and generation

| Source | Design consequence |
|---|---|
| [P2996R13: Reflection for C++26](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2996r13.html) | Reflections are compile-time values; member, function, layout, type, and name queries feed ordinary constant-evaluation programs and spliced direct operations. |
| [P3394R4: Annotations for Reflection](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3394r4.html) | Typed annotations carry resource semantics on the declarations they describe. |
| [P1306R5: Expansion Statements](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p1306r5.html) | Heterogeneous reflected ranges expand into ordinary statements without recursive template iteration. |
| [P3491R3: `define_static_*`](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3491r3.html) and [P3560R2: Error Handling in Reflection](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3560r2.html) | Final compile-time products have static storage, and malformed resource declarations fail at their responsible declarations. |
| [P3437R1: Reflect C++, generate C++](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3437r1.pdf), [P3466R1: design principles](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3466r1.pdf), and [P0707R5: metaclass functions](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p0707r5.pdf) | Reflection reads the program and generation writes direct C++; templates do not become a second algorithm language. |

### External contracts

| Authority | Resource boundary |
|---|---|
| [glTF 2.0](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html) | glTF/GLB structure, validation rules, buffer layout, scene semantics, materials, and dependencies |
| [PNG](https://www.w3.org/TR/png-3/), [JPEG](https://www.itu.int/rec/T-REC-T.81/en), and [OpenType](https://learn.microsoft.com/en-us/typography/opentype/spec/) | Image and font source schemas and their validation constraints |
| [RIFF](https://learn.microsoft.com/en-us/windows/win32/xaudio2/resource-interchange-file-format--riff-) and [WAVE format tags](https://www.rfc-editor.org/rfc/rfc2361) | WAV container structure and codec identification |
| [GLSL 4.60](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html) and [SPIR-V](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html) | Shader source dependencies, compiled-module structure, interfaces, and validation |
| [Ogg Opus](https://www.rfc-editor.org/rfc/rfc7845) and [Opus](https://www.rfc-editor.org/rfc/rfc6716) | Streaming container, codec, granule-position, pre-skip, and seeking semantics |
| [KTX 2.0](https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html) | Texture levels, data-format descriptors, supercompression, and Basis Universal payload carriage |
| [Vulkan object lifetime](https://docs.vulkan.org/spec/latest/chapters/fundamentals.html) and [synchronization](https://docs.vulkan.org/spec/latest/chapters/synchronization.html) | Renderer ownership, queue-domain execution, fence-safe publication, and deferred retirement |

### Systems prior art

| System | Adapted idea | Deliberate difference |
|---|---|---|
| [Bazel remote caching](https://bazel.build/remote/caching) | Separate action lookup from content-addressed object storage | Cook actions and artifacts are typed by reflected schemas and platform profiles. |
| [Nix content-addressed store objects](https://nix.dev/manual/nix/2.26/store/store-object/content-address) | Content identity incorporates an object's content and dependency references | Shipping manifests preserve stable semantic `AssetId` values separately from exact `ContentId` values. |
| [FlatBuffers internals](https://flatbuffers.dev/internals/) and [schema evolution](https://flatbuffers.dev/evolution/) | Relative offsets, direct in-buffer access, explicit field identity, and constrained schema evolution | Reflected C++ declarations are the schema and generate the operations directly; there is no parallel IDL or external source-generation step. |
| [Linux RCU](https://www.kernel.org/doc/html/latest/RCU/whatisRCU.html) | Readers consume an immutable publication while replaced state waits for safe reclamation | Residency epochs coordinate typed CPU, GPU, audio, text, manifest, and ECS bindings as commit groups. |
| [Bevy assets and handles](https://docs.rs/bevy/latest/bevy/asset/) | Typed handles separate entity/component references from asset storage | `AssetRef<T>` is a stable manifest identity; liveness and replacement belong to demand reconciliation and immutable epochs rather than handle reference counts. |

### Repository precedent

| Source | Established technique reused here |
|---|---|
| [`include/anogltf.h`](../../include/anogltf.h) | Typed annotations, stabilized reflection queries, expansion statements, spliced field access, generic structural parsing, and validation |
| [`src/render/gltf/ano_GltfParser_reflect.c`](../../src/render/gltf/ano_GltfParser_reflect.c) | Reflected structural projection from imported glTF records into renderer-owned material data |
| [`src/vulkan_backend/gpu_abi_schema.c`](../../src/vulkan_backend/gpu_abi_schema.c) | Reflection-driven GPU ABI inspection and validation |
| [`src/vulkan_backend/frame/schema/attachment_graph.h`](../../src/vulkan_backend/frame/schema/attachment_graph.h) and [`mesh_rows.h`](../../src/vulkan_backend/frame/schema/mesh_rows.h) | Reflected render-graph contracts and direct generated structural projection |
