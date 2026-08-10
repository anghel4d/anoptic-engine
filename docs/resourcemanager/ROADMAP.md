# Resource Manager Feature Roadmap

## Contract

**Types and laws are compile-time. Asset instances and schedules are runtime.**

This document maps the architecture in [DESIGN.md](DESIGN.md) onto public module
features. `DESIGN.md` remains the architectural contract; this file defines the
public surface and implementation shape of each deliverable feature.

C++26 reflection compiles the closed language of resource types, transformations,
and ECS asset references. The cooker evaluates that language over source assets.
The runtime reconciles demand into immutable residency epochs. Renderer, audio,
text, and ECS modules retain ownership of their own effects and objects.

The module has one structural authority: public reflected declarations and typed
annotations. It has no parallel typelist, traits registry, X-macro inventory,
artifact-kind switch forest, schema mirror, or external source generator.

This roadmap is organized by feature. Every feature describes its public header,
compile-time product, ordinary implementation, and observable behavior.

## Public module surface

The resource API follows the existing base-plus-extension pattern used by
`anoptic_strings.h` and `anoptic_strings_utf.h`:

```text
include/
|- anoptic_resources.h
|- anoptic_resources_typed.h
|- anoptic_resources_cook.h
|- anoptic_resources_pack.h
|- anoptic_resources_runtime.h
|- anoptic_resources_ecs.h
|- anoptic_render_resources.h
|- anoptic_audio_resources.h
`- anoptic_text_resources.h
```

| Header | Public responsibility |
|---|---|
| `anoptic_resources.h` | C-compatible stable IDs, content IDs, schema fingerprints, byte/range values, quality values, errors, and the compiled-language view |
| `anoptic_resources_typed.h` | C++26 artifact annotations, transform annotations, `AssetRef<T>`, relative wire types, reflection compiler, and typed generated operations |
| `anoptic_resources_cook.h` | Source roots, import requests, build profiles, cooker configuration, diagnostics, incremental cook results, and CAS control |
| `anoptic_resources_pack.h` | Vendor-neutral manifest and pack construction, opening, querying, range reads, and root identity |
| `anoptic_resources_runtime.h` | Residency goals, commit groups, manager lifetime, manifest transactions, epoch acquisition, changed IDs, and binding resolution |
| `anoptic_resources_ecs.h` | Reflected component integration, demand deltas, prefab/world-cell schemas, bulk instantiation, and coordinated ECS/resource publication |
| `anoptic_render_resources.h` | Render-owned artifact declarations, render transforms, opaque GPU slots, and the renderer residency connection |
| `anoptic_audio_resources.h` | Audio artifact declarations, audio transforms, opaque audio slots, streaming adoption, and the mixer residency connection |
| `anoptic_text_resources.h` | Font artifacts, font-bake transforms, text slots, and text-module residency connection |

`anoptic_resources.h` does not include a module-owner extension. The focused
headers include the base header and only the public module headers their
signatures require. No public header includes a private `src/` header.

Runtime C ABI functions begin with `ano_`. C++26 compile-time facilities live in
namespace `ano`. C-compatible headers expose no `std::meta::info`, templates, or
foreign backend types.

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
A resource type ID comes from a reflected semantic declaration; it is not a
hand-maintained enum. Compiler display strings never contribute to persistent
identity.

### Public function families

The extension headers expose narrow functions for their consumers rather than
one all-purpose manager interface:

| Header | Required public entry points |
|---|---|
| `anoptic_resources.h` | `ano_resource_language`, `ano_resource_error_string`, ID comparison and formatting |
| `anoptic_resources_cook.h` | `ano_resource_cooker_create`, `ano_resource_cooker_destroy`, `ano_resource_import`, `ano_resource_cook`, `ano_resource_cooker_cancel` |
| `anoptic_resources_pack.h` | `ano_resource_manifest_open`, `ano_resource_manifest_close`, `ano_resource_manifest_find`, `ano_resource_pack_open`, `ano_resource_pack_read`, `ano_resource_pack_close` |
| `anoptic_resources_runtime.h` | `ano_resource_manager_create`, `ano_resource_manager_destroy`, `ano_resource_goal_set`, `ano_resource_goal_remove`, `ano_resource_reconcile`, `ano_resource_reload`, `ano_resource_epoch_acquire`, `ano_resource_epoch_resolve`, `ano_resource_epoch_release` |
| `anoptic_resources_ecs.h` | Demand-delta submission, reflected component registration, world-cell instantiation, and coordinated epoch publication |
| Owner extensions | Concrete connect/disconnect and owner-boundary functions for render, audio, and text; no generic user callback table |

The typed extension adds direct C++ operations such as
`resource_type_id<T>`, `schema_fingerprint<T>`, `validate<T>`, `encode<T>`,
`decode<T>`, and `resolve<T>`. Their structure is generated from `T`; they do
not forward into a runtime reflection interpreter.

### Typed resource language

`anoptic_resources_typed.h` requires C++26 and includes `anoptic_meta.h`. It owns
the annotation vocabulary and reflection compiler, not the artifact inventory.
Artifact and transform declarations remain in their responsible module headers.

```cpp
namespace ano {

template<class SemanticAsset>
struct AssetRef final {
    AnoAssetId id;
};

struct Artifact final {
    uint64_t wireId;
    Storage storage;
    uint32_t version;
};

struct Transform final {
    Executor executor;
    Streaming streaming;
    bool deterministic;
};

struct Field final {
    uint32_t wireId;
    FieldPolicy policy;
};

consteval auto compile_resource_language(
    std::meta::info schemaNamespace,
    ResourceCompileProfile profile);

} // namespace ano
```

Annotation value types are structural types. Persistent wire IDs and field IDs
are explicit semantic constants. Declaration identifiers may supply retained
debug names, but renaming an ordinary C++ identifier does not silently change a
shipping schema.

### Owner extensions

Each owner extension reopens the shared schema namespace and declares its actual
types and functions:

```cpp
namespace ano::asset_schema {

struct [[=Artifact{wire_id("mesh"), Storage::portable, 1}]] Mesh final {
    [[=Field{1, FieldPolicy::required}]] RelativeSpan<Vertex> vertices;
    [[=Field{2, FieldPolicy::required}]] RelativeSpan<uint32_t> indices;
    [[=Field{3, FieldPolicy::dependency}]] AssetRef<Material> material;
};

struct [[=Artifact{wire_id("gpu-mesh"), Storage::resident, 1}]] GpuMesh final {
    AnoGpuMeshSlot slot;
};

[[=Transform{Executor::render_master, Streaming::whole, true}]]
bool upload_mesh(const Mesh&, RenderResourceContext&, GpuMesh&) noexcept;

} // namespace ano::asset_schema
```

The same pattern describes import functions, migrations, texture mip transforms,
font bakes, audio adoption, and world-cell materialization. The reflected
function declaration is the dispatch declaration. There is no second route
registration call.

### Resource-universe translation unit

Reflection can inspect only declarations visible at its point of use. Every
executable or tool therefore has one resource-universe translation unit which
includes all public resource extensions before compilation:

```cpp
#include <anoptic_resources_typed.h>
#include <anoptic_render_resources.h>
#include <anoptic_audio_resources.h>
#include <anoptic_text_resources.h>
#include <anoptic_resources_ecs.h>

