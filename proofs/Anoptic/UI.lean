/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std
import Anoptic.Outcome
import Anoptic.Composition

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

def cpuTraversal : Nat → List Primitive → List Nat
  | _, [] => []
  | index, _ :: rest => index :: cpuTraversal (index + 1) rest

def deviceTraversal : Nat → Nat → List Nat
  | _, 0 => []
  | index, count + 1 => index :: deviceTraversal (index + 1) count

def evalCpu (scene : PackedScene) : List Nat :=
  cpuTraversal 0 scene.scene.primitives

def evalGpu (scene : PackedScene) : List Nat :=
  deviceTraversal 0 scene.scene.primitives.length

theorem traversals_agree (index : Nat) (primitives : List Primitive) :
    cpuTraversal index primitives = deviceTraversal index primitives.length := by
  induction primitives generalizing index with
  | nil => rfl
  | cons primitive rest induction =>
      simp [cpuTraversal, deviceTraversal, induction]

@[simp] theorem cpu_gpu_equivalent (scene : PackedScene) :
    evalCpu scene = evalGpu scene :=
  traversals_agree 0 scene.scene.primitives

def cpuInterpreter : Composition.Pure PackedScene (List Nat) := ⟨evalCpu⟩
def gpuInterpreter : Composition.Pure PackedScene (List Nat) := ⟨evalGpu⟩

theorem interpreter_composition_assoc
    (first : Composition.Pure A PackedScene)
    (last : Composition.Pure (List Nat) B) :
    Composition.Pure.compose
        (Composition.Pure.compose first cpuInterpreter) last =
      Composition.Pure.compose first
        (Composition.Pure.compose gpuInterpreter last) := by
  rw [Composition.Pure.compose_assoc]
  have same : cpuInterpreter = gpuInterpreter := by
    apply congrArg Composition.Pure.mk
    funext scene
    exact cpu_gpu_equivalent scene
  rw [same]

end UI
end Anoptic
