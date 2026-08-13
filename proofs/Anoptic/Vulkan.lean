/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Render
import Anoptic.Resource
import Anoptic.Composition

namespace Anoptic

namespace Vulkan

structure SessionState where
  submittedFrame : Nat
  deriving DecidableEq

structure PresentedFrame where
  number : Nat
  residentRevision : Nat
  commandCount : Nat
  viewRevision : Nat
  deriving DecidableEq

structure Result where
  state : SessionState
  frame : PresentedFrame
  events : List Render.Event
  deriving DecidableEq

/-- The backend is a private interpreter, not a public resource type. -/
structure Interpreter (Resident View : Type) where
  run : SessionState → Resident → List Render.Command → View → Result

def referenceInterpreter : Interpreter Nat Nat where
  run := fun state resident commands view =>
    ⟨⟨state.submittedFrame + 1⟩,
      ⟨state.submittedFrame + 1, resident, commands.length, view⟩, []⟩

@[simp] theorem reference_advances_frame (state : SessionState)
    (resident : Nat) (commands : List Render.Command) (view : Nat) :
    (referenceInterpreter.run state resident commands view).frame.number =
      state.submittedFrame + 1 :=
  rfl

@[simp] theorem reference_observes_inputs (state : SessionState)
    (resident : Nat) (commands : List Render.Command) (view : Nat) :
    let frame := (referenceInterpreter.run state resident commands view).frame
    frame.residentRevision = resident ∧
      frame.commandCount = commands.length ∧ frame.viewRevision = view :=
  ⟨rfl, rfl, rfl⟩

def Interpreter.Refines
    (implementation specification : Interpreter Resident View) : Prop :=
  ∀ state resident commands view,
    implementation.run state resident commands view =
      specification.run state resident commands view

def Interpreter.stateful (interpreter : Interpreter Resident View) :
    Composition.Stateful SessionState
      (Resident × List Render.Command × View)
      (PresentedFrame × List Render.Event) where
  run := fun input state =>
    let result := interpreter.run state input.1 input.2.1 input.2.2
    ((result.frame, result.events), result.state)

theorem stateful_composition_assoc
    (first : Composition.Stateful SessionState A B)
    (second : Composition.Stateful SessionState B C)
    (third : Composition.Stateful SessionState C D) :
    Composition.Stateful.compose
        (Composition.Stateful.compose first second) third =
      Composition.Stateful.compose first
        (Composition.Stateful.compose second third) :=
  Composition.Stateful.compose_assoc first second third

end Vulkan
end Anoptic
