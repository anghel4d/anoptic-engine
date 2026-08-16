/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Generated.ResourceSchema

namespace Anoptic.ResourceCertificate

open Generated.ResourceSchema

def artifactNames : List String := artifacts.map (·.name)
def artifactTypeIds : List Nat := artifacts.map (·.typeId)
def artifactFingerprints : List String := artifacts.map (·.fingerprint)
def importerExtensions : List String := importers.map (·.extension)
def importerProducerIds : List Nat := importers.map (·.producerId)

def lowerHexDigit (character : Char) : Bool :=
  let code := character.toNat
  (0x30 ≤ code && code ≤ 0x39) || (0x61 ≤ code && code ≤ 0x66)

def fingerprintWellFormed (fingerprint : String) : Bool :=
  fingerprint.length == 64 && fingerprint.toList.all lowerHexDigit

def dependencyTargets : List String :=
  artifacts.flatMap (·.dependencies)

def transformEndpoints : List String :=
  transforms.flatMap fun transform => transform.inputs ++ transform.outputs

def namesKnown (names : List String) : Bool :=
  names.all fun name => artifactNames.contains name

def directDependencies (source : String) : List String :=
  match artifacts.find? fun artifact => artifact.name == source with
  | some artifact => artifact.dependencies
  | none => []

def reachableWithin : Nat → String → String → Bool
  | 0, _, _ => false
  | fuel + 1, source, target =>
      (directDependencies source).any fun next =>
        next == target || reachableWithin fuel next target

def dependencyGraphAcyclic : Bool :=
  artifactNames.all fun name =>
    !(reachableWithin artifactNames.length name name)

theorem artifact_names_unique : artifactNames.Nodup := by decide
theorem artifact_type_ids_nonzero : artifactTypeIds.all (· != 0) = true := by decide
theorem artifact_type_ids_unique : artifactTypeIds.Nodup := by decide
theorem artifact_fingerprints_well_formed :
    artifactFingerprints.all fingerprintWellFormed = true := by
  decide
theorem artifact_fingerprints_unique : artifactFingerprints.Nodup := by decide
theorem dependency_targets_exist : namesKnown dependencyTargets = true := by decide
theorem transform_endpoints_exist : namesKnown transformEndpoints = true := by decide
theorem transform_ports_nonempty :
    transforms.all (fun transform =>
      !transform.inputs.isEmpty && !transform.outputs.isEmpty) = true := by
  decide
theorem dependency_graph_acyclic : dependencyGraphAcyclic = true := by decide
theorem importer_extensions_unique : importerExtensions.Nodup := by decide
theorem importer_producer_ids_nonzero :
    importerProducerIds.all (· != 0) = true := by decide
theorem importer_producer_ids_unique : importerProducerIds.Nodup := by decide

end Anoptic.ResourceCertificate
