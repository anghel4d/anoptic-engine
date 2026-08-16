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

/-! ## One reflected callable shape -/

/-- Reflection records one callable carrier; arity-specific APIs refine it. -/
structure CallableShape where
  Parameters : List (Type u)
  Return : Type u
  carrier : Inspection Return

namespace CallableShape

def arity (shape : CallableShape.{u}) : Nat :=
  shape.Parameters.length

def pure (parameters : List (Type u)) (Return : Type u) :
    CallableShape.{u} where
  Parameters := parameters
  Return := Return
  carrier := .invalid

def result (parameters : List (Type u)) (Value Error : Type u) :
    CallableShape.{u} where
  Parameters := parameters
  Return := Outcome.Result Value Error
  carrier := .valid (factor Value Error)

@[simp] theorem pure_arity (parameters : List (Type u)) (Return : Type u) :
    (pure parameters Return).arity = parameters.length :=
  rfl

@[simp] theorem result_arity (parameters : List (Type u))
    (Value Error : Type u) :
    (result parameters Value Error).arity = parameters.length :=
  rfl

@[simp] theorem result_reconstructs (parameters : List (Type u))
    (Value Error : Type u) :
    (result parameters Value Error).Return = Outcome.Result Value Error :=
  rfl

end CallableShape

/-- Unary/nullary result arrows and binary folds are arity refinements. -/
inductive CallableRole (shape : CallableShape.{u}) : Type (u + 1) where
  | operation (supported : shape.arity ≤ 1)
  | fold (binary : shape.arity = 2)

def resultOperationShape (Domain Value Error : Type u) :
    CallableShape.{u} :=
  CallableShape.result [Domain] Value Error

def pureFoldShape (State Item : Type u) : CallableShape.{u} :=
  CallableShape.pure [State, Item] State

@[simp] theorem resultOperationShape_is_unary
    (Domain Value Error : Type u) :
    (resultOperationShape Domain Value Error).arity = 1 :=
  rfl

@[simp] theorem pureFoldShape_is_binary (State Item : Type u) :
    (pureFoldShape State Item).arity = 2 :=
  rfl

def resultOperationRole (Domain Value Error : Type u) :
    CallableRole (resultOperationShape Domain Value Error) :=
  .operation (by simp)

def pureFoldRole (State Item : Type u) :
    CallableRole (pureFoldShape State Item) :=
  .fold (by simp)

/-! ## Raw parameter-list normalization -/

/-- The categorical unit lifted into the callable shape's universe. -/
abbrev ParameterUnit : Type u := ULift.{u} Unit

/-- A reflected parameter list denotes one categorical domain object. -/
def ParameterDomain : List (Type u) → Type u
  | [] => ParameterUnit
  | [parameter] => parameter
  | first :: second :: rest => first × ParameterDomain (second :: rest)

/-- The source-language presentation of a declaration with several parameters. -/
def CurriedCallable : List (Type u) → Type u → Type u
  | [], Return => Return
  | parameter :: parameters, Return =>
      parameter → CurriedCallable parameters Return

/-- Turn a reflected parameter list into one function over its product domain. -/
def uncurryParameters {Return : Type u} :
    (parameters : List (Type u)) →
      CurriedCallable parameters Return → ParameterDomain parameters → Return
  | [], function, _ => function
  | [_], function, value => function value
  | _ :: second :: rest, function, value =>
      uncurryParameters (second :: rest) (function value.1) value.2

/-- Recover the source-language parameter presentation from one product arrow. -/
def curryParameters {Return : Type u} :
    (parameters : List (Type u)) →
      (ParameterDomain parameters → Return) → CurriedCallable parameters Return
  | [], function => function (ULift.up ())
  | [_], function => function
  | _ :: second :: rest, function =>
      fun first => curryParameters (second :: rest)
        (fun remaining => function (first, remaining))

