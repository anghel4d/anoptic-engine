/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Resource
import Anoptic.RenderResources
import Anoptic.World
import Anoptic.Music
import Anoptic.Synth
import Anoptic.Transaction

namespace Anoptic

namespace Integration

inductive RoomCell where
  | vikingRoom
  | gltf
  | texture
  deriving DecidableEq

inductive RoomInput where
  | gltf
  | texture
  deriving DecidableEq

def roomSignature : ResourceRoute.Signature RoomCell where
  InputPort := RoomInput
  OutputPort := Unit
  inputType
    | .gltf => .gltf
    | .texture => .texture
  outputType _ := .vikingRoom

inductive RoomTransform where
  | assemble
  deriving DecidableEq

def roomTransformSignature : RoomTransform → ResourceRoute.Signature RoomCell
  | .assemble => roomSignature

abbrev RoomRef : RoomCell → Type := Resource.Ref RoomCell

def roomInstance : ResourceRoute.Instance RoomTransform
    roomTransformSignature RoomRef .vikingRoom where
  transform := .assemble
  outputPort := ()
  produces := rfl
  inputs
    | .gltf => ⟨11⟩
    | .texture => ⟨12⟩
  parent := ⟨10⟩

def gltfFocus := roomInstance.down .gltf
def textureFocus := roomInstance.down .texture

/-- `down` bifurcates by typed input port; either focus returns to one instance. -/
example : gltfFocus.up = roomInstance.parent ∧
    textureFocus.up = roomInstance.parent :=
  ⟨rfl, rfl⟩

example : gltfFocus.reference.identity = 11 :=
  rfl

def revision : Resource.Revision := ⟨4, 91⟩

def texture : RenderResources.Portable .texture := ⟨12, 44⟩

def residentTexture : Resource.Epoch revision (RenderResources.Portable .texture) :=
  ⟨texture⟩

def textureSlot : RenderResources.Slot .texture :=
  RenderResources.realize 7 residentTexture.resident

/-- Revision-indexed residency and device realization preserve semantic identity. -/
example : textureSlot.identity = texture.identity :=
  rfl

def frame : World.FrameWorld 4 :=
  ⟨⟨101⟩, ⟨revision.contentIdentity⟩⟩

/-- ECS and residency observations used by one frame cannot mix generations. -/
example : frame.ecs.generation = frame.residency.generation :=
  World.frame_generation_agrees frame

def beforePublication : PublishedState Nat Resource.Revision :=
  ⟨10, some revision⟩

/-- A failed replacement may retain cooker work but cannot replace the frame revision. -/
example :
    (finishAttempt beforePublication 11 (.error "decode failed")).1.published =
      some revision :=
  rfl

/-- Music snapshots and synth input paths agree at the engine composition boundary. -/
example (state : Music.State) : Music.restore (Music.snapshot state) = state :=
  Music.restore_snapshot state

example (state : Synth.State) (inputs : List Synth.Input)
    (frames : Synth.FrameRange) :
    Synth.render state (Synth.batchStream inputs) frames =
      Synth.render state (Synth.liveStream [inputs]) frames :=
  Synth.batch_live_equivalent state inputs [inputs] frames (by simp)

end Integration
end Anoptic
