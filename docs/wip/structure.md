# Proposed structure

The tree is built up rather than rearranged: the previous sources are
frozen under `src/_old` and each module is written fresh into its
directory, replacing its frozen counterpart when it lands.

A sketch, not a plan. Synthesised from three agent passes and verified
where it names a defect. About eleven developer-days if taken whole.

## Shape

Four purposes, six directories, read in dependency order so a newcomer
starts at `recipe/` and never reads backwards.

```
src/
  main.cpp  Identity.h  Core.h  PCH.h        the process, the name, the value shapes
  Settings.{h,cpp}                           the settings record and its INI text
  SettingsFile.{h,cpp}                       the path, the disk, the process copy

  recipe/     what an effect is
      Recipe.{h,cpp}  Words.h
      RecipeRead.cpp  RecipeWrite.cpp
      Expression.{h,cpp}  Signals.{h,cpp}
      Merge.{h,cpp}  Importer.{h,cpp}

  mesh/       what an effect is applied to
      Mesh.{h,cpp}  TextureSize.h
      Islands.{h,cpp}  MaterialClusters.{h,cpp}  MeshFacts.{h,cpp}

  engine/     how an effect reaches an actor
      ActorState.h
      ActorApply.cpp  ActorTick.cpp  RecipeCommands.cpp  SnapshotBuild.cpp
      Manager.{h,cpp}
      RecipeStore  MeshReader  EngineForms  Environment  Events  Hooks

  render/     how an effect becomes pixels
      PBRMaterial.h  RuntimeTextures  Compositor  Binding

  studio/     the editor's model, engine-free
      Snapshot.h  View  Selection  Rows  Panels  Names
      Forms.h  Forms.cpp
      Edits  FieldCheck  History  Intent.h  MenuState
      Region  Presets  TermTemplates  PaintSession

  menu/       the editor's surface
      Menu  MenuWidgets  Intents  FormDraw
      BoardPage  ContextRows  StackPanel  ResourcePanels  PaintPanel  StudioPage
```

`src` stays the only include root, so every include gains its directory
and the structure is visible at each use.

## What produced it

Each module got one sentence saying what it is responsible for. Seven
needed an "and" joining unrelated clauses — `RecipeJson`, `Analysis`,
`Studio`, `MenuState`, `Paint`, `Manager`, `ComposePage` — and those seven
are exactly the seven this tree splits. That correspondence is the
evidence for the shape.

## What it buys beyond navigation

`CMakeLists.txt` names seventeen sources by hand in `HOST_OBJECT_SOURCES`
to define the engine-free boundary. Those become three globs, `recipe/`,
`mesh/` and `studio/`, so the most important fact about this codebase
stops being maintained by hand.

`tests/` mirrors the same directories, making the boundary visible from
both sides.

Two layer inversions disappear. `src/RecipeJson.cpp` currently defines
`Studio::ParsePresets`, so the recipe format's translation unit
implements the editor's preset parser. `MeshReader.h` includes
`Studio.h` for a single field, dragging the editor's view model into
`Compositor.h` and then `Manager.h`.

## Renames that resolve a wrong promise

- `Vocabulary.h` to `recipe/Words.h`: the tables are Recipe's private
  data, included by no header, not a peer module.
- `Analysis` splits into `mesh/Islands` and `mesh/MaterialClusters`,
  which share no function, no type and no test.
- `EditCheck` to `FieldCheck`: it never sees a `RecipeEdit`; it checks
  the text in a field.
- `SettingsCore` and `Settings` swap, so the plain name names the plain
  thing.

## The mask editor

Decided 2026-09-09: the paint feature stays, as an explicit mask editor.
It is primarily a UI concern, and the mesh and texture analysis behind it
must be accessible enough to drive that UI.

That resolves the `region` collision by removing the word rather than
relocating it. The format calls the thing a mask; the editor builds one
from terms; `region` is a third name for the same thing and appears
nowhere in the recipe format. `RegionStack`, `RegionPreset`,
`BuildRegion` and `presets/regions.json` take the format's word. `island`
is then free for the mesh concept with nothing to contest it.

It also makes `mesh/Islands` and `mesh/MaterialClusters` a service rather
than internal geometry utilities. Each owns the vocabulary for what it
produces — what a cluster is called, what an island is, how a texel is
described — and the editor consumes a described result instead of
re-deriving names. Today `presets/regions.json` restates eight biped
partition names that `src/Recipe.cpp:42` already holds, and
`PlainPartitionName` reads the JSON copy; that duplication goes when the
analysis owns its own labels.

This is also why the two halves of `Analysis` split despite both feeding
the offers list: they share the shape of a described result, not code.

## Order of work

`Forms.cpp` first: three hours, a pure cut of 940 lines out of
`Studio.cpp`, and it splits the repository's tightest co-change pair.
Then the small moves in `docs/wip/clusters.md`. The `Manager` and
`ComposePage` splits want the `Page` record and the `Do*` methods from
that document as prerequisites, not alternatives.
