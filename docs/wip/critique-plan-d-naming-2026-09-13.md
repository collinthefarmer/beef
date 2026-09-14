# Plan D: one name per concept — 2026-09-13

## Status

Implemented 2026-09-14 on `critique/d-naming`, branched from
`cleanup/stage-0` at `3534771`. Nine commits:

| Commit | What |
|---|---|
| `b542ca1` | `ActorState` -> `ActorPlan`, `LiveActor::structure` -> `plan` |
| `81fd23b` | `OutputIndex` -> `OutputId`, `SlotSource`/`LightSource` -> `SlotContributor`/`LightContributor` |
| `850989d` | `enum class BipedSlot`, `PartitionBake::slot` -> `bipedSlot` |
| `638c052` | `ResourceSlots` -> `TargetPool`, `SlotWriter::binding_` -> `material_`, `PbrMaterial::material_` -> `layout_` |
| `51c95fa` | `engine/ManagerShared` deleted; `engine/Clock` and `engine/LiveActor.cpp` |
| `9bbc313` | `RuntimeTextures.h` -> `TextureLab.h` and its three sources; `Status::runtimeLab` -> `textureLab` |
| `4438acd` | `Resolve.cpp`'s glob indices spelled out |
| `f6b3d3d` | the glossary in `docs/conventions.md` |
| (docs) | this Status block, the handoff row and log |

### Renames

- **`ActorState` -> `ActorPlan`.** `src/planners/ActorState.{h,cpp}` ->
  `ActorPlan.{h,cpp}`, `ActorPlanning.{h,cpp}`, `engine/LiveActor.h`,
  `tests/planners/actorstate_tests.cpp` -> `actorplan_tests.cpp`,
  `placementlookup_tests.cpp`, `actorplanning_tests.cpp`, `REFERENCE.md`,
  `docs/conventions.md`. The `a_state`/`state` parameters and locals are
  `a_plan`/`plan` (`actorPlan` in `actorplanning_tests.cpp`, where a
  `GeometryPlacementPlan` already held the name `plan`).
- **`LiveActor::structure` -> `LiveActor::plan`.** `engine/LiveActor.h`,
  `ManagerApplication.cpp`, `ManagerApply.cpp`, `ManagerSnapshot.cpp`,
  `ManagerTick.cpp` (28 references).
- **`OutputIndex` -> `OutputId`.** `planners/ActorPlan.h`,
  `ActorPlanning.cpp`, `REFERENCE.md`.
- **`SlotSource` -> `SlotContributor`, `LightSource` -> `LightContributor`.**
  `recipe/Merge.{h,cpp}`, `planners/BindingPlan.{h,cpp}`, `StackPlan.cpp`,
  `engine/ManagerSnapshot.cpp`, `tests/recipe/merge_tests.cpp`,
  `tests/planners/stackplan_tests.cpp`, `bindingplan_tests.cpp`,
  `REFERENCE.md`, `docs/conventions.md`.
