# Proofs

`proofs/` is the machine-checked semantic boundary for Anoptic Engine. Lean declarations state the abstract interfaces and their laws; C++26 reflection supplies normalized structural witnesses for the implementation bridge. The engine links no Lean runtime.

- `Polynomial`, `Compiler`, `Refinement`, and `Transaction` define the common
  API, compilation, verification, and publication algebras.
- `Outcome` through `Diagnostics` prove the engine foundation interfaces.
- `Gltf` through `Engine` define and prove every domain interface in the
  24-row whole-engine factorization.
- `Coverage.allInterfacesChecked` is the exhaustive theorem tying those 24
  interface constructors to their central laws.
- `Integration` composes representative resource, render, world, hot-reload,
  music, and synth laws across module boundaries.
- [interfaces.md](interfaces.md) is the exact interface-to-theorem coverage
  matrix.
- `Anoptic.lean` imports the complete checked kernel.

The interactive environment is the repository dev shell. `cd proofs && lake build` checks the kernel there. `nix build .#proofs` checks the same target in an isolated Nix build, and `nix flake check` includes it with the engine checks.