@[simp] theorem uncurry_curry_parameters {Return : Type u}
    (parameters : List (Type u))
    (function : ParameterDomain parameters → Return) :
    uncurryParameters parameters (curryParameters parameters function) =
      function := by
  funext input
  induction parameters with
  | nil =>
      rcases input with ⟨input⟩
      cases input
      rfl
  | cons first rest induction =>
      cases rest with
      | nil => rfl
      | cons second tail =>
          rcases input with ⟨head, remaining⟩
          simpa [uncurryParameters, curryParameters] using
            induction (fun tailValue => function (head, tailValue)) remaining

@[simp] theorem curry_uncurry_parameters {Return : Type u}
    (parameters : List (Type u))
    (function : CurriedCallable parameters Return) :
    curryParameters parameters (uncurryParameters parameters function) =
      function := by
  induction parameters with
  | nil => rfl
  | cons first rest induction =>
      cases rest with
      | nil => rfl
      | cons second tail =>
          funext head
          simpa [uncurryParameters, curryParameters] using
            induction (function head)

/-- Normalization changes only parameter presentation, never result semantics. -/
def CallableShape.normalizeParameters (shape : CallableShape.{u}) :
    CallableShape.{u} where
  Parameters := [ParameterDomain shape.Parameters]
  Return := shape.Return
  carrier := shape.carrier

@[simp] theorem CallableShape.normalizeParameters_arity
    (shape : CallableShape.{u}) : shape.normalizeParameters.arity = 1 :=
  rfl

@[simp] theorem CallableShape.normalizeParameters_return
    (shape : CallableShape.{u}) : shape.normalizeParameters.Return = shape.Return :=
  rfl

@[simp] theorem CallableShape.normalizeParameters_carrier
    (shape : CallableShape.{u}) :
    shape.normalizeParameters.carrier = shape.carrier :=
  rfl

/-- Every reflected parameter list therefore admits one normalized graph edge. -/
def CallableShape.normalizedOperationRole (shape : CallableShape.{u}) :
    CallableRole shape.normalizeParameters :=
  .operation (by simp)

@[simp] theorem normalize_nullary_shape (Value Error : Type u) :
    (CallableShape.result [] Value Error).normalizeParameters =
      CallableShape.result [ParameterUnit] Value Error :=
  rfl

@[simp] theorem normalize_unary_shape (Domain Value Error : Type u) :
    (CallableShape.result [Domain] Value Error).normalizeParameters =
      CallableShape.result [Domain] Value Error :=
  rfl

@[simp] theorem normalize_binary_shape (Left Right Value Error : Type u) :
    (CallableShape.result [Left, Right] Value Error).normalizeParameters =
      CallableShape.result [Left × Right] Value Error :=
  rfl

/-- Pure declarations lower through the same product-domain normalization. -/
def normalizePure {Return : Type u} (parameters : List (Type u))
    (function : CurriedCallable parameters Return) :
    Composition.Pure (ParameterDomain parameters) Return :=
  ⟨uncurryParameters parameters function⟩

/-- Fallible declarations become ordinary unary Kleisli arrows after normalization. -/
def normalizeFallible {Value Error : Type u} (parameters : List (Type u))
    (function : CurriedCallable parameters (Outcome.Result Value Error)) :
    Composition.Fallible Error (ParameterDomain parameters) Value :=
  ⟨uncurryParameters parameters function⟩

@[simp] theorem normalizePure_binary_run
    {Left Right Value : Type u}
    (function : Left → Right → Value) (left : Left) (right : Right) :
    (normalizePure [Left, Right] function).run (left, right) =
      function left right :=
  rfl

@[simp] theorem normalizeFallible_binary_run
    {Left Right Value Error : Type u}
    (function : Left → Right → Outcome.Result Value Error)
    (left : Left) (right : Right) :
    (normalizeFallible [Left, Right] function).run (left, right) =
      function left right :=
  rfl

