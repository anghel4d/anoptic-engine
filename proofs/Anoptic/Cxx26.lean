/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Compiler
import Anoptic.Composition
import Anoptic.Polynomial
import Anoptic.Refinement
import Anoptic.ResourceRoute

namespace Anoptic

universe u v w x

namespace Cxx26

/-! ## The staged C++26 subset used by Anoptic

This is a semantic model of the deliberately small C++26 image emitted from
Anoptic declarations.  It is not a model of all C++.  The indexed operation
language makes translation-only facilities unrepresentable in executable
plans, while the type language makes non-type template arguments
unrepresentable.
-/

inductive Stage where
  | translation
  | execution
  deriving DecidableEq, Repr

inductive Feature where
  | reflection
  | consteval
  | constexpr
  | typeTemplate
  | expansion
  | splice
  | directCall
  | stateThread
  | publication
  deriving DecidableEq, Repr

def Feature.translationOnly : Feature → Bool
  | .reflection | .consteval | .typeTemplate | .expansion | .splice => true
  | .constexpr | .directCall | .stateThread | .publication => false

/-- Operations are indexed by the only stage in which they may occur. -/
inductive Operation : Stage → Type where
  | reflect : String → Operation .translation
  | immediateCheck : String → Operation .translation
  | constexprFold : String → Operation .translation
  | instantiateType : String → Operation .translation
  | expand : String → Operation .translation
  | splice : String → Operation .translation
  | constexprCall : String → Operation .execution
  | directCall : String → Operation .execution
  | threadState : String → Operation .execution
  | publish : String → Operation .execution
  deriving Repr

def Operation.feature : Operation stage → Feature
  | .reflect _ => .reflection
  | .immediateCheck _ => .consteval
  | .constexprFold _ => .constexpr
  | .instantiateType _ => .typeTemplate
  | .expand _ => .expansion
  | .splice _ => .splice
  | .constexprCall _ => .constexpr
  | .directCall _ => .directCall
  | .threadState _ => .stateThread
  | .publish _ => .publication

/-- Reflection, immediate evaluation, expansion, and splicing cannot survive erasure. -/
@[simp] theorem execution_excludes_translation_only
    (operation : Operation .execution) :
    operation.feature.translationOnly = false := by
  cases operation <;> rfl

/-- The target type grammar has template applications whose arguments are types only. -/
inductive TypeExpr where
  | named : String → TypeExpr
  | product : List (String × TypeExpr) → TypeExpr
  | choice : List TypeExpr → TypeExpr
  | fixedArray : Nat → TypeExpr → TypeExpr
  | application : String → List TypeExpr → TypeExpr

structure TypeTemplate where
  name : String
  deriving DecidableEq, Repr

def TypeTemplate.apply (template : TypeTemplate)
    (arguments : List TypeExpr) : TypeExpr :=
  .application template.name arguments

def TypeExpr.templateArguments : TypeExpr → List TypeExpr
  | .application _ arguments => arguments
  | _ => []

/-- A template application recovers exactly its type arguments; there is no value case. -/
@[simp] theorem TypeTemplate.arguments_apply (template : TypeTemplate)
    (arguments : List TypeExpr) :
    (template.apply arguments).templateArguments = arguments :=
  rfl

def resultTemplate : TypeTemplate := ⟨"ano::Result"⟩
def assetRefTemplate : TypeTemplate := ⟨"ano::AssetRef"⟩
def spanTemplate : TypeTemplate := ⟨"ano::Span"⟩

def resultType (value error : TypeExpr) : TypeExpr :=
  resultTemplate.apply [value, error]

def assetRefType (target : TypeExpr) : TypeExpr :=
  assetRefTemplate.apply [target]

def spanType (element : TypeExpr) : TypeExpr :=
  spanTemplate.apply [element]

@[simp] theorem assetRef_has_one_type_argument (target : TypeExpr) :
    (assetRefType target).templateArguments = [target] :=
  rfl

structure Field where
  name : String
  type : TypeExpr

