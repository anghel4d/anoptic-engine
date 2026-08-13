/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

namespace Anoptic

universe u v w

namespace ResourceRoute

/-- A reflected transform is a typed many-input, many-output signature. -/
structure Signature (Cell : Type u) where
  InputPort : Type u
  OutputPort : Type u
  inputType : InputPort → Cell
  outputType : OutputPort → Cell

/-- Values for every typed input port of one transform invocation. -/
def Inputs (signature : Signature Cell) (Value : Cell → Type v) :=
  (port : signature.InputPort) → Value (signature.inputType port)

/-- Values for every typed output port of one transform invocation. -/
def Outputs (signature : Signature Cell) (Value : Cell → Type v) :=
  (port : signature.OutputPort) → Value (signature.outputType port)

/-- A producer of `target` is selected by both transform identity and output port. -/
structure Producer (Transform : Type v) (signature : Transform → Signature Cell)
    (target : Cell) where
  transform : Transform
  outputPort : (signature transform).OutputPort
  produces : (signature transform).outputType outputPort = target

/-- Selected provenance carries the producer port and every required input handle. -/
structure Provenance (Transform : Type v) (signature : Transform → Signature Cell)
    (Ref : Cell → Type w) (target : Cell) extends Producer Transform signature target where
  inputs : Inputs (signature transform) Ref

/-- Immutable references may be shared without postulating a diagonal on cell values. -/
def shareRef {Cell : Type u} {Ref : Cell → Type v} {cell : Cell}
    (reference : Ref cell) : Ref cell × Ref cell :=
  (reference, reference)

@[simp] theorem shared_left {Cell : Type u} {Ref : Cell → Type v} {cell : Cell}
    (reference : Ref cell) :
    (shareRef reference).1 = reference :=
  rfl

@[simp] theorem shared_right {Cell : Type u} {Ref : Cell → Type v} {cell : Cell}
    (reference : Ref cell) :
    (shareRef reference).2 = reference :=
  rfl

end ResourceRoute
end Anoptic
