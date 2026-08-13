/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std

namespace Anoptic

universe u v

namespace Mesh

structure Value (Vertex : Type u) (Index : Type v) where
  vertices : List Vertex
  indices : List Index

/-- Pure mesh kernels are ordinary maps; the law is supplied by each kernel. -/
structure Transform (Input : Type u) (Output : Type v) where
  run : Input → Output

def Transform.identity (Value : Type u) : Transform Value Value :=
  ⟨id⟩

def Transform.compose (first : Transform A B) (second : Transform B C) :
    Transform A C :=
  ⟨second.run ∘ first.run⟩

@[simp] theorem Transform.identity_left (transform : Transform A B) :
    Transform.compose (Transform.identity A) transform = transform := by
  cases transform
  rfl

@[simp] theorem Transform.identity_right (transform : Transform A B) :
    Transform.compose transform (Transform.identity B) = transform := by
  cases transform
  rfl

theorem Transform.compose_assoc (first : Transform A B)
    (second : Transform B C) (third : Transform C D) :
    Transform.compose (Transform.compose first second) third =
      Transform.compose first (Transform.compose second third) := by
  cases first
  cases second
  cases third
  rfl

structure Budget where
  triangles : Nat
  deriving DecidableEq

def lodChain (simplify : Budget → Input → Output) (input : Input)
    (budgets : List Budget) : List Output :=
  budgets.map (fun budget => simplify budget input)

@[simp] theorem lodChain_length (simplify : Budget → Input → Output)
    (input : Input) (budgets : List Budget) :
    (lodChain simplify input budgets).length = budgets.length := by
  simp [lodChain]

end Mesh
end Anoptic
