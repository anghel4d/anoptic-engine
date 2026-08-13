/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Std

namespace Anoptic

namespace Strings

abbrev View := List UInt8

structure Owned where
  bytes : List UInt8
  deriving DecidableEq

def empty : Owned := ⟨[]⟩

def concat (left right : Owned) : Owned :=
  ⟨left.bytes ++ right.bytes⟩

@[simp] theorem concat_left_identity (value : Owned) : concat empty value = value := by
  cases value
  rfl

@[simp] theorem concat_right_identity (value : Owned) : concat value empty = value := by
  cases value
  simp [concat, empty]

theorem concat_assoc (first second third : Owned) :
    concat (concat first second) third = concat first (concat second third) := by
  cases first
  cases second
  cases third
  simp [concat, List.append_assoc]

def continuation (byte : UInt8) : Bool :=
  0x80 ≤ byte.toNat && byte.toNat ≤ 0xbf

def decodeUtf8 : View → Option (List Nat)
  | [] => some []
  | firstByte :: rest =>
      let first := firstByte.toNat
      if first < 0x80 then
        (decodeUtf8 rest).map (first :: ·)
      else if 0xc2 ≤ first && first ≤ 0xdf then
        match rest with
        | secondByte :: tail =>
            if continuation secondByte then
              let codepoint :=
                (first - 0xc0) * 0x40 + (secondByte.toNat - 0x80)
              (decodeUtf8 tail).map (codepoint :: ·)
            else none
        | _ => none
      else if 0xe0 ≤ first && first ≤ 0xef then
        match rest with
        | secondByte :: thirdByte :: tail =>
            let second := secondByte.toNat
            let validSecond :=
              if first = 0xe0 then 0xa0 ≤ second && second ≤ 0xbf
              else if first = 0xed then 0x80 ≤ second && second ≤ 0x9f
              else 0xe1 ≤ first && first ≤ 0xef && continuation secondByte
            if validSecond && continuation thirdByte then
              let codepoint := (first - 0xe0) * 0x1000
                + (second - 0x80) * 0x40 + (thirdByte.toNat - 0x80)
              (decodeUtf8 tail).map (codepoint :: ·)
            else none
        | _ => none
      else if 0xf0 ≤ first && first ≤ 0xf4 then
        match rest with
        | secondByte :: thirdByte :: fourthByte :: tail =>
            let second := secondByte.toNat
            let validSecond :=
              if first = 0xf0 then 0x90 ≤ second && second ≤ 0xbf
              else if first = 0xf4 then 0x80 ≤ second && second ≤ 0x8f
              else continuation secondByte
            if validSecond && continuation thirdByte
                && continuation fourthByte then
              let codepoint := (first - 0xf0) * 0x40000
                + (second - 0x80) * 0x1000
                + (thirdByte.toNat - 0x80) * 0x40
                + (fourthByte.toNat - 0x80)
              (decodeUtf8 tail).map (codepoint :: ·)
            else none
        | _ => none
      else none
termination_by bytes => bytes.length
decreasing_by all_goals simp_wf <;> omega

structure Utf8 where
  bytes : View
  codepoints : List Nat
  decoded : decodeUtf8 bytes = some codepoints

def Utf8.forget (value : Utf8) : View := value.bytes

@[simp] theorem Utf8.forget_mk (bytes : View) (codepoints : List Nat)
    (decoded : decodeUtf8 bytes = some codepoints) :
    (Utf8.mk bytes codepoints decoded).forget = bytes :=
  rfl

inductive BuilderPhase where
  | empty
  | building

inductive Builder : BuilderPhase → Type where
  | empty : Builder .empty
  | building : List UInt8 → Builder .building

def begin : Builder .empty → Builder .building
  | .empty => .building []

def append (builder : Builder .building) (bytes : View) : Builder .building :=
  match builder with
  | .building current => .building (current ++ bytes)

def finish : Builder .building → Owned
  | .building bytes => ⟨bytes⟩

@[simp] theorem finish_begin (builder : Builder .empty) :
    (finish (begin builder)).bytes = [] := by
  cases builder
  rfl

structure Symbol where
  value : Nat
  deriving DecidableEq

structure InternState where
  next : Nat
  lookup : View → Option Symbol

def intern (state : InternState) (value : View) : Symbol × InternState :=
  match state.lookup value with
  | some symbol => (symbol, state)
  | none =>
      let symbol : Symbol := ⟨state.next⟩
      (symbol, {
        next := state.next + 1
        lookup := fun sought =>
          if sought = value then some symbol else state.lookup sought })

@[simp] theorem intern_records_symbol (state : InternState) (value : View) :
    ((intern state value).2.lookup value) = some (intern state value).1 := by
  simp [intern]
  split <;> simp_all

theorem intern_idempotent (state : InternState) (value : View) :
    let first := (intern state value).2
    (intern first value).2 = first := by
  cases found : state.lookup value with
  | none => simp [intern, found]
  | some symbol => simp [intern, found]

end Strings
end Anoptic
