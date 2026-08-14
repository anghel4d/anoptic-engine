/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Outcome
import Anoptic.Polynomial

namespace Anoptic

universe u

namespace Semantic

/-!
An API describes an interaction shape.  An effect describes the codomain in
which an interpretation runs.  An algebra supplies the interpretation.  These
are deliberately separate objects.
-/

/-- The functorial effect surrounding the result of one API interpretation. -/
structure Effect where
  Object : Type u → Type u
  map : {A B : Type u} → (A → B) → Object A → Object B
  map_id : ∀ {A : Type u} (value : Object A), map id value = value
  map_comp : ∀ {A B C : Type u} (first : A → B) (second : B → C)
    (value : Object A),
    map second (map first value) = map (second ∘ first) value

namespace Effect

/-- A natural transformation relates two effects without identifying them. -/
structure Hom (source target : Effect.{u}) where
  component : {A : Type u} → source.Object A → target.Object A
  natural : ∀ {A B : Type u} (function : A → B)
    (value : source.Object A),
    target.map function (component value) =
      component (source.map function value)

namespace Hom

@[ext] theorem ext {source target : Effect.{u}}
    {left right : Hom source target}
    (same : @left.component = @right.component) : left = right := by
  cases left
  cases right
  cases same
  rfl

def identity (effect : Effect.{u}) : Hom effect effect where
  component := id
  natural := by intros; rfl

def compose {first second third : Effect.{u}}
    (left : Hom first second) (right : Hom second third) : Hom first third where
  component := right.component ∘ left.component
  natural := by
    intro A B function value
    calc
      third.map function (right.component (left.component value)) =
          right.component (second.map function (left.component value)) :=
        right.natural function (left.component value)
      _ = right.component (left.component (first.map function value)) := by
        rw [left.natural function value]

@[simp] theorem identity_compose {source target : Effect.{u}}
    (hom : Hom source target) :
    compose (identity source) hom = hom := by
  apply ext
  rfl

@[simp] theorem compose_identity {source target : Effect.{u}}
    (hom : Hom source target) :
    compose hom (identity target) = hom := by
  apply ext
  rfl

theorem compose_assoc {A B C D : Effect.{u}}
    (first : Hom A B) (second : Hom B C) (third : Hom C D) :
    compose (compose first second) third =
      compose first (compose second third) := by
  apply ext
  rfl

end Hom

def identity : Effect.{u} where
  Object := id
  map := fun function value => function value
  map_id := by intros; rfl
  map_comp := by intros; rfl

def result (Error : Type u) : Effect.{u} where
  Object := fun Value => Outcome.Result Value Error
  map := Outcome.map
  map_id := Outcome.map_id
  map_comp := Outcome.map_comp

/-- The ordinary state functor used by state-owning module interpreters. -/
def state (State : Type u) : Effect.{u} where
  Object := fun Value => State → Value × State
  map := fun function computation state =>
    let output := computation state
    (function output.1, output.2)
  map_id := by
    intro Value computation
    funext state
    rcases result : computation state with ⟨value, nextState⟩
    rfl
  map_comp := by
    intro A B C first second computation
    funext state
    rcases result : computation state with ⟨value, nextState⟩
    simp [result]

/-- `StateT State (Result Error)`, used by partial owner realizations. -/
def stateResult (State Error : Type u) : Effect.{u} where
  Object := fun Value => State → Outcome.Result (Value × State) Error
  map := fun function computation state =>
    Outcome.map (fun output => (function output.1, output.2))
      (computation state)
  map_id := by
    intro Value computation
    funext state
    cases result : computation state with
    | error error => simp [Outcome.map, Outcome.bind]
    | ok output =>
        rcases output with ⟨value, nextState⟩
        simp [Outcome.map, Outcome.bind, Outcome.pure]
  map_comp := by
    intro A B C first second computation
    funext state
    cases result : computation state with
    | error error =>
        simp [result, Outcome.map, Outcome.bind]
    | ok output =>
        rcases output with ⟨value, nextState⟩
        simp [result, Outcome.map, Outcome.bind, Outcome.pure]

