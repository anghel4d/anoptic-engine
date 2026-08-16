# Formal interface matrix

`Coverage.Interface` is a selected abstract factorization. It is exhaustive only
over its 24 constructors; it is not generated from the current public headers or
the desired rows in `include/include.md`. Each row below distinguishes a proved
Lean law from a production refinement.

The kernel contains no `sorry`, `admit`, or project-defined axiom. Lean's trusted
environment still includes its standard quotient and propositional-extensionality
principles.

| Selected carrier | Machine-checked abstract facts | Production connection |
|---|---|---|
| Structural compiler | Empty record/choice members are normalized idempotently; witness projections compose; a `CompiledSignature` is noninterfering outside its projection; fail-fast pairing has explicit left-error precedence; compiler pullbacks and refined products obey their stated laws | Abstract only; no generated whole-declaration witness |
| Outcomes and fallible composition | `Except` identity, associativity, short-circuiting, mapping, recovery, modeled C-result conversion, inactive-output exclusion, safety-admitted Kleisli closure, value/error-map laws, left-biased fail-fast pairing, finite-parameter/product isomorphism, normalization compatibility with Kleisli postcomposition, bounded inclusive scan, and conditional-result branch laws | `anoptic_results.h` realizes the C++26 sum with `std::expected`; `anoptic_compose.h` enforces reflected declaration shape, `noexcept`, exact domains and safe storage in C++. Lean begins after that admission boundary and proves the denoted operations; it does not simulate reflection or C++ construction semantics |
| Polynomial APIs | Dependent extension, products, coproducts, zero, unit, adaptor identity and composition | Abstract only; this is the reusable mathematical nucleus |
| Semantic algebras | Identity, result, state, and state-plus-result effects are functorial; effect transformations are natural; algebra homomorphisms map shape, carrier, and effect while satisfying a commuting law; identities and composition preserve that law; a concrete shape morphism is proved to have no homomorphic lift for selected interpretations | Abstract only; no production interpreter supplies a carrier/effect refinement witness |
| Linear algebra | Space-indexed map identity and associative composition | Abstract only; arithmetic, layout, and engine ABI are not certified |
| Memory | Checked aligned reservation, zero-alignment/overflow rejection, successful reservation bounds, checked plan composition, sealed-volume layout/bounds/disjointness, owner retention, and scratch reset | Abstract only; it mirrors the resource volume contract but is not extracted from `anoptic_memory.h` |
| Concurrency | Queue send/receive examples, channel-map functor laws, endpoint identity sharing without copied queue state, lane independence, and common publication generation | Abstract only; atomics, memory order, interleavings, wraparound, and progress are unproved |
| Time | Clock-and-unit-indexed instants, same-unit advance/elapsed inverse, advance composition, and conversion composition | Abstract only |
| Filesystem | Root-indexed paths whose segments exclude empty, `.` and `..` names; source alternatives, snapshot preservation, and append composition | Abstract only; production I/O, OS canonicalization, confinement, and durability remain unproved |
| Strings | Byte monoid, concrete UTF-8 decoding witness, truly empty builder state, append/finalize transition, numeric symbol interning, and intern idempotence | Abstract only; production Unicode tables and allocators are unproved |
| Diagnostics | Record-sequence drain homomorphism and a nonreturning crash-interpreter type | Abstract only; queues, loss policy, and signal safety are unproved |
| glTF/GLB | JSON/GLB source sum, indexed-buffer binding shape, and fallible parse/bind/project regrouping | Abstract only; anogltf parsing is not refined |
| Mesh | Typed transform identity/associativity and one LOD output per requested budget | Abstract only; topology and numerical properties are unproved |
| Resource manager | Many-port signatures, output-port provenance, route composition/tensor, provenance/producer-polynomial isomorphism, typed one-hole derivative navigation, dependency closure, canonical codec bijection, noncanonical rejection consequence, revision-indexed epochs, demand union, COW locality, and failure-preserving publication | Real artifact/transform/importer inventory is generated from headers; one production texture codec witness executes at compile time. Cooker/pack/runtime behavior remains unrefined |
| Render resources | Capability-indexed portable/slot values, semantic and payload identity preservation, batch cardinality, and retirement threshold | Abstract only; production renderer realization is unproved |
| Render protocol | Closed command/event sums and ordered command-fold composition | Abstract only; bulk transport and backend effects are unrefined |
| Input/display | Closed input-event sum, display transitions, batch composition, and an explicit adapter-refinement predicate | Abstract only; no GLFW adapter witness |
| Vulkan | Private interpreter shape; reference semantics advances a frame and observes residency, command count, and view revision; state-arrow composition | Abstract only; no production Vulkan refinement witness |
| Text | Font-source/face/bake/atlas retention; shaping consumes proved UTF-8 code points; measurement agrees with generated glyph advances | Abstract only; FreeType/OpenType behavior is unproved |
| UI | Primitive sum, capacity outcome, and distinct CPU/device traversal definitions with equal observation | Abstract only; production CPU/GPU interpreters are unrefined |
| Audio | Command/event sums, command-fold composition, distinct native/offline definitions with equal modeled observation, and resource identity propagation | Abstract only; mixer and codec behavior are unproved |
| Music | Deterministic control fold and snapshot/restore inverse laws | Abstract only; production algorithm is unrefined |
| Synth | Input sum, render state transition, distinct batch/live normalization, and adjacent-range composition | Abstract only; production synthesis is unrefined |
| World/ECS/save | Demand delta reconstruction/removal/history, generation-indexed ECS/residency pairs, and save/load of the stored demand and persistent state | Abstract only; reflected ECS extraction and serialization are unproved |
| Engine root | Pure/fallible composition regrouping, selected joins/forks, generation indexing, and lifecycle ordering | Abstract only; no generated engine-root refinement |

## Concrete reflected resource certificate

`Generated.ResourceSchema` is emitted by a C++26 program that includes and
reflects the production resource headers. `ResourceCertificate` proves concrete
properties of that emitted inventory:

- 9 artifacts, 4 transforms, and 2 importer routes are present;
- artifact names, type IDs, fingerprints, importer extensions, and producer IDs
  are unique where required;
- all identifiers are nonzero and fingerprints are structurally well formed;
- all dependency and transform endpoints exist;
- all transforms have nonempty artifact input/output boundaries;
- the current typed dependency graph is acyclic.

The Nix proof gate regenerates and compares the certificate before building Lean.
This is currently the only mechanically derived production inventory.

## Composition scope

`CompositionCoverage.allCompositionsChecked` covers the selected abstract enum.
Its additional compiler examples prove projection noninterference and its
semantic examples deliberately distinguish a shape morphism from an algebra
homomorphism by exhibiting both a commuting map and a noncommuting map.
`ArchitectureGraph.Path.run_assoc` and `EngineComposition` prove laws about their
handwritten typed models. `Cxx26Mapping` proves that the abstract algebras admit
candidate staged images and that modeled lowering preserves modeled composition.
None of those definitions constitutes a generated inventory of production C++
modules or edges.

Production equivalence requires a separately supplied refinement witness. A
same-function equality, a reflexive determinism statement, or successful C++
compilation alone is not such a witness.
