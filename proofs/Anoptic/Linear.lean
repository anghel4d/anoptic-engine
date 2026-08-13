/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

namespace Anoptic

universe u v

namespace Linear

structure Vec (Scalar : Type u) (Dimension : Nat) (Space Layout : Type v) where
  coordinate : Fin Dimension → Scalar

/-- A space-indexed map; incompatible middle spaces cannot compose. -/
structure Map (From : Type u) (To : Type v) where
  apply : From → To

def identity (Space : Type u) : Map Space Space :=
  ⟨id⟩

def compose (first : Map A B) (second : Map B C) : Map A C :=
  ⟨second.apply ∘ first.apply⟩

@[simp] theorem identity_left (mapping : Map A B) :
    compose (identity A) mapping = mapping := by
  cases mapping
  rfl

@[simp] theorem identity_right (mapping : Map A B) :
    compose mapping (identity B) = mapping := by
  cases mapping
  rfl

theorem compose_assoc (first : Map A B) (second : Map B C)
    (third : Map C D) :
    compose (compose first second) third =
      compose first (compose second third) := by
  cases first
  cases second
  cases third
  rfl

/-- A matrix is represented semantically by its typed action between vectors. -/
abbrev Matrix (Scalar : Type u) (Rows Columns : Nat)
    (FromSpace ToSpace Layout : Type v) :=
  Map (Vec Scalar Columns FromSpace Layout)
    (Vec Scalar Rows ToSpace Layout)

end Linear
end Anoptic
