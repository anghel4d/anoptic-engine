/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Render
import Anoptic.Resource

namespace Anoptic

namespace Vulkan

structure SessionState where
  submittedFrame : Nat
  deriving DecidableEq

structure PresentedFrame where
  number : Nat
  deriving DecidableEq

structure Result where
  state : SessionState
  frame : PresentedFrame
  events : List Render.Event
  deriving DecidableEq

/-- The backend is a private interpreter, not a public resource type. -/
structure Interpreter (Resident View : Type) where
  run : SessionState → Resident → List Render.Command → View → Result

def referenceInterpreter (Resident View : Type) : Interpreter Resident View where
  run := fun state _ _ _ =>
    ⟨⟨state.submittedFrame + 1⟩, ⟨state.submittedFrame + 1⟩, []⟩

@[simp] theorem reference_advances_frame (state : SessionState)
    (resident : Resident) (commands : List Render.Command) (view : View) :
    ((referenceInterpreter Resident View).run state resident commands view).frame.number =
      state.submittedFrame + 1 :=
  rfl

theorem Interpreter.deterministic (interpreter : Interpreter Resident View)
    (state : SessionState) (resident : Resident)
    (commands : List Render.Command) (view : View) :
    interpreter.run state resident commands view =
      interpreter.run state resident commands view :=
  rfl

end Vulkan
end Anoptic
