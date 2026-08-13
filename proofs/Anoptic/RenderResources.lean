/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std

namespace Anoptic

namespace RenderResources

inductive Capability where
  | texture
  | material
  | mesh
  | model
  | text
  | ui
  deriving DecidableEq

structure Portable (capability : Capability) where
  identity : Nat
  payloadIdentity : Nat
  deriving DecidableEq

/-- Device slots retain semantic identity while hiding backend representation. -/
structure Slot (capability : Capability) where
  identity : Nat
  ownerIndex : Nat
  deriving DecidableEq

def realize (ownerIndex : Nat) (portable : Portable capability) : Slot capability :=
  ⟨portable.identity, ownerIndex⟩

@[simp] theorem realize_preserves_identity (ownerIndex : Nat)
    (portable : Portable capability) :
    (realize ownerIndex portable).identity = portable.identity :=
  rfl

def realizeMany (firstOwnerIndex : Nat)
    (values : List (Portable capability)) : List (Slot capability) :=
  values.mapIdx fun index value => realize (firstOwnerIndex + index) value

@[simp] theorem realizeMany_length (firstOwnerIndex : Nat)
    (values : List (Portable capability)) :
    (realizeMany firstOwnerIndex values).length = values.length := by
  simp [realizeMany]

structure Retired (capability : Capability) where
  slot : Slot capability
  afterFrame : Nat

def reclaimable (completedFrame : Nat) (retired : Retired capability) : Bool :=
  retired.afterFrame ≤ completedFrame

@[simp] theorem reclaimable_at_retirement (retired : Retired capability) :
    reclaimable retired.afterFrame retired = true := by
  simp [reclaimable]

end RenderResources
end Anoptic
