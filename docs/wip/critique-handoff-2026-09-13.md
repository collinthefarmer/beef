# Critique remediation handoff — 2026-09-13

This document hands the remediation of the 2026-09-13 codebase critique to an
administering agent. It states where the repository stands, how work is done
here, which plans exist, in what order they run, what each must prove before it
is done, and which decisions belong to the user. Read it in full before opening
any plan.

## What the critique found

Ten rubric reviews rated the codebase on readability for an outside reader and
on best practice for an extensible codebase. Both scored 6.5/10. Full findings
with file and line citations live in the six plan documents listed below.

| Rubric | Rating | Key issue |
|---|---|---|
| Architecture | 7/10 | Three upward includes; `Manager` is a god class on a singleton |
| Naming | 7/10 | `Slot`, `Source`, `Binding`, `ActorState` each name more than one thing |
| File organisation | 6/10 | `studio/Edits.cpp` holds four concerns; GPU constant structs copied into three files |
| Function design | 7/10 | Engine functions read a settings global and three singletons ambiently |
| Type leverage | 7/10 | The studio snapshot re-stringifies typed data; checked and unchecked recipes share a type |
| Onboarding and docs | 5/10 | No README; `REFERENCE.md` and `docs/wip/deletions.md` cite files that do not exist |
| Consistency | 5/10 | Five error-reporting idioms; a second JSON parser grew in `studio/Presets.cpp` |
| Tests | 6/10 | Failure output has no file, line or values; render, menu and most of engine untested |
| Error handling | 8/10 | Boundary is exemplary; four unchecked index sites in otherwise checked files |
| Extensibility | 6/10 | A new source kind touches about thirteen files, half enforced only by discipline |

The strengths to preserve: the native object library that makes module purity
a compile error (`CMakeLists.txt:80-93`), `Match` in `src/Core.h:143` that
makes variant dispatch exhaustive, the `Reader` and `Reporter` boundary in
`src/recipe/RecipeRead.cpp`, and the `Named` word tables with count asserts in
`src/recipe/Words.h`. Every plan extends those patterns to the places that
lack them. No plan replaces them.

## Repository state at handoff

- Branch `cleanup/stage-0`, last commit `a983787 cleanup`.
- The working tree holds 138 modified, deleted or untracked paths, including
  source edits across engine, render, studio and menu, two deleted planner
  files, and untracked regression evidence under `docs/regression-evidence/`.
- The user reported an in-game smoke test passing for the render cleanup
  described in `docs/wip/render-cleanup-2026-09-13.md`. That report predates
  some of the uncommitted edits.

Part of that uncommitted work is the UI v2 rework's first wave (see the next
section). It belongs to other agents. Do not stash, reset or reformat it.

First action (decided by the user 2026-09-13): commit the tree in four
pieces, in this order, each through `tools/gate.sh commit`, on
`cleanup/stage-0`:

1. The cleanup source edits: every modified path under `src/` that is not
   in piece 2, the two deleted `src/planners/BindingDiff.*` files and their
   test, `tests/`, `tools/`, `CMakeLists.txt`, and the `docs/wip/*-cleanup-*`,
   `*-checkpoint-*`, `render-state-*`, `expression-cleanup-*` notes.
2. The UI v2 first wave: `src/menu/Workspace.*`, `src/menu/RecipeActions.*`,
   `src/studio/EditResult.*`, `src/studio/FileOperation.h`,
   `src/studio/Navigation.h`, and the modified files under `src/menu/` and
   `src/studio/` listed in `docs/wip/ui-v2-framework-checkpoint-2026-09-13.md`,
   plus that checkpoint, `docs/ui-*.md` and `docs/wip/ui-primitives-ownership.md`.
   Use `git diff` per file to confirm a studio or menu hunk is UI work and
   not cleanup; the checkpoint names what the wave touched.
3. Regression evidence: `docs/regression-evidence/`, and
   `docs/regression-feedback-*`.
4. The critique documents: `docs/wip/critique-*.md`.

