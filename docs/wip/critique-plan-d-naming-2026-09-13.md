# Plan D: one name per concept — 2026-09-13

Status: not started.

Covers critique recommendation 6. Runs after Plan C so files are in their
final places before symbols are renamed. Every rename goes through
`tools/rename.py Old New --apply` inside `nix develop`, after
`tools/compile-db.sh`. The tool leaves docs and strings alone and prints what
it skipped; every rename below ends with a hand pass over `REQUIREMENTS.md`,
`REFERENCE.md`, `docs/conventions.md`, the schema descriptions and the tests'
assertion labels.

## UI rework impact

- **Safe now:** every rename in `planners/`, `recipe/`, `render/` and
  `engine/`: `ActorPlan`, `OutputId`, the `Merge.h` contributors,
  `PartitionBake`, `BipedSlot`, `TargetPool`, `material_`, `ManagerShared`,
  `TextureLab.h`, `Resolve.cpp` names, and the glossary. `tools/rename.py`
  edits references in `menu/` and `studio/` where they occur, and those
  edits are mechanical; commit them separately so the UI branch rebases
  cleanly.
- **Coordinate:** `Status::runtimeLab` is one member the menu reads. Rename
  it in the same commit as `TextureLab.h` and tell the UI owner.
- **Defer:** the `MenuState.h` extraction, `ScratchRebuilt`, and anything
  else in `src/studio/Intent.h`, `Selection.h` or `MenuState.cpp`. UI slice
  1B is extending selection and editor state in exactly those files (its
  first wave added `Navigation.h`, `PendingIndexedEdit` and two resolver
  functions to `Intent.h`), and 3E rewrites the Paint reducers around
  `ScratchRebuilt`. Revisit after the complete-editor checkpoint.
