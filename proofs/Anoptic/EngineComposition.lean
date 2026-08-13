/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Composition
import Anoptic.Polynomial
import Anoptic.Resource
import Anoptic.World
import Anoptic.Render
import Anoptic.Audio
import Anoptic.Music
import Anoptic.Synth

namespace Anoptic

namespace EngineComposition

structure Request (Source WorldInput : Type) where
  source : Source
  worldInput : WorldInput
  musicState : Music.State
  musicControls : List Music.Control
  synthState : Synth.State
  frameRange : Synth.FrameRange

/-- All resident branches are indexed by the exact same immutable revision. -/
structure Epochs (revision : Resource.Revision)
    (RenderResident AudioResident TextResident : Type) where
  render : Resource.Epoch revision RenderResident
  audio : Resource.Epoch revision AudioResident
  text : Resource.Epoch revision TextResident
  publication : World.ResidencyEpoch revision.generation

/-- Owner-issued values retain the revision index without exposing an address. -/
structure Owned (revision : Resource.Revision) (Value : Type) where
  value : Value

def Owned.generation {revision : Resource.Revision}
    (_owned : Owned revision Value) : Nat :=
  revision.generation

structure Modules
    (Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock : Type) where
  cook : Source → Outcome.Result Resource.Revision Error
  revisionCodec : Resource.Codec Resource.Revision Bytes Error
  world : WorldInput → World.Output
  residency : (revision : Resource.Revision) → World.Demand →
    Epochs revision RenderResident AudioResident TextResident
  renderOwner : {revision : Resource.Revision} →
    Resource.Epoch revision RenderResident → Owned revision RenderOwner
  audioOwner : {revision : Resource.Revision} →
    Resource.Epoch revision AudioResident → Owned revision AudioOwner
  textOwner : {revision : Resource.Revision} →
    Resource.Epoch revision TextResident → Owned revision TextOwner
  ecs : {revision : Resource.Revision} →
    World.State → World.EcsEpoch revision.generation
  music : Music.State → List Music.Control → Music.State × Music.Bar
  synth : Synth.State → List Synth.Input → Synth.FrameRange → Synth.Output
  render : {revision : Resource.Revision} →
    Owned revision RenderOwner → Owned revision TextOwner →
    List Render.Command → UI.Scene → RenderFrame
  audio : {revision : Resource.Revision} →
    Owned revision AudioOwner → List Audio.Command → Synth.Output → AudioBlock

abbrev Cooked (Source WorldInput : Type) :=
  Sigma fun _revision : Resource.Revision => Request Source WorldInput

structure PreparedAt
    (Source WorldInput RenderResident AudioResident TextResident : Type)
    (revision : Resource.Revision) where
  request : Request Source WorldInput
  world : World.Output
  epochs : Epochs revision RenderResident AudioResident TextResident
  nextMusicState : Music.State
  musicBar : Music.Bar

abbrev Prepared
    (Source WorldInput RenderResident AudioResident TextResident : Type) :=
  Sigma fun revision : Resource.Revision =>
    PreparedAt Source WorldInput RenderResident AudioResident TextResident revision

structure RealizedAt
    (Source WorldInput RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner : Type)
    (revision : Resource.Revision) where
  prepared : PreparedAt Source WorldInput
    RenderResident AudioResident TextResident revision
  renderOwner : Owned revision RenderOwner
  audioOwner : Owned revision AudioOwner
  textOwner : Owned revision TextOwner
  synthOutput : Synth.Output

abbrev Realized
    (Source WorldInput RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner : Type) :=
  Sigma fun revision : Resource.Revision =>
    RealizedAt Source WorldInput RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner revision

structure ResultAt (RenderFrame AudioBlock : Type)
    (revision : Resource.Revision) where
  frameWorld : World.FrameWorld revision.generation
  renderFrame : RenderFrame
  audioBlock : AudioBlock
  nextMusicState : Music.State
  nextSynthState : Synth.State

