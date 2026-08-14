# Resource Manager Feature Roadmap

## Contract

**Types and laws are compile-time. Asset instances and schedules are runtime.**

This document maps the architecture in [DESIGN.md](DESIGN.md) onto public module
features. `DESIGN.md` remains the architectural contract; this file defines the
public surface and implementation shape of each deliverable feature.

C++26 reflection compiles the closed language of addressable resource-cell
types, transformations, navigation, and ECS asset references. The cooker
evaluates that language over source assets and records the selected provenance
of every produced cell. The runtime reconciles demand and copy-on-write edits
into immutable residency epochs. Renderer, audio, text, and ECS modules retain
ownership of their own effects and objects.

The module has one structural authority: public reflected declarations and typed
annotations. It has no parallel typelist, traits registry, X-macro inventory,
artifact-kind switch forest, schema mirror, or external source generator.

The closed declaration universe is normalized once as compile-time witness `W`.
Each module compiler consumes a projection `W_M` and may reject it at the
`consteval` boundary:

```text
D --reify--> W --rho_M--> W_M --C_M--> CompileError_M + Plan_M
```

Independent plans pair; plans sharing an equality boundary use a pullback; an
arbitrary compatibility predicate uses a refined dependent product. API
adaptors, resource routes, owner effects, transport products, and refinement
proofs keep their distinct composition laws rather than being described as one
global category.

This roadmap is organized by feature. Every feature describes its public header,
compile-time product, ordinary implementation, and observable behavior.

## Public module surface

The resource API follows the existing base-plus-extension pattern used by
`anoptic_strings.h` and `anoptic_strings_utf.h`:

```text
include/
|- anoptic_resources.h
|- anoptic_resources_typed.h
|- anoptic_resources_revision.h
|- anoptic_resources_cook.h
|- anoptic_resources_pack.h
|- anoptic_resources_runtime.h
|- anoptic_resources_ecs.h
|- anoptic_render_resources.h
|- anoptic_audio_resources.h
`- anoptic_text_resources.h
```

The module builds its immutable storage on the engine-wide pair
`anoptic_memory.h` and `anoptic_memory_typed.h`. The former exposes lifetime
regions and sealed contiguous volumes; the latter reflects typed segment plans
into checked layouts. Resource headers consume this substrate without adding a
resource-specific arena API.

| Header | Public responsibility |
|---|---|
| `anoptic_resources.h` | C++26 stable IDs, content IDs, schema fingerprints, byte/range values, quality values, errors, and the compiled-language view |
| `anoptic_resources_typed.h` | C++26 cell and transform annotations, `AssetRef<T>`, reflected `up()`/`down()` navigation, relative wire types, reflection compiler, and typed generated operations |
| `anoptic_resources_revision.h` | Immutable revision ownership, identity, typed lookup, selected `(transform, output-port)` provenance, dependencies, and navigation views shared by live cooking and opened packs |
| `anoptic_resources_cook.h` | Source roots, import requests, build profiles, cooker configuration, diagnostics, incremental cook results, and CAS control |
| `anoptic_resources_pack.h` | Vendor-neutral manifest and pack construction, cell provenance, packed navigation, opening, querying, range reads, and root identity |
| `anoptic_resources_runtime.h` | Cell goals, commit groups, manager lifetime, copy-on-write transactions, epoch acquisition, changed IDs, typed navigation, and resolution |
| `anoptic_resources_ecs.h` | Reflected component integration, demand deltas, prefab/world-cell schemas, bulk instantiation, and coordinated ECS/resource publication |
| `anoptic_render_resources.h` | Render-owned artifact declarations, render transforms, opaque GPU slots, and the renderer residency connection |
| `anoptic_audio_resources.h` | Audio artifact declarations, audio transforms, opaque audio slots, streaming adoption, and the mixer residency connection |
| `anoptic_text_resources.h` | Font artifacts, font-bake transforms, text slots, and text-module residency connection |

`anoptic_resources.h` does not include a module-owner extension. The focused
headers include the base header and only the public module headers their
signatures require. No public header includes a private `src/` header.

Every resource header is a C++26 interface in namespace `ano`. Reflection may
generate an `extern "C"` projection only where a genuine foreign boundary needs
one; the engine API itself may expose `std::meta::info`, templates, and other
C++26 facilities while foreign backend types remain private.

### Base values

`anoptic_resources.h` defines distinct value types rather than interchangeable
integers:

```cpp
typedef struct AnoAssetId { uint64_t value; } AnoAssetId;
typedef struct AnoContentId { uint8_t bytes[32]; } AnoContentId;
typedef struct AnoSchemaFingerprint { uint8_t bytes[32]; } AnoSchemaFingerprint;
typedef struct AnoManifestId { uint8_t bytes[32]; } AnoManifestId;
typedef struct AnoResidencyEpochId { uint64_t value; } AnoResidencyEpochId;
typedef struct AnoResourceTypeId { uint64_t value; } AnoResourceTypeId;