namespace Hom

/-- Every natural endomorphism of the identity effect acts identically. -/
theorem identity_component
    {A : Type u} (hom : Hom Effect.identity Effect.identity) (value : A) :
    hom.component value = value := by
  let point : ULift.{u} Unit := ⟨()⟩
  have natural := @hom.natural (ULift.{u} Unit) A
    (fun _ : ULift.{u} Unit => value) point
  exact natural.symm

end Hom

end Effect

/-- Translate one polynomial request along both its shape and carrier maps. -/
def translate {source target : API.{u}} {A B : Type u}
    (shape : API.Hom source target) (carrier : A → B) :
    source.Extension A → target.Extension B :=
  fun request => API.map carrier (API.Hom.induced shape request)

@[simp] theorem translate_identity {shape : API.{u}} {A : Type u}
    (request : shape.Extension A) :
    translate (API.Hom.refl shape) id request = request := by
  cases request
  rfl

/-- Shape translation and carrier translation compose as one operation. -/
theorem translate_compose {A B C : API.{u}} {X Y Z : Type u}
    (firstShape : API.Hom A B) (secondShape : API.Hom B C)
    (firstCarrier : X → Y) (secondCarrier : Y → Z)
    (request : A.Extension X) :
    translate (API.Hom.trans firstShape secondShape)
        (secondCarrier ∘ firstCarrier) request =
      translate secondShape secondCarrier
        (translate firstShape firstCarrier request) := by
  cases request
  rfl

/-- An algebra gives one API shape a carrier, effect, and interpretation. -/
structure Algebra (shape : API.{u}) where
  effect : Effect.{u}
  Carrier : Type u
  interpret : shape.Extension Carrier → effect.Object Carrier

namespace Algebra

/-!
An `API.Hom` alone is only a morphism of shapes.  An algebra homomorphism also
maps carriers and effects, and proves that interpretation commutes with both.
-/
structure Hom {sourceShape targetShape : API.{u}}
    (source : Algebra sourceShape) (target : Algebra targetShape) where
  shape : API.Hom sourceShape targetShape
  effect : Effect.Hom source.effect target.effect
  carrier : source.Carrier → target.Carrier
  commutes : ∀ request,
    target.effect.map carrier (effect.component (source.interpret request)) =
      target.interpret (translate shape carrier request)

namespace Hom

@[ext] theorem ext {sourceShape targetShape : API.{u}}
    {source : Algebra sourceShape} {target : Algebra targetShape}
    {left right : Hom source target}
    (sameShape : left.shape = right.shape)
    (sameEffect : left.effect = right.effect)
    (sameCarrier : left.carrier = right.carrier) : left = right := by
  cases left
  cases right
  cases sameShape
  cases sameEffect
  cases sameCarrier
  rfl

def identity {shape : API.{u}} (algebra : Algebra shape) : Hom algebra algebra where
  shape := API.Hom.refl shape
  effect := Effect.Hom.identity algebra.effect
  carrier := id
  commutes := by
    intro request
    change algebra.effect.map id (algebra.interpret request) =
      algebra.interpret (translate (API.Hom.refl shape) id request)
    rw [translate_identity]
    exact algebra.effect.map_id (algebra.interpret request)