abbrev Result (RenderFrame AudioBlock : Type) :=
  Sigma fun revision : Resource.Revision => ResultAt RenderFrame AudioBlock revision

/-- A public stage exposes requests as queries and acknowledges every response. -/
def StageAPI (Value : Type) : API where
  Query := Value
  Response _ := Unit

/-- A fallible module boundary induces a morphism between public stage APIs. -/
def stageHom (arrow : Composition.Fallible Error Input Output) :
    API.Hom (StageAPI (Outcome.Result Input Error))
      (StageAPI (Outcome.Result Output Error)) where
  onQuery := fun input => Outcome.bind input arrow.run
  onResponse := fun _ _ => ()

def prepare
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (cooked : Cooked Source WorldInput) :
    Prepared Source WorldInput RenderResident AudioResident TextResident :=
  match cooked with
  | ⟨revision, request⟩ =>
      let world := modules.world request.worldInput
      let music := modules.music request.musicState request.musicControls
      ⟨revision,
        ⟨request, world, modules.residency revision world.state.demand,
          music.1, music.2⟩⟩

def realize
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (prepared : Prepared Source WorldInput
      RenderResident AudioResident TextResident) :
    Realized Source WorldInput RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner :=
  match prepared with
  | ⟨revision, value⟩ =>
      ⟨revision,
        ⟨value,
          modules.renderOwner value.epochs.render,
          modules.audioOwner value.epochs.audio,
          modules.textOwner value.epochs.text,
          modules.synth value.request.synthState [.music value.musicBar]
            value.request.frameRange⟩⟩

def interpret
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (realized : Realized Source WorldInput
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner) : Result RenderFrame AudioBlock :=
  match realized with
  | ⟨revision, value⟩ =>
      let prepared := value.prepared
      let frameWorld : World.FrameWorld revision.generation :=
        ⟨modules.ecs prepared.world.state, prepared.epochs.publication⟩
      ⟨revision,
        ⟨frameWorld,
          modules.render value.renderOwner value.textOwner
            prepared.world.render prepared.world.ui,
          modules.audio value.audioOwner prepared.world.audio value.synthOutput,
          prepared.nextMusicState, value.synthOutput.state⟩⟩

def prepareArrow
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock) :
    Composition.Pure (Cooked Source WorldInput)
      (Prepared Source WorldInput RenderResident AudioResident TextResident) :=
  ⟨prepare modules⟩

def realizeArrow
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock) :
    Composition.Pure
      (Prepared Source WorldInput RenderResident AudioResident TextResident)
      (Realized Source WorldInput RenderResident AudioResident TextResident
        RenderOwner AudioOwner TextOwner) :=
  ⟨realize modules⟩

def interpretArrow
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock) :
    Composition.Pure
      (Realized Source WorldInput RenderResident AudioResident TextResident
        RenderOwner AudioOwner TextOwner)
      (Result RenderFrame AudioBlock) :=
  ⟨interpret modules⟩

def cookArrow
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock) :
    Composition.Fallible Error (Request Source WorldInput)
      (Cooked Source WorldInput) where
  run := fun request =>
    Outcome.map (fun revision => Sigma.mk revision request)
      (modules.cook request.source)

def prepareFallible
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock) :
    Composition.Fallible Error
      (Cooked Source WorldInput)
      (Prepared Source WorldInput RenderResident AudioResident TextResident) :=
  Composition.Fallible.lift (prepareArrow modules)

def realizeFallible
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock) :
    Composition.Fallible Error
      (Prepared Source WorldInput RenderResident AudioResident TextResident)
      (Realized Source WorldInput RenderResident AudioResident TextResident
        RenderOwner AudioOwner TextOwner) :=
  Composition.Fallible.lift (realizeArrow modules)

def interpretFallible
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock) :
    Composition.Fallible Error
      (Realized Source WorldInput RenderResident AudioResident TextResident
        RenderOwner AudioOwner TextOwner)
      (Result RenderFrame AudioBlock) :=
  Composition.Fallible.lift (interpretArrow modules)

