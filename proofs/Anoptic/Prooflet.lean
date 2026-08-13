/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Polynomial
import Anoptic.Compiler
import Anoptic.ResourceRoute
import Anoptic.Refinement
import Anoptic.Transaction

namespace Anoptic.Prooflet

/-- One small dependent interface: each query determines its response type. -/
inductive Query where
  | revision
  | ready

def Response : Query → Type
  | .revision => Nat
  | .ready => Bool

def api : API where
  Query := Query
  Response := Response

def revisionRequest : api.Extension Nat :=
  ⟨.revision, Nat.succ⟩

/-- The concrete request survives identity interpretation and computes its continuation. -/
example :
    match API.Hom.induced (API.Hom.refl api) revisionRequest with
    | ⟨.revision, continueWith⟩ => continueWith (41 : Nat) = 42
    | ⟨.ready, _⟩ => False := by
  rfl

inductive Cell where
  | source
  | mesh
  | material

inductive ParseOutput where
  | mesh
  | material

def parseScene : ResourceRoute.Signature Cell where
  InputPort := Unit
  OutputPort := ParseOutput
  inputType _ := .source
  outputType
    | .mesh => .mesh
    | .material => .material

inductive Transform where
  | parse

def routeSignature : Transform → ResourceRoute.Signature Cell
  | .parse => parseScene

def meshProducer : ResourceRoute.Producer Transform routeSignature .mesh where
  transform := .parse
  outputPort := .mesh
  produces := rfl

/-- One transform exposes distinct, addressable products through output ports. -/
example :
    (routeSignature meshProducer.transform).outputType meshProducer.outputPort =
      .mesh :=
  meshProducer.produces

def leftCompiler : Compiler.Partial Nat String Nat :=
  fun witness => .ok witness

def rightCompiler : Compiler.Partial Nat String Bool :=
  fun witness => .ok (witness != 0)

/-- Independent compilers pair after consuming the same normalized witness. -/
example :
    Compiler.pair leftCompiler rightCompiler 4 = .ok (4, true) :=
  rfl

/-- The concrete dependent interface obeys adapter composition. -/
example (value : api.Extension String) :
    API.Hom.induced (API.Hom.trans (API.Hom.refl api) (API.Hom.refl api)) value =
      API.Hom.induced (API.Hom.refl api)
        (API.Hom.induced (API.Hom.refl api) value) :=
  API.Hom.induced_trans (API.Hom.refl api) (API.Hom.refl api) value

def incrementImplementation : Relation Nat Nat :=
  fun source target => target = Nat.succ source

def incrementSpecification : Relation Nat Nat :=
  fun source target => source < target

theorem increment_refines :
    Relation.Refines incrementImplementation incrementSpecification := by
  intro source target implemented
  subst target
  exact Nat.lt_succ_self source

/-- Independently refined module steps remain refined after composition. -/
theorem composed_increment_refines :
    Relation.Refines
      (Relation.compose incrementImplementation incrementImplementation)
      (Relation.compose incrementSpecification incrementSpecification) :=
  Relation.compose_mono increment_refines increment_refines

def before : PublishedState Nat Nat :=
  ⟨7, some 3⟩

/-- Failed work retains its cache update but leaves the visible revision untouched. -/
example :
    finishAttempt before 8 (.error "decode failed") =
      (⟨8, some 3⟩, .error "decode failed") :=
  rfl

/-- Successful work atomically publishes the new revision. -/
example :
    finishAttempt (Error := String) before 8 (.ok 4) =
      (⟨8, some 4⟩, .ok 4) :=
  rfl

end Anoptic.Prooflet
