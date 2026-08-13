# Lean semantic kernel

The semantic kernel uses Lean 4.33.0 and `Std` only. Its Lake library is `Anoptic`, and every theorem is built by the Nix flake.

## Checked foundations

1. `API.Hom.refl_trans`, `trans_refl`, and `trans_assoc` establish the category laws for API adapters whose queries travel forward and responses travel backward.
2. `API.Hom.induced_refl` and `induced_trans` establish that adapters act compositionally on polynomial extensions.
3. `API.choice_unpack_pack`, `choice_pack_unpack`, `tensor_unpack_pack`, and `tensor_pack_unpack` establish the sum and tensor representations.
4. `API.zeroElim` and `unit_unique` keep the zero polynomial distinct from the unit polynomial.
5. `Refines.refl` and `Refines.trans` establish trace-refinement preorder laws.
6. `Relation.compose_assoc`, `identity_left`, and `identity_right` establish sequential-composition laws.
7. `Relation.compose_mono` establishes that independently proved module refinements compose.
8. `failure_retains_cache` and `failure_preserves_publication` establish that failed work may advance reusable cooker state without changing the published revision.
9. `success_retains_cache`, `success_publishes_revision`, and `publication_changes_only_on_success` establish the publication boundary.
10. `Prooflet` instantiates the dependent API, compositional refinement, and transactional publication foundations as one concrete toolchain smoke proof.

## Trust boundary

Lean checks the abstract laws and implementation-refinement witnesses presented to it. The Lean kernel, the Nix-pinned Lean executable, the C++ compiler, and the reflection-to-witness exporter form the build-time trusted boundary. Operating systems, drivers, devices, and foreign codecs enter only through explicitly typed module-local assumptions. A theorem about an abstract operation becomes an engine guarantee only when the reflected implementation bridge supplies its corresponding witness.
