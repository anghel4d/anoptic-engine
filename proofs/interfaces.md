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

The remaining engineering boundary is concrete refinement: the C++26
reflection exporter and each platform interpreter must construct the
law-bearing Lean witness corresponding to the code and foreign behavior they
ship. Lean deliberately does not infer driver fairness, operating-system
progress, codec correctness, or C++ object-code equivalence from an abstract
model.
