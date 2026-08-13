# Lean semantic kernel

The semantic kernel uses Lean 4.33.0 and `Std` only. Its Lake library is `Anoptic`, and every theorem is built by the Nix flake.

## Checked foundations and interfaces

1. `API.Hom.refl_trans`, `trans_refl`, and `trans_assoc` establish the category laws for API adapters whose queries travel forward and responses travel backward.
2. `API.Hom.induced_refl` and `induced_trans` establish that adapters act compositionally on polynomial extensions.
3. `API.choice_unpack_pack`, `choice_pack_unpack`, `tensor_unpack_pack`, and `tensor_pack_unpack` establish the sum and tensor representations.
4. `API.zeroElim` and `unit_unique` keep the zero polynomial distinct from the unit polynomial; `API.Hom.toUnit_unique` proves the terminal arrow is `A -> 1`.
5. `API.Hom.choiceMap` and `tensorMap` lift independent adaptors through API
   coproducts and products; their query and response interchange theorems prove
   compatibility with adaptor composition.
6. `Compiler.projected_apply` establishes that module compilers consume a projection of one shared witness.
7. `Compiler.pair_succeeds_iff` establishes independent partial-compiler pairing without duplicating the witness.
8. `Compiler.Pullback.ofRefined_toRefined` and `toRefined_ofRefined` establish that equality compatibility is the pullback instance of a general proof-carrying refined product.
9. `ResourceRoute.Signature`, `Producer`, and `Provenance` make every transform many-input/many-output and index a producer by its output port.
10. `ResourceRoute.shared_left` and `shared_right` establish immutable-reference sharing without adding a diagonal to artifact values.
11. `Refines.refl` and `Refines.trans` establish finite-trace safety-refinement preorder laws; `Safety` and `Progress` keep finite safety distinct from infinite behavior.
12. `Relation.compose_assoc`, `identity_left`, and `identity_right` establish sequential-composition laws.
13. `Relation.compose_mono` establishes that independently proved module refinements compose.
14. `failure_retains_cache` and `failure_preserves_publication` establish that failed work may advance reusable cooker state without changing the published revision.
15. `success_retains_cache`, `success_publishes_revision`, and `publication_changes_only_on_success` establish the publication boundary.
16. `Outcome` through `Diagnostics` prove selected abstract sum, value,
    ownership, transport, clock, filesystem, string, and diagnostic laws.
17. `Gltf` through `Engine` prove selected abstract domain carriers and laws;
    they are not production implementation witnesses.
18. `Coverage.allInterfacesChecked` is exhaustive over its handwritten
    24-constructor enum only.
19. `Prooflet` instantiates dependent APIs, projected compiler pairing,
    multi-output producer identity, compositional refinement, and transactional
    publication as one concrete toolchain smoke proof.
20. `Integration` checks cross-module route navigation, revision-indexed
    residency and realization, coherent frame publication, failed hot reload,
    music snapshots, and batch/live synthesis.
21. `Composition` proves identity and associativity for pure, fallible,
    state-threading, and failure-retaining arrows, plus product and coproduct
    interchange.
22. `ResourceRoute.Morphism` proves identity, associativity, and tensor
    interchange for typed many-input/many-output boundaries.
23. `CompositionCoverage.allCompositionsChecked` supplies the central
    composition law for the same selected abstract enum.
24. `EngineComposition.full_path_assoc` and `run_is_composed_path` prove
    grouping independence of the complete fallible
    cook/prepare/realize/interpret path and identify it with the public run
    semantics. Its routing theorems prove the exact demand, owner, render,
    audio, music/synth, generation, pack/open, and reload-publication
    connections in the abstract `Modules` model.
25. `ArchitectureGraph.Path.run_assoc` proves every regrouping of every
    well-typed path in the handwritten abstract graph has one interpretation.
26. `Cxx26` separates translation and execution operations by an indexed type,
    makes template arguments types by construction, proves exact reflection
    reification/expansion/splicing, `consteval` composition and failure, one
    stage-coherent `constexpr` meaning, and erasure of translation-only work.
27. `Cxx26Mapping.everyAlgebraComposes` and
    `everyAlgebraLoweringPreservesComposition` prove the target and lowering
    laws for compiler, polynomial, pure, fallible, stateful, transactional,
    resource-route, and relational interfaces.
28. `Cxx26Mapping.allAbstractInterfacesAdmitCandidateImages` and
    `allAbstractEdgesAdmitCandidateImages` prove candidate images inside the
    staged model; they are not extracted from C++ headers.
29. `Generated.ResourceSchema` is emitted from the real reflected resource
    declarations. `ResourceCertificate` proves concrete identity uniqueness,
    endpoint closure, and dependency acyclicity over that generated inventory.

The complete carrier and theorem table is [interfaces.md](interfaces.md).

## Trust boundary

Lean checks the abstract laws, staged target, and any refinement witnesses
presented to it. The Nix proof target also compiles a real-header resource
certificate with `-nostdlib++`, compares its generated Lean data byte-for-byte,
and executes a production texture-codec witness. The Lean kernel, the pinned
Lean executable, GCC, and generated object code remain build-time trust
boundaries. Operating systems, drivers, devices, and foreign codecs are not
proved by the current suite. An abstract theorem becomes an engine guarantee
only when a mechanically connected concrete implementation supplies its
corresponding refinement witness.

The kernel proves resource closure, proof-carrying focus navigation, and
committed-generation consistency in its model. The generated certificate proves
facts about the current reflected resource declarations. It does not prove GCC
object-code correspondence, full runtime resource behavior, foreign codecs,
driver progress, or platform bridges.