/-- Algebra homomorphisms compose only because both commuting squares do. -/
def compose {firstShape secondShape thirdShape : API.{u}}
    {first : Algebra firstShape} {second : Algebra secondShape}
    {third : Algebra thirdShape}
    (left : Hom first second) (right : Hom second third) : Hom first third where
  shape := API.Hom.trans left.shape right.shape
  effect := Effect.Hom.compose left.effect right.effect
  carrier := right.carrier ∘ left.carrier
  commutes := by
    intro request
    calc
      third.effect.map (right.carrier ∘ left.carrier)
          (right.effect.component
            (left.effect.component (first.interpret request))) =
        third.effect.map right.carrier
          (third.effect.map left.carrier
            (right.effect.component
              (left.effect.component (first.interpret request)))) := by
          rw [third.effect.map_comp]
      _ = third.effect.map right.carrier
          (right.effect.component
            (second.effect.map left.carrier
              (left.effect.component (first.interpret request)))) := by
          rw [right.effect.natural]
      _ = third.effect.map right.carrier
          (right.effect.component
            (second.interpret
              (translate left.shape left.carrier request))) := by
          rw [left.commutes]
      _ = third.interpret
          (translate right.shape right.carrier
            (translate left.shape left.carrier request)) :=
        right.commutes (translate left.shape left.carrier request)
      _ = third.interpret
          (translate (API.Hom.trans left.shape right.shape)
            (right.carrier ∘ left.carrier) request) := by
        rw [translate_compose]

def forgetShape {sourceShape targetShape : API.{u}}
    {source : Algebra sourceShape} {target : Algebra targetShape}
    (hom : Hom source target) : API.Hom sourceShape targetShape :=
  hom.shape

@[simp] theorem identity_compose {sourceShape targetShape : API.{u}}
    {source : Algebra sourceShape} {target : Algebra targetShape}
    (hom : Hom source target) :
    compose (identity source) hom = hom := by
  apply ext
  · exact API.Hom.refl_trans hom.shape
  · exact Effect.Hom.identity_compose hom.effect
  · rfl

@[simp] theorem compose_identity {sourceShape targetShape : API.{u}}
    {source : Algebra sourceShape} {target : Algebra targetShape}
    (hom : Hom source target) :
    compose hom (identity target) = hom := by
  apply ext
  · exact API.Hom.trans_refl hom.shape
  · exact Effect.Hom.compose_identity hom.effect
  · rfl

theorem compose_assoc {A B C D : API.{u}}
    {firstAlgebra : Algebra A} {secondAlgebra : Algebra B}
    {thirdAlgebra : Algebra C} {fourthAlgebra : Algebra D}
    (first : Hom firstAlgebra secondAlgebra)
    (second : Hom secondAlgebra thirdAlgebra)
    (third : Hom thirdAlgebra fourthAlgebra) :
    compose (compose first second) third =
      compose first (compose second third) := by
  apply ext
  · exact API.Hom.trans_assoc first.shape second.shape third.shape
  · exact Effect.Hom.compose_assoc first.effect second.effect third.effect
  · rfl

end Hom
end Algebra

namespace NonFullShapeMap

/-!
The forgetful map from semantic algebras to API shapes is not full: the
identity shape morphism below has no algebra-homomorphic lift for the selected
interpretations.
-/

def shape : API where
  Query := Bool
  Response _ := Unit

def source : Algebra shape where
  effect := Effect.identity
  Carrier := Bool
  interpret := fun _ => false

def target : Algebra shape where
  effect := Effect.identity
  Carrier := Bool
  interpret := fun request => request.1

def adapter : API.Hom shape shape := API.Hom.refl shape

theorem no_algebra_hom_over_adapter :
    ¬ ∃ hom : Algebra.Hom source target, hom.shape = adapter := by
  rintro ⟨hom, sameShape⟩
  have onFalse := hom.commutes
    (⟨false, fun _ => false⟩ : shape.Extension Bool)
  have onTrue := hom.commutes
    (⟨true, fun _ => false⟩ : shape.Extension Bool)
  rw [sameShape] at onFalse onTrue
  change hom.carrier (hom.effect.component false) = false at onFalse
  change hom.carrier (hom.effect.component false) = true at onTrue
  rw [Effect.Hom.identity_component hom.effect false] at onFalse onTrue
  rw [onFalse] at onTrue
  contradiction

end NonFullShapeMap
end Semantic
end Anoptic
