/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std
import Anoptic.Compiler

namespace Anoptic

namespace Structural

inductive PointerPolicy where
  | forbidden
  | borrowed
  | owned
  deriving DecidableEq

inductive Shape where
  | atom : String → Shape
  | record : List Shape → Shape
  | choice : List Shape → Shape
  | array : Nat → Shape → Shape
  | pointer : PointerPolicy → Shape

def keepMember : Shape → Bool
  | .record [] => false
  | .choice [] => false
  | _ => true

/-- Canonical records and choices omit structurally empty alternatives. -/
def normalize : Shape → Shape
  | .record fields => .record (fields.filter keepMember)
  | .choice alternatives => .choice (alternatives.filter keepMember)
  | shape => shape

@[simp] theorem normalize_idempotent (shape : Shape) :
    normalize (normalize shape) = normalize shape :=
  by
    cases shape with
    | record fields => simp [normalize, List.filter_filter]
    | choice alternatives => simp [normalize, List.filter_filter]
    | atom => rfl
    | array => rfl
    | pointer => rfl

structure Witness where
  declarations : List Shape

def project (select : Shape → Bool) (witness : Witness) : Witness :=
  ⟨witness.declarations.filter select⟩

@[simp] theorem project_all (witness : Witness) :
    project (fun _ => true) witness = witness := by
  cases witness
  simp [project]

theorem project_idempotent (select : Shape → Bool) (witness : Witness) :
    project select (project select witness) = project select witness := by
  cases witness
  simp [project, List.filter_filter]

theorem project_compose (first second : Shape → Bool) (witness : Witness) :
    project second (project first witness) =
      project (fun shape => second shape && first shape) witness := by
  cases witness
  simp [project, List.filter_filter]

abbrev RecordPlan := List Shape

@[simp] theorem recordPlan_left_identity (plan : RecordPlan) :
    [] ++ plan = plan :=
  rfl

@[simp] theorem recordPlan_right_identity (plan : RecordPlan) :
    plan ++ [] = plan :=
  List.append_nil plan

theorem recordPlan_assoc (first second third : RecordPlan) :
    (first ++ second) ++ third = first ++ (second ++ third) :=
  List.append_assoc first second third

end Structural
end Anoptic
