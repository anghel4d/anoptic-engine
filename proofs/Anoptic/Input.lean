/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Transport

namespace Anoptic

namespace Input

inductive Event where
  | key : Nat → Bool → Event
  | button : Nat → Bool → Event
  | cursor : Int → Int → Event
  | scroll : Int → Int → Event
  | focus : Bool → Event
  | resize : Nat → Nat → Event
  | character : Char → Event
  | close : Event
  deriving DecidableEq

structure DisplayState where
  width : Nat
  height : Nat
  focused : Bool
  deriving DecidableEq

def applyDisplayEvent (state : DisplayState) : Event → DisplayState
  | .resize width height => ⟨width, height, state.focused⟩
  | .focus focused => ⟨state.width, state.height, focused⟩
  | _ => state

@[simp] theorem resize_sets_extent (state : DisplayState) (width height : Nat) :
    applyDisplayEvent state (.resize width height) =
      ⟨width, height, state.focused⟩ :=
  rfl

@[simp] theorem key_preserves_display (state : DisplayState)
    (key : Nat) (pressed : Bool) :
    applyDisplayEvent state (.key key pressed) = state :=
  rfl

structure Adapter (Foreign : Type) where
  translate : Foreign → Event

theorem Adapter.deterministic (adapter : Adapter Foreign) (event : Foreign) :
    adapter.translate event = adapter.translate event :=
  rfl

end Input
end Anoptic
