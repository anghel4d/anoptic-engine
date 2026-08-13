/-
SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0
Anoptic targets ISO C++26.
-/

import Anoptic.Composition

namespace Anoptic

namespace ArchitectureGraph

/-- Semantic cells appearing on the whole-engine dataflow diagram. -/
inductive Cell where
  | source
  | rawGltf
  | boundGltf
  | canonicalMesh
  | revision
  | pack
  | demand
  | epoch
  | renderSlot
  | audioSlot
  | textSlot
  | inputEvent
  | timeStep
  | world
  | renderCommand
  | audioCommand
  | uiScene
  | musicControl
  | musicBar
  | synthBus
  | frame
  | audioBlock
  | save
  deriving DecidableEq

/-- Each constructor is one declared dependency edge; joins have one edge per input. -/
inductive Step : Cell → Cell → Type where
  | acquireGltf : Step .source .rawGltf
  | parseBindGltf : Step .rawGltf .boundGltf
  | projectMesh : Step .boundGltf .canonicalMesh
  | cookMesh : Step .canonicalMesh .revision
  | cookOtherSource : Step .source .revision
  | packRevision : Step .revision .pack
  | openPack : Step .pack .revision
  | inputWorld : Step .inputEvent .world
  | timeWorld : Step .timeStep .world
  | revisionWorld : Step .revision .world
  | worldDemand : Step .world .demand
  | worldRender : Step .world .renderCommand
  | worldAudio : Step .world .audioCommand
  | worldUi : Step .world .uiScene
  | worldMusic : Step .world .musicControl
  | worldSave : Step .world .save
  | loadSave : Step .save .world
  | revisionResidency : Step .revision .epoch
  | demandResidency : Step .demand .epoch
  | realizeRender : Step .epoch .renderSlot
  | realizeAudio : Step .epoch .audioSlot
  | realizeText : Step .epoch .textSlot
  | renderSlotFrame : Step .renderSlot .frame
  | textSlotFrame : Step .textSlot .frame
  | renderCommandFrame : Step .renderCommand .frame
  | uiFrame : Step .uiScene .frame
  | audioSlotBlock : Step .audioSlot .audioBlock
  | audioCommandBlock : Step .audioCommand .audioBlock
  | musicStep : Step .musicControl .musicBar
  | synthStep : Step .musicBar .synthBus
  | synthBlock : Step .synthBus .audioBlock

inductive EdgeName where
  | acquireGltf
  | parseBindGltf
  | projectMesh
  | cookMesh
  | cookOtherSource
  | packRevision
  | openPack
  | inputWorld
  | timeWorld
  | revisionWorld
  | worldDemand
  | worldRender
  | worldAudio
  | worldUi
  | worldMusic
  | worldSave
  | loadSave
  | revisionResidency
  | demandResidency
  | realizeRender
  | realizeAudio
  | realizeText
  | renderSlotFrame
  | textSlotFrame
  | renderCommandFrame
  | uiFrame
  | audioSlotBlock
  | audioCommandBlock
  | musicStep
  | synthStep
  | synthBlock
  deriving DecidableEq

structure SomeStep where
  source : Cell
  target : Cell
  value : Step source target

/-- Exhaustive names make omission of a declared architecture edge visible. -/
def namedStep : EdgeName → SomeStep
  | .acquireGltf => ⟨.source, .rawGltf, .acquireGltf⟩
  | .parseBindGltf => ⟨.rawGltf, .boundGltf, .parseBindGltf⟩
  | .projectMesh => ⟨.boundGltf, .canonicalMesh, .projectMesh⟩
  | .cookMesh => ⟨.canonicalMesh, .revision, .cookMesh⟩
  | .cookOtherSource => ⟨.source, .revision, .cookOtherSource⟩
  | .packRevision => ⟨.revision, .pack, .packRevision⟩
  | .openPack => ⟨.pack, .revision, .openPack⟩
  | .inputWorld => ⟨.inputEvent, .world, .inputWorld⟩
  | .timeWorld => ⟨.timeStep, .world, .timeWorld⟩
  | .revisionWorld => ⟨.revision, .world, .revisionWorld⟩
  | .worldDemand => ⟨.world, .demand, .worldDemand⟩
  | .worldRender => ⟨.world, .renderCommand, .worldRender⟩
  | .worldAudio => ⟨.world, .audioCommand, .worldAudio⟩
  | .worldUi => ⟨.world, .uiScene, .worldUi⟩
  | .worldMusic => ⟨.world, .musicControl, .worldMusic⟩
  | .worldSave => ⟨.world, .save, .worldSave⟩
  | .loadSave => ⟨.save, .world, .loadSave⟩
  | .revisionResidency => ⟨.revision, .epoch, .revisionResidency⟩
  | .demandResidency => ⟨.demand, .epoch, .demandResidency⟩
  | .realizeRender => ⟨.epoch, .renderSlot, .realizeRender⟩
  | .realizeAudio => ⟨.epoch, .audioSlot, .realizeAudio⟩
  | .realizeText => ⟨.epoch, .textSlot, .realizeText⟩
  | .renderSlotFrame => ⟨.renderSlot, .frame, .renderSlotFrame⟩
  | .textSlotFrame => ⟨.textSlot, .frame, .textSlotFrame⟩
  | .renderCommandFrame => ⟨.renderCommand, .frame, .renderCommandFrame⟩
  | .uiFrame => ⟨.uiScene, .frame, .uiFrame⟩
  | .audioSlotBlock => ⟨.audioSlot, .audioBlock, .audioSlotBlock⟩
  | .audioCommandBlock => ⟨.audioCommand, .audioBlock, .audioCommandBlock⟩
  | .musicStep => ⟨.musicControl, .musicBar, .musicStep⟩
  | .synthStep => ⟨.musicBar, .synthBus, .synthStep⟩
  | .synthBlock => ⟨.synthBus, .audioBlock, .synthBlock⟩