typedef struct AnoResourceBytes {
    const uint8_t *data;
    uint64_t size;
} AnoResourceBytes;
```

Their representations are fixed and portable in the actual header. An asset ID
does not change when its content changes. A content ID identifies exact bytes.
A resource type ID comes from a reflected cell declaration; it is not a
hand-maintained enum. Compiler display strings never contribute to persistent
identity. An asset ID remains stable across content replacement and may identify
a top-level, source, canonical, cooked, runtime-loadable, or owner-resident cell.

### Public function families

The extension headers expose narrow functions for their consumers rather than
one all-purpose manager interface:

| Header | Required public entry points |
|---|---|
| `anoptic_resources.h` | `ano_resource_error_string`, content identity, ID comparison and formatting |
| `anoptic_resources_revision.h` | Revision retain/release, identity, typed cell resolution, dependencies, selected provenance, and navigation views |
| `anoptic_resources_cook.h` | `ano_resource_cooker_create`, `ano_resource_cooker_destroy`, `ano_resource_import`, `ano_resource_cook`, `ano_resource_cooker_cancel` |
| `anoptic_resources_pack.h` | `ano_resource_manifest_open`, `ano_resource_manifest_close`, `ano_resource_manifest_find`, `ano_resource_pack_open`, `ano_resource_pack_read`, `ano_resource_pack_close` |
| `anoptic_resources_runtime.h` | `ano_resource_manager_create`, `ano_resource_manager_destroy`, `ano_resource_goal_set`, `ano_resource_goal_remove`, `ano_resource_reconcile`, transactional typed-cell edit and reload prepare/commit/abort, `ano_resource_epoch_acquire`, `ano_resource_epoch_resolve`, `ano_resource_epoch_release` |
| `anoptic_resources_ecs.h` | Demand-delta submission, reflected component registration, world-cell instantiation, and coordinated epoch publication |
| Owner extensions | Concrete connect/disconnect and owner-boundary functions for render, audio, and text; no generic user callback table |

The typed extension adds direct C++ operations such as
`resource_type_id<T>`, `schema_fingerprint<T>`, `validate<T>`, `encode<T>`,
`decode<T>`, `resolve<T>`, `up<T>`, and `down<T>`. Their structure is generated
from `T` and its reflected transforms; they do not forward into a runtime
reflection interpreter.

### Typed resource language

`anoptic_resources_typed.h` requires C++26 and includes `anoptic_meta.h`. It owns
the annotation vocabulary and reflection compiler, not the artifact inventory.
Artifact and transform declarations remain in their responsible module headers.

```cpp
namespace ano {

template<class Cell>
struct AssetRef final {
    AnoAssetId id;
};

struct Artifact final {};

struct Transform final {
    Executor executor;
    Streaming streaming;
    bool deterministic;
};

consteval bool compile_resource_language(ResourceWitness witness,
                                         BuildProfile profile);

} // namespace ano
```

Annotation value types are structural types. `Artifact` is only a marker. The
fully qualified reflected identifier supplies semantic type identity; member
identifiers, exact types, and declaration order supply the canonical schema.
Renaming or reordering a declaration changes its identity or fingerprint and
requires a recook. Explicit aliases and migrations are introduced only when a
shipped artifact format actually needs them.

A transform signature contributes a typed family of input ports and a typed
family of output ports:

```text
g : product(i : I_g) A_i -> product(j : J_g) B_j
```

The one-output case is the degenerate product with one output port. A transform
that parses one source into a scene, meshes, materials, textures, animations,
and lights executes once; each produced cell remains independently addressable
by its `(transform, output-port)` identity.

Every annotated cell is independently addressable through `AssetRef<T>`. A
top-level editor/game type is a conventional entry point rather than the only
public layer. Source bytes, parsed foreign representations, canonical values,
cooked values, runtime-loadable values, and opaque owner realizations use the
same typed handle model.

### Reflected focus and navigation

The reflection compiler treats transform signatures as typed arrows. For a
route factored around cell `X`:

```text
source --p--> X --q--> top-level asset
```

an `AssetRef<X>` is a pointer-free focus between `p` and `q`. Navigation moves
that focus without resolving the value:

- `down()` follows the actual producing transform toward its inputs.
- `up()` returns toward the particular result retained by the focused route.
- Resolution or editing is explicit and separate from navigation.

The result type of `down()` is compiled from every `(function, output-port)`
capable of producing the selected cell. Alternatives form a closed sum; the
input ports of one function form a product. Optional and repeated inputs
preserve their cardinality. Conceptually:

```haskell
f :: A -> H
g :: B -> H
k :: (A, B) -> H

data HProvenance
  = ViaF (Ref A)
  | ViaG (Ref B)
  | ViaK (Ref A) (Ref B)
```

If `k` also produces `J`, `H` and `J` record distinct output ports of the same
invocation and share its input-handle product. Several instance edges may name
the same immutable handle; no diagonal or copy operation is inferred for the
artifact value itself.

The cooker records one selected constructor and its input identities for each
produced `H`. Generated `down()` switches over that compact closed tag and
returns typed handles, not values. Each returned focused handle retains compact
context back to that `H`, so its ordinary `up()` is singular. A separate
reverse-dependency query enumerates multiple consumers of a globally shared
cell; it is not the meaning of focused `up()`.

Reflection and ordinary `constexpr` graph algorithms generate:

- The closed producer sum for every cell type.
- The exact input and output products for every transform.
- One stable producer identity for each `(transform, output-port)` pair.
- Direct typed construction and visitation of those alternatives.
- Packed forward, downward-provenance, and focused-up indices.
- Compile-time route composition and ambiguity diagnostics.
- Forward reachability masks used by copy-on-write invalidation.

The public handle and generated navigation products contain no resource pointer,
virtual interface, RTTI value, callable closure, or runtime reflection object.
The runtime representation is compact identity, type-selected dense columns,
small transform/output-port tags, packed spans, and direct generated calls.

### Owner extensions

Each owner extension reopens the shared schema namespace and declares its actual
types and functions:

```cpp
namespace ano::asset_schema {

struct [[=Artifact{}]] Mesh final {
    RelativeSpan<Vertex> vertices;
    RelativeSpan<uint32_t> indices;
    AssetRef<Material> material;
};

struct [[=Artifact{}]] GpuMesh final {
    AnoGpuMeshSlot slot;
};

struct UploadMeshOutputs final {
    GpuMesh mesh;
};

[[=Transform{Executor::render_master, Streaming::whole, true}]]
ano::Result<UploadMeshOutputs, RenderResourceError>
upload_mesh(const Mesh&, RenderResourceContext&) noexcept;

} // namespace ano::asset_schema
```

The reflected output record defines the output-port product. The same pattern
describes import functions, migrations, texture mip transforms, font bakes,
audio adoption, and world-cell materialization. The reflected function
declaration is the dispatch declaration. There is no second route registration
call.

### Resource-universe translation unit

Reflection can inspect only declarations visible at its point of use. Every
executable or tool therefore has one engine/resource-universe translation unit
which includes all public reflected extensions, normalizes the shared witness
once, and supplies its resource projection:

```cpp
#include <anoptic_resources_typed.h>
#include <anoptic_render_resources.h>
#include <anoptic_audio_resources.h>
#include <anoptic_text_resources.h>
#include <anoptic_resources_ecs.h>

constexpr auto declarations =
    ano::reify_engine_language(^^ano::asset_schema);
static_assert(ano::compile_resource_language(
    ano::project_resources(declarations), selected_profile));