structure Declaration where
  name : String
  fields : List Field

/-- `std::meta::info` is modeled as a reified declaration at translation time. -/
structure MetaInfo where
  declaration : Declaration

def reify (declaration : Declaration) : MetaInfo :=
  ⟨declaration⟩

def membersOf (info : MetaInfo) : List Field :=
  info.declaration.fields

def expandMembers (info : MetaInfo) (operation : Field → A) : List A :=
  info.declaration.fields.map operation

def spliceType (info : MetaInfo) : TypeExpr :=
  .product (info.declaration.fields.map fun field => (field.name, field.type))

@[simp] theorem membersOf_reify_complete (declaration : Declaration) :
    membersOf (reify declaration) = declaration.fields :=
  rfl

@[simp] theorem expansion_reify_complete (declaration : Declaration)
    (operation : Field → A) :
    expandMembers (reify declaration) operation =
      declaration.fields.map operation :=
  rfl

@[simp] theorem splice_reify_exact (declaration : Declaration) :
    spliceType (reify declaration) =
      .product (declaration.fields.map fun field => (field.name, field.type)) :=
  rfl

/-! ## `consteval` and `constexpr` -/

/-- An immediate computation is total as a compiler action and explicitly fallible. -/
structure Immediate (Input : Type u) (Error : Type v) (Output : Type w) where
  evaluate : Input → Except Error Output

namespace Immediate

@[ext] theorem ext {left right : Immediate Input Error Output}
    (same : ∀ input, left.evaluate input = right.evaluate input) : left = right := by
  cases left
  cases right
  congr
  funext input
  exact same input

def identity (Error : Type v) (Value : Type u) : Immediate Value Error Value :=
  ⟨Except.ok⟩

def compose (first : Immediate A Error B) (second : Immediate B Error C) :
    Immediate A Error C :=
  ⟨fun input => Outcome.bind (first.evaluate input) second.evaluate⟩

@[simp] theorem identity_compose (arrow : Immediate A Error B) :
    compose (identity Error A) arrow = arrow := by
  apply ext
  intro input
  rfl

@[simp] theorem compose_identity (arrow : Immediate A Error B) :
    compose arrow (identity Error B) = arrow := by
  apply ext
  intro input
  exact Outcome.right_identity (arrow.evaluate input)

theorem compose_assoc (first : Immediate A Error B)
    (second : Immediate B Error C) (third : Immediate C Error D) :
    compose (compose first second) third =
      compose first (compose second third) := by
  apply ext
  intro input
  exact Outcome.bind_assoc (first.evaluate input) second.evaluate third.evaluate

@[simp] theorem failure_short_circuits (first : Immediate A Error B)
    (second : Immediate B Error C) (input : A) (error : Error)
    (failed : first.evaluate input = .error error) :
    (compose first second).evaluate input = .error error := by
  simp [compose, failed]

end Immediate

/-- A `constexpr` function has one pure meaning at translation and execution. -/
structure Constant (Input : Type u) (Output : Type v) where
  evaluate : Input → Output

namespace Constant

def atTranslation (function : Constant A B) : A → B :=
  function.evaluate

def atExecution (function : Constant A B) : A → B :=
  function.evaluate

def compose (first : Constant A B) (second : Constant B C) : Constant A C :=
  ⟨second.evaluate ∘ first.evaluate⟩

@[simp] theorem stage_coherent (function : Constant A B) (input : A) :
    function.atTranslation input = function.atExecution input :=
  rfl

@[simp] theorem compose_evaluate (first : Constant A B)
    (second : Constant B C) (input : A) :
    (compose first second).evaluate input =
      second.evaluate (first.evaluate input) :=
  rfl

theorem compose_assoc (first : Constant A B) (second : Constant B C)
    (third : Constant C D) :
    compose (compose first second) third =
      compose first (compose second third) := by
  cases first
  cases second
  cases third
  rfl

end Constant

inductive SchemaError where
  | emptyRecord
  deriving DecidableEq, Repr