- **`PartitionBake::slot` -> `PartitionBake::bipedSlot`, typed
  `BipedSlot`.** The member is a biped slot number, not a partition index
  (default 32, read by `Reader::BipedSlotFrom`, reported as "no partition
  in biped slot N"), so the plan's conditional applies. The type it was
  missing is new: `enum class BipedSlot : std::uint32_t {}` in
  `recipe/Recipe.h`, parsed once at the JSON boundary, carried by
  `BipedSlotSpec::slot`, `kFirstBipedSlot`/`kLastBipedSlot`,
  `BipedSlotFromName`, `BipedSlotName`, `BipedSlotToJson`,
  `Reader::BipedSlot`/`BipedSlotFrom`, `Studio::MaskPreset::partition` and
  `Studio::PartitionTerm::slot`. Touched `recipe/Recipe.h`, `Words.h`,
  `Vocabulary.cpp`, `Binders.{h,cpp}`, `RecipeWrite.cpp`, `mesh/Mesh.cpp`,
  `MeshFacts.cpp`, `studio/Mask.h`, `Presets.h`, `TermTemplates.cpp`,
  `Forms.cpp`, `SourceRows.cpp`, seven test suites and `REFERENCE.md`.
- **`ResourceSlots` -> `TargetPool`.** `planners/ResourceSlots.h` ->
  `planners/TargetPool.h` (`slots_` -> `leases_`), `render/RenderTargetPool.h`,
  `tests/planners/resourceslots_tests.cpp` -> `targetpool_tests.cpp`. The
  header's two comment lines became a `REFERENCE.md` entry under planners.
- **`SlotWriter::binding_` -> `material_`** and, because the writer's
  `PbrMaterial` has its own `material_`, **`PbrMaterial::material_` ->
  `layout_`** (it holds a `PBRMaterialLayout`). `render/Binding.{h,cpp}`,
  `PBRMaterial.{h,cpp}`, `CompositorSource.cpp`.
- **`engine/ManagerShared.{h,cpp}` deleted.** `NowMS` is
  `engine/Clock.{h,cpp}`; `RetireGeometry`, `RetireActorEffects`,
  `TargetFor` and `OutputAt` are declared in `engine/LiveActor.h` below the
  types and defined in a new `engine/LiveActor.cpp`. The retire-order
  comment became a `REFERENCE.md` entry under the engine heading.
- **`render/RuntimeTextures.h` -> `render/TextureLab.h`**, with
  `RuntimeTexturesLab/Pass/Readback.cpp` -> `TextureLabLifecycle/Pass/
  Readback.cpp`; `ShaderSource.cpp` stays. Fourteen includers followed.
- **`Status::runtimeLab` -> `Status::textureLab`** in both records:
  `engine/Manager.h`, `engine/ManagerSnapshot.cpp`, `studio/Snapshot.h`,
  `menu/Menu.cpp`.
- **`Resolve.cpp`'s `g`, `t`, `starG`, `starT`** -> `globAt`, `textAt`,
  `starAt`, `starMatchedTo`, one declarator per line.
- **Glossary** in `docs/conventions.md` under House rules: `Slot`,
  `BipedSlot`, `Partition`, `Contributor`, `Plan`, `Binding`, `Lease`,
  `Target`, `Region`, plus `Binders`, `RowLevel`, `ProblemText`,
  `TextFile`, `TextureIdentity`, `TextureHandle`, `ApplicationRecord`,
  `Visit.h`, `ShaderConstants`, `D3DResult`, `MeshCache` and `layers.sh`
  from Plans A, B and C. `grep -rni 'region' src/studio src/menu` finds
  only `ImGui::GetContentRegionAvail` and one armor-coverage sentence in
  `menu/PatternChooser.cpp`, so no paint-era use survives.

### Deviations

- **`SignalEnvironment::ActorState(ActorStateKind)` keeps its name.** The
  acceptance grep for `\bActorState\b` still matches it in `recipe/Signals.h`,
  `Signals.cpp`, `engine/Environment.{h,cpp}` and two test doubles. It is
  the environment query for `ActorStateKind`, beside `ActorValue`,
  `Enchantment` and `EffectShader` — the same concept the enum names, not a
  fifth meaning. The planners type the finding named is gone.
- **`Reader::BipedSlot` and `Reader::BipedSlotFrom` spell the return type
  qualified** (`std::optional<BetterEnchantmentEffects::BipedSlot>`): inside
  the class the member function name hides the namespace-scope enum.
- **`PbrMaterial::material_` -> `layout_` was not in the plan.** Renaming
  `binding_` to `material_` produced `material_.material_` at twenty sites;
  the inner member now names what it holds.
- **`mesh/Mesh.cpp`'s file-local `SlotName(std::uint32_t)` is gone** — it
  duplicated `BipedSlotName` — and `MeshFacts.cpp`'s `SlotLabel` is two
  lines over the same function.
- **`MeshPartition::slot` and `SlotCoverage::slot` stay raw.** They are the
  NIF's 16-bit dismember field with its `kNoSlot` sentinel and out-of-range
  values, not a parsed biped slot; they convert with `std::to_underlying`
  at the comparison. Recorded in `REFERENCE.md` under mesh.
- **`RenderTargetPool::presenterSlots_` keeps its name.** "Presenter slot"
  is the render layer's own word (the assets folder and the log lines), and
  the plan's constraint is on planners type names.
- **`studio/SourceRows.cpp` gained a `PartitionText` helper** so
  `SourceRowOf` stays under the tidy function-size baseline.
- **`RetireActorEffects` moved with `RetireGeometry`.** The plan's row
  listed three functions; the fourth belongs to the same type.
- `docs/wip/cleanup-checkpoint-2026-09-13.md:36` still names
  `ResourceSlots`. It is a dated checkpoint record; left for Plan E.

### UI-owned files a rename touched

Only what a rename's references forced, and nothing beyond it:

| File | Lines | Why |
|---|---|---|
| `src/studio/TermTemplates.cpp` | 18 +, 14 - | `BipedSlot` at the `PartitionTerm`/`SlotCoverage` boundary |
| `src/studio/SourceRows.cpp` | 8 +, 4 - | `BipedSlot`, plus the `PartitionText` extraction |
| `src/studio/Forms.cpp` | 4 +, 3 - | `BipedSlotNames` and the partition field's parse |
| `src/studio/Mask.h` | 1 | `PartitionTerm::slot` type |
| `src/studio/Presets.h` | 1 | `MaskPreset::partition` type |
| `src/studio/Snapshot.h` | 1 | `Status::textureLab` |
| `src/menu/Menu.cpp` | 1 | `st.textureLab` |
| `src/menu/MenuWidgets.cpp` | 1 | the `render/TextureLab.h` include |

### Deferred

Waiting on the UI complete-editor checkpoint (UI plan section 6):

- **The `MenuState.h` extraction.** `MenuState`, `FiringDraft`,
  `ResourceTab` and the `State()` singleton stay in `src/studio/Intent.h`,
  and `src/studio/MenuState.cpp` still implements no header of its own. UI
  slice 1B is extending selection and editor state in exactly those files.
  `State()` is the last singleton under `studio/`.
- **`ScratchRebuilt`.** Still a past-tense fact among imperative intents in
  `Intent.h`; UI slice 3E rewrites the Paint reducers around it, so whether
  it becomes `RebuildScratch` or moves to the snapshot is 3E's call.
- Anything else in `src/studio/Intent.h` or `src/studio/Selection.h`.

### Acceptance

| Check | Result |
|---|---|
| `tests/run-native.sh` | green, 66 suites |
| `BEEF_SANITIZE=1 tests/run-native.sh` | green, 66 suites |
| `tools/layers.sh` | "every include stays inside the graph" |
| `./build.sh Release -j 4` | links, zero warnings |
| `tools/gate.sh commit` (per commit) | format and tidy clean on every commit |
| `grep -rn '\bActorState\b' src tests` | only `SignalEnvironment::ActorState` and its overrides (see Deviations) |
| `grep -rn 'Slot' src/planners --include='*.h'` | `Slot`, `SlotContribution`, `SlotContributor`, `SlotStackPlan` — all the PBR slot from `recipe/` |
| `src/engine/ManagerShared.h` | gone |
| `src/render/TextureLab.h` | exists |
| `src/studio/MenuState.h` | not created (deferred) |
| `grep -rn 'ActorState\|OutputIndex\|SlotSource\|runtimeLab\|ManagerShared\|RuntimeTextures\.h' REQUIREMENTS.md REFERENCE.md docs/conventions.md` | prints nothing |
| tidy baseline | regenerated for the renamed files; 57 findings before and after |

`tools/gate.sh push`, `./install.sh` and the in-game checkpoint below are
batched into the single pass after the last plan (decided 2026-09-14).

## The plan

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
