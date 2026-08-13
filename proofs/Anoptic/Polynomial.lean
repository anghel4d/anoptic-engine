/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

namespace Anoptic

universe u v w x

/-- An API is a family of response types indexed by its possible queries. -/
structure API where
  Query : Type u
  Response : Query → Type u

namespace API

/-- The polynomial extension represented by an API container. -/
def Extension (api : API.{u}) (value : Type v) : Type (max u v) :=
  Sigma fun query => api.Response query → value

/-- Polynomial extensions are covariant in their returned value. -/
def map {api : API.{u}} {A : Type v} {B : Type w} (function : A → B) :
    api.Extension A → api.Extension B
  | ⟨query, continueWith⟩ => ⟨query, function ∘ continueWith⟩

@[simp] theorem map_id {api : API.{u}} {A : Type v}
    (value : api.Extension A) : map id value = value := by
  cases value
  rfl

@[simp] theorem map_comp {api : API.{u}} {A : Type v} {B : Type w}
    {C : Type x} (first : A → B) (second : B → C)
    (value : api.Extension A) :
    map second (map first value) = map (second ∘ first) value := by
  cases value
  rfl

/--
An API morphism sends queries forward and translates target responses backward.
This is the container morphism represented by an ordinary API adapter.
-/
structure Hom (source target : API.{u}) where
  onQuery : source.Query → target.Query
  onResponse : (query : source.Query) →
    target.Response (onQuery query) → source.Response query

namespace Hom

def refl (api : API.{u}) : Hom api api where
  onQuery := id
  onResponse := fun _ response => response

/-- Composition applies `first` and then `second`. -/
def trans {A B C : API.{u}} (first : Hom A B) (second : Hom B C) : Hom A C where
  onQuery := second.onQuery ∘ first.onQuery
  onResponse := fun query response =>
    first.onResponse query (second.onResponse (first.onQuery query) response)

/-- Every container morphism induces a natural map of polynomial extensions. -/
def induced {source target : API.{u}} (hom : Hom source target) {A : Type v} :
    source.Extension A → target.Extension A
  | ⟨query, continueWith⟩ =>
      ⟨hom.onQuery query,
        fun response => continueWith (hom.onResponse query response)⟩

@[simp] theorem refl_trans {A B : API.{u}} (hom : Hom A B) :
    trans (refl A) hom = hom := by
  cases hom
  rfl

@[simp] theorem trans_refl {A B : API.{u}} (hom : Hom A B) :
    trans hom (refl B) = hom := by
  cases hom
  rfl

@[simp] theorem trans_assoc {A B C D : API.{u}}
    (ab : Hom A B) (bc : Hom B C) (cd : Hom C D) :
    trans (trans ab bc) cd = trans ab (trans bc cd) := by
  cases ab
  cases bc
  cases cd
  rfl

@[simp] theorem induced_refl {api : API.{u}} {A : Type v}
    (value : api.Extension A) : induced (refl api) value = value := by
  cases value
  rfl

@[simp] theorem induced_trans {source middle target : API.{u}}
    (first : Hom source middle) (second : Hom middle target) {A : Type v}
    (value : source.Extension A) :
    induced (trans first second) value = induced second (induced first value) := by
  cases value
  rfl

end Hom

/-- Choice of one interface is the sum of its query shapes. -/
def choice (left right : API.{u}) : API.{u} where
  Query := Sum left.Query right.Query
  Response
    | .inl query => left.Response query
    | .inr query => right.Response query

def choicePack {left right : API.{u}} {A : Type v} :
    Sum (left.Extension A) (right.Extension A) →
      (choice left right).Extension A
  | .inl ⟨query, continueWith⟩ => ⟨.inl query, continueWith⟩
  | .inr ⟨query, continueWith⟩ => ⟨.inr query, continueWith⟩

def choiceUnpack {left right : API.{u}} {A : Type v} :
    (choice left right).Extension A →
      Sum (left.Extension A) (right.Extension A)
  | ⟨.inl query, continueWith⟩ => .inl ⟨query, continueWith⟩
  | ⟨.inr query, continueWith⟩ => .inr ⟨query, continueWith⟩

