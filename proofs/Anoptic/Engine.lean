/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std
import Anoptic.Refinement

namespace Anoptic

universe u v w x

namespace Engine

/-- Root composition is ordinary interpreter composition. -/
def compose (first : A → B) (second : B → C) : A → C :=
  second ∘ first

@[simp] theorem compose_identity_left (function : A → B) :
    compose id function = function :=
  rfl

@[simp] theorem compose_identity_right (function : A → B) :
    compose function id = function :=
  rfl

theorem compose_assoc (first : A → B) (second : B → C) (third : C → D) :
    compose (compose first second) third =
      compose first (compose second third) :=
  rfl

structure Session where
  identity : Nat
  deriving DecidableEq

def teardownOrder (acquired : List Session) : List Session :=
  acquired.reverse

@[simp] theorem teardown_reverses_acquisition (acquired : List Session) :
    (teardownOrder acquired).reverse = acquired := by
  simp [teardownOrder]

inductive Phase where
  | cold
  | running
  | stopped

structure Process (phase : Phase) where
  sessions : List Session

def start (process : Process .cold) (sessions : List Session) : Process .running :=
  ⟨process.sessions ++ sessions⟩

def stop (_process : Process .running) : Process .stopped :=
  ⟨[]⟩

@[simp] theorem start_retains_sessions (process : Process .cold)
    (sessions : List Session) :
    (start process sessions).sessions = process.sessions ++ sessions :=
  rfl

@[simp] theorem stop_releases_sessions (process : Process .running) :
    (stop process).sessions = [] :=
  rfl

end Engine
end Anoptic