def cookHom
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock) :=
  stageHom (cookArrow modules)

def prepareHom
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock) :=
  stageHom (prepareFallible modules)

def realizeHom
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock) :=
  stageHom (realizeFallible modules)

def interpretHom
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock) :=
  stageHom (interpretFallible modules)

def falliblePath
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock) :=
  Composition.Fallible.compose (cookArrow modules)
    (Composition.Fallible.compose (prepareFallible modules)
      (Composition.Fallible.compose (realizeFallible modules)
        (interpretFallible modules)))

def leftFalliblePath
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock) :=
  Composition.Fallible.compose
    (Composition.Fallible.compose
      (Composition.Fallible.compose (cookArrow modules)
        (prepareFallible modules))
      (realizeFallible modules))
    (interpretFallible modules)

/-- The public engine run is the composite of its module API morphisms. -/
def publicPath
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock) :=
  API.Hom.trans (cookHom modules)
    (API.Hom.trans (prepareHom modules)
      (API.Hom.trans (realizeHom modules) (interpretHom modules)))

@[simp] theorem publicPath_on_ok
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (request : Request Source WorldInput) :
    (publicPath modules).onQuery (.ok request) =
      (falliblePath modules).run request := by
  change (leftFalliblePath modules).run request =
    (falliblePath modules).run request
  congr 1
  unfold leftFalliblePath falliblePath
  rw [Composition.Fallible.compose_assoc]
  rw [Composition.Fallible.compose_assoc]

/-- Regrouping the complete prepare/realize/interpret path changes nothing. -/
theorem whole_path_assoc
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock) :
    Composition.Pure.compose
        (Composition.Pure.compose (prepareArrow modules) (realizeArrow modules))
        (interpretArrow modules) =
      Composition.Pure.compose (prepareArrow modules)
        (Composition.Pure.compose (realizeArrow modules) (interpretArrow modules)) :=
  Composition.Pure.compose_assoc _ _ _

/-- The public module morphisms have one meaning under all regrouping. -/
theorem full_path_assoc
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock) :
    API.Hom.trans
        (API.Hom.trans
          (API.Hom.trans (cookHom modules) (prepareHom modules))
          (realizeHom modules))
        (interpretHom modules) =
      API.Hom.trans (cookHom modules)
        (API.Hom.trans (prepareHom modules)
          (API.Hom.trans (realizeHom modules) (interpretHom modules))) := by
  rw [API.Hom.trans_assoc]
  rw [API.Hom.trans_assoc]

def runAfterRevision
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (revision : Resource.Revision) (request : Request Source WorldInput) :
    Result RenderFrame AudioBlock :=
  interpret modules (realize modules (prepare modules ⟨revision, request⟩))

def run
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (request : Request Source WorldInput) :
    Outcome.Result (Result RenderFrame AudioBlock) Error :=
  (publicPath modules).onQuery (.ok request)

theorem run_is_composed_path
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (request : Request Source WorldInput) :
    run modules request =
      (falliblePath modules).run request :=
  publicPath_on_ok modules request

theorem run_is_public_path
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (request : Request Source WorldInput) :
    run modules request =
      (API.Hom.trans (cookHom modules)
        (API.Hom.trans (prepareHom modules)
          (API.Hom.trans (realizeHom modules)
            (interpretHom modules)))).onQuery (.ok request) :=
  rfl

@[simp] theorem cook_failure_stops_graph
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (request : Request Source WorldInput) (error : Error)
    (failed : modules.cook request.source = .error error) :
    run modules request = .error error := by
  simp [run, falliblePath, Composition.Fallible.compose, cookArrow,
    Outcome.map, Outcome.bind, failed]