theorem every_named_edge_is_typed (name : EdgeName) :
    Nonempty (Step (namedStep name).source (namedStep name).target) :=
  ⟨(namedStep name).value⟩

/-- A path is a typed composition of primitive dependency interpretations. -/
inductive Path : Cell → Cell → Type where
  | identity (cell : Cell) : Path cell cell
  | single (step : Step source target) : Path source target
  | compose : Path source middle → Path middle target → Path source target

def Interpretation (Value : Cell → Type) :=
  {fromCell toCell : Cell} → Step fromCell toCell →
    Composition.Pure (Value fromCell) (Value toCell)

def Path.run (interpret : Interpretation Value) :
    Path source target → Composition.Pure (Value source) (Value target)
  | .identity cell => Composition.Pure.identity (Value cell)
  | .single step => interpret step
  | .compose first second =>
      Composition.Pure.compose (first.run interpret) (second.run interpret)

@[simp] theorem Path.run_identity (interpret : Interpretation Value) :
    (Path.identity cell).run interpret = Composition.Pure.identity (Value cell) :=
  rfl

theorem Path.run_compose (interpret : Interpretation Value)
    (first : Path source middle) (second : Path middle target) :
    (Path.compose first second).run interpret =
      Composition.Pure.compose (first.run interpret) (second.run interpret) :=
  rfl

@[simp] theorem Path.run_identity_left (interpret : Interpretation Value)
    (path : Path source target) :
    (Path.compose (.identity source) path).run interpret = path.run interpret :=
  Composition.Pure.identity_compose _

@[simp] theorem Path.run_identity_right (interpret : Interpretation Value)
    (path : Path source target) :
    (Path.compose path (.identity target)).run interpret = path.run interpret :=
  Composition.Pure.compose_identity _

/-- Every regrouping of every well-typed architecture path has one meaning. -/
theorem Path.run_assoc (interpret : Interpretation Value)
    (first : Path A B) (second : Path B C) (third : Path C D) :
    (Path.compose (Path.compose first second) third).run interpret =
      (Path.compose first (Path.compose second third)).run interpret :=
  Composition.Pure.compose_assoc _ _ _

def sourceToFrame : Path .source .frame :=
  .compose (.single .acquireGltf)
    (.compose (.single .parseBindGltf)
      (.compose (.single .projectMesh)
        (.compose (.single .cookMesh)
          (.compose (.single .revisionResidency)
            (.compose (.single .realizeRender) (.single .renderSlotFrame))))))

def packedRevisionToFrame : Path .pack .frame :=
  .compose (.single .openPack)
    (.compose (.single .revisionResidency)
      (.compose (.single .realizeRender) (.single .renderSlotFrame)))

def inputToFrame : Path .inputEvent .frame :=
  .compose (.single .inputWorld)
    (.compose (.single .worldRender) (.single .renderCommandFrame))

def worldMusicToAudio : Path .world .audioBlock :=
  .compose (.single .worldMusic)
    (.compose (.single .musicStep)
      (.compose (.single .synthStep) (.single .synthBlock)))

def saveRoundTripPath : Path .world .world :=
  .compose (.single .worldSave) (.single .loadSave)

end ArchitectureGraph
end Anoptic