structure Schema where
  name : String
  fields : List Field

def schemaCompiler : Immediate Declaration SchemaError Schema where
  evaluate declaration :=
    if declaration.fields.isEmpty then
      .error .emptyRecord
    else
      .ok ⟨declaration.name, declaration.fields⟩

@[simp] theorem schemaCompiler_rejects_empty (name : String) :
    schemaCompiler.evaluate ⟨name, []⟩ = .error .emptyRecord :=
  rfl

theorem schemaCompiler_success_exact (declaration : Declaration)
    (schema : Schema)
    (success : schemaCompiler.evaluate declaration = .ok schema) :
    schema.name = declaration.name ∧ schema.fields = declaration.fields := by
  simp only [schemaCompiler] at success
  split at success
  · contradiction
  · cases success
    exact ⟨rfl, rfl⟩

/-! ## Erased direct-runtime calculus -/

namespace Runtime

inductive Atom where
  | constexprCall : String → Atom
  | directCall : String → Atom
  | constructProduct : Atom
  | inspectChoice : Atom
  | threadState : Atom
  | publish : Atom
  deriving DecidableEq, Repr

def Atom.operation : Atom → Operation .execution
  | .constexprCall name => .constexprCall name
  | .directCall name => .directCall name
  | .constructProduct => .directCall "construct-product"
  | .inspectChoice => .directCall "inspect-choice"
  | .threadState => .threadState "state"
  | .publish => .publish "transaction"

@[simp] theorem Atom.excludes_translation_only (atom : Atom) :
    atom.operation.feature.translationOnly = false :=
  execution_excludes_translation_only atom.operation

structure Pure (Input : Type u) (Output : Type v) where
  operations : List Atom
  run : Input → Output

namespace Pure

@[ext] theorem ext {left right : Pure Input Output}
    (operations : left.operations = right.operations)
    (run : ∀ input, left.run input = right.run input) : left = right := by
  cases left
  cases right
  cases operations
  congr
  funext input
  exact run input

def identity (Value : Type u) : Pure Value Value :=
  ⟨[], id⟩

def compose (first : Pure A B) (second : Pure B C) : Pure A C :=
  ⟨first.operations ++ second.operations, second.run ∘ first.run⟩

def tensor (left : Pure A B) (right : Pure C D) : Pure (A × C) (B × D) :=
  ⟨left.operations ++ right.operations,
    fun input => (left.run input.1, right.run input.2)⟩

def choice (left : Pure A B) (right : Pure C D) :
    Pure (Sum A C) (Sum B D) :=
  ⟨left.operations ++ right.operations,
    fun
      | .inl value => .inl (left.run value)
      | .inr value => .inr (right.run value)⟩

@[simp] theorem identity_compose (arrow : Pure A B) :
    compose (identity A) arrow = arrow := by
  cases arrow
  rfl

@[simp] theorem compose_identity (arrow : Pure A B) :
    compose arrow (identity B) = arrow := by
  apply ext
  · exact List.append_nil _
  · intro input
    rfl

theorem compose_assoc (first : Pure A B) (second : Pure B C)
    (third : Pure C D) :
    compose (compose first second) third =
      compose first (compose second third) := by
  apply ext
  · exact List.append_assoc _ _ _
  · intro input
    rfl

end Pure

structure Fallible (Error : Type u) (Input : Type v) (Output : Type w) where
  operations : List Atom
  run : Input → Except Error Output

namespace Fallible

@[ext] theorem ext {left right : Fallible Error Input Output}
    (operations : left.operations = right.operations)
    (run : ∀ input, left.run input = right.run input) : left = right := by
  cases left
  cases right
  cases operations
  congr
  funext input
  exact run input

def identity (Error : Type u) (Value : Type v) : Fallible Error Value Value :=
  ⟨[], Except.ok⟩

def compose (first : Fallible Error A B) (second : Fallible Error B C) :
    Fallible Error A C :=
  ⟨first.operations ++ second.operations,
    fun input => Outcome.bind (first.run input) second.run⟩

