/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std

namespace Anoptic

namespace Strings

abbrev View := List UInt8

structure Owned where
  bytes : List UInt8
  deriving DecidableEq

def empty : Owned := ⟨[]⟩

def concat (left right : Owned) : Owned :=
  ⟨left.bytes ++ right.bytes⟩

@[simp] theorem concat_left_identity (value : Owned) : concat empty value = value := by
  cases value
  rfl

@[simp] theorem concat_right_identity (value : Owned) : concat value empty = value := by
  cases value
  simp [concat, empty]

theorem concat_assoc (first second third : Owned) :
    concat (concat first second) third = concat first (concat second third) := by
  cases first
  cases second
  cases third
  simp [concat, List.append_assoc]

structure Utf8 (Valid : View → Prop) where
  bytes : View
  valid : Valid bytes

def Utf8.forget (value : Utf8 Valid) : View := value.bytes

@[simp] theorem Utf8.forget_mk {Valid : View → Prop}
    (bytes : View) (valid : Valid bytes) :
    (Utf8.mk bytes valid).forget = bytes :=
  rfl

inductive BuilderPhase where
  | empty
  | building

structure Builder (phase : BuilderPhase) where
  bytes : List UInt8

def begin : Builder .empty → Builder .building
  | ⟨bytes⟩ => ⟨bytes⟩

def finish : Builder .building → Owned
  | ⟨bytes⟩ => ⟨bytes⟩

@[simp] theorem finish_begin (builder : Builder .empty) :
    (finish (begin builder)).bytes = builder.bytes := by
  cases builder
  rfl

abbrev Symbol := View

structure InternState where
  seen : List Symbol
  deriving DecidableEq

def intern (state : InternState) (value : View) : Symbol × InternState :=
  if value ∈ state.seen then
    (value, state)
  else
    (value, ⟨value :: state.seen⟩)

@[simp] theorem intern_symbol (state : InternState) (value : View) :
    (intern state value).1 = value := by
  unfold intern
  split <;> rfl

theorem intern_idempotent (state : InternState) (value : View) :
    let first := (intern state value).2
    (intern first value).2 = first := by
  by_cases present : value ∈ state.seen
  · simp [intern, present]
  · simp [intern, present]

end Strings
end Anoptic