```

The compiler validates every visible artifact and transform without retaining a
runtime type graph. A headless tool can inspect a render transform declaration
without retaining a call to its unavailable implementation. Other module
compilers consume their own projections of the same `declarations` witness.

## Compile-time execution model

The reflection compiler uses the complete implemented C++26 facility:

```text
GCC 16.1+
-std=gnu++26 -freflection -fno-exceptions -fno-rtti -nostdlib++
```

Reflection first normalizes the visible declaration universe into `W`.
Domain compilers receive projected witnesses rather than independently walking
the declarations. A compiler is partial—`W_M -> CompileError_M + Plan_M`—with
the error branch reported as a translation-time diagnostic. Product pairing is
used only for independent plans; shared constraints compile as pullbacks or
proof-carrying compatibility products.

| Facility | Use in the resource module |
|---|---|
| Reflection | Discovers namespace members, artifact fields, enums, functions, parameters, return types, annotations, access, layout, and source locations |
| `consteval` | Defines the mandatory language-compilation boundary and rejects incomplete or illegal declarations during translation |
| Ordinary `constexpr` | Implements schema compilation, checked arithmetic, canonical hashing, graph algorithms, layout, parsing, validation, mapping, route pruning, and pure transforms |
| `std::define_static_array` | Stabilizes reflection query results before expansion and retains final immutable tables |
| Expansion statements | Emits one concrete operation for each reflected field, enumerator, or transform |
| Splicing | Produces direct member access, exact types, constants, and direct transform calls |
| `define_static_string`, `define_static_object` | Retains only shipping names, descriptors, capability matrices, and dispatch products |
| `define_aggregate` | Generates a computed aggregate only when a declared wire or column shape genuinely requires one; it is not treated as arbitrary body synthesis |
| `std::meta::exception` and `static_assert` | Attach structural failures to the responsible declaration |
| Templates | Parameterize inherently typed interfaces such as `AssetRef<T>`, typed views, and operations over a selected type; they do not encode a parallel metaprogram |

Reflection query ranges are stabilized with `std::define_static_array` before
`template for`, as required by GCC 16.1. The implementation does not depend on
arbitrary token injection, arbitrary generated function bodies, runtime
`std::meta::info`, or facilities absent from GCC 16.1.

An expanded field operation has direct ordinary C++ semantics:

```cpp
template for (constexpr std::meta::info field : fields) {
    using FieldType = [:std::meta::type_of(field):];
    validate_field<FieldType>(value.[:field:], cursor, limits);
}
```

The generated runtime code contains concrete accesses and calls. It does not
walk a reflection database or interpret a generic field descriptor for every
asset.

Pure algorithms are `constexpr` even when most calls occur at runtime. This
allows the same checked reader, canonical encoder, graph algorithm, or format
validator to run over compile-time fixtures and runtime bytes. I/O, allocation,
foreign-library calls, and device effects remain ordinary runtime operations;
reflection still compiles their eligibility, signatures, selection, and typed
adaptation.

### What C++26 reflection uniquely buys RCRG

**RCRG makes the C++ compiler itself the resource-language compiler.**

```text
declarations + typed annotations
        -> reflection values
        -> ordinary constexpr compilation
        -> consteval validation
        -> expansion + splicing
        -> direct typed runtime code
