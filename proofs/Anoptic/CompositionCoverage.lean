/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Coverage
import Anoptic.Compiler
import Anoptic.Composition

namespace Anoptic

namespace CompositionCoverage

/-! ## Public-algebra composition witness -/

/-- One normalized declaration witness supplies every public-module compiler. -/
structure Witness where
  demandCapacity : Nat
  residencyCapacity : Nat
  deriving DecidableEq

/-- A public algebra binds one request/response family to its witness projection. -/
structure PublicAlgebra (Shared : Type) where
  api : API
  Local : Type
  Error : Type
  Plan : Type
  projection : Shared → Local
  localCompiler : Compiler.Partial Local Error Plan

def PublicAlgebra.compiler (algebra : PublicAlgebra Shared) :
    Compiler.Partial Shared algebra.Error algebra.Plan :=
  Compiler.projected algebra.projection algebra.localCompiler

structure AssetRequest where
  identity : Nat
  deriving DecidableEq

namespace DemandAlgebra

def publicAPI : API where
  Query := AssetRequest
  Response _ := Bool

structure LocalWitness where
  capacity : Nat
  deriving DecidableEq

inductive CompileError where
  | emptyCapacity
  deriving DecidableEq

structure Plan where
  capacity : Nat
  deriving DecidableEq

def localCompiler : Compiler.Partial LocalWitness CompileError Plan
  | ⟨0⟩ => .error .emptyCapacity
  | ⟨capacity + 1⟩ => .ok ⟨capacity + 1⟩

def algebra : PublicAlgebra Witness where
  api := publicAPI
  Local := LocalWitness
  Error := CompileError
  Plan := Plan
  projection := fun witness => ⟨witness.demandCapacity⟩
  localCompiler := localCompiler

end DemandAlgebra

namespace ResidencyAlgebra

def publicAPI : API where
  Query := AssetRequest
  Response _ := Nat

structure LocalWitness where
  capacity : Nat
  deriving DecidableEq

inductive CompileError where
  | emptyCapacity
  deriving DecidableEq

structure Plan where
  capacity : Nat
  deriving DecidableEq

def localCompiler : Compiler.Partial LocalWitness CompileError Plan
  | ⟨0⟩ => .error .emptyCapacity
  | ⟨capacity + 1⟩ => .ok ⟨capacity + 1⟩

def algebra : PublicAlgebra Witness where
  api := publicAPI
  Local := LocalWitness
  Error := CompileError
  Plan := Plan
  projection := fun witness => ⟨witness.residencyCapacity⟩
  localCompiler := localCompiler

end ResidencyAlgebra

/-- The tensor compiler is exactly the pair of two projections from one witness. -/
def pairedCompiler :=
  Compiler.pair DemandAlgebra.algebra.compiler
    ResidencyAlgebra.algebra.compiler

theorem pairedCompiler_succeeds_iff
    (witness : Witness) (demandPlan : DemandAlgebra.Plan)
    (residencyPlan : ResidencyAlgebra.Plan) :
    pairedCompiler witness = .ok (demandPlan, residencyPlan) ↔
      DemandAlgebra.algebra.compiler witness = .ok demandPlan ∧
      ResidencyAlgebra.algebra.compiler witness = .ok residencyPlan :=
  Compiler.pair_succeeds_iff
    DemandAlgebra.algebra.compiler ResidencyAlgebra.algebra.compiler
    witness demandPlan residencyPlan

/-- Demand queries lower to residency queries; a generation answers demand. -/
def demandToResidency :
    API.Hom DemandAlgebra.algebra.api ResidencyAlgebra.algebra.api where
  onQuery := fun request => request
  onResponse := fun (_ : AssetRequest) (generation : Nat) => generation != 0

/-- Residency may in turn query demand and turn presence into a generation token. -/
def residencyToDemand :
    API.Hom ResidencyAlgebra.algebra.api DemandAlgebra.algebra.api where
  onQuery := fun request => request
  onResponse := fun (_ : AssetRequest) (demanded : Bool) =>
    (if demanded = true then 1 else 0 : Nat)

def serialMorphism :
    API.Hom DemandAlgebra.algebra.api DemandAlgebra.algebra.api :=
  API.Hom.trans demandToResidency residencyToDemand

def tensorMorphism :
    API.Hom (API.tensor DemandAlgebra.algebra.api ResidencyAlgebra.algebra.api)
      (API.tensor ResidencyAlgebra.algebra.api DemandAlgebra.algebra.api) :=
  API.Hom.tensorMap demandToResidency residencyToDemand