@[simp] theorem choice_unpack_pack {left right : API.{u}} {A : Type v}
    (value : Sum (left.Extension A) (right.Extension A)) :
    choiceUnpack (choicePack value) = value := by
  cases value <;> rename_i value <;> cases value <;> rfl

@[simp] theorem choice_pack_unpack {left right : API.{u}} {A : Type v}
    (value : (choice left right).Extension A) :
    choicePack (choiceUnpack value) = value := by
  cases value with
  | mk query continueWith =>
      cases query <;> rfl

/-- Simultaneous independent interfaces pair queries and sum response positions. -/
def tensor (left right : API.{u}) : API.{u} where
  Query := left.Query × right.Query
  Response query := Sum (left.Response query.1) (right.Response query.2)

def tensorPack {left right : API.{u}} {A : Type v} :
    left.Extension A × right.Extension A → (tensor left right).Extension A
  | (⟨leftQuery, leftContinue⟩, ⟨rightQuery, rightContinue⟩) =>
      ⟨(leftQuery, rightQuery), fun
        | .inl response => leftContinue response
        | .inr response => rightContinue response⟩

def tensorUnpack {left right : API.{u}} {A : Type v} :
    (tensor left right).Extension A → left.Extension A × right.Extension A
  | ⟨(leftQuery, rightQuery), continueWith⟩ =>
      (⟨leftQuery, fun response => continueWith (.inl response)⟩,
       ⟨rightQuery, fun response => continueWith (.inr response)⟩)

@[simp] theorem tensor_unpack_pack {left right : API.{u}} {A : Type v}
    (value : left.Extension A × right.Extension A) :
    tensorUnpack (tensorPack value) = value := by
  rcases value with ⟨⟨leftQuery, leftContinue⟩, ⟨rightQuery, rightContinue⟩⟩
  rfl

@[simp] theorem tensor_pack_unpack {left right : API.{u}} {A : Type v}
    (value : (tensor left right).Extension A) :
    tensorPack (tensorUnpack value) = value := by
  rcases value with ⟨⟨leftQuery, rightQuery⟩, continueWith⟩
  apply Sigma.ext
  · rfl
  · apply heq_of_eq
    funext response
    cases response <;> rfl

/-- The zero polynomial has no possible query. -/
def zero : API.{u} where
  Query := ULift.{u} Empty
  Response query := nomatch query.down

/-- The unit polynomial has one query and no response positions. -/
def unit : API.{u} where
  Query := ULift.{u} Unit
  Response _ := ULift.{u} Empty

namespace Hom

/-- The unit polynomial is terminal: every API has exactly one adaptor to it. -/
def toUnit (api : API.{u}) : Hom api unit where
  onQuery := fun _ => ULift.up ()
  onResponse := fun _ response => nomatch response.down

theorem toUnit_unique (api : API.{u}) (hom : Hom api unit) :
    hom = toUnit api := by
  rcases hom with ⟨onQuery, onResponse⟩
  have queryUnique : onQuery = fun _ => ULift.up () := by
    funext query
    rcases onQuery query with ⟨value⟩
    cases value
    rfl
  subst onQuery
  have responseUnique : onResponse =
      fun _ response => nomatch response.down := by
    funext query response
    exact nomatch response.down
  subst onResponse
  rfl

end Hom

theorem zeroElim {A : Type v} : (zero : API.{u}).Extension A → False
  | ⟨query, _⟩ => nomatch query.down

def unitValue {A : Type v} : (unit : API.{u}).Extension A :=
  ⟨ULift.up (), fun response => nomatch response.down⟩

theorem unit_unique {A : Type v} (value : (unit : API.{u}).Extension A) :
    value = unitValue := by
  rcases value with ⟨query, continueWith⟩
  rcases query with ⟨query⟩
  cases query
  apply Sigma.ext
  · rfl
  · apply heq_of_eq
    funext response
    exact nomatch response.down

end API
end Anoptic
