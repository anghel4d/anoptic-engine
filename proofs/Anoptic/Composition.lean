/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Outcome

namespace Anoptic

universe u v w x y

namespace Composition

/-- Pure module boundaries form the ordinary function category. -/
structure Pure (Input : Type u) (Output : Type v) where
  run : Input → Output

namespace Pure

def identity (Value : Type u) : Pure Value Value := ⟨id⟩

def compose (first : Pure A B) (second : Pure B C) : Pure A C :=
  ⟨second.run ∘ first.run⟩

def tensor (left : Pure A B) (right : Pure C D) : Pure (A × C) (B × D) :=
  ⟨fun input => (left.run input.1, right.run input.2)⟩

def choice (left : Pure A B) (right : Pure C D) :
    Pure (Sum A C) (Sum B D) :=
  ⟨fun
    | .inl value => .inl (left.run value)
    | .inr value => .inr (right.run value)⟩

@[simp] theorem identity_compose (arrow : Pure A B) :
    compose (identity A) arrow = arrow := by
  cases arrow
  rfl

@[simp] theorem compose_identity (arrow : Pure A B) :
    compose arrow (identity B) = arrow := by
  cases arrow
  rfl

theorem compose_assoc (first : Pure A B) (second : Pure B C)
    (third : Pure C D) :
    compose (compose first second) third = compose first (compose second third) := by
  cases first
  cases second
  cases third
  rfl

@[simp] theorem tensor_identity :
    tensor (identity A) (identity B) = identity (A × B) :=
  rfl

theorem tensor_interchange (firstLeft : Pure A B) (secondLeft : Pure B C)
    (firstRight : Pure D E) (secondRight : Pure E F) :
    tensor (compose firstLeft secondLeft) (compose firstRight secondRight) =
      compose (tensor firstLeft firstRight) (tensor secondLeft secondRight) := by
  cases firstLeft
  cases secondLeft
  cases firstRight
  cases secondRight
  rfl

@[simp] theorem choice_identity :
    choice (identity A) (identity B) = identity (Sum A B) := by
  apply congrArg Pure.mk
  funext input
  cases input <;> rfl

theorem choice_interchange (firstLeft : Pure A B) (secondLeft : Pure B C)
    (firstRight : Pure D E) (secondRight : Pure E F) :
    choice (compose firstLeft secondLeft) (compose firstRight secondRight) =
      compose (choice firstLeft firstRight) (choice secondLeft secondRight) := by
  apply congrArg Pure.mk
  funext input
  cases input <;> rfl

def associate (A : Type u) (B : Type v) (C : Type w) :
    Pure ((A × B) × C) (A × (B × C)) :=
  ⟨fun value => (value.1.1, value.1.2, value.2)⟩

def unassociate (A : Type u) (B : Type v) (C : Type w) :
    Pure (A × (B × C)) ((A × B) × C) :=
  ⟨fun value => ((value.1, value.2.1), value.2.2)⟩

@[simp] theorem associate_inverse :
    compose (associate A B C) (unassociate A B C) = identity ((A × B) × C) :=
  rfl

@[simp] theorem unassociate_inverse :
    compose (unassociate A B C) (associate A B C) = identity (A × (B × C)) :=
  rfl

end Pure

/-- Fallible boundaries compose in the result Kleisli category. -/
structure Fallible (Error : Type u) (Input : Type v) (Output : Type w) where
  run : Input → Outcome.Result Output Error

namespace Fallible

@[ext] theorem ext {left right : Fallible Error Input Output}
    (same : ∀ input, left.run input = right.run input) : left = right := by
  cases left
  cases right
  congr
  funext input
  exact same input

def identity (Error : Type u) (Value : Type v) : Fallible Error Value Value :=
  ⟨Except.ok⟩

def compose (first : Fallible Error A B) (second : Fallible Error B C) :
    Fallible Error A C :=
  ⟨fun input => Outcome.bind (first.run input) second.run⟩

def lift (arrow : Pure A B) : Fallible Error A B :=
  ⟨fun input => .ok (arrow.run input)⟩

@[simp] theorem lift_identity :
    lift (Error := Error) (Pure.identity A) = identity Error A :=
  rfl

theorem lift_compose (first : Pure A B) (second : Pure B C) :
    lift (Error := Error) (Pure.compose first second) =
      compose (lift (Error := Error) first) (lift (Error := Error) second) :=
  rfl

