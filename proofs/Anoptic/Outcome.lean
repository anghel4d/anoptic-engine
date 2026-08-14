/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

namespace Anoptic

universe u v w x

namespace Outcome

abbrev Result (Value : Type u) (Error : Type v) := Except Error Value

def pure (value : Value) : Result Value Error := .ok value

def bind (result : Result Value Error)
    (next : Value → Result Next Error) : Result Next Error :=
  match result with
  | .error error => .error error
  | .ok value => next value

def map (function : Value → Next) (result : Result Value Error) :
    Result Next Error :=
  bind result (pure ∘ function)

def orElse (result : Result Value Error)
    (recover : Error → Result Value NextError) : Result Value NextError :=
  match result with
  | .error error => recover error
  | .ok value => .ok value

@[simp] theorem bind_error (error : Error) (next : Value → Result Next Error) :
    bind (.error error) next = .error error :=
  rfl

@[simp] theorem bind_ok (value : Value) (next : Value → Result Next Error) :
    bind (.ok value) next = next value :=
  rfl

@[simp] theorem orElse_error (error : Error)
    (recover : Error → Result Value NextError) :
    orElse (.error error) recover = recover error :=
  rfl

@[simp] theorem orElse_ok (value : Value)
    (recover : Error → Result Value NextError) :
    orElse (.ok value) recover = .ok value :=
  rfl

@[simp] theorem left_identity (value : Value)
    (next : Value → Result Next Error) :
    bind (pure value) next = next value :=
  rfl

@[simp] theorem right_identity (result : Result Value Error) :
    bind result pure = result := by
  cases result <;> rfl

theorem bind_assoc (result : Result Value Error)
    (first : Value → Result Middle Error)
    (second : Middle → Result Next Error) :
    bind (bind result first) second =
      bind result (fun value => bind (first value) second) := by
  cases result <;> rfl

@[simp] theorem map_id (result : Result Value Error) :
    map id result = result := by
  cases result <;> rfl

theorem map_comp (first : Value → Middle) (second : Middle → Next)
    (result : Result Value Error) :
    map second (map first result) = map (second ∘ first) result := by
  cases result <;> rfl

/-- Generated C ABI outcomes retain the semantic sum rather than a free product. -/
abbrev CResult (Error : Type u) (Value : Type v) := Sum Error Value

def CResult.toResult : CResult Error Value → Result Value Error
  | Sum.inl error => Except.error error
  | Sum.inr value => Except.ok value

def CResult.ofResult : Result Value Error → CResult Error Value
  | Except.error error => Sum.inl error
  | Except.ok value => Sum.inr value

def CResult.error? : CResult Error Value → Option Error
  | Sum.inl error => some error
  | Sum.inr _ => none

def CResult.value? : CResult Error Value → Option Value
  | Sum.inl _ => none
  | Sum.inr value => some value

@[simp] theorem CResult.inactive_output_unobservable
    (result : CResult Error Value) :
    (result.error?.isSome && result.value?.isSome) = false := by
  cases result <;> rfl

@[simp] theorem CResult.toResult_ofResult (result : Result Value Error) :
    CResult.toResult (CResult.ofResult result) = result := by
  cases result <;> rfl

@[simp] theorem CResult.ofResult_toResult (result : CResult Error Value) :
    CResult.ofResult (CResult.toResult result) = result := by
  cases result <;> rfl

end Outcome
end Anoptic