- **Glossary:** the UI proposal (section 4.7, "Armor regions and
  contribution inspection") reintroduces the word "region" for a named area
  of armor coverage used by overlays and legends. The 2026-09-09 decision
  retired "region" as the old paint vocabulary. Define `Region` in the
  glossary with the UI proposal's meaning only, and make sure no surviving
  paint-era use of the word remains under `src/studio/` or `src/menu/`
  (`grep -rni 'region' src/studio src/menu`). That keeps one meaning when
  slice 4C lands.

## Findings addressed

- `Slot` has four meanings: the PBR texture slot (`src/recipe/Recipe.h:488`,
  `kSlots`), the biped equipment slot (`Recipe.h:456`), a partition index
  (`PartitionBake::slot`, `Recipe.h:388`), and a render-target pool index
  (`src/planners/ResourceSlots.h`).
- `Source` is a texture producer in `Recipe.h:450` and a placed-recipe index
  in `src/recipe/Merge.h:20-21` (`SlotSource`, `LightSource`).
- `Binding` is the attached object (`MaterialBinding`), the decision
  (`src/planners/BindingPlan.h:11`), and a member (`PbrMaterial binding_`,
  `src/render/Binding.h:95`).
- `ActorState` (`src/planners/ActorState.h:47`) holds geometries, instances
  and placements, which is a plan, and collides with `ActorStateKind` and
  `ActorStateSignal` (`Recipe.h:180-189`), which mean in-combat or sneaking.
  `src/engine/LiveActor.h:76` names the member `structure`. 61 references in
  13 files.
- `OutputIndex` (`ActorState.h:16`) breaks the `*Id` suffix of its four
  siblings.
- `src/engine/ManagerShared.h:9` is a bucket holding `NowMS`,
  `RetireGeometry`, `TargetFor`, `OutputAt`.
- `TextureLab` (`src/render/RuntimeTextures.h:31`) lives in a file that
  does not name it; its three `.cpp` files are `RuntimeTexturesLab`, `Pass`,
  `Readback`; it leaks as `Status::runtimeLab` (`src/studio/Snapshot.h:234`).
- `src/studio/Intent.h:49` defines `MenuState`, `FiringDraft`, `ResourceTab`
  and a `State()` singleton; `src/studio/MenuState.cpp` has no header and
  defines nothing named `MenuState`. `ScratchRebuilt` (`Intent.h:107`) is a
  past-tense fact among imperative intents.
- `src/recipe/Words.h:142`: the signal word is `"av"` beside `"actorState"`.
  Decided: keep. `av` is the modding community's word for an actor value and
  the wire word is part of the format contract. Plan E fixes
  `REQUIREMENTS.md` to say `av`.
- `src/recipe/Resolve.cpp:14`: a multi-declarator line with the names `g`,
  `t`, `starG`, `starT`.
- `Inputs` as a suffix at four layers, and root-namespace flatness: decided,
  leave both.

## Renames

Do them in this order; each is one commit through the gate.

| Old | New | Notes |
|---|---|---|
| `ActorState` (planners) | `ActorPlan` | The type, `ActorState.h`/`.cpp`, `tests/planners/actorstate_tests.cpp`. `ActorStateKind` and `ActorStateSignal` keep their names. |
| `LiveActor::structure` | `LiveActor::plan` | Hand rename; it is a member. |
| `OutputIndex` | `OutputId` | Matches `GeometryId`, `InstanceId`, `PlacementId`, `RecipeId`. |
| `SlotSource`, `LightSource` (Merge.h) | `SlotContributor`, `LightContributor` | They index the placed recipe that won the slot or light. |
| `PartitionBake::slot` | `PartitionBake::partition` | Read `Recipe.h:388` first; if the member is a biped slot number rather than a partition index, use `bipedSlot`. |
| The biped slot type at `Recipe.h:456` | `BipedSlot` | Read the declaration first. `BipedSlotFromName` already exists in `Presets.cpp`; align with it. If it is a bare `std::uint32_t`, introduce `enum class BipedSlot : std::uint32_t {}` and parse into it at the boundary. |
| `ResourceSlots` (planners) | `TargetPool` | Read `ResourceSlots.h:9-10` for its invariant sentence and choose between `TargetPool` and `TargetSlots`; the word `Slot` must not survive in a planners type name. Rename the file and its test. |
| `PbrMaterial binding_` | `material_` | Hand rename in `Binding.h`/`.cpp`. |
| `ManagerShared.h` | delete | `NowMS` to `src/engine/Clock.h`; `RetireGeometry`, `TargetFor`, `OutputAt` to `src/engine/LiveActor.h` below the type, types first then functions. |
| `RuntimeTextures.h` | `TextureLab.h` | `git mv`; the class keeps its name. The three `.cpp` become `TextureLabLifecycle.cpp`, `TextureLabPass.cpp`, `TextureLabReadback.cpp`. `ShaderSource.cpp` stays. |
| `Status::runtimeLab` | `Status::textureLab` | Snapshot member. |
| `MenuState`, `FiringDraft`, `ResourceTab`, `State()` | move to `src/studio/MenuState.h` | `Intent.h` keeps only intents. `MenuState.cpp` implements its own header. The `State()` singleton stays for now; note in the Status block that it is the last singleton in studio. |
| `ScratchRebuilt` | read it first | If it is a command the menu issues, rename to `RebuildScratch`. If it is a notification from the engine, it is not an intent: move it to the snapshot as a fact. |
| `Resolve.cpp:14` names | spell them out | `geometry`, `texture`, and whatever the starred forms are once read. |

Do not rename `BindingPlan` or `MaterialBinding`; the suffixes `Plan` and the
object noun already distinguish them.

## Steps

1. `tools/compile-db.sh`, then `tools/rename.py ActorState ActorPlan` without
   `--apply` to check that clangd resolves the planners symbol and not
   `ActorStateKind`. Pass `--kind Struct` or the qualified name if it lists
   more than one. Then `--apply`.
2. Repeat for each row. File renames go through `git mv`; the tool handles a
   file rename only when clangd asks for one.
3. After each rename: build the native suite, run `tools/format.sh`, then
   hand-pass docs and test labels (`grep -rn 'OldName' --include='*.md'
   --include='*.cpp' --include='*.json' . | grep -v _old`).
4. `./build.sh Release -j 4` once at the end of the plan; the renames in
   `engine/` and `render/` are not covered by the native suite.
5. Add to `docs/conventions.md` under House rules a short glossary: one line
   each for `Slot` (PBR texture slot only), `BipedSlot`, `Partition`,
   `Contributor`, `Plan`, `Binding`, `Lease`, `Target`. New code must use
   these words with these meanings.

## Acceptance

- `grep -rn '\bActorState\b' src tests --include='*.cpp' --include='*.h' |
  grep -v _old` prints nothing.
- `grep -rn 'Slot' src/planners --include='*.h'` prints only uses of the
  PBR slot type from `recipe/`.
- `src/engine/ManagerShared.h` does not exist. `src/render/TextureLab.h`
  exists. `src/studio/MenuState.h` exists and `Intent.h` declares no state.
- `grep -rn 'ActorState\|OutputIndex\|SlotSource\|runtimeLab\|ManagerShared\|
  RuntimeTextures\.h' REQUIREMENTS.md REFERENCE.md docs/conventions.md`
  prints nothing.
- Release build zero-warning; native and sanitized suites green; gate push
  green.

## In-game checkpoint

Renames only, but they cross the engine and render layers and the native
suite does not compile those. After the release build and install, ask the
user for one load-and-equip cycle and confirm the log shows the same recipe
load, placement and render lines as the previous checkpoint. No new lines
are expected.
