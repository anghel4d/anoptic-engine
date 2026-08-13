/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std

namespace Anoptic

namespace Memory

structure Segment where
  bytes : Nat
  alignment : Nat
  deriving DecidableEq

abbrev Plan := List Segment

@[simp] theorem plan_left_identity (plan : Plan) : [] ++ plan = plan := rfl

@[simp] theorem plan_right_identity (plan : Plan) : plan ++ [] = plan :=
  List.append_nil plan

theorem plan_assoc (first second third : Plan) :
    (first ++ second) ++ third = first ++ (second ++ third) :=
  List.append_assoc first second third

structure Cursor where
  offset : Nat
  deriving DecidableEq

structure Reservation where
  offset : Nat
  size : Nat
  deriving DecidableEq

inductive LayoutError where
  | zeroAlignment
  | overflow
  deriving DecidableEq

def alignUp (offset alignment : Nat) : Nat :=
  if alignment = 0 then offset
  else offset + ((alignment - offset % alignment) % alignment)

theorem alignUp_not_before (offset alignment : Nat) :
    offset ≤ alignUp offset alignment := by
  unfold alignUp
  split <;> omega

theorem alignUp_aligned (offset alignment : Nat) (positive : 0 < alignment) :
    alignUp offset alignment % alignment = 0 := by
  unfold alignUp
  rw [if_neg (Nat.ne_of_gt positive)]
  rw [Nat.add_mod]
  let remainder := offset % alignment
  have less : remainder < alignment := Nat.mod_lt _ positive
  by_cases zero : remainder = 0
  · simp [remainder, zero]
  · have differenceLess : alignment - remainder < alignment := by omega
    rw [show offset % alignment = remainder from rfl]
    have inner : (alignment - remainder) % alignment =
        alignment - remainder := Nat.mod_eq_of_lt differenceLess
    have fills : remainder + (alignment - remainder) = alignment := by omega
    calc
      (remainder + ((alignment - remainder) % alignment) % alignment) %
          alignment =
          (remainder + (alignment - remainder)) % alignment := by
            simp only [inner]
      _ = alignment % alignment := by rw [fills]
      _ = 0 := Nat.mod_self alignment

def reserveChecked (limit : Nat) (cursor : Cursor) (segment : Segment) :
    Except LayoutError (Reservation × Cursor) :=
  if segment.alignment = 0 then .error .zeroAlignment
  else
    let start := alignUp cursor.offset segment.alignment
    if start + segment.bytes ≤ limit then
      .ok (⟨start, segment.bytes⟩, ⟨start + segment.bytes⟩)
    else
      .error .overflow

@[simp] theorem reserveChecked_rejects_zero_alignment
    (limit : Nat) (cursor : Cursor) (bytes : Nat) :
    reserveChecked limit cursor ⟨bytes, 0⟩ = .error .zeroAlignment := by
  simp [reserveChecked]

theorem reserveChecked_success
    (success : reserveChecked limit cursor segment = .ok (reservation, next)) :
    cursor.offset ≤ reservation.offset ∧
      reservation.offset % segment.alignment = 0 ∧
      reservation.size = segment.bytes ∧
      reservation.offset + reservation.size = next.offset ∧
      next.offset ≤ limit := by
  unfold reserveChecked at success
  split at success
  · contradiction
  · next nonzero =>
      dsimp at success
      split at success
      · next fits =>
          simp only [Except.ok.injEq, Prod.mk.injEq] at success
          rcases success with ⟨rfl, rfl, rfl⟩
          refine ⟨alignUp_not_before _ _, ?_, rfl, rfl, fits⟩
          exact alignUp_aligned _ _ (Nat.pos_of_ne_zero nonzero)
      · contradiction

/-- Measurement is the checked execution of the same aligned reservation plan. -/
def measure (limit : Nat) (cursor : Cursor) : Plan → Except LayoutError Cursor
  | [] => .ok cursor
  | segment :: rest =>
      match reserveChecked limit cursor segment with
      | .error error => .error error
      | .ok (_, next) => measure limit next rest

theorem measure_append (limit : Nat) (cursor : Cursor) (first second : Plan) :
    measure limit cursor (first ++ second) =
      match measure limit cursor first with
      | .error error => .error error
      | .ok next => measure limit next second := by
  induction first generalizing cursor with
  | nil => rfl
  | cons segment rest induction =>
      simp only [List.cons_append, measure]
      cases reserved : reserveChecked limit cursor segment with
      | error error => rfl
      | ok pair =>
          cases pair with
          | mk reservation next =>
              exact induction next

inductive Phase where
  | building
  | sealed

def LayoutMatches : Plan → List Reservation → Prop
  | [], [] => True
  | segment :: plan, reservation :: reservations =>
      reservation.size = segment.bytes ∧
        0 < segment.alignment ∧
        reservation.offset % segment.alignment = 0 ∧
        LayoutMatches plan reservations
  | _, _ => False

def OrderedDisjoint : List Reservation → Prop
  | [] => True
  | reservation :: rest =>
      (∀ next ∈ rest,
        reservation.offset + reservation.size ≤ next.offset) ∧
      OrderedDisjoint rest

structure Volume (phase : Phase) (plan : Plan) where
  size : Nat
  reservations : List Reservation
  layout : LayoutMatches plan reservations
  inBounds : ∀ reservation ∈ reservations,
    reservation.offset + reservation.size ≤ size
  disjoint : OrderedDisjoint reservations

def freeze (volume : Volume .building plan) : Volume .sealed plan :=
  ⟨volume.size, volume.reservations, volume.layout,
    volume.inBounds, volume.disjoint⟩

@[simp] theorem freeze_preserves_size (volume : Volume .building plan) :
    (freeze volume).size = volume.size :=
  rfl

@[simp] theorem freeze_preserves_reservations (volume : Volume .building plan) :
    (freeze volume).reservations = volume.reservations :=
  rfl

structure Owner (plan : Plan) where
  volume : Volume .sealed plan
  references : Nat

def retain (owner : Owner plan) : Owner plan :=
  ⟨owner.volume, owner.references + 1⟩

@[simp] theorem retain_preserves_volume (owner : Owner plan) :
    (retain owner).volume = owner.volume :=
  rfl

@[simp] theorem retain_increments_owner_count (owner : Owner plan) :
    (retain owner).references = owner.references + 1 :=
  rfl

inductive RegionPhase where
  | live
  | dead

structure Region (phase : RegionPhase) where
  identity : Nat

def winkOut (_region : Region .live) : Region .dead :=
  ⟨0⟩

structure ScratchRegion where
  generation : Nat

def reset (scratch : ScratchRegion) : ScratchRegion :=
  ⟨scratch.generation + 1⟩

@[simp] theorem reset_advances (scratch : ScratchRegion) :
    (reset scratch).generation = scratch.generation + 1 :=
  rfl

end Memory
end Anoptic
