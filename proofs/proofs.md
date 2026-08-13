# Proofs

`proofs/` contains Anoptic's Lean 4 abstract semantic kernel and the mechanically
connected certificate for the reflected resource universe. The engine links no
Lean runtime.

- `Polynomial`, `Compiler`, `Composition`, `Refinement`, and `Transaction`
  define the reusable abstract algebras.
- The domain files define selected reference carriers and prove their local
  laws. They do not by themselves certify corresponding C++ implementations.
- `Coverage` and `CompositionCoverage` are exhaustive over a handwritten
  24-constructor abstract factorization only.
- `ArchitectureGraph` and `EngineComposition` prove typed composition inside
  their abstract models.
- `Cxx26` models the staged language of reflection, `consteval`, `constexpr`,
  type-only templates, expansion, splicing, and direct execution.
- `Cxx26Mapping` proves candidate images and preservation inside that model; it
  is not a production-header inventory.
- `cpp/resource_schema_certificate.cpp` includes the real resource headers,
  reflects `ano::asset_schema`, and emits
  `Anoptic/Generated/ResourceSchema.lean`.
- `ResourceCertificate` proves uniqueness, endpoint closure, and acyclicity over
  that generated production inventory.
- [interfaces.md](interfaces.md) is the exact abstract-versus-production matrix.
- [PROOF_STATUS.md](../docs/resourcemanager/PROOF_STATUS.md) states the trust
  boundary and remaining obligations.

`cd proofs && lake build` checks the Lean sources already present in the working
tree. `nix build .#proofs` additionally compiles the real-header C++26 generator
with GCC reflection and `-nostdlib++`, rejects a C++ runtime dependency,
regenerates the resource certificate, compares it byte-for-byte with the checked
Lean file, and only then builds the Lean library.
