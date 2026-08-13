# Formal interface coverage

The Lean kernel contains an abstract carrier and checked laws for every one of
the 24 ideal interfaces in the whole-engine factorization. The exhaustive
enumeration is `Anoptic.Coverage.Interface`; `allInterfacesChecked` pattern
matches every constructor and produces its central law. Adding an interface to
that enumeration without adding a proof case fails the build.

This is semantic coverage, not a claim that the current C++ implementation
already refines every ideal interface. Pure reference definitions are proved
directly. Operations supplied by codecs, devices, operating systems, and C++
reflection use law-bearing structures: constructing the bridge value requires
the corresponding proof. There are no Lean axioms, `sorry`, or admitted
theorems in the kernel.

| Ideal interface | Lean carrier | Machine-checked laws | Abstract status |
|---|---|---|---|
| Structural compiler | `Structural.Shape`, `Witness`; `Compiler.Partial`, `Pullback`, `RefinedProduct` | Normalization and projection idempotence; projected application; independent compiler success; pullback/refined-product isomorphism | Checked |
| Outcomes | `Outcome.Result`, `CResult` | Left/right identity, associativity, functor composition, and C-lowering round trips | Checked |
| Collections | `API.zero`, `API.unit`; standard lists for finite sequences | Zero is uninhabited, unit is unique, and every API has one arrow to unit | Checked; no engine collection module is introduced |
| Linear algebra | `Linear.Vec(Scalar, Dimension, Space, Layout)`, typed `Map`, `Matrix` | Typed composition identity and associativity | Checked |
| Memory | Ordered `Plan`, checked cursor operations, plan-indexed `Volume`, `Owner`, `Region` | Plan monoid laws, fold composition, zero-alignment and overflow behavior, build/freeze index preservation, owner-level retention, and scratch reset | Checked |
| Concurrency | `Channel`, capability-separated `Producer`/`Consumer`, `Latest`, `Lanes`, `Publication` | FIFO base case, channel map identity/composition, lane independence, and committed-generation agreement | Checked |
| Time | Clock-indexed `Instant`, unit-indexed `Duration` | Advance identity/composition, elapsed/advance inverse, and conversion identity/composition | Checked |
| Filesystem | Root-indexed `Path`, `Source` sum, immutable `Snapshot`, `AppendSink` | Path and sink append identity/associativity; source-sum elimination; snapshot preservation | Checked |
| Strings | `View`, `Owned`, refined `Utf8`, phase-indexed `Builder`, `InternState` | Concatenation monoid, builder transition preservation, UTF forgetting, and intern idempotence | Checked |
| Diagnostics | `Record`, free `Log`, `Sink`, `CrashRecord -> Empty` | Drain is a list-monoid homomorphism; crash interpretation cannot return | Checked |
| glTF/GLB | `Gltf.Raw = json + glb`, `Parsed`, dependent `Bound` | Source alternatives are disjoint and exhaustive; binding requires every indexed buffer and retains parsed source identity | Checked |
| Mesh | Pure typed `Mesh.Transform`, `lodChain` | Transform identity/associativity and one LOD result per requested budget | Checked |
| Resource manager | `ResourceRoute.Signature`, `Producer`, `Provenance`; `Reach`, `Codec`, revision-indexed `Epoch`, bifurcating `Route`, `Demand`, COW store; transactional publication | Many-port producer identity, immutable reference sharing, closure extensivity/monotonicity/idempotence, codec/open round trip, epoch map laws, `up()` after selected `down()`, demand union laws, COW locality, and failure publication isolation | Checked |
| Render resources | Capability-indexed `Portable`, opaque `Slot`, `Retired` | Realization preserves semantic identity, batch cardinality, and fence retirement threshold | Checked |
| Render protocol | Actual `Command` and `Event` sums, `State`, `Bulk`, `FrameInput` | Sequential command append and bulk/scalar equivalence | Checked |
| Input/display | Independent `Input.Event` sum and `DisplayState` | Resize/focus interpretation and unrelated-event noninterference | Checked |
| Vulkan | Private `Vulkan.Interpreter` and reference interpreter | Reference frame advancement and interpreter determinism at equal inputs | Checked abstract interpreter |
| Text | Retaining `FontSource -> Face -> Bake -> Atlas`; pure shape/measure | Atlas retains the original font source; glyph count and measurement agree | Checked |
| UI | `Primitive` sum, `Scene`, `PackedScene`, typed capacity outcome | Capacity success and CPU/GPU interpretation equivalence to one scene specification | Checked abstract interpreters |
| Audio | Actual command/event sums, deterministic block transition, encoded/resident/stream source branches | Command fold composition, native/offline equivalence, and identity preservation through resident/stream branches | Checked |
| Music | `Control` sum, deterministic `State -> State x Bar`, canonical `Snapshot` | Determinism and both snapshot/restore inverse laws | Checked |
| Synth | `Input` sum and score/bar-to-samples `render` transducer | Equal ordered streams produce equal output; batch/live paths coincide | Checked |
| World/ECS/save | Stateful demand delta, generation-indexed `FrameWorld`, persistent `SaveCell` | Derived deltas reconstruct current demand, removal retracts demand, deltas are history-dependent, frame generations agree by construction, and matching-manifest save/load round trips | Checked |
| Engine root | Pure interpreter composition, phase-indexed `Process`, reversed session teardown | Composition identity/associativity, reverse teardown, and legal start/stop transitions | Checked |

