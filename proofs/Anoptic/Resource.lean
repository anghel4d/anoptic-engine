/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std
import Anoptic.Outcome
import Anoptic.ResourceRoute
import Anoptic.Transaction

namespace Anoptic

universe u v w

namespace Resource

/-- A reference carries stable semantic identity, never a storage address. -/
structure Ref (Cell : Type u) (cell : Cell) where
  identity : Nat
  deriving DecidableEq

abbrev Edge (Node : Type u) := Node → Node → Prop

/-- Reflexive transitive dependency reachability. -/
inductive Reach (edge : Edge Node) : Node → Node → Prop where
  | refl (node : Node) : Reach edge node node
  | step {source middle target : Node} :
      edge source middle → Reach edge middle target → Reach edge source target

theorem Reach.trans {edge : Edge Node} {first middle last : Node}
    (left : Reach edge first middle) (right : Reach edge middle last) :
    Reach edge first last := by
  induction left with
  | refl => exact right
  | step edgeTo next induction => exact .step edgeTo (induction right)

abbrev NodeSet (Node : Type u) := Node → Prop

def closure (edge : Edge Node) (roots : NodeSet Node) : NodeSet Node :=
  fun node => ∃ root, roots root ∧ Reach edge root node

theorem closure_extensive (edge : Edge Node) (roots : NodeSet Node) :
    ∀ node, roots node → closure edge roots node := by
  intro node present
  exact ⟨node, present, .refl node⟩

theorem closure_monotone (edge : Edge Node) {left right : NodeSet Node}
    (included : ∀ node, left node → right node) :
    ∀ node, closure edge left node → closure edge right node := by
  intro node present
  rcases present with ⟨root, inLeft, reachable⟩
  exact ⟨root, included root inLeft, reachable⟩

theorem closure_idempotent (edge : Edge Node) (roots : NodeSet Node) :
    closure edge (closure edge roots) = closure edge roots := by
  funext node
  apply propext
  constructor
  · rintro ⟨middle, ⟨root, present, first⟩, second⟩
    exact ⟨root, present, first.trans second⟩
  · intro present
    exact closure_extensive edge (closure edge roots) node present

/-- Current action keys compare only against the presently selected result. -/
structure Action (Result : Type u) where
  key : Nat
  result : Result

def dirty (current : Action Result) (nextKey : Nat) : Bool :=
  current.key != nextKey

@[simp] theorem same_action_key_is_clean (current : Action Result) :
    dirty current current.key = false := by
  simp [dirty]

/-- A codec is admitted only with its round-trip law. -/
structure Codec (Value : Type u) (Bytes : Type v) (DecodeError : Type w) where
  encode : Value → Bytes
  decode : Bytes → Outcome.Result Value DecodeError
  roundTrip : ∀ value, decode (encode value) = .ok value

def Codec.identity (Value : Type u) (DecodeError : Type w) :
    Codec Value Value DecodeError where
  encode := id
  decode := Except.ok
  roundTrip := fun _ => rfl

@[simp] theorem Codec.identity_roundTrip (value : Value) :
    (Codec.identity Value DecodeError).decode
        ((Codec.identity Value DecodeError).encode value) = Except.ok value :=
  rfl

def openPacked (codec : Codec Value Bytes DecodeError) (value : Value) :
    Outcome.Result Value DecodeError :=
  codec.decode (codec.encode value)

@[simp] theorem openPacked_roundTrip
    (codec : Codec Value Bytes DecodeError) (value : Value) :
    openPacked codec value = .ok value :=
  codec.roundTrip value

structure Revision where
  generation : Nat
  contentIdentity : Nat
  deriving DecidableEq

/-- An epoch is indexed by the exact revision from which it was realized. -/
structure Epoch (revision : Revision) (Resident : Type u) where
  resident : Resident

def Epoch.map (function : A → B) (epoch : Epoch revision A) : Epoch revision B :=
  ⟨function epoch.resident⟩

@[simp] theorem Epoch.map_id (epoch : Epoch revision A) :
    epoch.map id = epoch := by
  cases epoch
  rfl

theorem Epoch.map_comp (first : A → B) (second : B → C)
    (epoch : Epoch revision A) :
    (epoch.map first).map second = epoch.map (second ∘ first) := by
  cases epoch
  rfl

/-- Down may bifurcate; every returned focus carries proof of its unique up cell. -/
structure Route (Upper : Type u) (Lower : Type v) where
  down : Upper → List Lower
  up : Lower → Upper
  coherent : ∀ upper lower, lower ∈ down upper → up lower = upper

structure LowerFocus (route : Route Upper Lower) (upper : Upper) where
  value : Lower
  selected : value ∈ route.down upper

def LowerFocus.up {Upper : Type u} {Lower : Type v}
    {route : Route Upper Lower} {upper : Upper}
    (focus : LowerFocus route upper) : Upper :=
  route.up focus.value

@[simp] theorem LowerFocus.up_eq {Upper : Type u} {Lower : Type v}
    {route : Route Upper Lower} {upper : Upper}
    (focus : LowerFocus route upper) :
    focus.up = upper :=
  route.coherent upper focus.value focus.selected

abbrev Demand (Cell : Type u) := Cell → Prop

def Demand.union (left right : Demand Cell) : Demand Cell :=
  fun cell => left cell ∨ right cell

theorem Demand.union_comm (left right : Demand Cell) :
    Demand.union left right = Demand.union right left := by
  funext cell
  apply propext
  exact or_comm

theorem Demand.union_assoc (first second third : Demand Cell) :
    Demand.union (Demand.union first second) third =
      Demand.union first (Demand.union second third) := by
  funext cell
  apply propext
  exact or_assoc

def cow (affected : Node → Bool) (replace before : Node → Value) : Node → Value :=
  fun node => if affected node then replace node else before node

theorem cow_preserves_unaffected (affected : Node → Bool)
    (replace before : Node → Value) (node : Node)
    (unaffected : affected node = false) :
    cow affected replace before node = before node := by
  simp [cow, unaffected]

theorem cow_replaces_affected (affected : Node → Bool)
    (replace before : Node → Value) (node : Node)
    (changed : affected node = true) :
    cow affected replace before node = replace node := by
  simp [cow, changed]

end Resource
end Anoptic
