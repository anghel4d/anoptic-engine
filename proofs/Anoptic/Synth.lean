/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std
import Anoptic.Music

namespace Anoptic

namespace Synth

inductive Input where
  | score : Nat → Input
  | music : Music.Bar → Input
  | transport : Nat → Input
  deriving DecidableEq

structure FrameRange where
  first : Nat
  count : Nat
  deriving DecidableEq

structure State where
  cursor : Nat
  voices : Nat
  deriving DecidableEq

structure Output where
  state : State
  busSamples : List Int
  automation : List Nat
  events : List Nat
  deriving DecidableEq

def render (state : State) (inputs : List Input) (frames : FrameRange) : Output :=
  ⟨⟨state.cursor + frames.count, state.voices + inputs.length⟩,
    List.replicate frames.count (Int.ofNat (state.voices + inputs.length)),
    [], []⟩

def batchStream (inputs : List Input) : List Input := inputs
def liveStream (chunks : List (List Input)) : List Input := chunks.flatten

theorem batch_live_equivalent (state : State) (inputs : List Input)
    (chunks : List (List Input)) (frames : FrameRange)
    (sameOrder : chunks.flatten = inputs) :
    render state (batchStream inputs) frames =
      render state (liveStream chunks) frames := by
  simp [batchStream, liveStream, sameOrder]

theorem render_respects_stream_equality (state : State)
    (left right : List Input) (frames : FrameRange) (same : left = right) :
    render state left frames = render state right frames :=
  same ▸ rfl

@[simp] theorem replicate_append (first second : Nat) (value : Int) :
    List.replicate first value ++ List.replicate second value =
      List.replicate (first + second) value := by
  induction first with
  | zero => simp
  | succ first induction => simp [List.replicate_succ, induction, Nat.succ_add]

theorem render_chunks_compose (state : State) (inputs : List Input)
    (firstCount secondCount : Nat) :
    let first := render state inputs ⟨0, firstCount⟩
    let second := render first.state [] ⟨firstCount, secondCount⟩
    second.state = (render state inputs ⟨0, firstCount + secondCount⟩).state ∧
      first.busSamples ++ second.busSamples =
        (render state inputs ⟨0, firstCount + secondCount⟩).busSamples := by
  simp [render, Nat.add_assoc]

end Synth
end Anoptic