```

#### C++26 as the embedded resource compiler

| Compiler role | C++26 facility | RCRG meaning |
|---|---|---|
| Source language | C++ types, functions, enums and `[[=typed annotations]]` | Artifact schemas, transforms, foreign mappings and ownership policies |
| AST | `std::meta::info` | Declarations become compile-time values |
| Discovery | `members_of`, `nonstatic_data_members_of`, `enumerators_of`, `parameters_of` | The complete resource universe is discovered without registration |
| Semantic analysis | Ordinary `constexpr` algorithms | Graph closure, fingerprints, migrations, validation, capability analysis and route pruning |
| Mandatory compiler phase | `consteval` | An invalid resource language makes the engine fail compilation |
| Code generation | Expansion statements and splicing | Computed structure becomes direct field accesses, exact types and direct calls |
| Static-data emission | `define_static_*`, `define_aggregate` | Only final tables, strings, layouts and descriptors survive |
| Diagnostics | `source_location_of`, `std::meta::exception` | Errors point back to the declaration responsible |

“Unique” means unique under the constraint that C++ declarations remain the
sole authority, without a parallel IDL, external generator, macro inventory or
handwritten registry.

| Unique purchase | Concrete RCRG result | What would otherwise be required |
|---|---|---|
| Closed-world discovery from independently owned modules | The resource-universe translation unit discovers every annotated render, audio, text and ECS artifact and transform | Central registration file, linker tricks, typelist or plugin registry |
| One structural authority | A field declaration simultaneously governs wire encoding, validation, hashing, dependencies, migration and diagnostics | Separate structs, serializer schemas, validators and field tables |
| Typed semantic metadata | Executor, streaming policy, foreign names and backend codes remain typed values attached only where structure cannot express them | String attributes, side tables, magic enums or external schema files |
| Functions become graph edges | Input and output port products define representations; annotations define execution and capability policy | Callback registration, type-erased nodes and duplicated route declarations |
| Function composition becomes addressable | Every intermediate result is a typed focus; reflected paths compile into `AssetRef<T>` navigation without storing closures | Runtime object nodes, hand-authored handle adapters, or an external route generator |
| Producer choice becomes an algebraic type | Alternative `(function, output-port)` pairs form a generated sum and each function's inputs form its required product | Untyped dependency bags, kind switches, or lossy “previous node” links |
| Focused return is statically known | A downward handle retains a compact route witness, making ordinary `up()` singular while shared-consumer enumeration remains explicit | Parent pointers, virtual navigation, or ambiguous global reverse lookup |
| Whole-language graph compilation | `constexpr` graph algorithms compute reachability, dependency closure, cycles, legal stages, capabilities and Pareto-optimal routes | Runtime graph search or a separate build-time generator |
| Compile-time exhaustiveness | Missing routes, migrations, mappings, executor bridges or backend annotations fail the build | Runtime “unsupported type” paths and incomplete switch defaults |
| Generated structural operations | Parsing, encoding, endian conversion, validation, dependency extraction and projection directly access reflected fields | Per-type serializers, visitors and duplicated traversal code |
| Direct invocation | A selected reflected transform is emitted as `[:function:](...)` with its exact signature | Function-pointer tables, virtual interfaces, `void*` contexts or type erasure |
| Foreign-schema compilation | glTF fields, JSON names, Vulkan mappings, shader interfaces and multimedia codes compile from typed declarations | Thousands of lines of mapping switches and mirror tables |
| Layout-aware generation | Size, alignment, offsets, bit fields and representation properties participate in validation and code generation | Mirrored ABI descriptions and scattered handwritten `static_assert`s |
| Canonical identities from semantics | Reflected qualified identifiers, field names, declaration order and exact types produce deterministic type IDs, schema fingerprints and action-key inputs | Manually synchronized IDs, versions and hashing recipes |
| Migration closure | Reflected artifact aliases and migration functions form a compile-checked migration graph once a shipped schema requires compatibility | Handwritten version dispatch and undiscovered upgrade gaps |
| Typed ECS demand | Reflection finds `AssetRef<T>` fields and emits component-specific demand-delta functions | Per-component visitors or continuous generic ECS scans |
| Owner-safe realization | Reflected signatures bind legal routes to renderer, audio and text owner bridges while opaque slots preserve ownership | Generic service locators, backend callbacks or resource-manager-owned device objects |
| Cell-level incremental publication | Reflected forward closure drives COW invalidation and direct recomputation from any edited source, intermediate, or resident cell | Whole-graph copies, bespoke reload paths, or mutable mixed generations |
| Runtime reflection disappears | Runtime executes ordinary field accesses, array lookups and direct calls | RTTI, metadata interpretation, registry traversal and dynamic dispatch |
| Structural edits propagate automatically | Adding a field or transform recompiles every affected product; omissions become compiler errors | A human audit across import, cook, pack, runtime, ECS and backend layers |
| Scale follows semantics rather than formats times phases | One declaration feeds every stage, keeping the full implementation plausibly below 8,000 lines | Reimplementing every asset kind independently at every pipeline stage |

#### The crucial non-distinction

The split is not “reflection versus real algorithms.”

| Concern | Correct owner |
|---|---|
| Discovering structure, signatures and relationships | Reflection |
| Computing schemas, graphs, hashes, mappings, parsers and transforms | Ordinary `constexpr` functions |
| Requiring the computation during translation | `consteval` |
| Turning its result into direct code | Expansion and splicing |
| Performing I/O, allocation or device effects | Small ordinary runtime functions selected and bound by the compile-time program |

Compression math, decoding math, graph algorithms and canonical encoding remain
ordinary C++ algorithms, and are `constexpr` whenever their operations permit
constant evaluation. Reflection supplies their structure, validates their
applicability, selects them, specializes their structural portions and emits
their direct invocation.

Templates own none of this architecture. They appear only where the resulting
interface is inherently parameterized, such as `AssetRef<T>`.

An external compiler could imitate individual outputs. It cannot preserve the
defining combination: the actual C++ declarations are simultaneously the
schema, graph, validation input and generated-code source, with failures
reported by the C++ compiler and no second language to drift.

### Implementation homes

| Feature implementation | Repository home |
|---|---|
| Checked regions, reflected volume layouts, and scratch storage | `include/anoptic_memory*.h` and `src/memory/` |
| Reflection compiler and generated typed operations | Header-only in `anoptic_resources_typed.h`; instantiated by `src/resources/resource_universe.c` |
| Checked bytes, canonical identity, and artifact validation | `src/resources/artifact/` |
| General import front end and portable source handling | `src/resources/import/`, with format declarations and effects in their owning modules |
| Instance DAG, action keys, and CAS | `src/resources/cook/` |
| Manifest and pack encoding/reading | `src/resources/pack/` |
| Goals, scheduling, epochs, reload, and retirement | `src/resources/runtime/` |
| ECS demand and native world cells | `src/resources/ecs/` |
| GPU realization | `src/vulkan_backend/resources/` |
| Audio realization and streaming | `src/audio/resources/` |
| Font realization and bake ownership | `src/text/resources/` |

Private directories implement public signatures. They do not redeclare the
artifact inventory, format map, transform routes, or owner map found by the
reflection compiler.

## Implementation budget

The complete production implementation described by this roadmap fits within
8,000 source lines of code. The ceiling covers the current-asset vertical,
native spatial ECS scenes and world cells, Ogg Opus streaming, and KTX2/Basis
Universal textures.

The count uses `cloc` code lines and includes:

- Every `anoptic_resources*.h` public header.
- The render, audio, and text resource-extension headers.
- `src/resources/` and each owner module's resource-specific implementation.
- Cooker and pack tools implemented solely for this resource system.
- Any resource-manager-only code moved elsewhere in the tree.

The count excludes blank lines, comments, documentation, tests, fuzz and
malformed-input corpora, expected outputs, source assets, compiler-generated
products, build products, vendored dependencies, and unchanged pre-existing
general-purpose renderer, audio, text, ECS, or platform code.

Cross-cutting code counts when the resource manager is its only consumer,
regardless of directory. A ceiling breach is an architecture defect: the design
must consolidate duplicated structure, generate direct operations from reflected
declarations, or remove an unnecessary abstraction before the feature is
complete. A new format does not silently raise the ceiling.

## Feature: immutable memory regions and reflected volumes

**Public surface:** `anoptic_memory.h` and `anoptic_memory_typed.h`.

The shared allocation mechanism is independent of each module's allocation
policy. A `MemoryRegion` owns one mimalloc lifetime heap. Destruction winks out
all allocations after users have quiesced; reset replaces the heap and then
destroys the retired heap. Regions provide scratch and other scoped allocations,
not individual object ownership.

A `MemoryVolume` owns an exclusive region and one explicitly contiguous,
aligned payload. Construction receives a checked `MemoryLayoutCursor`, permits
bounded `std::span` writes through trivial `{offset, size}` reservations, seals
once, and then exposes only bounded immutable spans. One owner-level reference
count retains the complete volume. No payload allocation carries its own
reference count, free-list entry, or teardown callback.

The C++26 extension declares layouts as ordinary records of
`MemorySegment<T, Alignment>` fields. `reflect_memory_plan` inspects the actual
members, rejects any non-segment field at translation, and `memory_layout`
performs the checked aligned prefix sum with runtime element counts:

```cpp
struct RevisionPlan final {
    MemorySegment<AnoResourceManifestEntry> entries;
    MemorySegment<RevisionStorage> storage;
    MemorySegment<Dependency> dependencies;
    MemorySegment<MemoryVolume *> retainedVolumes;
};
```

The resource policy applies this mechanism as follows:

1. Exact source snapshots occupy sealed volumes.
2. A cook groups changed artifacts by reflected type and commit group into
   bounded volumes, isolates oversized artifacts, assigns aligned reservations
   once, fills disjoint ranges in parallel, and seals each volume.
3. Unchanged revision items retain prior volumes once per distinct volume.
4. Opened shipping packs keep copied bytes, decoded manifest columns, and the
   minimal revision storage row in one sealed volume. Physical placements are
   authenticated during opening and then discarded.
5. Residency epochs keep only dense bindings, changed IDs, and retained-owner
   rows in a sealed volume. They retain revision metadata plus the artifact
   volumes in the demanded closure, not the complete cooked revision.
6. Persistent executor workers reset private scratch regions between batches.

The substrate contains no asset IDs, schemas, hashes, DAG nodes, packs, epochs,
Vulkan memory types, geometry holes, frame quarantine, size classes, or general
free lists. Those remain module policies. GPU heaps, audio pools, render-slot
quarantine, and geometry allocation therefore do not inherit a universal
allocator hierarchy. A size-class multipool is introduced only for a measured
independent-lifetime workload.

## Feature: addressable cell identities and canonical artifacts

**Public surface:** `anoptic_resources.h`, `anoptic_resources_typed.h`, and the
owner extension declaring each artifact.

An artifact declaration is the sole schema authority for one addressable cell
type. A cell address is the pair of stable `AnoAssetId` and reflected type `T`
carried by `AssetRef<T>`. Consecutive representations of one logical lane may
share the asset ID and differ by `T`; decomposed or repeated resources use their
own stable asset IDs. Reflection compiles:

- Semantic type IDs from fully qualified reflected identifiers.
- Canonical wire fields from reflected declaration order.
- Schema fingerprints from canonical semantic tokens.
- Direct typed encode, decode, endian, validate, and dependency operations.
- Dense column identity and pointer-free handle lookup for every cell type.
- Closed `(transform, output-port)` producer alternatives and exact transform
  input/output products.
- Direct focused `up()` and algebraic `down()` navigation.
- Pointer-free and load-in-place classification.
- Relative-span and relative-reference bounds checks.
- Streaming-atom extraction.
- Retained field names for diagnostics only.

The `consteval` compiler rejects type-identity collisions, unsupported field
types, illegal pointers in portable artifacts, invalid alignment, malformed
transform signatures, non-`noexcept` transforms, ambiguous focused routes, and
unrepresentable producer cardinality. `AssetRef<T>` itself marks a dependency;
there is no parallel field policy to synchronize.

Ordinary `constexpr` code owns checked addition, multiplication, ranges, endian
loads/stores, canonical byte framing, and hashing. These functions have
compile-time boundary tests and runtime known-answer tests. Content hashing uses
a standard algorithm with published test vectors; no private checksum becomes a
shipping identity primitive.

For valid typed values and canonical byte strings, `decode(encode(t)) = t` and
`encode(decode(b)) = b`. Malformed, noncanonical, out-of-bounds, and
schema-incompatible byte strings produce typed errors; the codec is not an
isomorphism between an artifact type and all possible bytes.

Portable artifacts contain no process pointers, allocator state, Vulkan
handles, audio backend handles, or compiler-dependent type spellings. A raw
file cell may retain a normalized source identity and bytes because that is its
declared semantics; higher cells never acquire paths accidentally.

## Feature: reflected source import

**Public surface:** import requests and diagnostics in
`anoptic_resources_cook.h`; format declarations in the responsible owner
extensions.

Importer functions carry typed annotations describing source signatures,
extensions, dependency policy, output-port products, determinism, and limits. The
resource compiler reflects those functions and produces:

- A collision-checked signature and extension classifier.
- Direct importer dispatch.
- Typed output-port and dependency adaptation.
- Source-format diagnostics.
- Compile-time rejection of duplicate or ambiguous import claims.

The generalized importer covers:

| Source | Reflected structural authority | Canonical result and behavior |
|---|---|---|
| glTF/GLB | `anogltf` records, fields, indices, enums, extensions, JSON names, and requiredness | Scene, mesh, material, image, light, animation, and dependency artifacts |
| PNG | Chunk declarations, signatures, colour records, required order, and decoder binding | Validated image pixels, colour space, dimensions, and provenance |
| JPEG | Marker and segment declarations, component records, limits, and decoder binding | Validated image pixels, colour space, dimensions, and provenance |
| OpenType | Table-directory, tag, offset, face, cmap, metric, and outline declarations | Validated font source, face inventory, and table dependencies |
| WAV | RIFF chunk declarations, format fields, sample representations, and audio conversion bindings | Canonical sample data and stream metadata |
| GLSL/includes | Shader-stage declarations, include form, source policy, and compiler binding | Canonical shader source with normalized, sandboxed include dependencies |
| SPIR-V | Header, instruction, opcode, decoration, storage-class, stage, and interface declarations | Validated shader module with stage and interface metadata |

glTF JSON structure continues to use `anogltf` reflection, expansion, and
spliced field access. Canonical scene conversion uses typed annotations on the
actual glTF and artifact declarations rather than a handwritten parallel
property registry. Accessor decoding, node closure, index rebasing, material
extensions, and dependency extraction are ordinary algorithms, `constexpr`
where their operations permit it.

The same reflective pattern applies to the other foreign formats. Typed
declarations and annotations describe their structural families; `consteval`
compiles required-field masks, tag/marker dispatch, layout checks, legal enum
maps, and direct decoder adaptation. `constexpr` parsers perform bounded cursor
work using those products. Adding a chunk, marker, table, opcode family, sample
format, or shader stage updates its declaration and forces every affected map to
remain exhaustive at compile time.

Foreign decoders may perform irreducible runtime work. Their concrete calls do
not own format selection, schema identity, dependency structure, or output
adaptation; those remain compiled from reflected declarations.

Source hints never override bytes silently. A conflicting extension, magic
signature, declared MIME type, or requested output is either resolved by one
unambiguous reflected rule or rejected.

Raw-file cells, parsed foreign representations, and importer results remain
typed addressable cells. A build profile may omit an otherwise unreachable
source cell from a shipping pack, but any cell referenced by code, content, an
editor operation, or an explicit runtime goal is retained or has a retained
legal materialization route.

## Feature: incremental cooking and content-addressed storage

**Public surface:** `anoptic_resources_cook.h`.

The cooker evaluates asset instances. It does not rebuild the C++ type graph and
does not require engine recompilation when an asset is added.

`AnoResourceCooker` is a long-lived evaluator. Stable-address source and
artifact-instance records live in `ano::hive`; each artifact node retains its
current successful action key while the immutable revision owns its result.
One persistent executor belongs to
the cooker, its caller participates in every batch, and background workers reset
private scratch regions between batches. No cook or reload creates another
worker group.

For each source asset the cooker:

- Imports source bytes through the compiled importer table.
- Builds the instance dependency DAG.
- Selects a legal compiled transform route for the build profile.
- Records the selected producer constructor, output port, and typed input
  identities for every output cell.
- Computes an action key from transform identity, exact inputs, settings,
  capabilities, and schema fingerprints.
- Reuses a verified CAS result or executes the direct typed transform.
- Stores canonical outputs by content ID.
- Invalidates only transitive dependants whose action inputs changed.

Source acquisition opens each actual input once, reads the exact byte count into
one immutable snapshot, hashes that snapshot, and compares file metadata before
and after acquisition. External glTF buffers and images are explicit selected
inputs read and hashed by the persistent executor. Metadata only identifies a
candidate change; it never substitutes for content identity. Candidate source
snapshots and topology inventories publish transactionally with their cooked
revision. A failed cook preserves the previously published source/result pair,
but verified hashes, parsed structure, dependency discovery, and other reusable
private cache state may advance. Publication rollback is required; wholesale
cooker-state rollback is not.

Candidate revisions compile semantic metadata directly into manifest entries
plus minimal `{source, volume, reservation}` storage rows. Runtime instance
edges are materialized only when a dependency, navigation, or fan-out query
consumes them; the cooker does not construct a second reverse graph merely for
bookkeeping. Selected producers compare current action keys before execution.
Changed artifacts encode directly into disjoint final reservations.
Validation, schema lookup, SHA-256, and reflected dependency extraction complete
in the encoding worker phase. The opaque revision records successful validation,
so manager construction and explicit pack export do not repeat those scans. An
equal current action reuses its span; a rerun
whose output content is equal discards the candidate span and terminates that
branch's upward propagation.

Cooking returns an opaque immutable revision containing dense semantic metadata
and `{volume, offset, size}` artifact spans. It never serializes and reopens a
pack on the live path. Pack offsets are physical serialization data and do not
belong to the semantic revision.

SHA-256 dispatches to SHA-NI on supporting x86-64 processors and retains the
standard scalar path elsewhere. Canonical bulk movement uses tuned `memcpy`.
The image decoder already produces the final RGBA8 extent consumed by reflected
encoding, leaving no material pixel-conversion loop for a separate SIMD kernel.

The transform type graph and each cell's sum-of-products provenance shape are
computed by `consteval`; the instance DAG is runtime tool data. Runtime cooking
code chooses among the closed compiled alternatives but never searches
declarations or reconstructs the type graph.

The CAS verifies existing objects before reuse, writes through a temporary file,
publishes atomically, and repairs a corrupt object from a reproducible action.
Cancellation leaves no visible partial object.

Migration functions are ordinary reflected declarations. The compiler validates
their exact source fingerprint, destination fingerprint, signature, and route.
Only fingerprints emitted outside the current build receive migrations; the
module does not invent legacy schemas for unshipped data.

## Feature: manifests and shipping packs

**Public surface:** `anoptic_resources_pack.h`.

The manifest maps typed cell addresses to content, selected transform/output
ports, typed input identities, focused-up context, dependency edges, commit
groups, streaming atoms, schemas, and pack extents. Top-level asset IDs remain
entry points into this packed cell graph. The manifest contains no vendor device
object and no process-local address.

Reflection compiles the manifest record encoders and validators from their
declared wire types. Expansion emits direct field operations. `constexpr`
canonical encoding fixes byte order, integer widths, ordering, padding, and hash
framing.

Pack codec functions are reflected transforms annotated with codec identity,
determinism, scratch, size bounds, and portability. `consteval` compilation
produces the codec ID map and direct encode/decode dispatch. Compression does not
introduce a handwritten codec registry or a second artifact schema.

Pack construction:

- Orders equivalent inputs deterministically.
- Deduplicates identical content.
- Packs transform/output-port tags, input tuples/spans, and navigation indices
  deterministically.
- Preserves atom boundaries required for range reads.
- Applies declared portable compression without changing artifact identity.
- Authenticates manifest and pack contents.
- Emits the same bytes for the same language, inputs, and profile.

Pack opening validates the complete manifest before exposing roots. Every later
range read repeats the arithmetic and authentication needed for untrusted pack
bytes. No range becomes a pointer until its bounds, codec, unpacked size, and
content identity are valid.

An opened pack owns one sealed backing volume containing its authenticated bytes
and decoded dense columns. It exposes the same `AnoCookedRevision`
representation as a live cook; retaining that revision keeps the backing volume
alive after the pack handle closes.

## Feature: runtime demand and residency epochs

**Public surface:** `anoptic_resources_runtime.h`.

The runtime receives structural and behavioral goals expressed as stable asset
IDs, required reflected cell types, quality floors, refinements, importance,
and commit groups. Any addressable cell may be the goal: top-level model,
foreign source representation, raw pixels, PCM, canonical artifact, or owner
realization. Entity deletion removes a demand contribution; it does not call an
asset-owned `unload()` function.

The compile-time language retains the legal Pareto frontier of transforms for
each source-to-addressable-cell pair. Runtime selects from that small frontier
using actual capabilities, current representations, memory budgets, and
deadlines. It does not perform general type-graph search during gameplay.

Workers perform pack I/O, authentication, validation, decompression, decoding,
and CPU preparation. Cancellation is observed at bounded points. A worker never
calls Vulkan, mutates the mixer, publishes ECS state, or destroys an owner
object.

Epoch construction compares content and resident identities before publication.
Its volume contains only dense bindings, changed IDs, and retained-owner rows;
artifact payloads remain in their sealed cook or pack volumes. The epoch retains
the revision metadata owner and only the distinct artifact owners reachable from
current demand. Releasing the last reader therefore reclaims obsolete volumes
incrementally rather than pinning an unrelated revision generation.

The runtime product is an immutable, structurally shared residency epoch:

```text
ResidencyEpoch
|- manifest root
|- packed typed cell columns
|- selected transform/output-port tags and input spans
|- focused-up and forward-dependency indices
|- CPU and owner-resident slots
`- changed asset IDs
```

