/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Polynomial

namespace Anoptic

universe u v w x

abbrev TraceSet (Trace : Type u) := Trace → Prop

/-- Every implementation trace is admitted by the specification. -/
def Refines {Trace : Type u} (implementation specification : TraceSet Trace) : Prop :=
  ∀ trace, implementation trace → specification trace

namespace Refines

@[refl] theorem refl {Trace : Type u} (traces : TraceSet Trace) :
    Refines traces traces :=
  fun _ present => present

theorem trans {Trace : Type u} {implementation middle specification : TraceSet Trace}
    (first : Refines implementation middle)
    (second : Refines middle specification) :
    Refines implementation specification :=
  fun trace present => second trace (first trace present)

end Refines

/-- One completed API interaction. -/
structure Observation (api : API.{u}) where
  query : api.Query
  response : api.Response query

abbrev Trace (api : API.{u}) := List (Observation api)

/-- Finite observations state safety properties. -/
abbrev Safety (api : API.{u}) := TraceSet (Trace api)

/-- Infinite observations state progress, fairness, and liveness properties separately. -/
abbrev InfiniteTrace (api : API.{u}) := Nat → Observation api

abbrev Progress (api : API.{u}) := TraceSet (InfiniteTrace api)

/-- A module-local platform interpretation and its shared semantic trace boundary. -/
structure Interpretation (api : API.{u}) (Platform : Type v) where
  implementation : Platform → TraceSet (Trace api)
  specification : Safety api
  sound : ∀ platform, Refines (implementation platform) specification

/-- Relational composition represents sequential module composition. -/
def Relation (A : Type u) (B : Type v) := A → B → Prop

namespace Relation

def identity : Relation A A := Eq

def compose (first : Relation A B) (second : Relation B C) : Relation A C :=
  fun source target => ∃ middle, first source middle ∧ second middle target

def Refines (implementation specification : Relation A B) : Prop :=
  ∀ source target, implementation source target → specification source target

@[refl] theorem refines_refl (relation : Relation A B) : Refines relation relation :=
  fun _ _ present => present

theorem refines_trans {implementation middle specification : Relation A B}
    (first : Refines implementation middle)
    (second : Refines middle specification) :
    Refines implementation specification :=
  fun source target present => second source target (first source target present)

theorem compose_mono {implementationAB specificationAB : Relation A B}
    {implementationBC specificationBC : Relation B C}
    (left : Refines implementationAB specificationAB)
    (right : Refines implementationBC specificationBC) :
    Refines (compose implementationAB implementationBC)
      (compose specificationAB specificationBC) := by
  intro source target implemented
  rcases implemented with ⟨middle, first, second⟩
  exact ⟨middle, left source middle first, right middle target second⟩

theorem compose_assoc (ab : Relation A B) (bc : Relation B C)
    (cd : Relation C D) (source : A) (target : D) :
    compose (compose ab bc) cd source target ↔
      compose ab (compose bc cd) source target := by
  constructor
  · rintro ⟨middleC, ⟨middleB, first, second⟩, third⟩
    exact ⟨middleB, first, middleC, second, third⟩
  · rintro ⟨middleB, first, middleC, second, third⟩
    exact ⟨middleC, ⟨middleB, first, second⟩, third⟩

theorem identity_left (relation : Relation A B) (source : A) (target : B) :
    compose identity relation source target ↔ relation source target := by
  constructor
  · rintro ⟨middle, same, present⟩
    exact same ▸ present
  · intro present
    exact ⟨source, rfl, present⟩

theorem identity_right (relation : Relation A B) (source : A) (target : B) :
    compose relation identity source target ↔ relation source target := by
  constructor
  · rintro ⟨middle, present, same⟩
    exact same ▸ present
  · intro present
    exact ⟨target, present, rfl⟩

end Relation
end Anoptic