theorem compose_assoc (first : Fallible Error A B)
    (second : Fallible Error B C) (third : Fallible Error C D) :
    compose (compose first second) third =
      compose first (compose second third) := by
  apply ext
  · exact List.append_assoc _ _ _
  · intro input
    exact Outcome.bind_assoc (first.run input) second.run third.run

end Fallible

structure Stateful (State : Type u) (Input : Type v) (Output : Type w) where
  operations : List Atom
  run : Input → State → Output × State

namespace Stateful

@[ext] theorem ext {left right : Stateful State Input Output}
    (operations : left.operations = right.operations)
    (run : ∀ input state, left.run input state = right.run input state) :
    left = right := by
  cases left
  cases right
  cases operations
  congr
  funext input state
  exact run input state

def identity (State : Type u) (Value : Type v) : Stateful State Value Value :=
  ⟨[], fun value state => (value, state)⟩

def compose (first : Stateful State A B) (second : Stateful State B C) :
    Stateful State A C :=
  ⟨first.operations ++ second.operations,
    fun input state =>
      let middle := first.run input state
      second.run middle.1 middle.2⟩

theorem compose_assoc (first : Stateful State A B)
    (second : Stateful State B C) (third : Stateful State C D) :
    compose (compose first second) third =
      compose first (compose second third) := by
  apply ext
  · exact List.append_assoc _ _ _
  · intro input state
    rfl

end Stateful

structure Transactional (State : Type u) (Error : Type v)
    (Input : Type w) (Output : Type x) where
  operations : List Atom
  run : Input → State → State × Except Error Output

namespace Transactional

@[ext] theorem ext {left right : Transactional State Error Input Output}
    (operations : left.operations = right.operations)
    (run : ∀ input state, left.run input state = right.run input state) :
    left = right := by
  cases left
  cases right
  cases operations
  congr
  funext input state
  exact run input state

def identity (State : Type u) (Error : Type v) (Value : Type w) :
    Transactional State Error Value Value :=
  ⟨[], fun value state => (state, .ok value)⟩

def compose (first : Transactional State Error A B)
    (second : Transactional State Error B C) :
    Transactional State Error A C :=
  ⟨first.operations ++ second.operations,
    fun input state =>
      match first.run input state with
      | (nextState, .error error) => (nextState, .error error)
      | (nextState, .ok value) => second.run value nextState⟩

theorem compose_assoc (first : Transactional State Error A B)
    (second : Transactional State Error B C)
    (third : Transactional State Error C D) :
    compose (compose first second) third =
      compose first (compose second third) := by
  apply ext
  · exact List.append_assoc _ _ _
  · intro input state
    cases firstResult : first.run input state with
    | mk firstState firstOutcome =>
        cases firstOutcome with
        | error error => simp [compose, firstResult]
        | ok value =>
            cases secondResult : second.run value firstState with
            | mk secondState secondOutcome =>
                cases secondOutcome <;> simp [compose, firstResult, secondResult]

end Transactional

end Runtime

/-! ## Semantics-preserving lowerings from the proved engine algebras -/

def lowerPure (name : String) (arrow : Composition.Pure A B) :
    Runtime.Pure A B :=
  ⟨[.directCall name], arrow.run⟩

def lowerFallible (name : String) (arrow : Composition.Fallible Error A B) :
    Runtime.Fallible Error A B :=
  ⟨[.inspectChoice, .directCall name], arrow.run⟩

def lowerStateful (name : String) (arrow : Composition.Stateful State A B) :
    Runtime.Stateful State A B :=
  ⟨[.threadState, .directCall name], arrow.run⟩

def lowerTransactional (name : String)
    (arrow : Composition.Transactional State Error A B) :
    Runtime.Transactional State Error A B :=
  ⟨[.threadState, .inspectChoice, .publish, .directCall name], arrow.run⟩