The reflected type selects a dense column and the stable asset ID selects its
row. Generated navigation reads compact tags and spans. Hot paths perform
predictable array lookups and direct calls. They do not use paths, hash maps,
cache nodes, reflection values, generic ownership graphs, or virtual dispatch.

A commit group publishes its complete hard floor atomically. Optional LODs,
mips, or secondary media refine later epochs. A failed refinement leaves the
last complete floor published.

## Feature: module-owned realization

**Public surface:** `anoptic_render_resources.h`,
`anoptic_audio_resources.h`, and `anoptic_text_resources.h`.

Transform annotations assign each effect to a concrete executor. The reflection
compiler validates the exact context parameter, typed input and output products,
result sum, `noexcept` contract, and module ownership. It groups transforms by
executor and generates the compact job description and direct dispatch required
by that module. There is no handwritten artifact-kind switch.

### Renderer ownership

The renderer alone creates and destroys Vulkan objects. Worker preparation ends
in portable upload data. Validated texture bytes are borrowed directly from the
epoch and copied exactly once into mapped Vulkan staging. Mesh scratch exists
only for genuine vertex/index transformation, and scene spans project directly
into final renderer descriptors. The render-master consumes generated render
jobs and returns opaque slots.

Render resource declarations annotate vertex fields, index representations,
material fields, texture formats, shader interfaces, buffer/image uses, memory
policy, and descriptor roles. Reflection inspects exact field types, offsets,
sizes, alignments, and annotations. `consteval` compilation validates the
portable-to-Vulkan mapping and generates layout descriptors, capability tables,
upload records, and direct invocation of the selected allocation/upload
function. This extends the renderer's existing reflected contract pattern; it
does not add a resource-owned Vulkan format or allocation registry.

