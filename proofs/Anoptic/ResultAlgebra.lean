/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Composition

namespace Anoptic

universe u

namespace ResultAlgebra

/-! ## Reflected carrier factorization -/

/-- A result shape contains its factors, not the carrier being inspected. -/
structure Shape where
  Value : Type u
  Error : Type u

namespace Shape

def Carrier (shape : Shape.{u}) : Type u :=
  Outcome.Result shape.Value shape.Error

end Shape

/-- Successful inspection witnesses exact reconstruction of the subject type. -/
structure Factorization (Carrier : Type u) where
  shape : Shape.{u}
  reconstructs : Carrier = shape.Carrier

def factor (Value Error : Type u) :
    Factorization (Outcome.Result Value Error) where
  shape := ⟨Value, Error⟩
  reconstructs := rfl

@[simp] theorem factor_value (Value Error : Type u) :
    (factor Value Error).shape.Value = Value :=
  rfl

@[simp] theorem factor_error (Value Error : Type u) :
    (factor Value Error).shape.Error = Error :=
  rfl

@[simp] theorem factor_reconstructs (Value Error : Type u) :
    (factor Value Error).shape.Carrier = Outcome.Result Value Error :=
  rfl

/-- Aliases do not change the normalized factorization. -/
abbrev Alias (Value Error : Type u) := Outcome.Result Value Error

def factorAlias (Value Error : Type u) : Factorization (Alias Value Error) :=
  factor Value Error

@[simp] theorem alias_normalizes (Value Error : Type u) :
    (factorAlias Value Error).shape = (factor Value Error).shape :=
  rfl

/-- Inspection is explicitly tagged; invalidity is not encoded in either factor. -/
inductive Inspection (Carrier : Type u) where
  | invalid
  | valid (factorization : Factorization Carrier)

namespace Inspection

def isValid : Inspection Carrier → Bool
  | .invalid => false
  | .valid _ => true

def inspectResult (Value Error : Type u) :
    Inspection (Outcome.Result Value Error) :=
  .valid (factor Value Error)

@[simp] theorem inspect_result_valid (Value Error : Type u) :
    (inspectResult Value Error).isValid = true :=
  rfl

theorem valid_reconstructs {Carrier : Type u}
    (inspection : Inspection Carrier)
    (valid : inspection.isValid = true) :
    ∃ shape : Shape.{u}, Carrier = shape.Carrier := by
  cases inspection with
  | invalid => contradiction
  | valid factorization =>
      exact ⟨factorization.shape, factorization.reconstructs⟩

end Inspection

/-! ## Reflected operation compatibility -/

/-- The portion of one reflected operation declaration used by composition. -/
structure OperationShape where
  Domain : Type u
  result : Shape.{u}

/-- Adjacent operations compose exactly when value/domain and error types agree. -/
def Composable (first second : OperationShape.{u}) : Prop :=
  first.result.Value = second.Domain ∧
    first.result.Error = second.result.Error

def composeShape (first second : OperationShape.{u})
    (_compatible : Composable first second) : OperationShape.{u} where
  Domain := first.Domain
  result := second.result

@[simp] theorem composeShape_domain (first second : OperationShape.{u})
    (compatible : Composable first second) :
    (composeShape first second compatible).Domain = first.Domain :=
  rfl

@[simp] theorem composeShape_value (first second : OperationShape.{u})
    (compatible : Composable first second) :
    (composeShape first second compatible).result.Value = second.result.Value :=
  rfl

@[simp] theorem composeShape_error (first second : OperationShape.{u})
    (compatible : Composable first second) :
    (composeShape first second compatible).result.Error = second.result.Error :=
  rfl

/-! ## Safety-certified Kleisli morphisms -/

namespace Checked

/-- `SafeError` is supplied by the C++ nothrow error concept at the boundary. -/
structure Morphism (SafeError : Type u → Prop)
    (Error Input Output : Type u) where
  safeError : SafeError Error
  arrow : Composition.Fallible Error Input Output

namespace Morphism