@[simp] theorem identity_compose (arrow : Fallible Error A B) :
    compose (identity Error A) arrow = arrow := by
  apply ext
  intro input
  rfl

@[simp] theorem compose_identity (arrow : Fallible Error A B) :
    compose arrow (identity Error B) = arrow := by
  apply ext
  intro input
  exact Outcome.right_identity (arrow.run input)

theorem compose_assoc (first : Fallible Error A B)
    (second : Fallible Error B C) (third : Fallible Error C D) :
    compose (compose first second) third = compose first (compose second third) := by
  apply ext
  intro input
  exact Outcome.bind_assoc (first.run input) second.run third.run

@[simp] theorem failure_short_circuits (first : Fallible Error A B)
    (second : Fallible Error B C) (input : A) (error : Error)
    (failed : first.run input = .error error) :
    (compose first second).run input = .error error := by
  simp [compose, failed]

end Fallible

/-- Stateful module morphisms thread one owner state in a fixed order. -/
structure Stateful (State : Type u) (Input : Type v) (Output : Type w) where
  run : Input → State → Output × State

namespace Stateful

@[ext] theorem ext {left right : Stateful State Input Output}
    (same : ∀ input state, left.run input state = right.run input state) :
    left = right := by
  cases left
  cases right
  congr
  funext input state
  exact same input state

def identity (State : Type u) (Value : Type v) : Stateful State Value Value :=
  ⟨fun value state => (value, state)⟩

def compose (first : Stateful State A B) (second : Stateful State B C) :
    Stateful State A C :=
  ⟨fun input state =>
    let middle := first.run input state
    second.run middle.1 middle.2⟩

@[simp] theorem identity_compose (arrow : Stateful State A B) :
    compose (identity State A) arrow = arrow := by
  apply ext
  intro input state
  rfl

@[simp] theorem compose_identity (arrow : Stateful State A B) :
    compose arrow (identity State B) = arrow := by
  apply ext
  intro input state
  cases result : arrow.run input state
  simp [compose, identity, result]

theorem compose_assoc (first : Stateful State A B) (second : Stateful State B C)
    (third : Stateful State C D) :
    compose (compose first second) third = compose first (compose second third) := by
  apply ext
  intro input state
  simp [compose]

end Stateful

/-- Failed transitions retain their successor work state and skip later stages. -/
structure Transactional (State : Type u) (Error : Type v)
    (Input : Type w) (Output : Type x) where
  run : Input → State → State × Outcome.Result Output Error

namespace Transactional

@[ext] theorem ext {left right : Transactional State Error Input Output}
    (same : ∀ input state, left.run input state = right.run input state) :
    left = right := by
  cases left
  cases right
  congr
  funext input state
  exact same input state

def identity (State : Type u) (Error : Type v) (Value : Type w) :
    Transactional State Error Value Value :=
  ⟨fun value state => (state, .ok value)⟩

def compose (first : Transactional State Error A B)
    (second : Transactional State Error B C) : Transactional State Error A C :=
  ⟨fun input state =>
    match first.run input state with
    | (nextState, .error error) => (nextState, .error error)
    | (nextState, .ok value) => second.run value nextState⟩

@[simp] theorem identity_compose (arrow : Transactional State Error A B) :
    compose (identity State Error A) arrow = arrow := by
  apply ext
  intro input state
  rfl

@[simp] theorem compose_identity (arrow : Transactional State Error A B) :
    compose arrow (identity State Error B) = arrow := by
  apply ext
  intro input state
  cases result : arrow.run input state with
  | mk next outcome => cases outcome <;> simp [compose, identity, result]

theorem compose_assoc (first : Transactional State Error A B)
    (second : Transactional State Error B C)
    (third : Transactional State Error C D) :
    compose (compose first second) third = compose first (compose second third) := by
  apply ext
  intro input state
  cases firstResult : first.run input state with
  | mk firstState firstOutcome =>
      cases firstOutcome with
      | error error => simp [compose, firstResult]
      | ok value =>
          cases secondResult : second.run value firstState with
          | mk secondState secondOutcome =>
              cases secondOutcome <;> simp [compose, firstResult, secondResult]

@[simp] theorem failure_retains_state (first : Transactional State Error A B)
    (second : Transactional State Error B C) (input : A) (state nextState : State)
    (error : Error) (failed : first.run input state = (nextState, .error error)) :
    (compose first second).run input state = (nextState, .error error) := by
  simp [compose, failed]

end Transactional

end Composition
end Anoptic
