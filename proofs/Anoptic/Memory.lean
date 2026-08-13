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

def checkedAdd (limit left right : Nat) : Except LayoutError Nat :=
  if left + right ≤ limit then .ok (left + right) else .error .overflow

def reserveChecked (limit : Nat) (cursor : Cursor) (segment : Segment) :
    Except LayoutError (Reservation × Cursor) :=
  if segment.alignment = 0 then .error .zeroAlignment
  else
    let start := alignUp cursor.offset segment.alignment
    match checkedAdd limit start segment.bytes with
    | .error error => .error error
    | .ok next => .ok (⟨start, segment.bytes⟩, ⟨next⟩)

@[simp] theorem reserveChecked_rejects_zero_alignment
    (limit : Nat) (cursor : Cursor) (bytes : Nat) :
    reserveChecked limit cursor ⟨bytes, 0⟩ = .error .zeroAlignment := by
  simp [reserveChecked]

theorem checkedAdd_accepts (limit left right : Nat)
    (fits : left + right ≤ limit) :
    checkedAdd limit left right = .ok (left + right) := by
  simp [checkedAdd, fits]

theorem checkedAdd_rejects (limit left right : Nat)
    (overflows : ¬ left + right ≤ limit) :
    checkedAdd limit left right = .error .overflow := by
  simp [checkedAdd, overflows]

def reserve (cursor : Cursor) (bytes : Nat) : Reservation × Cursor :=
  (⟨cursor.offset, bytes⟩, ⟨cursor.offset + bytes⟩)

@[simp] theorem reserve_starts_at_cursor (cursor : Cursor) (bytes : Nat) :
    (reserve cursor bytes).1.offset = cursor.offset :=
  rfl

@[simp] theorem consecutive_reservations_touch (cursor : Cursor)
    (firstBytes secondBytes : Nat) :
    let first := (reserve cursor firstBytes).1
    let next := (reserve cursor firstBytes).2
    let second := (reserve next secondBytes).1
    first.offset + first.size = second.offset := by
  simp [reserve]

def measure (cursor : Cursor) (plan : Plan) : Cursor :=
  plan.foldl (fun current segment => (reserve current segment.bytes).2) cursor

theorem measure_append (cursor : Cursor) (first second : Plan) :
    measure cursor (first ++ second) = measure (measure cursor first) second := by
  simp [measure, List.foldl_append]

inductive Phase where
  | building
  | sealed

structure Volume (phase : Phase) (plan : Plan) where
  size : Nat
  reservations : List Reservation

def freeze (volume : Volume .building plan) : Volume .sealed plan :=
  ⟨volume.size, volume.reservations⟩

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
