# Proofs

`proofs/` is the machine-checked semantic boundary for Anoptic Engine. Lean declarations state the abstract interfaces and their laws; C++26 reflection supplies normalized structural witnesses for the implementation bridge. The engine links no Lean runtime.

- `Anoptic/Polynomial.lean` defines APIs as polynomial containers, their covariant extensions, contravariant response translation, choice, tensor, zero, and unit.
- `Anoptic/Refinement.lean` defines trace refinement, module-local platform interpretations, relational composition, and compositional refinement.
- `Anoptic/Transaction.lean` defines state-preserving transactions and proves that a failed cooker attempt may retain working state but cannot publish a failed revision.
- `Anoptic.lean` imports the complete checked kernel.

The interactive environment is the repository dev shell. `cd proofs && lake build` checks the kernel there. `nix build .#proofs` checks the same target in an isolated Nix build, and `nix flake check` includes it with the engine checks.