/-- Fixing one input of a binary declaration produces an ordinary unary edge. -/
def partialFirst
    {Fixed Remaining Value Error : Type u}
    (function : Fixed → Remaining → Outcome.Result Value Error)
    (fixed : Fixed) : Composition.Fallible Error Remaining Value :=
  normalizeFallible [Remaining] (function fixed)

@[simp] theorem partialFirst_run
    {Fixed Remaining Value Error : Type u}
    (function : Fixed → Remaining → Outcome.Result Value Error)
    (fixed : Fixed) (remaining : Remaining) :
    (partialFirst function fixed).run remaining = function fixed remaining :=
  rfl

/-- Structural recognition and permission to invoke are independent facts. -/
structure CallableDeclaration where
  shape : CallableShape.{u}
  nonthrowing : Bool
  exactlyInvocable : Bool

def CallableDeclaration.StructurallyResult
    (declaration : CallableDeclaration.{u}) : Prop :=
  declaration.shape.carrier.isValid = true

def CallableDeclaration.OperationallyAdmitted
    (declaration : CallableDeclaration.{u}) : Prop :=
  declaration.nonthrowing = true ∧ declaration.exactlyInvocable = true

def structurallyValidButThrowing (Domain Value Error : Type u) :
    CallableDeclaration.{u} :=
  ⟨resultOperationShape Domain Value Error, false, true⟩

theorem structural_recognition_does_not_admit_invocation
    (Domain Value Error : Type u) :
    (structurallyValidButThrowing Domain Value Error).StructurallyResult ∧
      ¬(structurallyValidButThrowing Domain Value Error).OperationallyAdmitted := by
  simp [structurallyValidButThrowing,
    CallableDeclaration.StructurallyResult,
    CallableDeclaration.OperationallyAdmitted,
    resultOperationShape, CallableShape.result,
    Inspection.isValid]

/-- The old flag implication accepted an unsupported non-nullary callable. -/
def LegacyCoherent (nullary supportedArity : Bool) : Bool :=
  !nullary || supportedArity

@[simp] theorem legacy_coherent_is_too_weak :
    LegacyCoherent false false = true :=
  rfl

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

/-- Functorial value mapping preserves the error carrier. -/
def transform {SafeError : Type u → Prop} {Error Input Value Next : Type u}
    (arrow : Morphism SafeError Error Input Value)
    (mapper : Value → Next) : Morphism SafeError Error Input Next :=
  ⟨arrow.safeError,
    ⟨fun input => Outcome.map mapper (arrow.arrow.run input)⟩⟩

/-- Error mapping preserves successful values and changes only the error factor. -/
def mapError {SafeError : Type u → Prop}
    {Error NextError Input Value : Type u}
    (safeError : SafeError NextError)
    (arrow : Morphism SafeError Error Input Value)
    (mapper : Error → NextError) :
    Morphism SafeError NextError Input Value :=
  ⟨safeError,
    ⟨fun input =>
      match arrow.arrow.run input with
      | .error error => .error (mapper error)
      | .ok value => .ok value⟩⟩

/-- Fail-fast applicative pairing evaluates two arrows on one shared witness. -/
def pair {SafeError : Type u → Prop}
    {Error Witness Left Right : Type u}
    (left : Morphism SafeError Error Witness Left)
    (right : Morphism SafeError Error Witness Right) :
    Morphism SafeError Error Witness (Left × Right) :=
  ⟨left.safeError,
    ⟨fun witness =>
      match left.arrow.run witness with
      | .error error => .error error
      | .ok leftValue =>
          match right.arrow.run witness with
          | .error error => .error error
          | .ok rightValue => .ok (leftValue, rightValue)⟩⟩

@[simp] theorem transform_identity {SafeError : Type u → Prop}
    {Error Input Value : Type u}
    (arrow : Morphism SafeError Error Input Value) :
    transform arrow id = arrow := by
  apply ext
  apply Composition.Fallible.ext
  intro input
  exact Outcome.map_id (arrow.arrow.run input)

