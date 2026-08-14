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

/-- A module compiler cannot observe parts of the shared witness outside its projection. -/
theorem projected_noninterference (projection : Shared → Local)
    (compile : Partial Local Error Plan) {left right : Shared}
    (same : projection left = projection right) :
    projected projection compile left = projected projection compile right := by
  simp only [projected_apply]
  rw [same]

/--
Fail-fast applicative pairing over one witness.  This combines two compiler
results; it is neither sequential plan composition nor a categorical product.
-/
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

@[simp] theorem pair_left_error
    (left : Partial Witness LeftError LeftPlan)
    (right : Partial Witness RightError RightPlan)
    (witness : Witness) (error : LeftError)
    (failed : left witness = .error error) :
    pair left right witness = .error (.inl error) := by
  simp [pair, failed]

@[simp] theorem pair_right_error
    (left : Partial Witness LeftError LeftPlan)
    (right : Partial Witness RightError RightPlan)
    (witness : Witness) (leftPlan : LeftPlan) (error : RightError)
    (leftSucceeded : left witness = .ok leftPlan)
    (rightFailed : right witness = .error error) :
    pair left right witness = .error (.inr error) := by
  simp [pair, leftSucceeded, rightFailed]

@[simp] theorem pair_both_error_selects_left
    (left : Partial Witness LeftError LeftPlan)
    (right : Partial Witness RightError RightPlan)
    (witness : Witness) (leftError : LeftError) (rightError : RightError)
    (leftFailed : left witness = .error leftError)
    (_rightFailed : right witness = .error rightError) :
    pair left right witness = .error (.inl leftError) :=
  pair_left_error left right witness leftError leftFailed

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
