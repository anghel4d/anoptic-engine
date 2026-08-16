/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Composition

namespace Anoptic

universe u

namespace ResultAlgebra

/-!
The mathematical surface is deliberately smaller than the C++ implementation.

For a fixed error type `E`, `Outcome.Result · E` is the sum functor
`R_E A = A + E`. Its Kleisli arrows are `A → R_E B`. The C++ concepts decide
which declarations may denote those arrows; Lean starts after that admission
decision and proves the resulting operations.

The file proves only parameter-product normalization, Kleisli composition,
value and error maps, fail-fast pairing, bounded scan, and conditional result
construction. Reflection queries, `noexcept`, moves, and generated C++ types are
checked by the C++ compile-time surface rather than simulated here.
-/

/-! ## Finite parameter products -/

/-- The empty parameter product in the callable universe. -/
abbrev ParameterUnit : Type u := ULift.{u} Unit

/-- One declaration parameter list denotes one categorical domain object. -/
def ParameterDomain : List (Type u) → Type u
  | [] => ParameterUnit
  | [parameter] => parameter
  | first :: second :: rest => first × ParameterDomain (second :: rest)

/-- The iterated-function presentation of a declaration parameter list. -/
def CurriedCallable : List (Type u) → Type u → Type u
  | [], Return => Return
  | parameter :: parameters, Return =>
      parameter → CurriedCallable parameters Return

/-- Convert an iterated callable into one function over its product domain. -/
def uncurryParameters {Return : Type u} :
    (parameters : List (Type u)) →
      CurriedCallable parameters Return → ParameterDomain parameters → Return
  | [], function, _ => function
  | [_], function, value => function value
  | _ :: second :: rest, function, value =>
      uncurryParameters (second :: rest) (function value.1) value.2

/-- Recover the iterated callable from one product-domain function. -/
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

/-- Normalize a fallible declaration to one unary Kleisli arrow. -/
def normalizeFallible {Value Error : Type u} (parameters : List (Type u))
    (function : CurriedCallable parameters (Outcome.Result Value Error)) :
    Composition.Fallible Error (ParameterDomain parameters) Value :=
  ⟨uncurryParameters parameters function⟩

/-- Postcompose an iterated declaration without changing its parameter shape. -/
def postcomposeParameters {Value Next Error : Type u}
    (parameters : List (Type u))
    (function : CurriedCallable parameters (Outcome.Result Value Error))
    (next : Value → Outcome.Result Next Error) :
    CurriedCallable parameters (Outcome.Result Next Error) :=
  curryParameters parameters fun input =>
    Outcome.bind (uncurryParameters parameters function input) next

/-- Parameter normalization commutes with Kleisli postcomposition. -/
theorem normalizeFallible_compose {Value Next Error : Type u}
    (parameters : List (Type u))
    (function : CurriedCallable parameters (Outcome.Result Value Error))
    (next : Composition.Fallible Error Value Next) :
    Composition.Fallible.compose (normalizeFallible parameters function) next =
      normalizeFallible parameters
        (postcomposeParameters parameters function next.run) := by
  apply Composition.Fallible.ext
  intro input
  simp [Composition.Fallible.compose, normalizeFallible,
    postcomposeParameters]

/-! ## Admitted Kleisli arrows -/

namespace Checked

/--
`SafeError Error` is the proposition supplied by the C++ `ResultError` concept.
It is an admission witness; runtime meaning remains one ordinary Kleisli arrow.
-/
structure Morphism (SafeError : Type u → Prop)
    (Error Input Output : Type u) where
  safeError : SafeError Error
  arrow : Composition.Fallible Error Input Output

namespace Morphism

@[ext] theorem ext {SafeError : Type u → Prop}
    {Error Input Output : Type u}
    {left right : Morphism SafeError Error Input Output}
    (same : left.arrow = right.arrow) : left = right := by
  cases left
  cases right
  cases same
  congr

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

/-- Error mapping preserves successful values and changes the error factor. -/
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

/-- Left-to-right fail-fast pairing evaluates two arrows on one input. -/
def pair {SafeError : Type u → Prop}
    {Error Input Left Right : Type u}
    (left : Morphism SafeError Error Input Left)
    (right : Morphism SafeError Error Input Right) :
    Morphism SafeError Error Input (Left × Right) :=
  ⟨left.safeError,
    ⟨fun input =>
      match left.arrow.run input with
      | .error error => .error error
      | .ok leftValue =>
          match right.arrow.run input with
          | .error error => .error error
          | .ok rightValue => .ok (leftValue, rightValue)⟩⟩

