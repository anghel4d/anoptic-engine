/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std
import Anoptic.Strings

namespace Anoptic

namespace Text

structure FontSource where
  identity : Nat
  bytes : List UInt8

structure Face where
  source : FontSource
  faceIndex : Nat

structure Bake where
  face : Face
  glyphs : List Nat

structure Atlas where
  bake : Bake
  textureIdentity : Nat

def openFace (source : FontSource) (faceIndex : Nat) : Face :=
  ⟨source, faceIndex⟩

def bakeFace (face : Face) (glyphs : List Nat) : Bake :=
  ⟨face, glyphs⟩

def makeAtlas (bake : Bake) (textureIdentity : Nat) : Atlas :=
  ⟨bake, textureIdentity⟩

def openBakeAtlas (source : FontSource) (faceIndex : Nat)
    (glyphs : List Nat) (textureIdentity : Nat) : Atlas :=
  makeAtlas (bakeFace (openFace source faceIndex) glyphs) textureIdentity

theorem open_bake_atlas_composes (source : FontSource) (faceIndex : Nat)
    (glyphs : List Nat) (textureIdentity : Nat) :
    openBakeAtlas source faceIndex glyphs textureIdentity =
      (makeAtlas (bakeFace (openFace source faceIndex) glyphs) textureIdentity) :=
  rfl

@[simp] theorem atlas_retains_source (source : FontSource) (faceIndex : Nat)
    (glyphs : List Nat) (textureIdentity : Nat) :
    (makeAtlas (bakeFace (openFace source faceIndex) glyphs)
      textureIdentity).bake.face.source = source :=
  rfl

structure GlyphInstance where
  glyph : Nat
  advance : Nat
  deriving DecidableEq

def shape (text : Strings.Utf8) : List GlyphInstance :=
  text.codepoints.map fun codepoint => ⟨codepoint, 1⟩

def measure (text : Strings.Utf8) : Nat :=
  (shape text).foldl (fun width glyph => width + glyph.advance) 0

private theorem fold_glyph_advances (codepoints : List Nat) (start : Nat) :
    (codepoints.map fun codepoint => GlyphInstance.mk codepoint 1).foldl
        (fun width glyph => width + glyph.advance) start =
      start + codepoints.length := by
  induction codepoints generalizing start with
  | nil => simp
  | cons codepoint rest induction =>
      simp only [List.map_cons, List.foldl_cons, List.length_cons]
      rw [induction]
      omega

@[simp] theorem shape_count (text : Strings.Utf8) :
    (shape text).length = text.codepoints.length := by
  simp [shape]

@[simp] theorem measure_count (text : Strings.Utf8) :
    measure text = text.codepoints.length := by
  simpa [measure, shape] using fold_glyph_advances text.codepoints 0

@[simp] theorem measure_matches_shape_count (text : Strings.Utf8) :
    measure text = (shape text).length := by
  simp

end Text
end Anoptic