namespace {
inline constexpr auto compiledLanguage =
    ano::compile_resource_language(
        ^^ano::asset_schema, ano::current_resource_profile());
}

extern "C" const AnoResourceLanguage *ano_resource_language() noexcept
{
    return compiledLanguage.runtime_view();
}
```

The build profile changes retained routes and executor bindings, not portable
artifact identities or schema fingerprints. A headless tool can inspect a
render transform declaration without retaining a call to its unavailable
implementation.

## Compile-time execution model

The reflection compiler uses the complete implemented C++26 facility:

```text
GCC 16.1+
-std=gnu++26 -freflection -fno-exceptions -fno-rtti -nostdlib++
```

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
| Typed semantic metadata | Storage class, wire identity, executor, requiredness, foreign names and backend codes remain typed values attached to declarations | String attributes, side tables, magic enums or external schema files |
| Functions become graph edges | Parameters and return types define representations; annotations define execution and capability policy | Callback registration, type-erased nodes and duplicated route declarations |
| Whole-language graph compilation | `constexpr` graph algorithms compute reachability, dependency closure, cycles, legal stages, capabilities and Pareto-optimal routes | Runtime graph search or a separate build-time generator |
| Compile-time exhaustiveness | Missing routes, migrations, mappings, executor bridges or backend annotations fail the build | Runtime “unsupported type” paths and incomplete switch defaults |
| Generated structural operations | Parsing, encoding, endian conversion, validation, dependency extraction and projection directly access reflected fields | Per-type serializers, visitors and duplicated traversal code |
| Direct invocation | A selected reflected transform is emitted as `[:function:](...)` with its exact signature | Function-pointer tables, virtual interfaces, `void*` contexts or type erasure |
| Foreign-schema compilation | glTF fields, JSON names, Vulkan mappings, shader interfaces and multimedia codes compile from typed declarations | Thousands of lines of mapping switches and mirror tables |
| Layout-aware generation | Size, alignment, offsets, bit fields and representation properties participate in validation and code generation | Mirrored ABI descriptions and scattered handwritten `static_assert`s |
| Canonical identities from semantics | Reflected wire IDs, field IDs and types produce deterministic schema fingerprints and action-key inputs | Manually synchronized version constants and hashing recipes |
| Migration closure | Versioned artifact declarations and reflected migration functions form a compile-checked migration graph | Handwritten version dispatch and undiscovered upgrade gaps |
| Typed ECS demand | Reflection finds `AssetRef<T>` fields and emits component-specific demand-delta functions | Per-component visitors or continuous generic ECS scans |
| Owner-safe realization | Reflected signatures bind legal routes to renderer, audio and text owner bridges while opaque slots preserve ownership | Generic service locators, backend callbacks or resource-manager-owned device objects |
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

## Feature: semantic identities and canonical artifacts

**Public surface:** `anoptic_resources.h`, `anoptic_resources_typed.h`, and the
owner extension declaring each artifact.

An artifact declaration is the sole schema authority. Reflection compiles:

- Stable semantic type IDs from explicit wire IDs.
- Ordered wire fields from explicit field IDs.
- Schema fingerprints from canonical semantic tokens.
- Direct typed encode, decode, endian, validate, and dependency operations.
- Pointer-free and load-in-place classification.
- Relative-span and relative-reference bounds checks.
- Streaming-atom extraction.
- Retained field names for diagnostics only.

The `consteval` compiler rejects duplicate IDs, missing field policy, unsupported
field types, illegal pointers in portable artifacts, invalid alignment,
non-`noexcept` generated operations, and schema changes without a version
transition.

Ordinary `constexpr` code owns checked addition, multiplication, ranges, endian
loads/stores, canonical byte framing, and hashing. These functions have
compile-time boundary tests and runtime known-answer tests. Content hashing uses
a standard algorithm with published test vectors; no private checksum becomes a
shipping identity primitive.

Portable artifacts contain no file paths, process pointers, allocator state,
Vulkan handles, audio backend handles, or compiler-dependent type spellings.

## Feature: reflected source import

**Public surface:** import requests and diagnostics in
`anoptic_resources_cook.h`; format declarations in the responsible owner
extensions.

Importer functions carry typed annotations describing source signatures,
extensions, dependency policy, output artifact, determinism, and limits. The
resource compiler reflects those functions and produces:

- A collision-checked signature and extension classifier.
- Direct importer dispatch.
- Typed output and dependency adaptation.
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

## Feature: incremental cooking and content-addressed storage

**Public surface:** `anoptic_resources_cook.h`.

The cooker evaluates asset instances. It does not rebuild the C++ type graph and
does not require engine recompilation when an asset is added.

For each source asset the cooker:

- Imports source bytes through the compiled importer table.
- Builds the instance dependency DAG.
- Selects a legal compiled transform route for the build profile.
- Computes an action key from transform identity, exact inputs, settings,
  capabilities, and schema fingerprints.
- Reuses a verified CAS result or executes the direct typed transform.
- Stores canonical outputs by content ID.
- Invalidates only transitive dependants whose action inputs changed.

The transform type graph is computed by `consteval`; the instance DAG is runtime
tool data. Runtime cooking code never searches declarations or reconstructs the
type graph.

The CAS verifies existing objects before reuse, writes through a temporary file,
publishes atomically, and repairs a corrupt object from a reproducible action.
Cancellation leaves no visible partial object.

Migration functions are ordinary reflected declarations. The compiler validates
their exact source fingerprint, destination fingerprint, signature, and route.
Only fingerprints emitted outside the current build receive migrations; the
module does not invent legacy schemas for unshipped data.

## Feature: manifests and shipping packs

**Public surface:** `anoptic_resources_pack.h`.

The manifest maps stable asset IDs to canonical artifact roots, dependencies,
commit groups, streaming atoms, schemas, and pack extents. It contains no vendor
device object and no process-local address.

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
- Preserves atom boundaries required for range reads.
- Applies declared portable compression without changing artifact identity.
- Authenticates manifest and pack contents.
- Emits the same bytes for the same language, inputs, and profile.

Pack opening validates the complete manifest before exposing roots. Every later
range read repeats the arithmetic and authentication needed for untrusted pack
bytes. No range becomes a pointer until its bounds, codec, unpacked size, and
content identity are valid.

## Feature: runtime demand and residency epochs

**Public surface:** `anoptic_resources_runtime.h`.

The runtime receives structural and behavioral goals expressed as stable asset
IDs, required semantic types, quality floors, refinements, importance, and
commit groups. Entity deletion removes a demand contribution; it does not call
an asset-owned `unload()` function.

The compile-time language retains the legal Pareto frontier of transforms for
each source-to-resident pair. Runtime selects from that small frontier using
actual capabilities, current representations, memory budgets, and deadlines. It
does not perform general type-graph search during gameplay.

Workers perform pack I/O, authentication, validation, decompression, decoding,
and CPU preparation. Cancellation is observed at bounded points. A worker never
calls Vulkan, mutates the mixer, publishes ECS state, or destroys an owner
object.

The runtime product is an immutable, structurally shared residency epoch:

```text
ResidencyEpoch
|- manifest root
|- portable artifact bindings
|- CPU representation slots
|- GPU representation slots
|- audio representation slots
`- changed asset IDs
```