@[simp] theorem transform_identity {SafeError : Type u → Prop}
    {Error Input Value : Type u}
    (arrow : Morphism SafeError Error Input Value) :
    transform arrow id = arrow := by
  apply ext
  apply Composition.Fallible.ext
  exact fun input => Outcome.map_id (arrow.arrow.run input)

theorem transform_comp {SafeError : Type u → Prop}
    {Error Input A B C : Type u}
    (arrow : Morphism SafeError Error Input A)
    (first : A → B) (second : B → C) :
    transform (transform arrow first) second =
      transform arrow (second ∘ first) := by
  apply ext
  apply Composition.Fallible.ext
  exact fun input => Outcome.map_comp first second (arrow.arrow.run input)

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
    {Error Input Left Right : Type u}
    (left : Morphism SafeError Error Input Left)
    (right : Morphism SafeError Error Input Right)
    (input : Input) (error : Error)
    (failed : left.arrow.run input = .error error) :
    (pair left right).arrow.run input = .error error := by
  simp [pair, failed]

@[simp] theorem pair_right_error {SafeError : Type u → Prop}
    {Error Input Left Right : Type u}
    (left : Morphism SafeError Error Input Left)
    (right : Morphism SafeError Error Input Right)
    (input : Input) (leftValue : Left) (error : Error)
    (leftSucceeded : left.arrow.run input = .ok leftValue)
    (rightFailed : right.arrow.run input = .error error) :
    (pair left right).arrow.run input = .error error := by
  simp [pair, leftSucceeded, rightFailed]

@[simp] theorem pair_both_error_selects_left {SafeError : Type u → Prop}
    {Error Input Left Right : Type u}
    (left : Morphism SafeError Error Input Left)
    (right : Morphism SafeError Error Input Right)
    (input : Input) (leftError rightError : Error)
    (leftFailed : left.arrow.run input = .error leftError)
    (_rightFailed : right.arrow.run input = .error rightError) :
    (pair left right).arrow.run input = .error leftError :=
  pair_left_error left right input leftError leftFailed

@[simp] theorem pair_succeeds_iff {SafeError : Type u → Prop}
    {Error Input Left Right : Type u}
    (left : Morphism SafeError Error Input Left)
    (right : Morphism SafeError Error Input Right)
    (input : Input) (leftValue : Left) (rightValue : Right) :
    (pair left right).arrow.run input = .ok (leftValue, rightValue) ↔
      left.arrow.run input = .ok leftValue ∧
        right.arrow.run input = .ok rightValue := by
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

def reassociate (value : (A × B) × C) : A × (B × C) :=
  (value.1.1, value.1.2, value.2)

/-- Pairing is associative up to the canonical product associator. -/
theorem pair_assoc {SafeError : Type u → Prop}
    {Error Input A B C : Type u}
    (first : Morphism SafeError Error Input A)
    (second : Morphism SafeError Error Input B)
    (third : Morphism SafeError Error Input C) :
    transform (pair (pair first second) third) reassociate =
      pair first (pair second third) := by
  apply ext
  apply Composition.Fallible.ext
  intro input
  cases firstResult : first.arrow.run input with
  | error error => simp [transform, pair, Outcome.map, firstResult]
  | ok firstValue =>
      cases secondResult : second.arrow.run input with
      | error error =>
          simp [transform, pair, Outcome.map, firstResult, secondResult]
      | ok secondValue =>
          cases thirdResult : third.arrow.run input <;>
            simp [transform, pair, Outcome.map, Outcome.bind, Outcome.pure,
              reassociate, firstResult, secondResult, thirdResult]

end Morphism
end Checked

/-! ## Bounded scan -/

/-- A caller can observe the written prefix even when a later step fails. -/
structure ScanObservation (Error State : Type u) where
  written : List State
  outcome : Outcome.Result (List State) Error

/-- Inclusive bounded scan with left-to-right fail-fast evaluation. -/
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
      | cons item items => simp [scanPure, induction]

/-- A pure step lifted into `Result` has exactly the pure scan semantics. -/
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
          rcases induction (state := step state item) (items := items) with
            ⟨written, outcome⟩
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

/-! ## Conditional result construction -/

def resultIf (condition : Bool) (value : Value) (error : Error) :
    Outcome.Result Value Error :=
  if condition then .ok value else .error error

@[simp] theorem resultIf_true (value : Value) (error : Error) :
    resultIf true value error = .ok value :=
  rfl

@[simp] theorem resultIf_false (value : Value) (error : Error) :
    resultIf false value error = .error error :=
  rfl

theorem resultIf_bind (condition : Bool) (value : Value) (error : Error)
    (next : Value → Outcome.Result Next Error) :
    Outcome.bind (resultIf condition value error) next =
      if condition then next value else .error error := by
  cases condition <;> rfl

end ResultAlgebra
end Anoptic
