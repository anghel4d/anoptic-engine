/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std

namespace Anoptic

universe u v

namespace Filesystem

def ValidSegment (text : String) : Prop :=
  text ≠ "" ∧ text ≠ "." ∧ text ≠ ".."

structure Segment where
  text : String
  valid : ValidSegment text
  deriving DecidableEq

structure Path (Root : Type u) where
  segments : List Segment
  deriving DecidableEq

def root (Root : Type u) : Path Root := ⟨[]⟩

def append (path : Path Root) (relative : List Segment) : Path Root :=
  ⟨path.segments ++ relative⟩

@[simp] theorem append_empty (path : Path Root) : append path [] = path := by
  cases path
  simp [append]

theorem append_assoc (path : Path Root) (first second : List Segment) :
    append (append path first) second = append path (first ++ second) := by
  cases path
  simp [append, List.append_assoc]

structure PackRange where
  offset : Nat
  size : Nat
  deriving DecidableEq

inductive Source (Root : Type u) where
  | path : Path Root → Source Root
  | memory : List UInt8 → Source Root
  | packRange : PackRange → Source Root

structure Snapshot where
  bytes : List UInt8
  identity : Nat
  deriving DecidableEq

def readMemory (bytes : List UInt8) (identity : Nat) : Snapshot :=
  ⟨bytes, identity⟩

@[simp] theorem readMemory_is_immutable_value (bytes : List UInt8) (identity : Nat) :
    (readMemory bytes identity).bytes = bytes :=
  rfl

structure AppendSink where
  bytes : List UInt8
  deriving DecidableEq

def appendBytes (sink : AppendSink) (bytes : List UInt8) : AppendSink :=
  ⟨sink.bytes ++ bytes⟩

@[simp] theorem appendBytes_empty (sink : AppendSink) :
    appendBytes sink [] = sink := by
  cases sink
  simp [appendBytes]

theorem appendBytes_assoc (sink : AppendSink)
    (first second : List UInt8) :
    appendBytes (appendBytes sink first) second =
      appendBytes sink (first ++ second) := by
  cases sink
  simp [appendBytes, List.append_assoc]

def Source.fold (onPath : Path Root → Result)
    (onMemory : List UInt8 → Result)
    (onPack : PackRange → Result) : Source Root → Result
  | .path value => onPath value
  | .memory value => onMemory value
  | .packRange value => onPack value

@[simp] theorem Source.fold_memory {Root : Type u} {Result : Type v}
    (onPath : Path Root → Result) (onMemory : List UInt8 → Result)
    (onPack : PackRange → Result) (bytes : List UInt8) :
    Source.fold onPath onMemory onPack
        (Source.memory (Root := Root) bytes) =
      onMemory bytes :=
  rfl

end Filesystem
end Anoptic
