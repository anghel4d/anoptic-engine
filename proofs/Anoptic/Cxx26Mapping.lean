/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Cxx26
import Anoptic.Coverage
import Anoptic.CompositionCoverage
import Anoptic.ArchitectureGraph
import Anoptic.EngineComposition

namespace Anoptic

namespace Cxx26Mapping

open Cxx26

/-! ## Algebra-to-C++26 lowering coverage -/

inductive Algebra where
  | compiler
  | polynomial
  | pure
  | fallible
  | stateful
  | transactional
  | resourceRoute
  | relational
  deriving DecidableEq, Repr

/-- Each abstract algebra has an associative direct target representation. -/
def SupportsComposition : Algebra → Prop
  | .compiler =>
      ∀ (A B C D Error : Type)
        (first : Immediate A Error B) (second : Immediate B Error C)
        (third : Immediate C Error D),
        Immediate.compose (Immediate.compose first second) third =
          Immediate.compose first (Immediate.compose second third)
  | .polynomial =>
      ∀ (A B C D : API.{0}) (first : API.Hom A B)
        (second : API.Hom B C) (third : API.Hom C D),
        API.Hom.trans (API.Hom.trans first second) third =
          API.Hom.trans first (API.Hom.trans second third)
  | .pure =>
      ∀ (A B C D : Type) (first : Runtime.Pure A B)
        (second : Runtime.Pure B C) (third : Runtime.Pure C D),
        Runtime.Pure.compose (Runtime.Pure.compose first second) third =
          Runtime.Pure.compose first (Runtime.Pure.compose second third)
  | .fallible =>
      ∀ (A B C D Error : Type) (first : Runtime.Fallible Error A B)
        (second : Runtime.Fallible Error B C)
        (third : Runtime.Fallible Error C D),
        Runtime.Fallible.compose (Runtime.Fallible.compose first second) third =
          Runtime.Fallible.compose first
            (Runtime.Fallible.compose second third)
  | .stateful =>
      ∀ (A B C D State : Type) (first : Runtime.Stateful State A B)
        (second : Runtime.Stateful State B C)
        (third : Runtime.Stateful State C D),
        Runtime.Stateful.compose (Runtime.Stateful.compose first second) third =
          Runtime.Stateful.compose first
            (Runtime.Stateful.compose second third)
  | .transactional =>
      ∀ (A B C D State Error : Type)
        (first : Runtime.Transactional State Error A B)
        (second : Runtime.Transactional State Error B C)
        (third : Runtime.Transactional State Error C D),
        Runtime.Transactional.compose
            (Runtime.Transactional.compose first second) third =
          Runtime.Transactional.compose first
            (Runtime.Transactional.compose second third)
  | .resourceRoute =>
      ∀ (Cell : Type) (Value : Cell → Type)
        (A B C D : ResourceRoute.Boundary Cell)
        (first : ResourceRoute.Morphism Value A B)
        (second : ResourceRoute.Morphism Value B C)
        (third : ResourceRoute.Morphism Value C D),
        ResourceRoute.Morphism.compose
            (ResourceRoute.Morphism.compose first second) third =
          ResourceRoute.Morphism.compose first
            (ResourceRoute.Morphism.compose second third)
  | .relational =>
      ∀ (A B C D : Type) (first : Relation A B) (second : Relation B C)
        (third : Relation C D) (source : A) (target : D),
        Relation.compose (Relation.compose first second) third source target ↔
          Relation.compose first (Relation.compose second third) source target

theorem everyAlgebraComposes : ∀ algebra, SupportsComposition algebra := by
  intro algebra
  cases algebra with
  | compiler =>
      simp only [SupportsComposition]
      intro A B C D Error first second third
      exact Immediate.compose_assoc first second third
  | polynomial =>
      simp only [SupportsComposition]
      intro A B C D first second third
      exact API.Hom.trans_assoc first second third
  | pure =>
      simp only [SupportsComposition]
      intro A B C D first second third
      exact Runtime.Pure.compose_assoc first second third
  | fallible =>
      simp only [SupportsComposition]
      intro A B C D Error first second third
      exact Runtime.Fallible.compose_assoc first second third
  | stateful =>
      simp only [SupportsComposition]
      intro A B C D State first second third
      exact Runtime.Stateful.compose_assoc first second third
  | transactional =>
      simp only [SupportsComposition]
      intro A B C D State Error first second third
      exact Runtime.Transactional.compose_assoc first second third
  | resourceRoute =>
      simp only [SupportsComposition]
      intro Cell Value A B C D first second third
      exact ResourceRoute.Morphism.compose_assoc first second third
  | relational =>
      simp only [SupportsComposition]
      intro A B C D first second third source target
      exact Relation.compose_assoc first second third source target