Leave `docs/.obsidian/` uncommitted and add it to `.gitignore` in piece 4.
Hand the user the `git push` command after the four commits. Each plan then
starts from that clean tree on its own branch named
`critique/<plan-letter>-<slug>`.

## Interaction with the UI v2 rework

`docs/ui-v2-implementation-plan.md` is in status "implementing" as of
2026-09-13. Its first wave added `src/menu/Workspace.*`,
`src/menu/RecipeActions.*`, `src/studio/EditResult.*`,
`src/studio/FileOperation.h`, `src/studio/Navigation.h`, and modified
`Forms`, `Fields`, `FieldCheck`, `Intent.h`, `MenuState.cpp`, `Selection.h`,
`Snapshot.h` and every menu page. Its early framework checkpoint
(`docs/wip/ui-v2-framework-checkpoint-2026-09-13.md`) awaits the user's
in-game run. Its later slices own, by name, the same seams several critique
steps edit: `Forms`/`Fields`/`FieldCheck` (slice 2A), the reference
traversal in `Edits.cpp` (2B), `Selection`/`Intent`/`MenuState` (1B),
`FormDraw`/`MenuWidgets` value widgets (2D), the expression parser (3C), the
Paint reducers (3E), and presets for the pattern chooser (3F). A separate
expression cleanup (`docs/wip/expression-cleanup-implementation-handoff-2026-09-13.md`)
is also editing `src/recipe/Expression.cpp`.

The critique was taken on the tree with the UI wave present, so a few
findings describe UI work in progress rather than settled code:
`FileOperationResult::error` and `RecipeEditResult::error` are bare strings
because the UI wave defined them last week, and `Snapshot.h` gained its
`EditResult.h` and `FileOperation.h` includes from the same wave.

