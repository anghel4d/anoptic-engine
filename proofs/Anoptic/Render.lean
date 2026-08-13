/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std
import Anoptic.RenderResources

namespace Anoptic

namespace Render

structure EntityId where value : Nat deriving DecidableEq
structure LightId where value : Nat deriving DecidableEq
structure ViewState where revision : Nat deriving DecidableEq

inductive Command where
  | spawn : EntityId → Nat → Command
  | update : EntityId → Nat → Command
  | retire : EntityId → Command
  | setLight : LightId → Nat → Command
  | drawText : Nat → Command
  | drawUi : Nat → Command
  deriving DecidableEq

inductive Event where
  | entityRetired : EntityId → Event
  | captureReady : Nat → Event
  | deviceLost : Event
  deriving DecidableEq

structure State where
  revision : Nat
  entities : List EntityId
  deriving DecidableEq

def applyCommand (state : State) : Command → State
  | .spawn entity _ => ⟨state.revision + 1, state.entities ++ [entity]⟩
  | .update _ _ => ⟨state.revision + 1, state.entities⟩
  | .retire entity =>
      ⟨state.revision + 1, state.entities.filter (fun current => current != entity)⟩
  | .setLight _ _ => ⟨state.revision + 1, state.entities⟩
  | .drawText _ => ⟨state.revision + 1, state.entities⟩
  | .drawUi _ => ⟨state.revision + 1, state.entities⟩

def applyCommands (state : State) (commands : List Command) : State :=
  commands.foldl applyCommand state

theorem applyCommands_append (state : State) (first second : List Command) :
    applyCommands state (first ++ second) =
      applyCommands (applyCommands state first) second := by
  simp [applyCommands, List.foldl_append]

structure Bulk where
  commands : List Command

def applyBulkCommands : State → List Command → State
  | state, [] => state
  | state, command :: rest => applyBulkCommands (applyCommand state command) rest

def applyBulk (state : State) (bulk : Bulk) : State :=
  applyBulkCommands state bulk.commands

theorem applyBulkCommands_refines_scalar (state : State)
    (commands : List Command) :
    applyBulkCommands state commands = applyCommands state commands := by
  induction commands generalizing state with
  | nil => rfl
  | cons command rest induction =>
      simp only [applyBulkCommands, applyCommands, List.foldl_cons]
      exact induction (applyCommand state command)

@[simp] theorem bulk_equals_scalar (state : State) (commands : List Command) :
    applyBulk state ⟨commands⟩ = applyCommands state commands :=
  applyBulkCommands_refines_scalar state commands

structure FrameInput (World View : Type) where
  world : World
  view : View
  commands : List Command

end Render
end Anoptic