A render safe point is a completed frame/fence serial, not an unconditional
boolean callback. New slots become visible only at a frame publication boundary.
Old slots retire only after every frame that could reference them has completed.
The resource module neither stores nor destroys a `VkBuffer`, `VkImage`, or
descriptor object.

### Audio ownership

The mixer alone adopts and retires audio objects. Structural changes become
visible at an audio block boundary. Decoding and stream filling occur outside
the callback. The callback allocates nothing, takes no mutex, performs no decode,
and acknowledges retirement before borrowed sample or stream memory is freed.

Portable audio artifacts preserve their declared channel layout. Any conversion
to the current mixer contract belongs to an explicit reflected audio transform;
the resource schema does not impose an unrelated global stereo restriction.
Reflection also compiles sample-format maps, adoption command shape, ownership,
and retirement acknowledgement from the audio declarations already responsible
for those facts.

### Text ownership

The text module owns FreeType lifetime, font faces, and CPU font-bake state. Font
source bytes remain alive for every face that borrows them. GPU text buffers and
atlases cross into renderer ownership through reflected render transforms and
retire at render fence boundaries.

Reflection compiles font-source fields, bake ranges, glyph-directory and point
layouts, persistent font dependencies, and the text-to-render ABI. Layout and
range mismatches fail translation or untrusted-byte validation before FreeType
or the renderer receives the value.