Stable asset IDs index dense typed binding tables. Hot paths perform predictable
array lookups. They do not use paths, hash maps, cache nodes, reflection values,
or generic ownership graphs.

A commit group publishes its complete hard floor atomically. Optional LODs,
mips, or secondary media refine later epochs. A failed refinement leaves the
last complete floor published.

## Feature: module-owned realization

**Public surface:** `anoptic_render_resources.h`,
`anoptic_audio_resources.h`, and `anoptic_text_resources.h`.

Transform annotations assign each effect to a concrete executor. The reflection
compiler validates the exact context parameter, artifact parameters, result,
`noexcept` contract, and module ownership. It groups transforms by executor and
generates the compact job description and direct dispatch required by that
module. There is no handwritten artifact-kind switch.

### Renderer ownership

The renderer alone creates and destroys Vulkan objects. Worker preparation ends
in portable upload data. The render-master consumes generated render jobs and
returns opaque slots.

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
compilation validates the referenced semantic type and optional `AssetUse`
policy. Expansion produces a component-specific function containing direct
field access and direct goal emission.

Generated structural extraction runs when a component is inserted, removed, or
changed; when a prefab expands; and when a world cell instantiates. It does not
scan every entity every frame.

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

The compiler rejects pointer-bearing persistent fields, unregistered component
types, duplicate persistent field IDs, invalid local-reference types, and a
world schema without a complete dependency floor.

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

