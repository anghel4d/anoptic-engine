/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Structural
import Anoptic.Outcome
import Anoptic.Polynomial
import Anoptic.Linear
import Anoptic.Memory
import Anoptic.Transport
import Anoptic.Time
import Anoptic.Filesystem
import Anoptic.Strings
import Anoptic.Diagnostics
import Anoptic.Gltf
import Anoptic.Mesh
import Anoptic.Resource
import Anoptic.RenderResources
import Anoptic.Render
import Anoptic.Input
import Anoptic.Vulkan
import Anoptic.Text
import Anoptic.UI
import Anoptic.Audio
import Anoptic.Music
import Anoptic.Synth
import Anoptic.World
import Anoptic.Engine

namespace Anoptic

namespace Coverage

/-- The 24 rows of the whole-engine ideal-interface inventory. -/
inductive Interface where
  | structural
  | outcomes
  | collections
  | linear
  | memory
  | concurrency
  | time
  | filesystem
  | strings
  | diagnostics
  | gltf
  | mesh
  | resources
  | renderResources
  | renderProtocol
  | inputDisplay
  | vulkan
  | text
  | ui
  | audio
  | music
  | synth
  | world
  | engine
  deriving DecidableEq

/-- One central, universally quantified law for every inventoried interface. -/
def Law : Interface → Prop
  | .structural => ∀ shape, Structural.normalize (Structural.normalize shape) =
      Structural.normalize shape
  | .outcomes => ∀ result : Outcome.CResult Nat Nat,
      Outcome.CResult.ofResult (Outcome.CResult.toResult result) = result
  | .collections =>
      (∀ _value : (API.zero : API.{0}).Extension Nat, False) ∧
      (∀ value : (API.unit : API.{0}).Extension Nat, value = API.unitValue)
  | .linear => ∀ first second third : Linear.Map Nat Nat,
      Linear.compose (Linear.compose first second) third =
        Linear.compose first (Linear.compose second third)
  | .memory =>
      (∀ first second third : Memory.Plan,
        (first ++ second) ++ third = first ++ (second ++ third)) ∧
      (∀ limit bytes, Memory.reserveChecked limit ⟨0⟩ ⟨bytes, 0⟩ =
        .error .zeroAlignment)
  | .concurrency =>
      (∀ channel : Transport.Channel Nat,
        Transport.map Nat.succ (Transport.map Nat.succ channel) =
          Transport.map (Nat.succ ∘ Nat.succ) channel) ∧
      (∀ publication : Transport.Publication Nat Nat,
        (Transport.observeLeft publication).1 =
          (Transport.observeRight publication).1)
  | .time => ∀ (instant : Time.Instant Unit) (duration : Time.Duration Unit),
      Time.elapsed (DurationUnit := Unit) (Time.advance instant duration) instant =
        duration
  | .filesystem => ∀ (sink : Filesystem.AppendSink) first second,
      Filesystem.appendBytes (Filesystem.appendBytes sink first) second =
        Filesystem.appendBytes sink (first ++ second)
  | .strings => ∀ first second third : Strings.Owned,
      Strings.concat (Strings.concat first second) third =
        Strings.concat first (Strings.concat second third)
  | .diagnostics => ∀ first second : Diagnostics.Log,
      Diagnostics.drain (first ++ second) =
        Diagnostics.drain first ++ Diagnostics.drain second
  | .gltf => ∀ raw : Gltf.Raw,
      (∃ bytes, raw = .json bytes) ∨ (∃ bytes, raw = .glb bytes)
  | .mesh => ∀ (simplify : Mesh.Budget → Nat → Nat) input budgets,
      (Mesh.lodChain simplify input budgets).length = budgets.length
  | .resources => ∀ (edge : Resource.Edge Nat) (roots : Resource.NodeSet Nat),
      Resource.closure edge (Resource.closure edge roots) =
        Resource.closure edge roots
  | .renderResources => ∀ (portable : RenderResources.Portable .texture) owner,
      (RenderResources.realize owner portable).identity = portable.identity
  | .renderProtocol => ∀ (state : Render.State) first second,
      Render.applyCommands state (first ++ second) =
        Render.applyCommands (Render.applyCommands state first) second
  | .inputDisplay => ∀ (state : Input.DisplayState) width height,
      Input.applyDisplayEvent state (.resize width height) =
        ⟨width, height, state.focused⟩
  | .vulkan => ∀ (state : Vulkan.SessionState) (resident : Nat)
      (commands : List Render.Command) (view : Nat),
      ((Vulkan.referenceInterpreter Nat Nat).run state resident commands view).frame.number =
        state.submittedFrame + 1
  | .text => ∀ value : Strings.View,
      Text.measure value = (Text.shape value).length
  | .ui => ∀ scene : UI.PackedScene,
      UI.evalCpu scene = UI.evalGpu scene
  | .audio => ∀ frames state commands listener,
      Audio.native frames state commands listener =
        Audio.offline frames state commands listener
  | .music => ∀ state : Music.State,
      Music.restore (Music.snapshot state) = state
  | .synth => ∀ state inputs frames,
      Synth.render state (Synth.batchStream inputs) frames =
        Synth.render state (Synth.liveStream inputs) frames
  | .world => ∀ previous current : World.Demand,
      World.applyDemandDelta previous (World.deriveDemandDelta previous current) =
        current
  | .engine => ∀ first second third : Nat → Nat,
      Engine.compose (Engine.compose first second) third =
        Engine.compose first (Engine.compose second third)

