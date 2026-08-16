# Lean semantic kernel

The semantic kernel uses Lean 4.33.0 and `Std` only. Its Lake library is `Anoptic`, and every theorem is built by the Nix flake.

## Checked foundations and interfaces

1. `API.Hom.refl_trans`, `trans_refl`, and `trans_assoc` establish the category laws for API-shape adapters whose queries travel forward and responses travel backward.
2. `API.Hom.induced_refl` and `induced_trans` establish that shape adapters act compositionally on polynomial extensions.
3. `Semantic.Effect.Hom` and `Semantic.Algebra.Hom` separately establish the category laws for natural effect maps and meaning-preserving algebra maps. An algebra map contains a shape adapter, carrier map, effect transformation, and commuting proof.
4. `Semantic.NonFullShapeMap.no_algebra_hom_over_adapter` proves that the forgetful map from semantic algebras to API shapes is not full: its identity shape adapter has no homomorphic lift for the selected interpretations. `CompositionCoverage.demandToResidencyAlgebra` separately exhibits a genuine algebra homomorphism, while `shape_morphism_is_not_automatically_algebraic` shows a domain adapter failing under the identity carrier map.
5. `API.choice_unpack_pack`, `choice_pack_unpack`, `tensor_unpack_pack`, and `tensor_pack_unpack` establish the sum and tensor representations.
6. `API.zeroElim` and `unit_unique` keep the zero polynomial distinct from the unit polynomial; `API.Hom.toUnit_unique` proves the terminal arrow is `A -> 1`.
7. `API.Hom.choiceMap` and `tensorMap` lift independent adaptors through API
   coproducts and products; their query and response interchange theorems prove
   compatibility with adaptor composition.
8. `Compiler.projected_apply` establishes that module compilers consume a projection of one shared witness; `projected_noninterference` proves that changing anything outside that projection cannot change the compiler result.
9. `Compiler.pair_succeeds_iff`, `pair_left_error`, `pair_right_error`, and `pair_both_error_selects_left` specify fail-fast applicative pairing, including its observable left-error precedence. This is not categorical product universality or sequential plan composition.
10. `Compiler.Pullback.ofRefined_toRefined` and `toRefined_ofRefined` establish that equality compatibility is the pullback instance of a general proof-carrying refined product.
11. `ResourceRoute.Signature`, `Producer`, and `Provenance` make every transform many-input/many-output and index a producer by its output port. `Provenance.toPolynomial` and `ofPolynomial` prove that provenance is exactly the producer polynomial.
12. `ResourceRoute.Derivative`, `Instance.down`, and `InputFocus.up` model a typed one-hole producer context: `down()` may select any typed input port, while the focus retains exactly one parent instance.
13. `ResourceRoute.shared_left` and `shared_right` establish immutable-reference sharing without adding a diagonal to artifact values.
14. `Refines.refl` and `Refines.trans` establish finite-trace safety-refinement preorder laws; `Safety` and `Progress` keep finite safety distinct from infinite behavior.
15. `Relation.compose_assoc`, `identity_left`, and `identity_right` establish sequential-composition laws.
16. `Relation.compose_mono` establishes that independently proved module refinements compose.
17. `failure_retains_cache` and `failure_preserves_publication` establish that failed work may advance reusable cooker state without changing the published revision.
18. `success_retains_cache`, `success_publishes_revision`, and `publication_changes_only_on_success` establish the publication boundary.
19. `Semantic.Effect.state` and `stateResult` give lawful functorial codomains for state-owning and partial state-owning interpreters; the latter is `StateT(State, Result)`.
20. `Outcome` through `Diagnostics` prove selected abstract sum, value,
    ownership, transport, clock, filesystem, string, and diagnostic laws.
    `ResultAlgebra` additionally proves the admitted Kleisli operations,
    value/error-map laws, fail-fast pairing, bounded inclusive scan, and the
    isomorphism between a finite parameter list and one product-domain arrow.
    Its normalization commutes with Kleisli postcomposition. Reflection,
    `noexcept`, and construction constraints remain C++ compile-time checks.
21. `Gltf` through `Engine` prove selected abstract domain carriers and laws;
    they are not production implementation witnesses.
22. `Coverage.allInterfacesChecked` is exhaustive over its handwritten
    24-constructor enum only.
23. `Prooflet` instantiates dependent APIs, projected compiler pairing,
    multi-output producer identity, compositional refinement, and transactional
    publication as one concrete toolchain smoke proof.
24. `Integration` checks typed polynomial-derivative route navigation, revision-indexed
    residency and realization, coherent frame publication, failed hot reload,
    music snapshots, and batch/live synthesis.
25. `Composition` proves identity and associativity for pure, fallible,
    state-threading, and failure-retaining arrows, plus product and coproduct
    interchange.
26. `ResourceRoute.Morphism` proves identity, associativity, and tensor
    interchange for typed many-input/many-output boundaries.
27. `CompositionCoverage.allCompositionsChecked` supplies the central
    composition law for the same selected abstract enum.
28. `EngineComposition.full_path_assoc` and `run_is_composed_path` prove
    grouping independence of the complete fallible
    cook/prepare/realize/interpret path and identify it with the public run
    semantics. Its routing theorems prove the exact demand, owner, render,
    audio, music/synth, generation, pack/open, and reload-publication
    connections in the abstract `Modules` model.
29. `ArchitectureGraph.Path.run_assoc` proves every regrouping of every
    well-typed path in the handwritten abstract graph has one interpretation.
30. `Cxx26` separates translation and execution operations by an indexed type,
    makes template arguments types by construction, proves exact reflection
    reification/expansion/splicing, `consteval` composition and failure, one
    stage-coherent `constexpr` meaning, and erasure of translation-only work.
31. `Cxx26Mapping.everyAlgebraComposes` and
    `everyAlgebraLoweringPreservesComposition` prove the target and lowering
    laws for compiler, polynomial-shape, semantic-algebra, pure, fallible,
    stateful, transactional, resource-route, and relational interfaces.
32. `Cxx26Mapping.allAbstractInterfacesAdmitCandidateImages` and
    `allAbstractEdgesAdmitCandidateImages` prove candidate images inside the
    staged model; they are not extracted from C++ headers.
33. `Generated.ResourceSchema` is emitted from the real reflected resource
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
