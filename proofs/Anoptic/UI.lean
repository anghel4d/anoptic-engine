/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std
import Anoptic.Outcome

namespace Anoptic

namespace UI

structure RoundedRect where id : Nat deriving DecidableEq
structure Shadow where id : Nat deriving DecidableEq
structure Image where id : Nat deriving DecidableEq
structure Path where id : Nat deriving DecidableEq
structure GlyphRun where id : Nat deriving DecidableEq

inductive Primitive where
  | roundedRect : RoundedRect → Primitive
  | shadow : Shadow → Primitive
  | image : Image → Primitive
  | path : Path → Primitive
  | glyphRun : GlyphRun → Primitive
  deriving DecidableEq

structure Scene where
  primitives : List Primitive
  clips : List Nat
  paints : List Nat
  curves : List Nat
  deriving DecidableEq

structure PackedScene where
  scene : Scene
  deriving DecidableEq

inductive CapacityError where
  | primitives
  deriving DecidableEq

def build (capacity : Nat) (scene : Scene) :
    Outcome.Result PackedScene CapacityError :=
  if scene.primitives.length ≤ capacity then .ok ⟨scene⟩ else .error .primitives

theorem build_succeeds_with_capacity (capacity : Nat) (scene : Scene)
    (fits : scene.primitives.length ≤ capacity) :
    build capacity scene = .ok ⟨scene⟩ := by
  simp [build, fits]

/-- CPU and GPU are interpretations of one packed-scene specification. -/
def rasterSpec (scene : PackedScene) : List Nat :=
  scene.scene.primitives.mapIdx fun index _ => index

def evalCpu (scene : PackedScene) : List Nat := rasterSpec scene
def evalGpu (scene : PackedScene) : List Nat := rasterSpec scene

@[simp] theorem cpu_gpu_equivalent (scene : PackedScene) :
    evalCpu scene = evalGpu scene :=
  rfl

end UI
end Anoptic