@[ext] theorem ext {SafeError : Type u → Prop}
    {Error Input Output : Type u}
    {left right : Morphism SafeError Error Input Output}
    (same : left.arrow = right.arrow) : left = right := by
  cases left with
  | mk leftSafe leftArrow =>
      cases right with
      | mk rightSafe rightArrow =>
          cases same
          have safetyEqual : leftSafe = rightSafe := Subsingleton.elim _ _
          cases safetyEqual
          rfl

def identity {SafeError : Type u → Prop} {Error Value : Type u}
    (safeError : SafeError Error) : Morphism SafeError Error Value Value :=
  ⟨safeError, Composition.Fallible.identity Error Value⟩

def compose {SafeError : Type u → Prop} {Error A B C : Type u}
    (first : Morphism SafeError Error A B)
    (second : Morphism SafeError Error B C) :
    Morphism SafeError Error A C :=
  ⟨first.safeError, Composition.Fallible.compose first.arrow second.arrow⟩

@[simp] theorem identity_compose {SafeError : Type u → Prop}
    {Error A B : Type u} (arrow : Morphism SafeError Error A B) :
    compose (identity arrow.safeError) arrow = arrow := by
  apply ext
  exact Composition.Fallible.identity_compose arrow.arrow

@[simp] theorem compose_identity {SafeError : Type u → Prop}
    {Error A B : Type u} (arrow : Morphism SafeError Error A B) :
    compose arrow (identity arrow.safeError) = arrow := by
  apply ext
  exact Composition.Fallible.compose_identity arrow.arrow

theorem compose_assoc {SafeError : Type u → Prop}
    {Error A B C D : Type u}
    (first : Morphism SafeError Error A B)
    (second : Morphism SafeError Error B C)
    (third : Morphism SafeError Error C D) :
    compose (compose first second) third =
      compose first (compose second third) := by
  apply ext
  exact Composition.Fallible.compose_assoc
    first.arrow second.arrow third.arrow

@[simp] theorem failure_short_circuits {SafeError : Type u → Prop}
    {Error A B C : Type u}
    (first : Morphism SafeError Error A B)
    (second : Morphism SafeError Error B C)
    (input : A) (error : Error)
    (failed : first.arrow.run input = .error error) :
    (compose first second).arrow.run input = .error error :=
  Composition.Fallible.failure_short_circuits
    first.arrow second.arrow input error failed

end Morphism
end Checked

/-! ## Exact construction witnesses for `result_if` -/

/-- A total source-to-target constructor models a truthful nothrow construction. -/
structure NothrowConstruction (Source Target : Type u) where
  run : Source → Target

def NothrowConstruction.identity (Value : Type u) :
    NothrowConstruction Value Value :=
  ⟨id⟩

def resultIf {ValueSource Value ErrorSource Error : Type u}
    (valueConstruction : NothrowConstruction ValueSource Value)
    (errorConstruction : NothrowConstruction ErrorSource Error)
    (condition : Bool) (value : ValueSource) (error : ErrorSource) :
    Outcome.Result Value Error :=
  if condition then
    .ok (valueConstruction.run value)
  else
    .error (errorConstruction.run error)

@[simp] theorem resultIf_true
    (valueConstruction : NothrowConstruction ValueSource Value)
    (errorConstruction : NothrowConstruction ErrorSource Error)
    (value : ValueSource) (error : ErrorSource) :
    resultIf valueConstruction errorConstruction true value error =
      .ok (valueConstruction.run value) :=
  rfl

@[simp] theorem resultIf_false
    (valueConstruction : NothrowConstruction ValueSource Value)
    (errorConstruction : NothrowConstruction ErrorSource Error)
    (value : ValueSource) (error : ErrorSource) :
    resultIf valueConstruction errorConstruction false value error =
      .error (errorConstruction.run error) :=
  rfl

theorem resultIf_bind
    (valueConstruction : NothrowConstruction ValueSource Value)
    (errorConstruction : NothrowConstruction ErrorSource Error)
    (condition : Bool) (value : ValueSource) (error : ErrorSource)
    (next : Value → Outcome.Result Next Error) :
    Outcome.bind
        (resultIf valueConstruction errorConstruction condition value error)
        next =
      if condition then
        next (valueConstruction.run value)
      else
        .error (errorConstruction.run error) := by
  cases condition <;> rfl