`Anoptic.Integration` then composes representative laws across the boundaries:
a bifurcating editor-to-source route, revision-indexed residency, render-slot
realization, coordinated world/resource publication, failed hot-reload
publication isolation, music snapshotting, and batch/live synthesis.

## Composition closure

`Anoptic.CompositionCoverage.allCompositionsChecked` exhaustively matches the
same 24-interface enumeration and proves the central composition law for every
case. The common operators remain separate because they have different
semantics: pure composition, product/coproduct interchange, fallible Kleisli
composition, owner-state threading, failure-retaining transactional
composition, relational composition, and many-port resource-route composition.

| Ideal interface | Composition | Checked result |
|---|---|---|
| Structural compiler | Witness projections and partial compiler projections | Projection composition and compiler factorization agree |
| Outcomes | `Result` bind | Left/right identity and associativity; failure short-circuits |
| Collections | Polynomial extension `map` | Identity and function composition |
| Linear algebra | Space-indexed maps | Category identity and associativity |
| Memory | Ordered plan concatenation and cursor fold | Plan associativity and `measure(P ++ Q) = measure(P); measure(Q)` |
| Concurrency | Channel `map`, publication `bimap`, independent lane tensor | Functor composition, generation preservation, and componentwise composition |
| Time | Duration advance and unit conversion | Sequential advance and conversion composition |
| Filesystem | Path and sink append | Ordered append identity and associativity |
| Strings | Immutable concatenation | Monoid identity and associativity |
| Diagnostics | Log concatenation followed by drain | Drain is a monoid homomorphism |
| glTF/GLB | Parse, bind indexed buffers, project | Both groupings of the fallible pipeline are equal |
| Mesh | Pure conditioning transforms | Identity and associative pipeline composition |
| Resource manager | Many-port morphisms, tensor, dependency closure, transaction | Route category laws, tensor interchange, closure idempotence, and failure isolation |
| Render resources | Portable-payload transforms followed by realization | Transform composition preserves semantic identity |
| Render protocol | Ordered command batches | Concatenated and sequential command folds agree |
| Input/display | Ordered event batches | Concatenated and sequential display folds agree |
| Vulkan | Session-state arrows | State threading is associative and retains order |
| Text | Source, face, bake, atlas | The composed path retains the original font source |
| UI | Scene packing and CPU/GPU interpreters | Regrouping is associative and both interpreters share one observation |
| Audio | Ordered commands, mixer core, native/offline sinks | Command folds compose and sink interpretations agree |
| Music | Ordered controls and snapshot isomorphism | Control folds compose; snapshot/restore are inverse |
| Synth | Ordered inputs and adjacent frame ranges | Chunked rendering composes to one range; batch/live paths agree |
| World/ECS/save | Demand deltas, coordinated epochs, save/load | Sequential deltas reconstruct the final demand; generations and save round trips agree |
| Engine root | Cook, prepare, realize, interpret, startup/teardown | Fallible whole-path regrouping is equal and lifecycle order is preserved |

`Anoptic.ArchitectureGraph` separately enumerates every declared cross-module
dependency edge as an indexed `Step`. Its `Path` type permits composition only
when the intermediate semantic cell matches. `Path.run_assoc` proves that every
regrouping of every well-typed architecture path has the same interpretation.
Concrete checked paths include source-to-frame, opened-pack-to-frame,
input-to-frame, world/music-to-audio, and world/save round trip.

`Anoptic.EngineComposition` proves the joins and forks that unary paths cannot
express alone. It routes current world demand into residency; shares one
revision-indexed epoch across render, audio, and text owners; sends world render
commands and UI to rendering; sends world audio commands plus music/synth output
to audio; pairs ECS and residency at one generation; proves the complete
fallible cook/prepare/realize/interpret composition independent of grouping;
proves cook/pack/open identity; and proves failed composed reloads cannot change
publication.

The remaining engineering boundary is concrete refinement: the C++26
reflection exporter and each platform interpreter must construct the
law-bearing Lean witness corresponding to the code and foreign behavior they
ship. Lean deliberately does not infer driver fairness, operating-system
progress, codec correctness, or C++ object-code equivalence from an abstract
model.
