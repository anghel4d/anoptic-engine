/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

namespace Anoptic

universe u v w

namespace ResourceRoute

/-- A typed port boundary is a dependent product of values indexed by cells. -/
structure Boundary (Cell : Type u) where
  Port : Type
  cell : Port → Cell

def Boundary.Values (boundary : Boundary Cell) (Value : Cell → Type v) :=
  (port : boundary.Port) → Value (boundary.cell port)

def Boundary.tensor (left right : Boundary Cell) : Boundary Cell where
  Port := Sum left.Port right.Port
  cell
    | .inl port => left.cell port
    | .inr port => right.cell port

/-- A many-port route morphism consumes and produces complete typed boundaries. -/
structure Morphism (Value : Cell → Type v)
    (source target : Boundary Cell) where
  run : source.Values Value → target.Values Value

namespace Morphism

def identity (boundary : Boundary Cell) : Morphism Value boundary boundary :=
  ⟨id⟩

def compose (first : Morphism Value A B) (second : Morphism Value B C) :
    Morphism Value A C :=
  ⟨second.run ∘ first.run⟩

@[simp] theorem identity_left (morphism : Morphism Value A B) :
    compose (identity A) morphism = morphism := by
  cases morphism
  rfl

@[simp] theorem identity_right (morphism : Morphism Value A B) :
    compose morphism (identity B) = morphism := by
  cases morphism
  rfl

theorem compose_assoc (first : Morphism Value A B)
    (second : Morphism Value B C) (third : Morphism Value C D) :
    compose (compose first second) third =
      compose first (compose second third) := by
  cases first
  cases second
  cases third
  rfl

def tensor {Cell : Type u} {Value : Cell → Type v}
    {A B C D : Boundary Cell}
    (left : Morphism Value A B) (right : Morphism Value C D) :
    Morphism Value (Boundary.tensor A C) (Boundary.tensor B D) where
  run := fun input port =>
    match port with
    | .inl output => left.run (fun inputPort => input (.inl inputPort)) output
    | .inr output => right.run (fun inputPort => input (.inr inputPort)) output

theorem tensor_interchange
    {Cell : Type u} {Value : Cell → Type v}
    {A B C D E F : Boundary Cell}
    (firstLeft : Morphism Value A B) (secondLeft : Morphism Value B C)
    (firstRight : Morphism Value D E) (secondRight : Morphism Value E F) :
    tensor (compose firstLeft secondLeft) (compose firstRight secondRight) =
      compose (tensor firstLeft firstRight) (tensor secondLeft secondRight) := by
  cases firstLeft
  cases secondLeft
  cases firstRight
  cases secondRight
  rfl

end Morphism

/-- A reflected transform is a typed many-input, many-output signature. -/
structure Signature (Cell : Type u) where
  InputPort : Type
  OutputPort : Type
  inputType : InputPort → Cell
  outputType : OutputPort → Cell

def Signature.inputBoundary (signature : Signature Cell) : Boundary Cell :=
  ⟨signature.InputPort, signature.inputType⟩

def Signature.outputBoundary (signature : Signature Cell) : Boundary Cell :=
  ⟨signature.OutputPort, signature.outputType⟩

abbrev Interpretation (signature : Signature Cell) (Value : Cell → Type v) :=
  Morphism Value signature.inputBoundary signature.outputBoundary

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