/-- Constructibility of the stored type does not imply constructibility from a source. -/
theorem stored_construction_is_insufficient :
    ¬ ∀ (Source Target : Type),
      NothrowConstruction Target Target →
        Nonempty (NothrowConstruction Source Target) := by
  intro assumed
  have impossible := assumed Unit Empty (NothrowConstruction.identity Empty)
  rcases impossible with ⟨construction⟩
  exact Empty.elim (construction.run ())

/-! ## Concrete three-stage engine-shaped witness -/

namespace Witness

inductive Error where
  | invalid
  deriving DecidableEq

inductive SafeError : Type → Prop where
  | error : SafeError Error

def parse : Checked.Morphism SafeError Error Bool Nat :=
  ⟨.error, ⟨fun valid => if valid then .ok 3 else .error .invalid⟩⟩

def canonicalize : Checked.Morphism SafeError Error Nat String :=
  ⟨.error, ⟨fun _ => .ok "canonical"⟩⟩

def publish : Checked.Morphism SafeError Error String Unit :=
  ⟨.error, ⟨fun _ => .ok ()⟩⟩

def path : Checked.Morphism SafeError Error Bool Unit :=
  Checked.Morphism.compose
    (Checked.Morphism.compose parse canonicalize) publish

theorem path_is_associative :
    path = Checked.Morphism.compose parse
      (Checked.Morphism.compose canonicalize publish) :=
  Checked.Morphism.compose_assoc parse canonicalize publish

@[simp] theorem path_succeeds : path.arrow.run true = .ok () :=
  rfl

@[simp] theorem path_short_circuits :
    path.arrow.run false = .error .invalid :=
  rfl

def parseShape : OperationShape where
  Domain := Bool
  result := ⟨Nat, Error⟩

def canonicalizeShape : OperationShape where
  Domain := Nat
  result := ⟨String, Error⟩

def publishShape : OperationShape where
  Domain := String
  result := ⟨Unit, Error⟩

theorem parse_canonicalize_compatible :
    Composable parseShape canonicalizeShape :=
  ⟨rfl, rfl⟩

theorem canonicalize_publish_compatible :
    Composable canonicalizeShape publishShape :=
  ⟨rfl, rfl⟩

def pathShape : OperationShape :=
  composeShape
    (composeShape parseShape canonicalizeShape
      parse_canonicalize_compatible)
    publishShape canonicalize_publish_compatible

@[simp] theorem path_shape_domain : pathShape.Domain = Bool :=
  rfl

@[simp] theorem path_shape_value : pathShape.result.Value = Unit :=
  rfl

@[simp] theorem path_shape_error : pathShape.result.Error = Error :=
  rfl

def wrongDomainShape : OperationShape where
  Domain := Empty
  result := ⟨Unit, Error⟩

def wrongErrorShape : OperationShape where
  Domain := Nat
  result := ⟨Unit, Empty⟩

theorem wrong_domain_rejected : ¬ Composable parseShape wrongDomainShape := by
  rintro ⟨sameDomain, _⟩
  change Nat = Empty at sameDomain
  have inhabitedEmpty : Nonempty Empty :=
    sameDomain ▸ (show Nonempty Nat from ⟨0⟩)
  rcases inhabitedEmpty with ⟨impossible⟩
  exact Empty.elim impossible

theorem wrong_error_rejected : ¬ Composable parseShape wrongErrorShape := by
  rintro ⟨_, sameError⟩
  change Error = Empty at sameError
  have inhabitedEmpty : Nonempty Empty :=
    sameError ▸ (show Nonempty Error from ⟨.invalid⟩)
  rcases inhabitedEmpty with ⟨impossible⟩
  exact Empty.elim impossible

def pathCarrier : Factorization (Outcome.Result Unit Error) :=
  factor Unit Error

@[simp] theorem path_carrier_reconstructs :
    pathCarrier.shape.Carrier = Outcome.Result Unit Error :=
  rfl

end Witness

end ResultAlgebra
end Anoptic
