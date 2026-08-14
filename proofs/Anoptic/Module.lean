/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Compiler
import Anoptic.Semantic

namespace Anoptic

universe u

/--
The compile-time plane of a module: one public shape and one projection-local
compiler.  It deliberately contains no semantic carrier or interpreter.
-/
structure CompiledSignature (Shared : Type u) where
  api : API.{u}
  Local : Type u
  Error : Type u
  Plan : Type u
  projection : Shared → Local
  localCompiler : Compiler.Partial Local Error Plan

namespace CompiledSignature

def compiler (signature : CompiledSignature Shared) :
    Compiler.Partial Shared signature.Error signature.Plan :=
  Compiler.projected signature.projection signature.localCompiler

/-- Equality after projection is the exact noninterference law. -/
theorem noninterference (signature : CompiledSignature Shared)
    {left right : Shared}
    (same : signature.projection left = signature.projection right) :
    signature.compiler left = signature.compiler right :=
  Compiler.projected_noninterference signature.projection
    signature.localCompiler same

end CompiledSignature

/--
A semantic module keeps its compiler plane and algebra plane distinct while
requiring both to describe the same public API shape.
-/
structure Module (Shared : Type u) where
  signature : CompiledSignature Shared
  algebra : Semantic.Algebra signature.api

end Anoptic
