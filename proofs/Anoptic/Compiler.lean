/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

namespace Anoptic

universe u v w x y z

namespace Compiler

/-- A module compiler consumes its projected structural witness and may reject it. -/
abbrev Partial (Witness : Type u) (Error : Type v) (Plan : Type w) :=
  Witness → Except Error Plan

/-- Every module consumes a projection of the one normalized declaration witness. -/
def projected (projection : Shared → Local)
    (compile : Partial Local Error Plan) : Partial Shared Error Plan :=
  compile ∘ projection

@[simp] theorem projected_apply (projection : Shared → Local)
    (compile : Partial Local Error Plan) (witness : Shared) :
    projected projection compile witness = compile (projection witness) :=
  rfl

theorem projected_compose (first : Shared → Middle)
    (second : Middle → Local) (compile : Partial Local Error Plan) :
    projected (second ∘ first) compile =
      projected first (projected second compile) :=
  rfl

/-- Independent compilers pair their successful plans and retain either local error. -/
def pair (left : Partial Witness LeftError LeftPlan)
    (right : Partial Witness RightError RightPlan) :
    Partial Witness (Sum LeftError RightError) (LeftPlan × RightPlan) :=
  fun witness =>
    match left witness with
    | .error error => .error (.inl error)
    | .ok leftPlan =>
        match right witness with
        | .error error => .error (.inr error)
        | .ok rightPlan => .ok (leftPlan, rightPlan)

@[simp] theorem pair_succeeds_iff
    (left : Partial Witness LeftError LeftPlan)
    (right : Partial Witness RightError RightPlan)
    (witness : Witness) (leftPlan : LeftPlan) (rightPlan : RightPlan) :
    pair left right witness = .ok (leftPlan, rightPlan) ↔
      left witness = .ok leftPlan ∧ right witness = .ok rightPlan := by
  constructor
  · intro paired
    simp only [pair] at paired
    split at paired <;> rename_i leftResult
    · contradiction
    · split at paired <;> rename_i rightResult
      · contradiction
      · cases paired
        exact ⟨leftResult, rightResult⟩
  · rintro ⟨leftResult, rightResult⟩
    simp [pair, leftResult, rightResult]

/-- Equality over a shared projection is represented by a pullback. -/
structure Pullback (leftBoundary : LeftPlan → Boundary)
    (rightBoundary : RightPlan → Boundary) where
  left : LeftPlan
  right : RightPlan
  agrees : leftBoundary left = rightBoundary right

/-- Arbitrary compatibility is a proof-carrying dependent product. -/
structure RefinedProduct (Compatible : LeftPlan → RightPlan → Prop) where
  left : LeftPlan
  right : RightPlan
  compatible : Compatible left right

def Pullback.toRefined
    {leftBoundary : LeftPlan → Boundary}
    {rightBoundary : RightPlan → Boundary}
    (value : Pullback leftBoundary rightBoundary) :
    RefinedProduct fun left right => leftBoundary left = rightBoundary right :=
  ⟨value.left, value.right, value.agrees⟩

def Pullback.ofRefined
    {leftBoundary : LeftPlan → Boundary}
    {rightBoundary : RightPlan → Boundary}
    (value : RefinedProduct fun left right =>
      leftBoundary left = rightBoundary right) :
    Pullback leftBoundary rightBoundary :=
  ⟨value.left, value.right, value.compatible⟩

@[simp] theorem Pullback.ofRefined_toRefined
    {leftBoundary : LeftPlan → Boundary}
    {rightBoundary : RightPlan → Boundary}
    (value : Pullback leftBoundary rightBoundary) :
    Pullback.ofRefined value.toRefined = value := by
  cases value
  rfl

@[simp] theorem Pullback.toRefined_ofRefined
    {leftBoundary : LeftPlan → Boundary}
    {rightBoundary : RightPlan → Boundary}
    (value : RefinedProduct fun left right =>
      leftBoundary left = rightBoundary right) :
    Pullback.toRefined (Pullback.ofRefined value) = value := by
  cases value
  rfl

end Compiler
end Anoptic
