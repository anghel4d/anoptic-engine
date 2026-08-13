/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std
import Anoptic.Input
import Anoptic.Render
import Anoptic.Audio
import Anoptic.UI
import Anoptic.Outcome

namespace Anoptic

namespace World

abbrev Demand := Nat → Bool

structure DemandDelta where
  added : Demand
  removed : Demand

def deriveDemandDelta (previous current : Demand) : DemandDelta :=
  ⟨fun asset => !previous asset && current asset,
    fun asset => previous asset && !current asset⟩

def applyDemandDelta (before : Demand) (delta : DemandDelta) : Demand :=
  fun asset => if delta.added asset then true
    else if delta.removed asset then false
    else before asset

theorem deriveDemandDelta_applies (previous current : Demand) :
    applyDemandDelta previous (deriveDemandDelta previous current) = current := by
  funext asset
  simp [applyDemandDelta, deriveDemandDelta]
  cases previous asset <;> cases current asset <;> rfl

theorem demand_deltas_compose (first middle last : Demand) :
    applyDemandDelta
        (applyDemandDelta first (deriveDemandDelta first middle))
        (deriveDemandDelta middle last) = last := by
  rw [deriveDemandDelta_applies]
  exact deriveDemandDelta_applies middle last

theorem removal_retracts (previous current : Demand) (asset : Nat)
    (wasPresent : previous asset = true) (isAbsent : current asset = false) :
    (deriveDemandDelta previous current).removed asset = true := by
  simp [deriveDemandDelta, wasPresent, isAbsent]

theorem identical_current_can_have_different_delta :
    let absent : Demand := fun _ => false
    let present : Demand := fun _ => true
    (deriveDemandDelta absent absent).removed 0 ≠
      (deriveDemandDelta present absent).removed 0 := by
  decide

inductive Input where
  | user : Anoptic.Input.Event → Input
  | timeStep : Nat → Input
  | game : Nat → Input
  | resourceChanged : Nat → Input
  deriving DecidableEq

structure State where
  tick : Nat
  demand : Demand

structure Output where
  state : State
  demandDelta : DemandDelta
  render : List Render.Command
  audio : List Audio.Command
  ui : UI.Scene

structure EcsEpoch (generation : Nat) where
  stateIdentity : Nat
  deriving DecidableEq

structure ResidencyEpoch (generation : Nat) where
  revisionIdentity : Nat
  deriving DecidableEq

def EcsEpoch.generation {generation : Nat} (_epoch : EcsEpoch generation) : Nat :=
  generation

def ResidencyEpoch.generation {generation : Nat}
    (_epoch : ResidencyEpoch generation) : Nat :=
  generation

/-- Both halves of a frame are indexed by one generation. -/
structure FrameWorld (generation : Nat) where
  ecs : EcsEpoch generation
  residency : ResidencyEpoch generation

theorem frame_generation_agrees (frame : FrameWorld generation) :
    frame.ecs.generation = frame.residency.generation :=
  rfl

structure Persistent where
  tick : Nat
  deriving DecidableEq

structure SaveCell where
  persistent : Persistent
  demand : Demand
  manifestRoot : Nat

inductive SaveError where
  | wrongManifest
  deriving DecidableEq

def save (manifestRoot : Nat) (state : State) : SaveCell :=
  ⟨⟨state.tick⟩, state.demand, manifestRoot⟩

def load (manifestRoot : Nat) (cell : SaveCell) :
    Outcome.Result State SaveError :=
  if cell.manifestRoot = manifestRoot then
    .ok ⟨cell.persistent.tick, cell.demand⟩
  else .error .wrongManifest

theorem load_save_roundTrip (manifestRoot : Nat) (state : State) :
    load manifestRoot (save manifestRoot state) =
      .ok ⟨state.tick, state.demand⟩ := by
  simp [load, save]

theorem load_save_is_original (manifestRoot : Nat) (state : State) :
    load manifestRoot (save manifestRoot state) = .ok state := by
  cases state
  simp [load, save]

end World
end Anoptic