def choiceMorphism :
    API.Hom (API.choice DemandAlgebra.algebra.api ResidencyAlgebra.algebra.api)
      (API.choice ResidencyAlgebra.algebra.api DemandAlgebra.algebra.api) :=
  API.Hom.choiceMap demandToResidency residencyToDemand

theorem public_serial_path_associates :
    API.Hom.trans
        (API.Hom.trans demandToResidency residencyToDemand)
        demandToResidency =
      API.Hom.trans demandToResidency
        (API.Hom.trans residencyToDemand demandToResidency) :=
  API.Hom.trans_assoc _ _ _

@[simp] theorem tensorMorphism_query (demand residency : AssetRequest) :
    tensorMorphism.onQuery (demand, residency) = (demand, residency) :=
  rfl

@[simp] theorem choiceMorphism_demand_query (request : AssetRequest) :
    choiceMorphism.onQuery (.inl request) = .inl request :=
  rfl

@[simp] theorem choiceMorphism_residency_query (request : AssetRequest) :
    choiceMorphism.onQuery (.inr request) = .inr request :=
  rfl

def oneBoundary : ResourceRoute.Boundary Unit where
  Port := Unit
  cell _ := ()

abbrev RouteValue : Unit → Type := fun _ => Nat

/-- The central composition law of each selected abstract carrier. -/
def Law : Coverage.Interface → Prop
  | .structural => ∀ first second witness,
      Structural.project second (Structural.project first witness) =
        Structural.project (fun shape => second shape && first shape) witness
  | .outcomes => ∀ (result : Outcome.Result Nat String)
      (first second : Nat → Outcome.Result Nat String),
      Outcome.bind (Outcome.bind result first) second =
        Outcome.bind result (fun value => Outcome.bind (first value) second)
  | .collections => ∀ (value : (API.unit : API.{0}).Extension Nat),
      API.map Nat.succ (API.map Nat.succ value) =
        API.map (Nat.succ ∘ Nat.succ) value
  | .linear => ∀ first second third : Linear.Map Nat Nat,
      Linear.compose (Linear.compose first second) third =
        Linear.compose first (Linear.compose second third)
  | .memory => ∀ limit cursor first second,
      Memory.measure limit cursor (first ++ second) =
        match Memory.measure limit cursor first with
        | .error error => .error error
        | .ok next => Memory.measure limit next second
  | .concurrency =>
      (∀ (channel : Transport.Channel Nat),
        Transport.map Nat.succ (Transport.map Nat.succ channel) =
          Transport.map (Nat.succ ∘ Nat.succ) channel) ∧
      (∀ publication : Transport.Publication Nat Nat,
        (publication.bimap Nat.succ Nat.succ).bimap Nat.succ Nat.succ =
          publication.bimap (Nat.succ ∘ Nat.succ) (Nat.succ ∘ Nat.succ))
  | .time => ∀ (instant : Time.Instant Unit Unit)
      (first second : Time.Duration Unit),
      Time.advance (Time.advance instant first) second =
        Time.advance instant
          (⟨first.ticks + second.ticks⟩ : Time.Duration Unit)
  | .filesystem => ∀ sink first second,
      Filesystem.appendBytes (Filesystem.appendBytes sink first) second =
        Filesystem.appendBytes sink (first ++ second)
  | .strings => ∀ first second third,
      Strings.concat (Strings.concat first second) third =
        Strings.concat first (Strings.concat second third)
  | .diagnostics => ∀ first second,
      Diagnostics.drain (first ++ second) =
        Diagnostics.drain first ++ Diagnostics.drain second
  | .gltf => ∀ (parse : Gltf.Raw → Outcome.Result Gltf.Parsed String)
      (inputs : (parsed : Gltf.Parsed) → Fin parsed.bufferCount → Gltf.Bytes)
      (projection : Gltf.Projection Nat) raw,
      Gltf.parseBindProject parse inputs projection raw =
        Outcome.bind (parse raw) (Gltf.bindProject inputs projection)
  | .mesh => ∀ first second third : Mesh.Transform Nat Nat,
      Mesh.Transform.compose (Mesh.Transform.compose first second) third =
        Mesh.Transform.compose first (Mesh.Transform.compose second third)
  | .resources => ∀
      (first second third :
        ResourceRoute.Morphism RouteValue oneBoundary oneBoundary),
      ResourceRoute.Morphism.compose
          (ResourceRoute.Morphism.compose first second) third =
        ResourceRoute.Morphism.compose first
          (ResourceRoute.Morphism.compose second third)
  | .renderResources => ∀ first second
      (portable : RenderResources.Portable .texture),
      RenderResources.mapPayload second
          (RenderResources.mapPayload first portable) =
        RenderResources.mapPayload (second ∘ first) portable
  | .renderProtocol => ∀ state first second,
      Render.applyCommands state (first ++ second) =
        Render.applyCommands (Render.applyCommands state first) second
  | .inputDisplay => ∀ state first second,
      Input.applyDisplayEvents state (first ++ second) =
        Input.applyDisplayEvents (Input.applyDisplayEvents state first) second
  | .vulkan => ∀
      (first second third :
        Composition.Stateful Vulkan.SessionState Nat Nat),
      Composition.Stateful.compose
          (Composition.Stateful.compose first second) third =
        Composition.Stateful.compose first
          (Composition.Stateful.compose second third)
  | .text => ∀ source faceIndex glyphs textureIdentity,
      Text.openBakeAtlas source faceIndex glyphs textureIdentity =
        Text.makeAtlas
          (Text.bakeFace (Text.openFace source faceIndex) glyphs) textureIdentity
  | .ui => ∀ (first : Composition.Pure Nat UI.PackedScene)
      (last : Composition.Pure (List Nat) Nat),
      Composition.Pure.compose
          (Composition.Pure.compose first UI.cpuInterpreter) last =
        Composition.Pure.compose first
          (Composition.Pure.compose UI.gpuInterpreter last)
  | .audio => ∀ state first second,
      Audio.applyCommands state (first ++ second) =
        Audio.applyCommands (Audio.applyCommands state first) second
  | .music => ∀ state first second,
      Music.applyControls state (first ++ second) =
        Music.applyControls (Music.applyControls state first) second
  | .synth => ∀ state inputs firstCount secondCount,
      let first := Synth.render state inputs ⟨0, firstCount⟩
      let second := Synth.render first.state [] ⟨firstCount, secondCount⟩
      second.state =
          (Synth.render state inputs ⟨0, firstCount + secondCount⟩).state ∧
        first.busSamples ++ second.busSamples =
          (Synth.render state inputs ⟨0, firstCount + secondCount⟩).busSamples
  | .world => ∀ first middle last,
      World.applyDemandDelta
          (World.applyDemandDelta first (World.deriveDemandDelta first middle))
          (World.deriveDemandDelta middle last) = last
  | .engine => ∀ first second third : Nat → Nat,
      Engine.compose (Engine.compose first second) third =
        Engine.compose first (Engine.compose second third)