def lowerPartial (compiler : Compiler.Partial Witness Error Plan) :
    Immediate Witness Error Plan :=
  ⟨compiler⟩

def lowerApiHom (name : String) (hom : API.Hom source target) :
    Runtime.Pure (source.Extension Value) (target.Extension Value) :=
  ⟨[.inspectChoice, .directCall name], API.Hom.induced hom⟩

def lowerRoute (name : String)
    (route : ResourceRoute.Morphism Value source target) :
    Runtime.Pure (source.Values Value) (target.Values Value) :=
  ⟨[.constructProduct, .directCall name], route.run⟩

theorem lowerPure_compose_preserves (first : Composition.Pure A B)
    (second : Composition.Pure B C) (input : A) :
    (Runtime.Pure.compose (lowerPure "first" first)
      (lowerPure "second" second)).run input =
    (lowerPure "composed" (Composition.Pure.compose first second)).run input :=
  rfl

theorem lowerFallible_compose_preserves
    (first : Composition.Fallible Error A B)
    (second : Composition.Fallible Error B C) (input : A) :
    (Runtime.Fallible.compose (lowerFallible "first" first)
      (lowerFallible "second" second)).run input =
    (lowerFallible "composed"
      (Composition.Fallible.compose first second)).run input :=
  rfl

theorem lowerStateful_compose_preserves
    (first : Composition.Stateful State A B)
    (second : Composition.Stateful State B C) (input : A) (state : State) :
    (Runtime.Stateful.compose (lowerStateful "first" first)
      (lowerStateful "second" second)).run input state =
    (lowerStateful "composed"
      (Composition.Stateful.compose first second)).run input state :=
  rfl

theorem lowerTransactional_compose_preserves
    (first : Composition.Transactional State Error A B)
    (second : Composition.Transactional State Error B C)
    (input : A) (state : State) :
    (Runtime.Transactional.compose (lowerTransactional "first" first)
      (lowerTransactional "second" second)).run input state =
    (lowerTransactional "composed"
      (Composition.Transactional.compose first second)).run input state :=
  rfl

theorem lowerPartial_projected_preserves (projection : Shared → Local)
    (compiler : Compiler.Partial Local Error Plan) (witness : Shared) :
    (lowerPartial (Compiler.projected projection compiler)).evaluate witness =
      (lowerPartial compiler).evaluate (projection witness) :=
  rfl

theorem lowerApiHom_trans_preserves (first : API.Hom source middle)
    (second : API.Hom middle target) (value : source.Extension Value) :
    (lowerApiHom "composed" (API.Hom.trans first second)).run value =
      (Runtime.Pure.compose (lowerApiHom "first" first)
        (lowerApiHom "second" second)).run value := by
  exact API.Hom.induced_trans first second value

theorem lowerRoute_compose_preserves
    (first : ResourceRoute.Morphism Value A B)
    (second : ResourceRoute.Morphism Value B C) (input : A.Values Value) :
    (Runtime.Pure.compose (lowerRoute "first" first)
      (lowerRoute "second" second)).run input =
    (lowerRoute "composed"
      (ResourceRoute.Morphism.compose first second)).run input :=
  rfl

structure ExecutablePlan where
  schema : Schema
  operations : List (Operation .execution)

def emit (schema : Schema) : ExecutablePlan :=
  ⟨schema,
    [.constexprCall (schema.name ++ "::layout"),
      .directCall (schema.name ++ "::run")]⟩

def compileDeclaration : Immediate Declaration SchemaError ExecutablePlan where
  evaluate declaration := (schemaCompiler.evaluate declaration).map emit

/-- Every operation in a successfully emitted plan is execution-stage safe. -/
theorem emitted_plan_erases_translation
    (declaration : Declaration) (plan : ExecutablePlan)
    (_success : compileDeclaration.evaluate declaration = .ok plan)
    (operation : Operation .execution) (_present : operation ∈ plan.operations) :
    operation.feature.translationOnly = false := by
  exact execution_excludes_translation_only operation

end Cxx26
end Anoptic