theorem transform_comp {SafeError : Type u → Prop}
    {Error Input A B C : Type u}
    (arrow : Morphism SafeError Error Input A)
    (first : A → B) (second : B → C) :
    transform (transform arrow first) second =
      transform arrow (second ∘ first) := by
  apply ext
  apply Composition.Fallible.ext
  intro input
  exact Outcome.map_comp first second (arrow.arrow.run input)

@[simp] theorem mapError_identity {SafeError : Type u → Prop}
    {Error Input Value : Type u}
    (arrow : Morphism SafeError Error Input Value) :
    mapError arrow.safeError arrow id = arrow := by
  apply ext
  apply Composition.Fallible.ext
  intro input
  cases result : arrow.arrow.run input <;> simp [mapError, result]

theorem mapError_comp {SafeError : Type u → Prop}
    {Error MiddleError NextError Input Value : Type u}
    (middleSafe : SafeError MiddleError)
    (nextSafe : SafeError NextError)
    (arrow : Morphism SafeError Error Input Value)
    (first : Error → MiddleError) (second : MiddleError → NextError) :
    mapError nextSafe (mapError middleSafe arrow first) second =
      mapError nextSafe arrow (second ∘ first) := by
  apply ext
  apply Composition.Fallible.ext
  intro input
  cases result : arrow.arrow.run input <;> simp [mapError, result]

@[simp] theorem pair_left_error {SafeError : Type u → Prop}
    {Error Witness Left Right : Type u}
    (left : Morphism SafeError Error Witness Left)
    (right : Morphism SafeError Error Witness Right)
    (witness : Witness) (error : Error)
    (failed : left.arrow.run witness = .error error) :
    (pair left right).arrow.run witness = .error error := by
  simp [pair, failed]

@[simp] theorem pair_right_error {SafeError : Type u → Prop}
    {Error Witness Left Right : Type u}
    (left : Morphism SafeError Error Witness Left)
    (right : Morphism SafeError Error Witness Right)
    (witness : Witness) (leftValue : Left) (error : Error)
    (leftSucceeded : left.arrow.run witness = .ok leftValue)
    (rightFailed : right.arrow.run witness = .error error) :
    (pair left right).arrow.run witness = .error error := by
  simp [pair, leftSucceeded, rightFailed]

@[simp] theorem pair_both_error_selects_left {SafeError : Type u → Prop}
    {Error Witness Left Right : Type u}
    (left : Morphism SafeError Error Witness Left)
    (right : Morphism SafeError Error Witness Right)
    (witness : Witness) (leftError rightError : Error)
    (leftFailed : left.arrow.run witness = .error leftError)
    (_rightFailed : right.arrow.run witness = .error rightError) :
    (pair left right).arrow.run witness = .error leftError :=
  pair_left_error left right witness leftError leftFailed

@[simp] theorem pair_succeeds {SafeError : Type u → Prop}
    {Error Witness Left Right : Type u}
    (left : Morphism SafeError Error Witness Left)
    (right : Morphism SafeError Error Witness Right)
    (witness : Witness) (leftValue : Left) (rightValue : Right)
    (leftSucceeded : left.arrow.run witness = .ok leftValue)
    (rightSucceeded : right.arrow.run witness = .ok rightValue) :
    (pair left right).arrow.run witness = .ok (leftValue, rightValue) := by
  simp [pair, leftSucceeded, rightSucceeded]

@[simp] theorem pair_succeeds_iff {SafeError : Type u → Prop}
    {Error Witness Left Right : Type u}
    (left : Morphism SafeError Error Witness Left)
    (right : Morphism SafeError Error Witness Right)
    (witness : Witness) (leftValue : Left) (rightValue : Right) :
    (pair left right).arrow.run witness = .ok (leftValue, rightValue) ↔
      left.arrow.run witness = .ok leftValue ∧
        right.arrow.run witness = .ok rightValue := by
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
    exact pair_succeeds left right witness leftValue rightValue
      leftResult rightResult