## Feature: ECS asset references and demand

**Public surface:** `anoptic_resources_ecs.h` and `AssetRef<T>` from
`anoptic_resources_typed.h`.

Persistent components store typed stable references:

```cpp
struct MeshRenderer final {
    [[=AssetUse{Representation::gpu_mesh, Quality::coarse}]]
    ano::AssetRef<ano::asset_schema::Mesh> mesh;

    ano::AssetRef<ano::asset_schema::Material> material;
};
```

Reflection recognizes `AssetRef<T>` structurally. `consteval` component
compilation validates the referenced cell type and optional `AssetUse`
policy. Expansion produces a component-specific function containing direct
field access and direct goal emission.

Components normally reference game-facing cells, but the type system imposes no
special top layer. A component or system with a deliberate reason may retain a
raw-pixel, PCM, foreign-representation, or other intermediate `AssetRef<T>` and
thereby contribute demand for exactly that cell.

Generated structural extraction runs when a component is inserted, removed, or
changed; when a prefab expands; and when a world cell instantiates. It does not
scan every entity every frame.

A full snapshot folds directly to current demand. Incremental demand, render,
audio, or UI deltas also consume the prior snapshot or explicit extractor state;
they are not functions of the current world alone. Demand is a finite join of
current consumers and may decrease when an entity or component is removed.
Only a specifically declared additive phase may assume monotonic increase.

Explicit systems supply behavioral refinements because they own the relevant
information:

- Rendering supplies visibility, projected size, mesh LOD, and texture mip need.
- Audio supplies distance, audibility, and prefetch horizon.
- Spatial streaming supplies cell demand.
- UI supplies active screen, font, and atlas demand.

Persistent ECS state never stores a content digest, resident slot, epoch ID,
backend handle, resource pointer, or file path. An ephemeral derived component
may cache typed slots together with the epoch that produced them.

## Feature: native spatial scenes and world cells

**Public surface:** `anoptic_resources_ecs.h`.

A world cell is a canonical immutable artifact containing archetype
descriptions, component columns, local entity references, stable asset
references, spatial bounds, and a dependency floor.

Reflection over the actual persistent component declarations compiles:

- Component and field schema fingerprints.
- Column layout and alignment.
- Direct column validation and bulk-copy operations.
- Local entity-reference patching.
- Asset-reference extraction.
- Persistent/default/ignored field behavior.
- Required schema migrations.

The compiler rejects pointer-bearing persistent fields, unsupported component
types, invalid local-reference types, and a world schema without a complete
dependency floor.

World-cell materialization keeps candidate columns private until their resource
commit group is complete. ECS bulk-instantiates the columns, remaps local entity
indices, and publishes the ECS epoch with the matching residency epoch:

```cpp
struct FrameWorld final {
    EcsEpoch ecs;
    AnoResidencyEpochId resources;
};
```

A rendered frame consumes one coherent pair. No frame observes new entities with
old resource bindings or new bindings attached to absent entities.

## Feature: transactional cell editing, reload, and retirement

**Public surface:** typed cell-edit, reload, epoch, and changed-ID functions in
`anoptic_resources_runtime.h`.

An edit may target any `AssetRef<T>`. The runtime starts a candidate epoch that
shares every column, page, provenance record, and owner binding with the current
epoch. It copies the touched cell or storage page, validates the replacement
through the generated operation for `T`, and uses the reflected forward
reachability mask plus packed instance edges to invalidate only the upward
consequence cone.

Demanded consequences are recomputed through direct compiled transforms.
Renderer, audio, and text realizations are prepared privately by their owning
modules. The complete candidate publishes atomically after its hard floors and
owner safe points succeed. Unchanged provenance, sibling branches, columns, and
pages remain structurally shared.

Ordered render, audio, text, and ECS lanes participating in that publication
share one committed-generation law: no consumer combines observations from
different committed transactions. An implementation may carry per-message
stamps, publish a lane epoch, use begin/commit markers, or reference one atomic
publication object. The law does not force the identifier into every queue
element.

Editing a middle cell does not imply a reverse transform. Replacing decoded PCM
rebuilds its declared upward consumers but does not synthesize new WAV bytes.
Replacing raw pixels rebuilds declared mips, compression, and GPU realization
but does not silently rewrite PNG or JPEG provenance.

Hot reload is the same transaction rooted at a replaced raw-file or parsed-source
cell. It installs a candidate manifest root without mutating the published
generation, invalidates superseded queued work, drains executing work while its
source packs remain alive, materializes every hard-floor member, obtains all
owner safe-point acknowledgements, and publishes one successor epoch.

