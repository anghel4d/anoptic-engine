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

def applyDisplayEvents (state : DisplayState) (events : List Event) : DisplayState :=
  events.foldl applyDisplayEvent state

theorem applyDisplayEvents_append (state : DisplayState)
    (first second : List Event) :
    applyDisplayEvents state (first ++ second) =
      applyDisplayEvents (applyDisplayEvents state first) second := by
  simp [applyDisplayEvents, List.foldl_append]

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

def Adapter.Refines (adapter : Adapter Foreign)
    (specification : Foreign → Event) : Prop :=
  ∀ event, adapter.translate event = specification event

theorem Adapter.Refines.compose (adapter : Adapter Foreign)
    (specification : Foreign → Event)
    (sound : adapter.Refines specification) (map : Event → Event) :
    (Adapter.mk (map ∘ adapter.translate)).Refines (map ∘ specification) := by
  intro event
  simp only [Function.comp_apply]
  rw [sound event]

end Input
end Anoptic