def reassociate (value : (A × B) × C) : A × (B × C) :=
  (value.1.1, value.1.2, value.2)

/-- Pairing is associative up to the canonical product associator. -/
theorem pair_assoc {SafeError : Type u → Prop}
    {Error Witness A B C : Type u}
    (first : Morphism SafeError Error Witness A)
    (second : Morphism SafeError Error Witness B)
    (third : Morphism SafeError Error Witness C) :
    transform (pair (pair first second) third) reassociate =
      pair first (pair second third) := by
  apply ext
  apply Composition.Fallible.ext
  intro witness
  cases firstResult : first.arrow.run witness with
  | error error =>
      simp [transform, pair, Outcome.map, Outcome.bind, firstResult]
  | ok firstValue =>
      cases secondResult : second.arrow.run witness with
      | error error =>
          simp [transform, pair, Outcome.map, Outcome.bind,
            firstResult, secondResult]
      | ok secondValue =>
          cases thirdResult : third.arrow.run witness <;>
            simp [transform, pair, Outcome.map, Outcome.bind, Outcome.pure,
              reassociate, firstResult, secondResult, thirdResult]

end Morphism
end Checked

/-! ## One bounded scan skeleton -/

/-- Observable scan state includes the caller-owned written prefix on failure. -/
structure ScanObservation (Error State : Type u) where
  written : List State
  outcome : Outcome.Result (List State) Error

/-- A bounded scan interprets every step through the result algebra. -/
def scan (step : State → Item → Outcome.Result State Error) :
    Nat → State → List Item → ScanObservation Error State
  | 0, _, _ => ⟨[], .ok []⟩
  | _ + 1, _, [] => ⟨[], .ok []⟩
  | fuel + 1, state, item :: items =>
      match step state item with
      | .error error => ⟨[], .error error⟩
      | .ok next =>
          let tail := scan step fuel next items
          ⟨next :: tail.written,
            Outcome.map (List.cons next) tail.outcome⟩

/-- The pure specialization contains no failure branch. -/
def scanPure (step : State → Item → State) : Nat → State → List Item → List State
  | 0, _, _ => []
  | _ + 1, _, [] => []
  | fuel + 1, state, item :: items =>
      let next := step state item
      next :: scanPure step fuel next items

@[simp] theorem scanPure_length (step : State → Item → State)
    (fuel : Nat) (state : State) (items : List Item) :
    (scanPure step fuel state items).length = min fuel items.length := by
  induction fuel generalizing state items with
  | zero => rfl
  | succ fuel induction =>
      cases items with
      | nil => rfl
      | cons item items =>
          simp [scanPure, induction]

/-- Lifting a pure fold step into `Result` preserves the exact bounded scan. -/
theorem scan_lift_pure (Error : Type u) (step : State → Item → State)
    (fuel : Nat) (state : State) (items : List Item) :
    let observation := scan (Error := Error)
      (fun current item => .ok (step current item)) fuel state items
    observation.written = scanPure step fuel state items ∧
      observation.outcome = .ok (scanPure step fuel state items) := by
  induction fuel generalizing state items with
  | zero => simp [scan, scanPure]
  | succ fuel induction =>
      cases items with
      | nil => simp [scan, scanPure]
      | cons item items =>
          simp only [scan, scanPure]
          have tail := induction (state := step state item) (items := items)
          simp only at tail
          rcases tail with ⟨written, outcome⟩
          constructor
          · exact congrArg (List.cons (step state item)) written
          · rw [outcome]
            rfl

@[simp] theorem scan_first_failure
    (step : State → Item → Outcome.Result State Error)
    (fuel : Nat) (state : State) (item : Item) (items : List Item)
    (error : Error) (failed : step state item = .error error) :
    scan step (fuel + 1) state (item :: items) =
      ⟨[], .error error⟩ := by
  simp [scan, failed]

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
