/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

namespace Anoptic

universe u v w

/-- A stateful operation returns its successor state on both success and failure. -/
abbrev Transaction (State : Type u) (Error : Type v) (Value : Type w) :=
  State → State × Except Error Value

/-- Cooker state separates reusable working data from the published revision. -/
structure PublishedState (Cache : Type u) (Revision : Type v) where
  cache : Cache
  published : Option Revision

/--
Finishing an attempt always retains its new cache. Only a successful result changes
the published revision.
-/
def finishAttempt {Cache : Type u} {Revision : Type v} {Error : Type w}
    (before : PublishedState Cache Revision) (nextCache : Cache) :
    Except Error Revision → PublishedState Cache Revision × Except Error Revision
  | .error error =>
      (⟨nextCache, before.published⟩, .error error)
  | .ok revision =>
      (⟨nextCache, some revision⟩, .ok revision)

@[simp] theorem failure_retains_cache {Cache : Type u} {Revision : Type v}
    {Error : Type w} (before : PublishedState Cache Revision)
    (nextCache : Cache) (error : Error) :
    (finishAttempt before nextCache (.error error)).1.cache = nextCache :=
  rfl

@[simp] theorem failure_preserves_publication {Cache : Type u} {Revision : Type v}
    {Error : Type w} (before : PublishedState Cache Revision)
    (nextCache : Cache) (error : Error) :
    (finishAttempt before nextCache (.error error)).1.published = before.published :=
  rfl

@[simp] theorem success_retains_cache {Cache : Type u} {Revision : Type v}
    {Error : Type w} (before : PublishedState Cache Revision)
    (nextCache : Cache) (revision : Revision) :
    (finishAttempt (Error := Error) before nextCache (.ok revision)).1.cache = nextCache :=
  rfl

@[simp] theorem success_publishes_revision {Cache : Type u} {Revision : Type v}
    {Error : Type w} (before : PublishedState Cache Revision)
    (nextCache : Cache) (revision : Revision) :
    (finishAttempt (Error := Error) before nextCache (.ok revision)).1.published =
      some revision :=
  rfl

theorem publication_changes_only_on_success {Cache : Type u} {Revision : Type v}
    {Error : Type w} (before : PublishedState Cache Revision)
    (nextCache : Cache) (outcome : Except Error Revision)
    (changed : (finishAttempt before nextCache outcome).1.published ≠ before.published) :
    ∃ revision, outcome = .ok revision ∧
      (finishAttempt before nextCache outcome).1.published = some revision := by
  cases outcome with
  | error error =>
      simp [finishAttempt] at changed
  | ok revision =>
      exact ⟨revision, rfl, rfl⟩

end Anoptic
