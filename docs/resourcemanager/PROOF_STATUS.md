# Formal proof status

This document states exactly what the Lean and C++26 proof gate establishes.
It deliberately separates three different claims:

1. an abstract mathematical model is internally lawful;
2. real C++ declarations were mechanically reflected into that model;
3. a production implementation refines the modeled behavior.

Only the reflected resource-schema subset currently reaches the second claim.
No engine module currently reaches the third claim in full.

## Current verdict

| Question | Status |
|---|---|
| Does the Lean kernel compile without `sorry`, `admit`, or project-defined axioms? | Yes |
| Are the selected abstract algebras and their compositions machine-checked? | Yes |
| Is the selected 24-constructor abstraction an exhaustive public-interface inventory? | No |
| Are all cross-module edges derived from production declarations? | No |
| Is the resource artifact/transform/importer inventory derived from real reflected headers? | Yes |
| Does the C++ witness include and exercise production resource headers? | Yes |
| Does passing `.#proofs` prove all production C++ behavior? | No |
| Does it prove compiler, driver, operating-system, or foreign-codec correctness? | No |

## Mechanically connected resource universe

[`resource_schema_certificate.cpp`](../../proofs/cpp/resource_schema_certificate.cpp)
includes the real public resource headers and reflects
`ano::asset_schema`. It emits:

- every artifact name, reflected type ID, schema fingerprint, fixed wire size,
  direct field count, and recursively discovered `AssetRef<T>` dependency;
- every reflected transform, its artifact input and output ports, executor,
  streaming policy, and determinism annotation;
- every importer extension, reflected producer ID, and determinism annotation.

The checked output is
[`ResourceSchema.lean`](../../proofs/Anoptic/Generated/ResourceSchema.lean).
The proof derivation regenerates that file and performs a byte-for-byte
comparison before Lean runs. A resource declaration, field, fingerprint, route,
or importer change therefore makes `nix build .#proofs` fail until the reviewed
certificate is updated.

The current reflected inventory is:

| Kind | Count | Members |
|---|---:|---|
| Artifacts | 9 | `Manifest`, `Texture`, `Material`, `Mesh`, `Scene`, `GpuTexture`, `GpuMaterial`, `GpuMesh`, `GpuScene` |
| Transforms | 4 | `realize_texture`, `realize_material`, `realize_mesh`, `realize_scene` |
| Import routes | 2 | `.gltf`, `.glb` through `import_gltf` |
| Typed dependency edges | 3 | `Material -> Texture`, `Mesh -> Material`, `Scene -> Mesh` |

[`ResourceCertificate.lean`](../../proofs/Anoptic/ResourceCertificate.lean)
checks the generated inventory rather than a duplicate enum. It establishes for
this concrete declaration set:

- artifact names, type IDs, and fingerprints are unique;
- type IDs and importer producer IDs are nonzero;
- fingerprints have the expected 256-bit hexadecimal representation;
- dependency targets and transform endpoints name declared artifacts;
- every transform has at least one artifact input and output;
- importer extensions and producer IDs are unique;
- the reflected typed dependency graph is acyclic.

The C++ certificate also executes `compile_resource_language` over the real
namespace and evaluates a production `Texture` encode/decode witness at compile
time. The witness checks a canonical round trip and malformed-magic rejection.
It is linked with `-nostdlib++`, and the proof gate rejects a `libstdc++`
dependency.

This is a genuine mechanical connection between current headers and Lean data.
It is not a formal semantics of arbitrary C++ execution and does not prove the
runtime implementations of cooking, packing, residency, rendering, or reload.

## Abstract kernel

The reusable Lean nucleus models:

- polynomial APIs and dependent request/response families;
- pure, fallible, stateful, transactional, relational, and many-port route
  composition;
- compiler projection, pullback, refined products, and compositional
  refinement;
