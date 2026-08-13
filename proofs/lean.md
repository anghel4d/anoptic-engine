# Lean semantic kernel

The semantic kernel uses Lean 4.33.0 and `Std` only. Its Lake library is `Anoptic`, and every theorem is built by the Nix flake.

## Checked foundations and interfaces

1. `API.Hom.refl_trans`, `trans_refl`, and `trans_assoc` establish the category laws for API adapters whose queries travel forward and responses travel backward.
2. `API.Hom.induced_refl` and `induced_trans` establish that adapters act compositionally on polynomial extensions.
3. `API.choice_unpack_pack`, `choice_pack_unpack`, `tensor_unpack_pack`, and `tensor_pack_unpack` establish the sum and tensor representations.
4. `API.zeroElim` and `unit_unique` keep the zero polynomial distinct from the unit polynomial; `API.Hom.toUnit_unique` proves the terminal arrow is `A -> 1`.
5. `Compiler.projected_apply` establishes that module compilers consume a projection of one shared witness.
6. `Compiler.pair_succeeds_iff` establishes independent partial-compiler pairing without duplicating the witness.
7. `Compiler.Pullback.ofRefined_toRefined` and `toRefined_ofRefined` establish that equality compatibility is the pullback instance of a general proof-carrying refined product.
8. `ResourceRoute.Signature`, `Producer`, and `Provenance` make every transform many-input/many-output and index a producer by its output port.
9. `ResourceRoute.shared_left` and `shared_right` establish immutable-reference sharing without adding a diagonal to artifact values.
10. `Refines.refl` and `Refines.trans` establish finite-trace safety-refinement preorder laws; `Safety` and `Progress` keep finite safety distinct from infinite behavior.
11. `Relation.compose_assoc`, `identity_left`, and `identity_right` establish sequential-composition laws.
12. `Relation.compose_mono` establishes that independently proved module refinements compose.
13. `failure_retains_cache` and `failure_preserves_publication` establish that failed work may advance reusable cooker state without changing the published revision.
14. `success_retains_cache`, `success_publishes_revision`, and `publication_changes_only_on_success` establish the publication boundary.
15. `Outcome` through `Diagnostics` prove the sum, value, ownership,
    transport, clock, filesystem, string, and diagnostic algebras.
16. `Gltf` through `Engine` prove the domain carriers and laws for every ideal
    interface in the architecture inventory.
17. `Coverage.allInterfacesChecked` exhaustively covers all 24 interface rows;
    Lean rejects a newly enumerated interface until its proof case exists.
18. `Prooflet` instantiates dependent APIs, projected compiler pairing,
    multi-output producer identity, compositional refinement, and transactional
    publication as one concrete toolchain smoke proof.
19. `Integration` checks cross-module route navigation, revision-indexed
    residency and realization, coherent frame publication, failed hot reload,
    music snapshots, and batch/live synthesis.

The complete carrier and theorem table is [interfaces.md](interfaces.md).

## Trust boundary

Lean checks the abstract laws and implementation-refinement witnesses presented to it. The Lean kernel, the Nix-pinned Lean executable, the C++ compiler, and the reflection-to-witness exporter form the build-time trusted boundary. Operating systems, drivers, devices, and foreign codecs enter only through explicitly typed module-local assumptions. A theorem about an abstract operation becomes an engine guarantee only when the reflected implementation bridge supplies its corresponding witness.

The kernel proves resource closure, proof-carrying focus navigation, and
committed-generation consistency in the abstract interface. It does not yet
prove the C++ reflection exporter, object-code correspondence, foreign codec
implementations, driver progress, or a concrete platform bridge. The abstract
theorems are therefore not described as end-to-end implementation correctness.