Replacement requests carry monotonically increasing generations. A newer request
cancels the active cooker at its next bounded observation point, discards a ready
but unpublished renderer candidate, and supersedes queued work. Owner publication
checks the generation before polling or committing, so only the newest complete
revision can become visible.

Readers holding the previous epoch continue to resolve the complete previous
generation. Every addressable focus in one asset route resolves within its
reader's epoch. Mesh, material, texture, font, audio, and world-cell members of
one commit group never mix generations.

Corruption, cancellation, allocation failure, budget rejection, device loss,
decode failure, or owner rejection preserves the last complete epoch. Prepared
values and partially realized owner values retire through their responsible
module. CPU memory waits for reader epochs; GPU memory waits for fence serials;
audio memory waits for mixer retirement acknowledgement.

Mutable ECS state is not resource data. A resource schema change that requires
pose, playback, or simulation migration invokes an explicit owning-system policy
rather than generic resource-manager mutation.

## Feature: current-engine asset vertical

**Public surface:** the complete header family above; no demo-only resource API.

The engine startup manifest contains Sponza, Viking Room, the candle holder,
fonts, shaders, textures, and their dependency closure. `main()` submits stable
asset goals and constructs the existing scene from resolved typed bindings. It
does not load glTF, fonts, images, or shader files directly.

The vertical exercises:

- glTF and GLB import through `anogltf` reflection.
- Scene selection, node closure, transforms, mesh accessors, index rebasing,
  materials, textures, punctual lights, and dependency resolution.
- PNG/JPEG image import.
- OpenType loading and the complete in-world font bake.
- GLSL include discovery and SPIR-V validation.
- Incremental cook, CAS reuse, manifest construction, pack reads, worker
  preparation, renderer realization, ECS demand, and epoch publication.
- Typed navigation from each scene entry point through selected glTF/GLB
  provenance, canonical meshes/materials/textures, and GPU realization.
- Direct resolution and copy-on-write editing of an intermediate pixel or
  material cell, with only its reflected upward consequence cone republished.

Required demo assets are licensed, tracked or reproducibly fetched, and hashed
with their complete dependency trees. A required asset has no synthetic fallback
that can make startup appear successful.

Semantic tests pin scene roots, primitive counts, vertex/index content,
materials, textures, lights, font ranges, and dependency identities. Rendered
goldens cover Sponza, the central Viking Room, candles, lighting, and in-world
text. Navigation tests inspect raw, parsed, canonical, runtime-loadable, and
owner-resident cells through typed handles. Reload and direct-cell edit tests
replace a mesh/material/texture/font group and demonstrate one coherent visible
generation while the previous epoch remains readable.

## Feature: Ogg Opus streaming audio

**Public surface:** `anoptic_audio_resources.h` plus cooker/runtime controls from
their resource headers.

Reflected declarations describe Ogg Opus source, validated stream pages, seek
inventory, decoded blocks, audio-resident stream state, and their transforms.
The language compiler validates worker and audio-master ownership and generates
the legal direct route.

`constexpr` byte readers, Ogg page parsing, checksum framing, granule arithmetic,
trim, loop, and seek calculations are shared by importer, cooker, and runtime.
The Opus codec call remains a concrete runtime effect. Worker threads decode into
a bounded single-producer/single-consumer ring; the mixer consumes without
allocation or locking and emits deterministic silence on underrun.

Reload prepares a replacement stream privately and changes it at an audio block
boundary. The old stream retires only after active voices and the mixer callback
can no longer reference it.

## Feature: KTX2 and Basis Universal textures

**Public surface:** `anoptic_render_resources.h` plus cooker/runtime controls.

Reflected declarations describe KTX2 source, validated levels, portable texture
semantics, target block representations, mip atoms, and the final opaque GPU
texture. Renderer capability annotations describe supported target formats and
upload constraints. `consteval` compilation produces the legal capability
matrix and transform frontier.

`constexpr` KTX2 header, DFD, level-index, range, dimension, and mip arithmetic
is shared across import, cook, and runtime. Basis transcoding and vendor API calls
remain concrete runtime functions selected and adapted by the compiled language.

The manifest remains vendor-neutral. It identifies semantic texture content and
portable atoms, not a Vulkan device choice. Runtime selects BC, ASTC, ETC, or a
portable fallback from actual device capabilities. Minimum mips may publish as
the hard floor; higher mips join later epochs without replacing a complete
texture with a partial generation.

## Feature dependency shape

| Feature | Depends on | Delivers |
|---|---|---|
| Addressable cell identities and canonical artifacts | Public base and typed headers | One reflected wire, type, and handle authority at every layer |
| Reflected focus and navigation | Cell and transform declarations | Composed typed routes, algebraic `down()`, and focused `up()` |
| Reflected source import | Addressable cells | Typed source ingress, provenance, and dependencies |
| Incremental cooking and CAS | Import and compiled transform graph | Reproducible addressable cell DAG |
| Manifests and packs | Cell DAG and cook results | Vendor-neutral shipping data plus packed provenance/navigation |
| Runtime demand and epochs | Compiled routes and packs | Immutable typed cell columns and resident bindings |
| Transactional cell editing | Reflected forward closure and epochs | COW recomputation and atomic publication from any cell focus |
| Module-owned realization | Runtime scheduling and owner extensions | Real GPU, audio, and text objects |
| Current-engine asset vertical | Import, cook, pack, runtime, realization | Existing Sponza demo entirely resource-managed |
| ECS demand and native world cells | Typed references and runtime epochs | Spatial ECS publication and persistence |
| Ogg Opus streaming | Audio ownership and runtime atoms | Streaming music and long-form audio |
| KTX2/Basis textures | Render ownership and runtime atoms | Portable compressed texture streaming |

New formats enter only as part of a concrete engine feature. The maintained
feature frontier is the current-engine asset vertical, native spatial ECS world
data, Ogg Opus streaming, and KTX2/Basis textures. Formats without an actual
consumer do not expand the language.

The resulting module remains one reflection-compiled many-input/many-output
route language of cell types and functions, one packed runtime cell-instance
hypergraph, and small concrete
implementations for I/O, allocation, codecs, and owner effects. Every cell is a
typed addressable focus; selected provenance is a generated sum of
transform/output ports and input products; copy-on-write republishes only the
affected upward cone. Reflection
supplies structure; `consteval` compiles and rejects; `constexpr` performs the
reusable work; generated direct C++ is what runs.
