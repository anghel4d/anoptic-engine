---
name: algebraic-reflect
description: Manual proof-first workflow for replacing an existing Anoptic C++26 type or API implementation with a reflected algebra. Invoke only when the user explicitly names $algebraic-reflect or explicitly asks to load this skill; do not infer invocation from ordinary mentions of Lean, proofs, reflection, constexpr, consteval, concepts, composition, types, APIs, refactoring, or correctness.
---

# Algebraic Reflect

Replace an existing implementation only after its abstract semantics have been
proved. Preserve this order: prove, delete, reason, implement.

## 1. Establish the target

1. Require a clean worktree or isolate unrelated user changes.
2. Name the exact public type, operations, observable behavior, and old code
   that the new algebra will replace.
3. Inspect its callers, tests, and existing Lean kernel without editing C++.
4. State what must remain observationally equivalent and what malformed uses
   the new type must reject.

Do not begin the replacement while the target boundary is ambiguous.

## 2. Prove the replacement in Lean

Model only the irreducible semantics needed by the target:

- the carrier and its factorization;
- public operations and their domains and codomains;
- identity, composition, associativity, and failure or state laws that apply;
- normalization and reconstruction of the carrier;
- the distinction between structural recognition and operational admission;
- a counterexample when the old condition was weaker or simply wrong.

Reuse the repository's existing algebra rather than inventing a parallel
kernel. Connect the new theorem to the public proof root and run the complete
proof derivation before touching the target C++.

Reject `sorry`, `admit`, project axioms, and `unsafe` shortcuts. Audit the main
theorems with `#print axioms`; standard Lean quotient soundness is acceptable
when it arises from extensional equality.

If the proof does not close, revise the proposed algebra. Do not compensate by
weakening the C++ boundary.

## 3. Delete the superseded implementation

After the Lean proof passes, remove the exact C++ declarations and bodies that
the formal implementation replaces. Make deletion a distinct edit before
writing any replacement. Do not retain a compatibility adapter, duplicate
registry, trait path, fallback overload, or commented copy unless a real
external contract requires it.

Inspect the deletion diff. Git history is the recovery mechanism.

## 4. Reason from the exposed boundary

With the old implementation absent, inspect every resulting caller and
constraint. Determine explicitly:

- the actual source and target types, including cvref and value category;
- which properties are structural and which authorize an operation;
- construction, movement, comparison, ownership, lifetime, and `noexcept`;
- where failure belongs in the type rather than in control-flow convention;
- which facts must be visible to a requires-expression before body
  instantiation;
- which reflected declaration is the sole structural authority;
- what compile-time product, if any, must survive into runtime code.

Translate the proof deliberately:

| Lean fact | C++26 boundary |
|---|---|
| proposition over a declaration | concept or requires-clause |
| normalization/factorization | `consteval` reflection inspection |
| witness or reconstructed carrier | reflected/spliced type identity |
| impossible inhabitant | unavailable overload or failed constraint |
| algebraic composition | constrained callable composition |
| executable pure law | `constexpr` operation and compile-time witness |

Do not use a body-level `static_assert` for a property that determines whether
an overload exists. Do not substitute a property of the stored type for the
required relation from the actual source expression to that stored type.

## 5. Implement the proved type

Only now introduce the corrected C++ type and operations.

- Express the public algebra with concepts and constraints.
- Use `consteval` reflection to inspect and normalize declaration shape.
- Use `constexpr` for computation that may run at translation time or runtime.
- Splice reflected types or declarations back into direct typed C++ where
  appropriate.
- Parameterize templates only where the interface genuinely varies by type or
  value.
- Make invalid programs fail at the public constraint boundary.
- Generate direct specialized runtime code; do not add generic runtime
  reflection traversal.
- Keep one implementation of each structural fact.

Add positive compile-time witnesses and adversarial rejection cases for the
specific mistake the proof excludes. Test real function bodies as well as
requires-expressions so constraints cannot claim an implementation is valid
when its body is not.

## 6. Verify equivalence and closure

1. Re-run the full Lean proof derivation.
2. Compile the focused positive and negative C++ witnesses.
3. Run the affected public-surface and fuzz tests.
4. Build every required engine target.
5. Measure code size or performance only when the replaced boundary can affect
   either; compare against the preserved baseline.
6. Confirm the superseded code is absent and no parallel path remains.

The work is complete only when the Lean theorem holds, the C++ constraints
encode that theorem's concrete obligations, legal callers retain their
observable behavior, illegal callers fail before implementation-body
instantiation, and the emitted runtime contains only the required direct code.