/-- Lowering preserves the composition operation of each source algebra. -/
def PreservesLowering : Algebra → Prop
  | .compiler =>
      ∀ (Shared Local Error Plan : Type) (projection : Shared → Local)
        (compiler : Compiler.Partial Local Error Plan) (witness : Shared),
        (lowerPartial (Compiler.projected projection compiler)).evaluate witness =
          (lowerPartial compiler).evaluate (projection witness)
  | .polynomial =>
      ∀ (source middle target : API.{0}) (Value : Type)
        (first : API.Hom source middle) (second : API.Hom middle target)
        (value : source.Extension Value),
        (lowerApiHom "composed" (API.Hom.trans first second)).run value =
          (Runtime.Pure.compose (lowerApiHom "first" first)
            (lowerApiHom "second" second)).run value
  | .pure =>
      ∀ (A B C : Type) (first : Composition.Pure A B)
        (second : Composition.Pure B C) (input : A),
        (Runtime.Pure.compose (lowerPure "first" first)
          (lowerPure "second" second)).run input =
          (lowerPure "composed"
            (Composition.Pure.compose first second)).run input
  | .fallible =>
      ∀ (A B C Error : Type) (first : Composition.Fallible Error A B)
        (second : Composition.Fallible Error B C) (input : A),
        (Runtime.Fallible.compose (lowerFallible "first" first)
          (lowerFallible "second" second)).run input =
          (lowerFallible "composed"
            (Composition.Fallible.compose first second)).run input
  | .stateful =>
      ∀ (A B C State : Type) (first : Composition.Stateful State A B)
        (second : Composition.Stateful State B C) (input : A) (state : State),
        (Runtime.Stateful.compose (lowerStateful "first" first)
          (lowerStateful "second" second)).run input state =
          (lowerStateful "composed"
            (Composition.Stateful.compose first second)).run input state
  | .transactional =>
      ∀ (A B C State Error : Type)
        (first : Composition.Transactional State Error A B)
        (second : Composition.Transactional State Error B C)
        (input : A) (state : State),
        (Runtime.Transactional.compose (lowerTransactional "first" first)
          (lowerTransactional "second" second)).run input state =
          (lowerTransactional "composed"
            (Composition.Transactional.compose first second)).run input state
  | .resourceRoute =>
      ∀ (Cell : Type) (Value : Cell → Type)
        (A B C : ResourceRoute.Boundary Cell)
        (first : ResourceRoute.Morphism Value A B)
        (second : ResourceRoute.Morphism Value B C) (input : A.Values Value),
        (Runtime.Pure.compose (lowerRoute "first" first)
          (lowerRoute "second" second)).run input =
          (lowerRoute "composed"
            (ResourceRoute.Morphism.compose first second)).run input
  | .relational =>
      ∀ (A B C : Type) (first : Relation A B) (second : Relation B C)
        (source : A) (target : C),
        Relation.compose first second source target ↔
          ∃ middle, first source middle ∧ second middle target

theorem everyAlgebraLoweringPreservesComposition :
    ∀ algebra, PreservesLowering algebra := by
  intro algebra
  cases algebra with
  | compiler =>
      simp only [PreservesLowering]
      intro Shared Local Error Plan projection compiler witness
      exact lowerPartial_projected_preserves projection compiler witness
  | polynomial =>
      simp only [PreservesLowering]
      intro source middle target Value first second value
      exact lowerApiHom_trans_preserves first second value
  | pure =>
      simp only [PreservesLowering]
      intro A B C first second input
      exact lowerPure_compose_preserves first second input
  | fallible =>
      simp only [PreservesLowering]
      intro A B C Error first second input
      exact lowerFallible_compose_preserves first second input
  | stateful =>
      simp only [PreservesLowering]
      intro A B C State first second input state
      exact lowerStateful_compose_preserves first second input state
  | transactional =>
      simp only [PreservesLowering]
      intro A B C State Error first second input state
      exact lowerTransactional_compose_preserves first second input state
  | resourceRoute =>
      simp only [PreservesLowering]
      intro Cell Value A B C first second input
      exact lowerRoute_compose_preserves first second input
  | relational =>
      simp only [PreservesLowering, Relation.compose]
      intros
      exact True.intro

/-! ## Exhaustive interface images -/

