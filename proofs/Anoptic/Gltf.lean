/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std
import Anoptic.Outcome

namespace Anoptic

namespace Gltf

abbrev Bytes := List UInt8

/-- The foreign source sum retains whether bytes arrived as JSON or GLB. -/
inductive Raw where
  | json : Bytes → Raw
  | glb : Bytes → Raw
  deriving DecidableEq

inductive SourceKind where
  | json
  | glb
  deriving DecidableEq

def Raw.kind : Raw → SourceKind
  | .json _ => .json
  | .glb _ => .glb

def Raw.bytes : Raw → Bytes
  | .json value => value
  | .glb value => value

@[simp] theorem json_kind (bytes : Bytes) : (Raw.json bytes).kind = .json := rfl

@[simp] theorem glb_kind (bytes : Bytes) : (Raw.glb bytes).kind = .glb := rfl

theorem json_ne_glb (jsonBytes glbBytes : Bytes) :
    Raw.json jsonBytes ≠ Raw.glb glbBytes := by
  intro equality
  cases equality

theorem raw_exhaustive (raw : Raw) :
    (∃ bytes, raw = .json bytes) ∨ (∃ bytes, raw = .glb bytes) := by
  cases raw with
  | json bytes => exact .inl ⟨bytes, rfl⟩
  | glb bytes => exact .inr ⟨bytes, rfl⟩

structure Parsed where
  source : Raw
  bufferCount : Nat

/-- Binding requires a value at every buffer input named by the parsed source. -/
structure Bound where
  parsed : Parsed
  buffers : Fin parsed.bufferCount → Bytes

def bind (parsed : Parsed) (buffers : Fin parsed.bufferCount → Bytes) : Bound :=
  ⟨parsed, buffers⟩

@[simp] theorem bind_retains_parsed (parsed : Parsed)
    (buffers : Fin parsed.bufferCount → Bytes) :
    (bind parsed buffers).parsed = parsed :=
  rfl

@[simp] theorem bind_retains_source (parsed : Parsed)
    (buffers : Fin parsed.bufferCount → Bytes) :
    (bind parsed buffers).parsed.source = parsed.source :=
  rfl

/-- A projection is pure; parsing and source acquisition remain owner effects. -/
structure Projection (Output : Type) where
  run : Bound → Output

def bindInputs (inputs : (parsed : Parsed) → Fin parsed.bufferCount → Bytes)
    (parsed : Parsed) : Bound :=
  bind parsed (inputs parsed)

def parseBind (parse : Raw → Outcome.Result Parsed Error)
    (inputs : (parsed : Parsed) → Fin parsed.bufferCount → Bytes)
    (raw : Raw) : Outcome.Result Bound Error :=
  Outcome.bind (parse raw) (fun parsed => .ok (bindInputs inputs parsed))

def bindProject (inputs : (parsed : Parsed) → Fin parsed.bufferCount → Bytes)
    (projection : Projection Output) (parsed : Parsed) :
    Outcome.Result Output Error :=
  .ok (projection.run (bindInputs inputs parsed))

def parseBindProject (parse : Raw → Outcome.Result Parsed Error)
    (inputs : (parsed : Parsed) → Fin parsed.bufferCount → Bytes)
    (projection : Projection Output) (raw : Raw) :
    Outcome.Result Output Error :=
  Outcome.bind (parseBind parse inputs raw) (fun bound => .ok (projection.run bound))

theorem parse_bind_project_assoc
    (parse : Raw → Outcome.Result Parsed Error)
    (inputs : (parsed : Parsed) → Fin parsed.bufferCount → Bytes)
    (projection : Projection Output) (raw : Raw) :
    parseBindProject parse inputs projection raw =
      Outcome.bind (parse raw) (bindProject inputs projection) := by
  cases parsed : parse raw <;>
    simp [parseBindProject, parseBind, bindProject, parsed]

end Gltf
end Anoptic
