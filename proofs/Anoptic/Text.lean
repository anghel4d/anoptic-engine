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
  glyph : UInt8
  advance : Nat
  deriving DecidableEq

def shape (text : Strings.View) : List GlyphInstance :=
  text.map fun byte => ⟨byte, 1⟩

def measure (text : Strings.View) : Nat :=
  text.length

@[simp] theorem shape_count (text : Strings.View) :
    (shape text).length = text.length := by
  simp [shape]

@[simp] theorem measure_count (text : Strings.View) :
    measure text = text.length :=
  rfl

@[simp] theorem measure_matches_shape_count (text : Strings.View) :
    measure text = (shape text).length := by
  simp [measure]

end Text
end Anoptic
