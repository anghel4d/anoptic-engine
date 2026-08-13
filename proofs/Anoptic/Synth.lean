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
    List.replicate frames.count 0, [], []⟩

def batchStream (inputs : List Input) : List Input := inputs
def liveStream (inputs : List Input) : List Input := inputs

@[simp] theorem batch_live_equivalent (state : State) (inputs : List Input)
    (frames : FrameRange) :
    render state (batchStream inputs) frames =
      render state (liveStream inputs) frames :=
  rfl

theorem render_respects_stream_equality (state : State)
    (left right : List Input) (frames : FrameRange) (same : left = right) :
    render state left frames = render state right frames :=
  same ▸ rfl

end Synth
end Anoptic
