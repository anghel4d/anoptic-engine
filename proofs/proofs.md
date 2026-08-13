# Proofs

`proofs/` is the machine-checked semantic boundary for Anoptic Engine. Lean declarations state the abstract interfaces and their laws; C++26 reflection supplies normalized structural witnesses for the implementation bridge. The engine links no Lean runtime.

- `Polynomial`, `Compiler`, `Refinement`, and `Transaction` define the common
  API, compilation, verification, and publication algebras.
- `Outcome` through `Diagnostics` prove the engine foundation interfaces.
- `Gltf` through `Engine` define and prove every domain interface in the
  24-row whole-engine factorization.
- `Coverage.allInterfacesChecked` is the exhaustive theorem tying those 24
  interface constructors to their central laws.
- `Composition` proves the distinct pure, fallible, stateful, transactional,
  product, and coproduct composition algebras.
- `CompositionCoverage.allCompositionsChecked` proves composition closure for
  all 24 ideal interfaces.
- `EngineComposition` proves the complete fallible
  cook/prepare/realize/interpret dataflow, joins, forks, generation coherence,
  cook/pack/open identity, and reload publication isolation.
- `ArchitectureGraph` enumerates every declared cross-module dependency edge
  and proves interpretation of all well-typed paths is associative.
- `Cxx26` proves the staged target language: reflection, `consteval`,
  `constexpr`, type-only templates, expansion, splicing, and runtime erasure.
- `Cxx26Mapping` proves composable target images and lowering preservation for
  all eight algebras, all 24 interfaces, all 31 declared architecture edges,
  and the complete engine composition root.
- `Integration` composes representative resource, render, world, hot-reload,
  music, and synth laws across module boundaries.
- [interfaces.md](interfaces.md) is the exact interface-to-theorem coverage
  matrix.
- `Anoptic.lean` imports the complete checked kernel.

The interactive environment is the repository dev shell. `cd proofs && lake
build` checks the kernel there. `nix build .#proofs` checks the kernel, compiles
the GCC 16 C++26 bridge with reflection and `-nostdlib++`, and executes it in an
isolated Nix build. `nix flake check` includes the same proof target with the
engine checks.