## Feature: transactional reload and retirement

**Public surface:** reload, epoch, and changed-ID functions in
`anoptic_resources_runtime.h`.

Hot reload installs a candidate manifest root without mutating the published
generation. The runtime invalidates superseded queued work, drains executing
work while its source packs remain alive, materializes every hard-floor member,
obtains all owner safe-point acknowledgements, and publishes one successor
epoch.

Readers holding the previous epoch continue to resolve the complete previous
generation. Mesh, material, texture, font, audio, and world-cell members of one
commit group never mix generations.

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

Required demo assets are licensed, tracked or reproducibly fetched, and hashed
with their complete dependency trees. A required asset has no synthetic fallback
that can make startup appear successful.

Semantic tests pin scene roots, primitive counts, vertex/index content,
materials, textures, lights, font ranges, and dependency identities. Rendered
goldens cover Sponza, the central Viking Room, candles, lighting, and in-world
text. Reload tests replace a mesh/material/texture/font group and demonstrate one
coherent visible generation.

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
| Semantic identities and canonical artifacts | Public base and typed headers | One reflected wire and type authority |
| Reflected source import | Canonical artifacts | Typed source ingress and dependencies |
| Incremental cooking and CAS | Import and compiled transform graph | Reproducible canonical artifact DAG |
| Manifests and packs | Canonical artifacts and cook results | Vendor-neutral shipping data |
| Runtime demand and epochs | Compiled routes and packs | Immutable resident bindings |
| Module-owned realization | Runtime scheduling and owner extensions | Real GPU, audio, and text objects |
| Current-engine asset vertical | Import, cook, pack, runtime, realization | Existing Sponza demo entirely resource-managed |
| ECS demand and native world cells | Typed references and runtime epochs | Spatial ECS publication and persistence |
| Ogg Opus streaming | Audio ownership and runtime atoms | Streaming music and long-form audio |
| KTX2/Basis textures | Render ownership and runtime atoms | Portable compressed texture streaming |

New formats enter only as part of a concrete engine feature. The maintained
feature frontier is the current-engine asset vertical, native spatial ECS world
data, Ogg Opus streaming, and KTX2/Basis textures. Formats without an actual
consumer do not expand the language.

The resulting module remains one reflection-compiled type graph, one runtime
asset-instance graph, and small concrete implementations for I/O, allocation,
codecs, and owner effects. Reflection supplies structure; `consteval` compiles
and rejects; `constexpr` performs the reusable work; generated direct C++ is what
runs.