/-- Every selected carrier is closed under its modeled composition. -/
theorem allCompositionsChecked : ∀ interface, Law interface := by
  intro interface
  cases interface with
  | structural =>
      simp only [Law]
      exact Structural.project_compose
  | outcomes =>
      simp only [Law]
      exact Outcome.bind_assoc
  | collections =>
      simp only [Law]
      exact API.map_comp Nat.succ Nat.succ
  | linear =>
      simp only [Law]
      exact Linear.compose_assoc
  | memory =>
      simp only [Law]
      exact Memory.measure_append
  | concurrency =>
      simp only [Law]
      exact ⟨Transport.map_comp Nat.succ Nat.succ,
        Transport.Publication.bimap_comp Nat.succ Nat.succ Nat.succ Nat.succ⟩
  | time =>
      simp only [Law]
      exact Time.advance_add
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
      exact Gltf.parse_bind_project_assoc
  | mesh =>
      simp only [Law]
      exact Mesh.Transform.compose_assoc
  | resources =>
      simp only [Law]
      exact ResourceRoute.Morphism.compose_assoc
  | renderResources =>
      simp only [Law]
      exact RenderResources.mapPayload_comp
  | renderProtocol =>
      simp only [Law]
      exact Render.applyCommands_append
  | inputDisplay =>
      simp only [Law]
      exact Input.applyDisplayEvents_append
  | vulkan =>
      simp only [Law]
      exact Composition.Stateful.compose_assoc
  | text =>
      simp only [Law]
      exact Text.open_bake_atlas_composes
  | ui =>
      simp only [Law]
      exact UI.interpreter_composition_assoc
  | audio =>
      simp only [Law]
      exact Audio.applyCommands_append
  | music =>
      simp only [Law]
      exact Music.applyControls_append
  | synth =>
      simp only [Law]
      exact Synth.render_chunks_compose
  | world =>
      simp only [Law]
      exact World.demand_deltas_compose
  | engine =>
      simp only [Law]
      exact Engine.compose_assoc

end CompositionCoverage
end Anoptic
