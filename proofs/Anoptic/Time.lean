/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std

namespace Anoptic

universe u v

namespace Time

structure Instant (Clock : Type u) where
  ticks : Int
  deriving DecidableEq

structure Duration (Unit : Type v) where
  ticks : Int
  deriving DecidableEq

def advance {DurationUnit : Type v} (instant : Instant Clock)
    (duration : Duration DurationUnit) : Instant Clock :=
  ⟨instant.ticks + duration.ticks⟩

def elapsed {DurationUnit : Type v} (later earlier : Instant Clock) :
    Duration DurationUnit :=
  ⟨later.ticks - earlier.ticks⟩

@[simp] theorem advance_zero (instant : Instant Clock) :
    advance instant (Duration.mk 0 : Duration Unit) = instant := by
  cases instant
  simp [advance]

theorem advance_add (instant : Instant Clock)
    (first second : Duration Unit) :
    advance (advance instant first) second =
      advance instant (⟨first.ticks + second.ticks⟩ : Duration Unit) := by
  cases instant
  cases first
  cases second
  simp [advance, Int.add_assoc]

@[simp] theorem elapsed_self (instant : Instant Clock) :
    elapsed (DurationUnit := DurationUnit) instant instant =
      (⟨0⟩ : Duration DurationUnit) := by
  cases instant
  simp [elapsed]

@[simp] theorem elapsed_advance (instant : Instant Clock)
    (duration : Duration DurationUnit) :
    elapsed (DurationUnit := DurationUnit) (advance instant duration) instant =
      duration := by
  cases instant
  cases duration
  simp [advance, elapsed]
  omega

def convert (scale : Int) (duration : Duration FromUnit) : Duration ToUnit :=
  ⟨duration.ticks * scale⟩

@[simp] theorem convert_identity (duration : Duration Unit) :
    convert (FromUnit := Unit) (ToUnit := Unit) 1 duration = duration := by
  cases duration
  simp [convert]

theorem convert_comp (firstScale secondScale : Int)
    (duration : Duration FirstUnit) :
    convert (FromUnit := MiddleUnit) (ToUnit := LastUnit) secondScale
        (convert (FromUnit := FirstUnit) (ToUnit := MiddleUnit) firstScale duration) =
      convert (FromUnit := FirstUnit) (ToUnit := LastUnit)
        (firstScale * secondScale) duration := by
  cases duration
  simp [convert, Int.mul_assoc]

end Time
end Anoptic