@[simp] theorem cook_success_runs_graph
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (request : Request Source WorldInput) (revision : Resource.Revision)
    (succeeded : modules.cook request.source = .ok revision) :
    run modules request = .ok (runAfterRevision modules revision request) := by
  simp [run, runAfterRevision, falliblePath, Composition.Fallible.compose,
    cookArrow, prepareFallible, realizeFallible, interpretFallible,
    prepareArrow, realizeArrow, interpretArrow, Composition.Fallible.lift,
    Outcome.map, Outcome.bind, Outcome.pure, succeeded]

def cookPackOpen
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (source : Source) : Outcome.Result Resource.Revision Error :=
  Outcome.bind (modules.cook source)
    (fun revision => Resource.openPacked modules.revisionCodec revision)

/-- Packing and reopening is observationally identity after any successful cook. -/
theorem cook_pack_open_identity
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (source : Source) :
    cookPackOpen modules source = modules.cook source := by
  cases cooked : modules.cook source with
  | error error => simp [cookPackOpen, cooked]
  | ok revision =>
      simp [cookPackOpen, cooked, Resource.openPacked,
        modules.revisionCodec.decodeEncode]

theorem prepare_routes_current_demand
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (revision : Resource.Revision) (request : Request Source WorldInput) :
    let prepared := (prepare modules ⟨revision, request⟩).2
    prepared.epochs =
      modules.residency revision prepared.world.state.demand :=
  rfl

theorem realize_routes_one_epoch_to_all_owners
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (prepared : PreparedAt Source WorldInput
      RenderResident AudioResident TextResident revision) :
    let realized := (realize modules ⟨revision, prepared⟩).2
    realized.renderOwner = modules.renderOwner prepared.epochs.render ∧
      realized.audioOwner = modules.audioOwner prepared.epochs.audio ∧
      realized.textOwner = modules.textOwner prepared.epochs.text :=
  ⟨rfl, rfl, rfl⟩

theorem music_routes_to_synth
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (prepared : PreparedAt Source WorldInput
      RenderResident AudioResident TextResident revision) :
    let realized := (realize modules ⟨revision, prepared⟩).2
    realized.synthOutput =
      modules.synth prepared.request.synthState [.music prepared.musicBar]
        prepared.request.frameRange :=
  rfl

theorem interpret_routes_world_products
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (realized : RealizedAt Source WorldInput
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner revision) :
    let result := (interpret modules ⟨revision, realized⟩).2
    result.renderFrame =
        modules.render realized.renderOwner realized.textOwner
          realized.prepared.world.render realized.prepared.world.ui ∧
      result.audioBlock =
        modules.audio realized.audioOwner realized.prepared.world.audio
          realized.synthOutput :=
  ⟨rfl, rfl⟩

theorem composed_reload_failure_preserves_publication
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (before : PublishedState Cache Resource.Revision) (nextCache : Cache)
    (source : Source) (error : Error)
    (failed : modules.cook source = .error error) :
    (finishAttempt before nextCache (modules.cook source)).1.published =
      before.published := by
  simp [failed]

theorem composed_reload_success_publishes
    (modules : Modules Source Error WorldInput Bytes
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner RenderFrame AudioBlock)
    (before : PublishedState Cache Resource.Revision) (nextCache : Cache)
    (source : Source) (revision : Resource.Revision)
    (succeeded : modules.cook source = .ok revision) :
    (finishAttempt before nextCache (modules.cook source)).1.published =
      some revision := by
  simp [succeeded]

theorem owner_generations_agree
    (realized : RealizedAt Source WorldInput
      RenderResident AudioResident TextResident
      RenderOwner AudioOwner TextOwner revision) :
    realized.renderOwner.generation = realized.audioOwner.generation ∧
      realized.audioOwner.generation = realized.textOwner.generation :=
  ⟨rfl, rfl⟩

theorem frame_publication_generation_agrees
    (result : ResultAt RenderFrame AudioBlock revision) :
    result.frameWorld.ecs.generation =
      result.frameWorld.residency.generation :=
  World.frame_generation_agrees result.frameWorld

end EngineComposition
end Anoptic
