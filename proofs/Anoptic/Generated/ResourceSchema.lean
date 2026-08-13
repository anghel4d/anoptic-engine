/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

namespace Anoptic.Generated.ResourceSchema

structure Artifact where
  name : String
  typeId : Nat
  fingerprint : String
  fixedSize : Nat
  fieldCount : Nat
  dependencies : List String
  deriving DecidableEq, Repr

structure Transform where
  name : String
  inputs : List String
  outputs : List String
  executor : String
  streaming : String
  deterministic : Bool
  deriving DecidableEq, Repr

structure Importer where
  name : String
  extension : String
  producerId : Nat
  deterministic : Bool
  deriving DecidableEq, Repr

def artifacts : List Artifact := [
  { name := "Manifest", typeId := 16373365112598258678, fingerprint := "14c707a6cdd17c3d59d00beaa2b1a6809b3bbd41779eeb568c15c2ad16f37ed0", fixedSize := 32, fieldCount := 2, dependencies := [] },
  { name := "Texture", typeId := 5386929302838048813, fingerprint := "0d2727c412e650f6a8e91ff681eea29392d62b1bc562f33318d369db740d5adb", fixedSize := 30, fieldCount := 6, dependencies := [] },
  { name := "Material", typeId := 6833136620976133851, fingerprint := "bf76e0c0ad6cc3be4c222cc27cfe35922145a4572cdda795aef63cb51ab6ff05", fixedSize := 1037, fieldCount := 31, dependencies := ["Texture"] },
  { name := "Mesh", typeId := 2896650656725398085, fingerprint := "0d82b4536de2452f43315648af7d48a1c6fa78824992933f9110207d209c1a5b", fixedSize := 64, fieldCount := 5, dependencies := ["Material"] },
  { name := "Scene", typeId := 5303434945547719155, fingerprint := "c70de09994e9a4694532f298b4c347374ae8ae6c6715b665a757a5c39d1fb1ef", fixedSize := 32, fieldCount := 2, dependencies := ["Mesh"] },
  { name := "GpuTexture", typeId := 3834007890459475266, fingerprint := "b3c32a5af49d795ee6086409a531d484ddf219a9ec7e9b02f66bbeebfb600707", fixedSize := 12, fieldCount := 3, dependencies := [] },
  { name := "GpuMaterial", typeId := 1787163105137246381, fingerprint := "630d01b8e41d561eb74acb20c400d63e8e579e466b46f937ed9716be642e1f1d", fixedSize := 4, fieldCount := 1, dependencies := [] },
  { name := "GpuMesh", typeId := 5898838479544885858, fingerprint := "27f89edf85aa755db7781b5990afd1a5eb587d9e4d84080e3e6f28f0ed30043b", fixedSize := 8, fieldCount := 2, dependencies := [] },
  { name := "GpuScene", typeId := 11733274144698161670, fingerprint := "e425cfcb2675fbbaad9eaab1ef2844cb6f8d43f86a6dc2debdc14f8932d3a006", fixedSize := 4, fieldCount := 1, dependencies := [] }
]

def transforms : List Transform := [
  { name := "realize_texture", inputs := ["Texture"], outputs := ["GpuTexture"], executor := "render_master", streaming := "whole", deterministic := true },
  { name := "realize_material", inputs := ["Material"], outputs := ["GpuMaterial"], executor := "render_master", streaming := "whole", deterministic := true },
  { name := "realize_mesh", inputs := ["Mesh"], outputs := ["GpuMesh"], executor := "render_master", streaming := "whole", deterministic := true },
  { name := "realize_scene", inputs := ["Scene"], outputs := ["GpuScene"], executor := "render_master", streaming := "whole", deterministic := true }
]

def importers : List Importer := [
  { name := "import_gltf", extension := ".gltf", producerId := 14553357784716813479, deterministic := true },
  { name := "import_gltf", extension := ".glb", producerId := 6451562606752839828, deterministic := true }
]

theorem reflectedArtifactCount : artifacts.length = 9 := rfl
theorem reflectedTransformCount : transforms.length = 4 := rfl
theorem reflectedImporterCount : importers.length = 2 := rfl

end Anoptic.Generated.ResourceSchema
