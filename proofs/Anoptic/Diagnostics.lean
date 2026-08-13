/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std

namespace Anoptic

namespace Diagnostics

inductive Severity where
  | debug
  | info
  | warning
  | error
  deriving DecidableEq

structure Record where
  severity : Severity
  route : String
  message : String
  deriving DecidableEq

abbrev Log := List Record
abbrev Sink := List Record

def drain (records : Log) : Sink := records

@[simp] theorem drain_empty : drain [] = [] := rfl

theorem drain_append (first second : Log) :
    drain (first ++ second) = drain first ++ drain second :=
  rfl

structure CrashRecord where
  signal : Nat
  message : String

/-- Crash interpretation cannot return to ordinary execution. -/
abbrev NoReturn := Empty
abbrev CrashInterpreter := CrashRecord → NoReturn

end Diagnostics
end Anoptic
