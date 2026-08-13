/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std

namespace Anoptic

namespace Audio

structure SourceId where value : Nat deriving DecidableEq
structure BusId where value : Nat deriving DecidableEq
structure BufferSlot where value : Nat deriving DecidableEq

inductive Command where
  | play : SourceId → Nat → Command
  | update : SourceId → Nat → Command
  | stop : SourceId → Command
  | setBus : BusId → Nat → Command
  | setEffect : BusId → Nat → Nat → Command
  | adoptBuffer : BufferSlot → List Int → Command
  | retireBuffer : BufferSlot → Command
  deriving DecidableEq

inductive Event where
  | sourceRetired : SourceId → Event
  | bufferRetired : BufferSlot → Event
  | capacity : Nat → Event
  | underrun : Nat → Event
  deriving DecidableEq

structure Listener where
  x : Int
  y : Int
  z : Int
  deriving DecidableEq

structure MixerState where
  block : Nat
  active : List SourceId
  deriving DecidableEq

structure MixResult where
  state : MixerState
  samples : List Int
  events : List Event
  telemetry : Nat
  deriving DecidableEq

def applyCommand (state : MixerState) : Command → MixerState
  | .play source _ => ⟨state.block, state.active ++ [source]⟩
  | .stop source => ⟨state.block, state.active.filter (fun active => active != source)⟩
  | _ => state

def applyCommands (state : MixerState) (commands : List Command) : MixerState :=
  commands.foldl applyCommand state

theorem applyCommands_append (state : MixerState) (first second : List Command) :
    applyCommands state (first ++ second) =
      applyCommands (applyCommands state first) second := by
  simp [applyCommands, List.foldl_append]

/-- One deterministic block transition shared by device and offline sinks. -/
def mix (frames : Nat) (state : MixerState) (commands : List Command)
    (_listener : Listener) : MixResult :=
  let commanded := applyCommands state commands
  ⟨⟨commanded.block + 1, commanded.active⟩, List.replicate frames 0, [], 0⟩

def native (frames : Nat) (state : MixerState) (commands : List Command)
    (listener : Listener) : MixResult :=
  mix frames state commands listener

def offline (frames : Nat) (state : MixerState) (commands : List Command)
    (listener : Listener) : MixResult :=
  mix frames state commands listener

@[simp] theorem native_offline_equivalent (frames : Nat) (state : MixerState)
    (commands : List Command) (listener : Listener) :
    native frames state commands listener = offline frames state commands listener :=
  rfl

inductive EncodedFormat where
  | wav
  | oggOpus
  | mp3
  deriving DecidableEq

structure Encoded where
  identity : Nat
  format : EncodedFormat
  bytes : List UInt8

structure ResidentSample where
  identity : Nat
  samples : List Int

structure Stream where
  identity : Nat
  pages : List (List UInt8)

inductive RuntimeSound where
  | resident : ResidentSample → RuntimeSound
  | stream : Stream → RuntimeSound

def decodeResident (encoded : Encoded) (samples : List Int) : ResidentSample :=
  ⟨encoded.identity, samples⟩

def openStream (encoded : Encoded) (pages : List (List UInt8)) : Stream :=
  ⟨encoded.identity, pages⟩

@[simp] theorem decode_preserves_identity (encoded : Encoded) (samples : List Int) :
    (decodeResident encoded samples).identity = encoded.identity :=
  rfl

@[simp] theorem stream_preserves_identity (encoded : Encoded)
    (pages : List (List UInt8)) :
    (openStream encoded pages).identity = encoded.identity :=
  rfl

end Audio
end Anoptic