/-- Exhaustive pattern matching prevents adding an interface without a proof case. -/
theorem allInterfacesChecked : ∀ interface, Law interface := by
  intro interface
  cases interface with
  | structural =>
      simp only [Law]
      exact Structural.normalize_idempotent
  | outcomes =>
      simp only [Law]
      exact Outcome.CResult.ofResult_toResult
  | collections =>
      simp only [Law]
      exact ⟨API.zeroElim, API.unit_unique⟩
  | linear =>
      simp only [Law]
      exact Linear.compose_assoc
  | memory =>
      simp only [Law]
      constructor
      · exact Memory.plan_assoc
      · intro limit bytes
        exact Memory.reserveChecked_rejects_zero_alignment limit ⟨0⟩ bytes
  | concurrency =>
      simp only [Law]
      exact ⟨Transport.map_comp Nat.succ Nat.succ,
        Transport.publication_generations_agree⟩
  | time =>
      simp only [Law]
      exact Time.elapsed_advance
  | filesystem =>
      simp only [Law]
      exact Filesystem.appendBytes_assoc
  | strings =>
      simp only [Law]
      exact Strings.concat_assoc
  | diagnostics =>
      simp only [Law]
      exact Diagnostics.drain_append
  | gltf =>
      simp only [Law]
      exact Gltf.raw_exhaustive
  | mesh =>
      simp only [Law]
      exact Mesh.lodChain_length
  | resources =>
      simp only [Law]
      exact Resource.closure_idempotent
  | renderResources =>
      simp only [Law]
      intro portable owner
      exact RenderResources.realize_preserves_identity owner portable
  | renderProtocol =>
      simp only [Law]
      exact Render.applyCommands_append
  | inputDisplay =>
      simp only [Law]
      exact Input.resize_sets_extent
  | vulkan =>
      simp only [Law]
      exact Vulkan.reference_advances_frame
  | text =>
      simp only [Law]
      exact Text.measure_matches_shape_count
  | ui =>
      simp only [Law]
      exact UI.cpu_gpu_equivalent
  | audio =>
      simp only [Law]
      exact Audio.native_offline_equivalent
  | music =>
      simp only [Law]
      exact Music.restore_snapshot
  | synth =>
      simp only [Law]
      exact Synth.batch_live_equivalent
  | world =>
      simp only [Law]
      exact World.deriveDemandDelta_applies
  | engine =>
      simp only [Law]
      exact Engine.compose_assoc

end Coverage
end Anoptic