structure InterfaceImage where
  name : String
  algebra : Algebra
  query : TypeExpr
  response : TypeExpr
  declaration : Declaration
  translation : List (Operation .translation)
  execution : List (Operation .execution)

def translationOperations (name : String) : List (Operation .translation) :=
  [.reflect name,
    .immediateCheck (name ++ "::validate"),
    .constexprFold (name ++ "::normalize"),
    .instantiateType (name ++ "::carrier"),
    .expand (name ++ "::members"),
    .splice (name ++ "::direct-operations")]

def executionOperations (algebra : Algebra)
    (name : String) : List (Operation .execution) :=
  [.constexprCall (name ++ "::checked-value-operation"),
    .directCall (name ++ "::run")] ++
  match algebra with
  | .stateful => [.threadState name]
  | .transactional => [.threadState name, .publish name]
  | _ => []

def makeImage (name : String) (algebra : Algebra)
    (query response : TypeExpr) : InterfaceImage :=
  { name
    algebra
    query
    response
    declaration :=
      { name := name ++ "::Interface"
        fields := [⟨"query", query⟩, ⟨"response", response⟩] }
    translation := translationOperations name
    execution := executionOperations algebra name }

def hasFeature {stage : Stage} (operations : List (Operation stage))
    (feature : Feature) : Bool :=
  operations.any fun operation => decide (operation.feature = feature)

def WellStaged (image : InterfaceImage) : Prop :=
  hasFeature image.translation .reflection = true ∧
  hasFeature image.translation .consteval = true ∧
  hasFeature image.translation .constexpr = true ∧
  hasFeature image.translation .typeTemplate = true ∧
  hasFeature image.translation .expansion = true ∧
  hasFeature image.translation .splice = true ∧
  hasFeature image.execution .constexpr = true ∧
  hasFeature image.execution .directCall = true ∧
  ∀ operation ∈ image.execution,
    operation.feature.translationOnly = false

def Compiles (image : InterfaceImage) : Prop :=
  ∃ plan, compileDeclaration.evaluate image.declaration = .ok plan

theorem makeImage_wellStaged (name : String) (algebra : Algebra)
    (query response : TypeExpr) :
    WellStaged (makeImage name algebra query response) := by
  refine ⟨?_, ?_, ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩
  · rfl
  · rfl
  · rfl
  · rfl
  · rfl
  · rfl
  · rfl
  · rfl
  · intro operation present
    exact execution_excludes_translation_only operation

theorem makeImage_compiles (name : String) (algebra : Algebra)
    (query response : TypeExpr) :
    Compiles (makeImage name algebra query response) := by
  let schema : Schema :=
    ⟨name ++ "::Interface",
      [⟨"query", query⟩, ⟨"response", response⟩]⟩
  refine ⟨emit schema, ?_⟩
  rfl

private def named (name : String) : TypeExpr := .named name