The rule for this effort: **a critique step does not edit a file that a UI
slice names as its seam until that slice has passed its checkpoint, unless
the UI owner agrees in writing in this file.** The UI plan's own working
rules say "Do not fold unrelated cleanup into UI work"; the reverse holds
too. Each plan file has a "UI rework impact" section classifying its steps
as **safe now**, **coordinate** (do it first, so the UI slice builds on the
result, or hand it to the UI agent), or **defer** (until the complete-editor
checkpoint in the UI plan's section 6).

Summary of the classification:

| Plan | Safe now | Coordinate | Defer |
|---|---|---|---|
| F | F1 harness, F1 planner scenarios, F2 items 4 to 8 | F2 items 1 to 3 go to the expression cleanup owner | none |
| A | A1 publish binders, A3 presets parser, A5 conventions | A2 result routing uses the UI wave's `FileOperationResult` and `RecipeEditResult` as they are | A4 `FieldCheck` return type; unifying the two result records' `error` fields to `Diagnostic` |
| B | B1 tables and asserts, B1 schema test, B2 catch-alls in `recipe/`, B4 `ApplicationRecord` move, B4 purity gate | B2 catch-alls in `Edits.cpp` (before or with UI 2B); B4 `TextureHandle` (before preview pinning is wired) | B3 `SourceRow` restructure, `LayerRow::blend`, `RecipeRow::key` |
| C | C1 `MeshReader`, C2 layers script and `run-native.sh`, C4 render headers, C5 `Recipe.cpp` split | C3 traversal to `recipe/Visit.h` (before UI 2B, so 2B extends the published header); C3 `RenameInExpression` move with the expression cleanup owner | C3 `EditChecks` split, C5 `Page.h` deletion |
| D | `ActorPlan`, `OutputId`, `Merge.h` contributors, `PartitionBake`, `BipedSlot`, `TargetPool`, `material_`, `ManagerShared`, `TextureLab.h`, `Resolve.cpp` names, glossary | `Status::runtimeLab` (one-line menu touch) | `MenuState.h` extraction, `ScratchRebuilt`, anything in `Intent.h` or `Selection.h` |
| E | README, docs index, REQUIREMENTS fixes, REFERENCE headings and constants, `presets.json` rename, comment removal outside `studio/` and `menu/`, `deletions.md` | tell the UI owner the presets file's new name (slice 3F reads it) | REFERENCE section on menu mechanics, comment removal in `studio/` and `menu/` |
| G | none | none | the whole plan waits for UI slice 2A, which owns the shell form in `Forms.cpp` |

The "safe now" column is enough work to run F, A, B, C, D and E in order
without waiting. Keep a `Deferred` list at the bottom of each plan's Status
block and revisit it after the UI complete-editor checkpoint.

## How work is done here

These rules come from `CLAUDE.md` and `docs/conventions.md` and are not
optional.

- Work inside `nix develop`. Every script stops with a message if a tool is
  missing from `PATH`.
- Build with `./build.sh Release -j 4`. More jobs exhaust WSL memory and kill
  the instance. Never build and run clang-tidy at the same time.
- Native tests: `tests/run-native.sh`. Sanitized: `BEEF_SANITIZE=1
  tests/run-native.sh`. These prove pure logic only. They do not prove
  rendering.
- The gate: `tools/gate.sh commit` and `tools/gate.sh push`. The git hooks and
  the Claude Code hook call the same script. `--no-verify` is defeated by
  the hook. The push gate checks formatting, the sanitized native suite, a
  full tidy run, and that tidy findings match `docs/wip/tidy-baseline.txt`.
  After an intended change to findings, regenerate with `tools/tidy.sh
  --force && tools/tidy-baseline.sh`.
- After adding or removing a source file, run `tools/compile-db.sh` so tidy
  and clangd see it.
- Rename symbols with `tools/rename.py Old New --apply`. It drives clangd
  and leaves docs and strings alone. It prints what it left for a hand pass.
  The first run builds the clangd index and takes minutes.
- No comments in C++ source. A fact the code cannot state goes in
  `REFERENCE.md` under the module's heading.
- Plain records and free functions. Complete type signatures. No `auto` in a
  signature. Every engine pointer null-checked at use. Malformed input makes a
  row inert plus a log line.
- Do not modify `decompiled/`, `reference/`, `src/_old/`, `src/extern/` or
  anything under `/mnt/a/mods/` except recipe files when the user asks for one.
- Pushes need the user's SSH passphrase. Hand them the `git push` command;
  do not attempt it.

### In-game checkpoints

The agent cannot run the game. Any plan that changes code under `src/engine/`,
`src/render/` or `src/menu/` ends with a checkpoint: build, run
`./install.sh`, then stop and give the user the exact log lines to look for
and the in-game actions to take. The game log path is recorded in the
project memory as an external path; ask the user if it is not to hand. A plan
is not done until the user reports the checkpoint passed.

## The plans

| Plan | File | Recommendations covered | Touches engine code |
|---|---|---|---|
| F | `critique-plan-f-tests-and-bounds-2026-09-13.md` | 9, 10 | render and engine bounds fixes only |
| A | `critique-plan-a-error-contract-2026-09-13.md` | 1, 2, part of 10 | `engine/RecipeStore`, `render/Binding` |
| B | `critique-plan-b-source-kinds-and-snapshot-2026-09-13.md` | 3, 4 | `engine/ManagerSnapshot`, `menu/` |
| C | `critique-plan-c-structure-2026-09-13.md` | 5, 7 | `engine/MeshReader` move, `render/RuntimeTextures*` |
| D | `critique-plan-d-naming-2026-09-13.md` | 6 | renames cross every module |
| E | `critique-plan-e-docs-2026-09-13.md` | 8 | comment removal in `render/`, `engine/` |
| G | `critique-plan-g-shell-pose-2026-09-13.md` | user decision on `ShellPose` | `render/Shell.cpp`, `engine/ManagerTick.cpp` |

### Order and dependencies

Run the plans in the order F, A, B, C, D, E, then G. The reasons:

1. **F first.** The harness improvements make every later plan's failures
   diagnosable, and the bounds fixes are small and independent.
2. **A before B.** Plan B reuses the published `Reader` and the published
   source-kind parser that Plan A creates.
3. **B before C.** Both edit `src/studio/Edits.cpp`. B changes the visitors
   inside it; C moves them out. Doing B first keeps C a pure move.
4. **D after C.** Renames through clangd are cheap, but running them before
   the file moves means renaming twice.
5. **E after D.** Docs are corrected against final names and file paths. The
   README can be drafted at any time, but is finalised in E.
6. **G last.** It is new render behaviour with its own in-game checkpoint,
   and it reads the shell form in `Forms.cpp`, which UI slice 2A owns; it
   waits for both the six plans and that slice.

Plan F's bounds fixes (F2) may be folded into A if the same files are open.
Nothing else may be merged across plans.

### Per-plan procedure

1. Read the plan file, then re-verify every cited line against the current
   tree. Line numbers drift; the finding is the contract, not the number.
2. Branch from the clean base. One plan per branch.
3. Do the steps in order. Where a plan says "decide", the decision is stated
   in the plan. Do not re-open it. Where a plan says "ask the user", stop and
   ask before proceeding on that step; finish every other step first.
4. Run `tests/run-native.sh`, then `tools/gate.sh commit` per commit.
5. Before declaring done, run `tools/gate.sh push` and the plan's acceptance
   checks. Then the in-game checkpoint if the plan requires one.
6. Update the `Status` block at the top of the plan file: date, branch,
   commits, what was done, what was left and why, checkpoint outcome as the
   user reported it.
7. Report to the user in plain terms: what changed, what was verified and by
   which means, and what remains.

## Decisions closed with the user on 2026-09-13

These are settled. Do not re-open them; apply them where the plans say.

- **`ShellPose` fields: implement them.** The shell honours `offset`,
  `scale`, `scalePoint`, `spin` and `spinAxis` alongside `inflate`. This is
  Plan G. The form controls in `src/studio/Forms.cpp:1495-1523` stay; the
  reader, writer and schema stay. `docs/wip/deletions.md:37-40` is resolved
  by implementation, not deletion.
- **Presets file: rename to `presets.json`, no fallback.** `presets/regions.json`
  in the repo becomes `presets/presets.json`; `CMakeLists.txt:162` and
  `src/Identity.h:46` follow. The installer stages the new file. The user
  deletes the old `regions.json` from
  `/mnt/a/mods/SkyrimSE/mods/BetterEnchantmentEffects/SKSE/Plugins/BetterEnchantmentEffects/`
  by hand; the plan's report gives them that path. (Plan E.)
- **Recipes with errors: only recipe-level errors hold a recipe back.** A
  row error keeps that row inert as now. An error whose `where` is the file
  or the recipe itself (`file`, `recipe`, `clock`, keys, metadata, format)
  loads the recipe for the menu so it can be fixed but keeps it out of the
  applied set. (Plan A, step A2.5.)
- **Namespaces: leave flat.** No change. Plan D's glossary is the one-name
  rule; the layers script is the layering rule.
- **Reasons for constants: search history first, then ask.** The
  implementer checks `src/_old`, `git log -S<value>`, and the ISL and
  Community Shaders sources under `reference/` for each value, records what
  it finds with a citation, and asks the user in one batched question only
  about the values with no trace. (Plan E, step E3.)
- **Working tree: separate commits by piece.** Four commits in the order
  given under Repository state.

## Definition of done for the whole effort

- All seven plan files show `Status: done` with the checkpoint outcome
  recorded, or `Status: deferred` with the UI slice they wait on named.
- `tools/gate.sh push` is green on the final branch.
- The layer check added in Plan C reports no upward include.
- `grep -rn '^\s*//' src --include='*.cpp' --include='*.h' | grep -v _old |
  grep -v extern | grep -v src/cs` prints nothing.
- The critique is re-run with the same ten rubrics (`/critique` in Claude Code,
  skipping negotiation) and the scorecard is appended to this file with the
  date, so the user can compare before and after.

## Status log

Append one line per plan as it completes: date, plan letter, branch, outcome.