- dependency reachability and closure;
- typed many-input/many-output resource signatures and output-port provenance;
- revision-indexed residency values and generation-indexed world pairs;
- demand deltas and failure-preserving publication;
- a staged target language for reflection, `consteval`, `constexpr`, type-only
  templates, expansion, splicing, and direct runtime calls.

`Coverage.Interface` is a selected abstract factorization. Pattern matching is
exhaustive over those constructors only. It is not derived from the 30 current
headers, the 35 desired rows in `include/include.md`, CMake targets, or every
foreign boundary. `ArchitectureGraph.EdgeName` has the same status: it is an
abstract typed graph, not a generated production-edge inventory.

`Cxx26Mapping` proves that the abstract algebras admit lawful staged candidate
images and that the modeled lowering preserves their modeled composition. It
does not claim that those images were extracted from engine C++ declarations.
The production resource certificate is the separate bridge that supplies such
extraction for one real declaration universe.

## Strengthened abstract contracts

The abstract kernel rejects several previously admissible broken models:

- `Resource.Codec` is a partial isomorphism between values and a canonical byte
  language. It requires both `decode(encode(value)) = value` and
  `encode(decode(bytes)) = bytes` for canonical bytes, connects successful
  decoding to canonicality, and entails rejection of noncanonical bytes.
- Memory measurement executes checked aligned reservations. A successful
  reservation is not before its cursor, is aligned, has the requested size,
  ends at the next cursor, and remains in bounds. A sealed volume carries
  layout, bound, and pairwise-disjointness evidence.
- Time instants include their duration unit in the type, so a duration of a
  different unit cannot advance them.
- UTF-8 values contain the code-point sequence produced by a concrete UTF-8
  decoder; callers cannot supply an arbitrary validity predicate. The empty
  string builder has no byte-bearing constructor, and interned symbols are
  stable numeric identities rather than aliases for source bytes.
- Render realization preserves payload identity as well as semantic identity.
- The Vulkan reference semantics observe residency revision, command count, and
  view revision instead of discarding all inputs.
- UI, audio, and synth comparison paths are distinct definitions related by
  proved observations rather than aliases of the same function.
- Save cells retain their own demand state; load no longer receives the value it
  is supposed to restore as an external argument.

These are abstract specifications. A production module is not certified merely
because an analogous Lean definition satisfies them.

## Proof derivation

`nix build .#proofs` performs one connected sequence:

1. compile the real-header C++26 resource certificate with GCC reflection,
   `-fno-exceptions`, `-fno-rtti`, and `-nostdlib++`;
2. reject any forbidden C++ runtime dependency;
3. execute the certificate generator;
4. compare its output byte-for-byte with the checked Lean resource certificate;
5. build the complete Lean library, including the laws over that generated data.

The gate cannot pass with stale resource-schema Lean data or a C++ witness that
omits the engine resource headers.

## Remaining refinement boundary

The following are not yet formal consequences of the proof target:

- exhaustiveness of the whole public header and semantic-module surface;
- the status/out-pointer ABI layout and inactive-payload rules;
- atomic memory ordering, queue interleavings, wraparound, progress, and
  starvation freedom;
- filesystem normalization, root confinement, durability, and snapshot
  consistency;
- anogltf parsing and validation behavior;
- mesh topology and attribute preservation;
- migration closure, pack authentication semantics, incremental scheduling,
  obsolete-generation cancellation, safe-point retirement, and atomic
  multi-owner publication;
- FreeType/OpenType shaping, renderer realization, Vulkan driver behavior,
  audio codec/mixer behavior, and device effects;
- concrete Sponza, Viking-room, font, candle, or hot-reload observations;
- object-code equivalence between C++ implementations and Lean interpreters.

Each such claim requires a generated production inventory where applicable, an
independent specification, a concrete implementation interpretation, and an
observational refinement witness. Compiler and Lean kernels, generated object
code, operating systems, drivers, and foreign libraries remain explicit trust
boundaries unless separately verified.