def interfaceImage : Coverage.Interface → InterfaceImage
  | .structural =>
      makeImage "structural" .compiler (named "Declaration")
        (resultType (named "NormalizedWitness") (named "CompileError"))
  | .outcomes =>
      makeImage "outcomes" .fallible (named "Input")
        (resultType (named "Value") (named "Error"))
  | .collections =>
      makeImage "collections" .polynomial
        (.choice [named "QueryA", named "QueryB"])
        (.application "Response" [named "SelectedQuery"])
  | .linear =>
      makeImage "linear" .pure
        (.product [("left", named "Vector"), ("right", named "Vector")])
        (named "Vector")
  | .memory =>
      makeImage "memory" .fallible
        (.product [("cursor", named "Cursor"), ("segments", spanType (named "Segment"))])
        (resultType (named "Layout") (named "MemoryError"))
  | .concurrency =>
      makeImage "concurrency" .transactional (named "PublicationRequest")
        (resultType (named "Generation") (named "TransportError"))
  | .time =>
      makeImage "time" .pure
        (.product [("instant", named "Instant"), ("duration", named "Duration")])
        (named "Instant")
  | .filesystem =>
      makeImage "filesystem" .fallible (named "FileRequest")
        (resultType (named "FileValue") (named "FileError"))
  | .strings =>
      makeImage "strings" .fallible
        (.product [("left", named "StringView"), ("right", named "StringView")])
        (resultType (named "OwnedString") (named "AllocationError"))
  | .diagnostics =>
      makeImage "diagnostics" .stateful (named "DiagnosticCommand")
        (named "DiagnosticState")
  | .gltf =>
      makeImage "gltf" .fallible
        (.choice [named "JsonBytes", named "GlbBytes"])
        (resultType (named "BoundGltf") (named "GltfError"))
  | .mesh =>
      makeImage "mesh" .pure (named "CanonicalMesh")
        (spanType (named "MeshLod"))
  | .resources =>
      makeImage "resources" .resourceRoute
        (.product [("inputs", spanType (assetRefType (named "InputCell"))),
          ("settings", named "TransformSettings")])
        (.product [("outputs", spanType (assetRefType (named "OutputCell"))),
          ("provenance", named "Provenance")])
  | .renderResources =>
      makeImage "render-resources" .pure
        (.product [("portable", assetRefType (named "PortableCell")),
          ("owner", named "RenderOwner")])
        (named "RenderSlot")
  | .renderProtocol =>
      makeImage "render-protocol" .stateful
        (.choice [named "Draw", named "Upload", named "Present"])
        (named "RenderState")
  | .inputDisplay =>
      makeImage "input-display" .stateful
        (.choice [named "InputEvent", named "DisplayEvent"])
        (named "DisplayState")
  | .vulkan =>
      makeImage "vulkan" .stateful (named "RenderCommands")
        (resultType (named "Frame") (named "DeviceError"))
  | .text =>
      makeImage "text" .fallible
        (.product [("font", assetRefType (named "FontBake")),
          ("text", named "StringView")])
        (resultType (spanType (named "Glyph")) (named "TextError"))
  | .ui =>
      makeImage "ui" .pure (named "UiDescription") (named "PackedUiScene")
  | .audio =>
      makeImage "audio" .stateful
        (.choice [named "Play", named "Stop", named "SetListener"])
        (named "AudioBlock")
  | .music =>
      makeImage "music" .stateful (named "MusicControl") (named "MusicBar")
  | .synth =>
      makeImage "synth" .stateful (named "SynthInput") (named "AudioBus")
  | .world =>
      makeImage "world" .stateful (named "WorldInput")
        (.product [("demand", named "Demand"),
          ("render", named "RenderCommands"),
          ("audio", named "AudioCommands"),
          ("ui", named "PackedUiScene")])
  | .engine =>
      makeImage "engine" .transactional (named "FrameRequest")
        (resultType
          (.product [("frame", named "Frame"), ("audio", named "AudioBlock")])
          (named "EngineError"))

theorem everyInterfaceWellStaged :
    ∀ interface, WellStaged (interfaceImage interface) := by
  intro interface
  cases interface <;> apply makeImage_wellStaged

theorem everyInterfaceCompiles :
    ∀ interface, Compiles (interfaceImage interface) := by
  intro interface
  cases interface <;> apply makeImage_compiles

def MappingCertificate (interface : Coverage.Interface) : Prop :=
  Coverage.Law interface ∧
  CompositionCoverage.Law interface ∧
  WellStaged (interfaceImage interface) ∧
  Compiles (interfaceImage interface) ∧
  SupportsComposition (interfaceImage interface).algebra ∧
  PreservesLowering (interfaceImage interface).algebra

/-- All 24 ideal interfaces have a checked, composable, staged C++26 image. -/
theorem allInterfacesMapToComposableCxx26 :
    ∀ interface, MappingCertificate interface := by
  intro interface
  exact ⟨Coverage.allInterfacesChecked interface,
    CompositionCoverage.allCompositionsChecked interface,
    everyInterfaceWellStaged interface,
    everyInterfaceCompiles interface,
    everyAlgebraComposes (interfaceImage interface).algebra,
    everyAlgebraLoweringPreservesComposition
      (interfaceImage interface).algebra⟩

/-! ## Whole-graph and composition-root preservation -/

def cellType : ArchitectureGraph.Cell → TypeExpr
  | .source => named "Source"
  | .rawGltf => named "RawGltf"
  | .boundGltf => named "BoundGltf"
  | .canonicalMesh => named "CanonicalMesh"
  | .revision => named "Revision"
  | .pack => named "Pack"
  | .demand => named "Demand"
  | .epoch => named "Epoch"
  | .renderSlot => named "RenderSlot"
  | .audioSlot => named "AudioSlot"
  | .textSlot => named "TextSlot"
  | .inputEvent => named "InputEvent"
  | .timeStep => named "TimeStep"
  | .world => named "World"
  | .renderCommand => named "RenderCommand"
  | .audioCommand => named "AudioCommand"
  | .uiScene => named "UiScene"
  | .musicControl => named "MusicControl"
  | .musicBar => named "MusicBar"
  | .synthBus => named "SynthBus"
  | .frame => named "Frame"
  | .audioBlock => named "AudioBlock"
  | .save => named "Save"

