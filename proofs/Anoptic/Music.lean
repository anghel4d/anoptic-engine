/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std

namespace Anoptic

namespace Music

inductive Control where
  | setAffect : Int → Control
  | requestKey : Nat → Control
  | requestMotif : Nat → Control
  | setOverride : Nat → Int → Control
  | clearOverride : Nat → Control
  deriving DecidableEq

structure State where
  seed : Nat
  bar : Nat
  affect : Int
  deriving DecidableEq

structure Bar where
  number : Nat
  seed : Nat
  deriving DecidableEq

def applyControl (state : State) : Control → State
  | .setAffect affect => ⟨state.seed, state.bar, affect⟩
  | .requestKey key => ⟨state.seed + key, state.bar, state.affect⟩
  | .requestMotif motif => ⟨state.seed + motif, state.bar, state.affect⟩
  | .setOverride parameter value =>
      ⟨state.seed + parameter + value.natAbs, state.bar, state.affect⟩
  | .clearOverride parameter => ⟨state.seed + parameter, state.bar, state.affect⟩

def applyControls (state : State) (controls : List Control) : State :=
  controls.foldl applyControl state

theorem applyControls_append (state : State) (first second : List Control) :
    applyControls state (first ++ second) =
      applyControls (applyControls state first) second := by
  simp [applyControls, List.foldl_append]

def step (state : State) (controls : List Control) : State × Bar :=
  let controlled := applyControls state controls
  let next := ⟨controlled.seed, controlled.bar + 1, controlled.affect⟩
  (next, ⟨controlled.bar, controlled.seed⟩)

theorem step_deterministic (state : State) (controls : List Control) :
    step state controls = step state controls :=
  rfl

structure Snapshot where
  seed : Nat
  bar : Nat
  affect : Int
  deriving DecidableEq

def snapshot (state : State) : Snapshot :=
  ⟨state.seed, state.bar, state.affect⟩

def restore (snapshot : Snapshot) : State :=
  ⟨snapshot.seed, snapshot.bar, snapshot.affect⟩

@[simp] theorem restore_snapshot (state : State) :
    restore (snapshot state) = state := by
  cases state
  rfl

@[simp] theorem snapshot_restore (value : Snapshot) :
    snapshot (restore value) = value := by
  cases value
  rfl

end Music
end Anoptic