def edgeName : ArchitectureGraph.EdgeName → String
  | .acquireGltf => "acquire-gltf"
  | .parseBindGltf => "parse-bind-gltf"
  | .projectMesh => "project-mesh"
  | .cookMesh => "cook-mesh"
  | .cookOtherSource => "cook-other-source"
  | .packRevision => "pack-revision"
  | .openPack => "open-pack"
  | .inputWorld => "input-world"
  | .timeWorld => "time-world"
  | .revisionWorld => "revision-world"
  | .worldDemand => "world-demand"
  | .worldRender => "world-render"
  | .worldAudio => "world-audio"
  | .worldUi => "world-ui"
  | .worldMusic => "world-music"
  | .worldSave => "world-save"
  | .loadSave => "load-save"
  | .revisionResidency => "revision-residency"
  | .demandResidency => "demand-residency"
  | .realizeRender => "realize-render"
  | .realizeAudio => "realize-audio"
  | .realizeText => "realize-text"
  | .renderSlotFrame => "render-slot-frame"
  | .textSlotFrame => "text-slot-frame"
  | .renderCommandFrame => "render-command-frame"
  | .uiFrame => "ui-frame"
  | .audioSlotBlock => "audio-slot-block"
  | .audioCommandBlock => "audio-command-block"
  | .musicStep => "music-step"
  | .synthStep => "synth-step"
  | .synthBlock => "synth-block"

def edgeImage (name : ArchitectureGraph.EdgeName) : InterfaceImage :=
  let edge := ArchitectureGraph.namedStep name
  makeImage (edgeName name) .pure (cellType edge.source) (cellType edge.target)

def EdgeCertificate (name : ArchitectureGraph.EdgeName) : Prop :=
  Nonempty (ArchitectureGraph.Step
    (ArchitectureGraph.namedStep name).source
    (ArchitectureGraph.namedStep name).target) ∧
  WellStaged (edgeImage name) ∧
  Compiles (edgeImage name) ∧
  SupportsComposition .pure ∧ PreservesLowering .pure

/-- All 31 declared architecture edges have typed, composable C++26 images. -/
theorem allArchitectureEdgesMapToComposableCxx26 :
    ∀ name, EdgeCertificate name := by
  intro name
  exact ⟨ArchitectureGraph.every_named_edge_is_typed name,
    makeImage_wellStaged _ _ _ _, makeImage_compiles _ _ _ _,
    everyAlgebraComposes .pure,
    everyAlgebraLoweringPreservesComposition .pure⟩

def lowerPath (interpret : ArchitectureGraph.Interpretation Value)
    (path : ArchitectureGraph.Path source target) :
    Runtime.Pure (Value source) (Value target) :=
  lowerPure "architecture-path" (path.run interpret)

theorem lowerPath_compose_preserves
    (interpret : ArchitectureGraph.Interpretation Value)
    (first : ArchitectureGraph.Path source middle)
    (second : ArchitectureGraph.Path middle target) (input : Value source) :
    (Runtime.Pure.compose (lowerPath interpret first)
      (lowerPath interpret second)).run input =
      (lowerPath interpret (.compose first second)).run input :=
  rfl

theorem loweredPathCompositionAssociates
    (interpret : ArchitectureGraph.Interpretation Value)
    (first : ArchitectureGraph.Path A B)
    (second : ArchitectureGraph.Path B C)
    (third : ArchitectureGraph.Path C D) :
    Runtime.Pure.compose
        (Runtime.Pure.compose (lowerPath interpret first)
          (lowerPath interpret second))
        (lowerPath interpret third) =
      Runtime.Pure.compose (lowerPath interpret first)
        (Runtime.Pure.compose (lowerPath interpret second)
          (lowerPath interpret third)) :=
  Runtime.Pure.compose_assoc _ _ _

/-- The concrete four-part engine path lowers without changing public meaning. -/
theorem engineFullPathPreserved
    (modules : EngineComposition.Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (request : EngineComposition.Request Source WorldInput) :
    (lowerFallible "engine-full-path"
      (Composition.Fallible.compose (EngineComposition.cookArrow modules)
        (Composition.Fallible.compose
          (EngineComposition.prepareFallible modules)
          (Composition.Fallible.compose
            (EngineComposition.realizeFallible modules)
            (EngineComposition.interpretFallible modules))))).run request =
      EngineComposition.run modules request := by
  rw [EngineComposition.run_is_composed_path]
  rfl

end Cxx26Mapping
end Anoptic
