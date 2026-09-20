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
| Onboarding and docs | 5/10 | No README; `REFERENCE.md` and `docs/plans/deletions.md` cite files that do not exist |
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

## Repository state (updated 2026-09-13, after Plan F)

The original handoff asked for a four-piece commit of the working tree.
That did not happen: the user committed the whole tree as
`505f24c cleanup, ui in progress. not verified` on `cleanup/stage-0`,
including `docs/.obsidian/`. Every critique branch is cut from `505f24c`.

Two checkouts exist and they must stay separate:

| Path | Branch | Who edits it |
|---|---|---|
| `/home/nixos/projects/skyrim-modding/plugins/WornEnchantmentPBR` | `cleanup/stage-0` | the UI v2 agent, live and uncommitted (`Manager.h`, `ManagerSnapshot.cpp`, `Panels.*`, `Selection.*`, untracked `Gesture.*`, and more as they go) |
| `/tmp/beef-critique` (a `git worktree` of the same repository) | `critique/<letter>-<slug>` | the critique admin |

Do not run critique work in the main checkout: its uncommitted UI edits
would be swept into your commits, and the gate would format or lint them.
Do not stash, reset, format or otherwise touch those edits. The worktree
has its own `build/` (dependencies fetched, compile database generated) and
its own native test output; `git log` and `git branch` see the same
history in both.

**`505f24c` does not build on its own.** Two pieces of the UI wave are still
uncommitted in the main checkout:

- `src/studio/Gesture.{h,cpp}` are untracked there, but the committed
  `src/studio/Snapshot.h` includes `studio/Gesture.h`. Every studio native
  suite and the DLL fail without them.
- The committed `src/menu/RecipesPage.cpp` calls a two-argument
  `Manager::Watch(request, document)` that exists only in the UI agent's
  uncommitted `src/engine/Manager.h`. The DLL does not link and a full
  `clang-tidy` run fails on that file, so `tools/tidy-baseline.sh --check`
  cannot complete.
- Nine committed sources are not clang-formatted: `engine/ManagerSnapshot.cpp`,
  `menu/ContextRows.cpp`, `menu/StudioPage.cpp`, `render/RuntimeTextures.h`,
  `render/RuntimeTexturesLab.cpp`, `render/TexturePreviews.{h,cpp}`,
  `studio/RecipeSnapshot.cpp`, `studio/Selection.cpp`. They are all UI
  seams, so the critique leaves them alone and `tools/gate.sh push` stops at
  its format step.

Consequences for every plan until the UI wave commits a building tree:

- Verify natively with an untracked copy of `Gesture.{h,cpp}` in the
  worktree (`cp` them from the main checkout; `git status` must keep showing
  them as `??`; never `git add` them). The copy is already in place.
- Prove engine and render edits compile by building the touched objects by
  ninja target instead of the whole DLL:
  `grep -o 'CMakeFiles/[^ ]*/src/<path>.cpp.obj' build/Release/build.ninja`
  gives the target; `nix develop --command ninja -C build/Release -j 4 <targets>`.
- `tools/gate.sh commit` works (it formats and lints only the staged files).
  `tools/gate.sh push` is red for the reasons above, not for anything on the
  critique branches; say so in the report rather than working around it.
- In-game checkpoints cannot run. Record each plan as "implemented,
  checkpoint pending" and run the checkpoints in plan order once the base
  builds (see Resuming).

Plan F's branch is `critique/f-tests-and-bounds`, one commit `f4edc48` on
top of `505f24c`, Status block in its plan file. Branch each later plan from
the previous plan's tip (A from F's tip, B from A's tip, and so on), because
each plan uses what the one before it built; the plans are merged into
`cleanup/stage-0` in the same order, by the user, after their checkpoints.

## Resuming

1. In the main checkout, check whether the UI wave has committed: `git log
   --oneline -5 cleanup/stage-0`, then `git status --short`. The base is
   buildable when `src/studio/Gesture.h` is tracked, `Manager.h` declares
   `Watch` with a `std::string_view a_document` parameter, and
   `nix develop --command tools/format.sh --check` reports every file
   formatted.
2. In `/tmp/beef-critique` (`git worktree list` confirms it; if it is gone,
   `git worktree add /tmp/beef-critique critique/f-tests-and-bounds`), run
   `nix develop --command tests/run-native.sh` first. Green means the copies
   and tools are in place.
3. If the base is buildable: rebase the critique branches onto the new
   `cleanup/stage-0` tip in order (F, then A on F, ...), delete the untracked
   `Gesture.*` copies, run `tools/compile-db.sh`, `./build.sh Release -j 4`,
   `./install.sh`, then the pending in-game checkpoints in plan order, each
   with the log lines its plan file names. Then `tools/gate.sh push` per
   branch and hand the user the push command.
4. If the base is not buildable: continue with the next plan under the
   consequences listed above. Next in order is Plan A on
   `critique/a-error-contract`, branched from `critique/f-tests-and-bounds`.

Tooling facts that cost time to rediscover:

- Inside `nix develop`, the native compiler is `$NATIVE_CXX` (the wrapped
  `clang++`); bare `clang++` on that PATH has no standard-library include
  paths. `tests/run-native.sh` already uses `$NATIVE_CXX`.
- `tests/run-native.sh` takes `SUITE=<substring>` to run only matching
  suites (`SUITE=recipe_recipe`, `SUITE=expression`). It no longer forwards
  arguments to the test binaries.
- A first `tools/compile-db.sh` in a fresh worktree fetches CommonLibSSE and
  spdlog (minutes). A first `./build.sh` compiles CommonLibSSE (about ten
  minutes at `-j 4`). Never run `clang-tidy` while a build runs.
- `tools/format.sh` with no file arguments formats all of `src/` and
  `tests/`; pass the files you touched, or it will reformat the nine UI-seam
  files above.
- Native test scratch files go under the test output directory through
  `test::ScratchDir("<suite>")` (`tests/test_support.h`), never under
  `tests/fixtures`.

## Interaction with the UI v2 rework

`docs/ui-v2-implementation-plan.md` is in status "implementing" as of
2026-09-13. Its first wave added `src/menu/Workspace.*`,
`src/menu/RecipeActions.*`, `src/studio/EditResult.*`,
`src/studio/FileOperation.h`, `src/studio/Navigation.h`, and modified
`Forms`, `Fields`, `FieldCheck`, `Intent.h`, `MenuState.cpp`, `Selection.h`,
`Snapshot.h` and every menu page. Its early framework checkpoint
(`docs/checkpoints/ui-v2-framework-checkpoint-2026-09-13.md`) awaits the user's
in-game run. Its later slices own, by name, the same seams several critique
steps edit: `Forms`/`Fields`/`FieldCheck` (slice 2A), the reference
traversal in `Edits.cpp` (2B), `Selection`/`Intent`/`MenuState` (1B),
`FormDraw`/`MenuWidgets` value widgets (2D), the expression parser (3C), the
Paint reducers (3E), and presets for the pattern chooser (3F). A separate
expression cleanup (`docs/plans/expression-cleanup-implementation-handoff-2026-09-13.md`)
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
  full tidy run, and that tidy findings match `tools/tidy-baseline.txt`.
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

### Progress

| Plan | Branch | Commit | State |
|---|---|---|---|
| F | `critique/f-tests-and-bounds` | `f4edc48`, merged in `303d352` | done; checkpoint passed 2026-09-14 (removed-file case verified natively; unreachable under MO2) |
| A | `critique/a-error-contract` | `f69d908`, merged in `303d352`, plus `dff63dd` | done; checkpoint passed 2026-09-14 (hold-back verified; in-game repair not supported by decision; rename presentation is UI finding 6) |
| B | `critique/b-source-kinds` | `6fb02d2`, `8f80b67`, plus a docs commit | implemented and native-verified on `cleanup/stage-0` (`6f9750e`); DLL builds; B3 deferred to the UI complete-editor checkpoint; done; push gate green and checkpoint passed 2026-09-14 (see the plan's Status) |
| C | `critique/c-structure` | `8233030`, `8ddcc08`, `4dfa74d`, `6453b63`, `83fe74b`, `eb77f40`, plus a docs commit | implemented and native-verified on `cleanup/stage-0` (`4d2dc16`); DLL builds; `tools/layers.sh` is the one layer graph and the push gate runs it; C3 step 3 and C5 step 1 deferred to the UI complete-editor checkpoint; done; push gate green and checkpoint passed 2026-09-14 (see the plan's Status) |
| D | `critique/d-naming` | `b542ca1`, `81fd23b`, `850989d`, `638c052`, `51c95fa`, `9bbc313`, `4438acd`, `f6b3d3d`, `340d7b4` | implemented and verified on `critique/d-naming` (66 suites green plain and sanitized, `tools/layers.sh` green, DLL builds, commit gate green on every commit); the `MenuState.h` extraction and `ScratchRebuilt` deferred to UI slices 1B and 3E; done; push gate green and checkpoint passed 2026-09-14 (see the plan's Status) |
| E | `critique/e-docs` | `ea88b12`, `4e69da4`, `314bb99`, `1c0c3da`, `3ab6c3f`, `ca035bc` | implemented and verified on `critique/e-docs`, branched from `cleanup/stage-0` at `c357f26` (66 suites green plain and sanitized, `tools/layers.sh` green, DLL builds, commit gate green on every commit, tidy baseline 57 findings before and after); `README.md` and `docs/README.md` exist and the push gate now fails on a comment in `src/`; the REFERENCE menu-mechanics body and the two `menu/MenuWidgets.cpp` comments deferred to the UI complete-editor checkpoint; done; push gate green and checkpoint passed 2026-09-14 (see the plan's Status) |
| G | | | deferred; waits on UI slice 2A (the shell form in `Forms.cpp`) |

What Plan F left for the later plans:

- `src/engine/TextFile.{h,cpp}` now holds `ReadText` (returns
  `std::expected<std::string, std::string>` with the failure named) and
  `WriteText`, engine-free and native-tested. Plan A's `RecipeStore` work
  (A2, A2.5) builds on the `LoadFile` path that already logs
  `unreadable (does not exist | cannot be opened | larger than ... | empty)`.
- `src/planners/TextureIdentity.{h,cpp}` holds `ImageCacheKey` and
  `IsPlaceholderExtent`; `render/SourceSampling.h` includes it. Plan C's
  layer check should accept render including planners. Plan D's glossary
  should list both names.
- `Republish` in `RecipeStore.cpp` takes an index from `LoadedIndex`, not a
  recipe id, because `RenameRecipe` changes the id before republishing.
  Plan A touches the same file; keep that.
- Plan F's F2 items 1 to 3 (`Program::Check` bounds, `std::get<float>`,
  `JoinOperands`) are recorded at the end of
  `expression-cleanup-implementation-handoff-2026-09-13.md` for that pass's
  owner. If that pass is abandoned, they come back to Plan A's bounds work.
- The test harness API: `test::Check(ok, what)`, `test::Equal(actual,
  expected, what)`, `test::Near(actual, expected, what[, eps])`,
  `test::Skip(what)`, `test::ScratchDir(suite)`. All print `file:line` on
  failure. New suites are named scenario functions called from `main`, as in
  `tests/planners/actorplanning_tests.cpp`.
- `tests/` is formatted by the same `.clang-format` as `src/` and the commit
  gate checks staged test files. `tests/_old/` is excluded like `src/_old/`.
- `RecipeStore.cpp` still carries comments (a `NOLINTNEXTLINE` note and two
  prose comments near `LoadedRecipe`); they are Plan E's.

What Plan A left for the later plans and for the UI owner:

- `recipe/Binders.h` is the JSON boundary; Plan B's index-checked source
  table lives behind `ParseSourceKind` there. `ParseObjectDocument`,
  `ReadRows(..., cap)` and `NamedRows(..., whereBuilder, ...)` are the
  row-reading vocabulary for any new format.
- The store's applied set is rebuilt by `RebuildApplied` from `g_loaded`;
  recipes with recipe-level errors are held back (`HasRecipeErrors`,
  `RowLevel`). Plan B's snapshot work and the UI's recipes page should
  label a held-back row with `HasRecipeErrors(row.problems)`; a dedicated
  `RecipeRow::heldBack` field waits on the UI's `Snapshot.h` edits.
- `RecipeEditor::NewRecipe` and `RenameRecipe` return a request id and
  finish a `RecipeEditResult`, the same journal Undo and Redo use, so a
  rename collision is visible to the page that reads `editResults`. The
  UI wave's uncommitted `RecipeEditor.cpp` rewrite will conflict with those
  two functions and with the `MessageOf` helper above the journal; the
  branch side is small and the message routing is what matters.
- `presets/regions.json` is now in the parser's format (it was in the
  frozen tree's format and never loaded). Plan E renames it to
  `presets.json`; UI slice 3F reads `MaskPresets` and can rely on
  `SerializePresets` for round-trips.
- `SlotTarget::Problem` returns `std::optional<Diagnostic>`; Plan D's
  naming pass sees `ProblemText` as the projection helper.

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
2. Branch from `cleanup/stage-0` in the main checkout (the previous plan is
   merged there before the next starts). One plan per branch.
3. Do the steps in order. Where a plan says "decide", the decision is stated
   in the plan. Do not re-open it. Where a plan says "ask the user", stop and
   ask before proceeding on that step; finish every other step first.
4. Run `tests/run-native.sh` (about 3 minutes cold, 30 seconds warm since
   the runner compiles in parallel), then commit through the gate.
5. Before declaring the plan done: `BEEF_SANITIZE=1 tests/run-native.sh`
   (about 5 minutes cold), the plan's own acceptance checks, and a DLL build
   if engine, render or menu code changed. `tools/gate.sh push` is optional
   per plan: its tidy step re-lints only the files whose recorded headers
   changed, so it costs 5 minutes plus that re-lint, and it is mandatory once
   after the last plan. `./install.sh` and the in-game checkpoints are
   batched into that final pass (decided by the user 2026-09-14).
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
  reader, writer and schema stay. `docs/plans/deletions.md:37-40` is resolved
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
- `tools/gate.sh push` is green on the final branch after the verification
  pass that follows the last plan (full tidy, baseline comparison, install,
  in-game checkpoints; decided 2026-09-14).
- The layer check added in Plan C reports no upward include.
- `grep -rn '^\s*//' src --include='*.cpp' --include='*.h' | grep -v _old |
  grep -v extern | grep -v src/cs` prints nothing.
- The critique is re-run with the same ten rubrics (`/critique` in Claude Code,
  skipping negotiation) and the scorecard is appended to this file with the
  date, so the user can compare before and after.

## Status log

Append one line per plan as it completes: date, plan letter, branch, outcome.

- 2026-09-14, checkpoints: the batched in-game pass ran on build `dff63dd0817a-f192084d1834f565-Release`
  (`9e8247c` plus `dff63dd`). F, A, B, C, D and E passed and are closed in
  their Status blocks. Two corrections came out of it: `dff63dd` (a newer
  `format` is held back, not unreadable) kept; `7450b02` (Save
  re-validates) reverted in `1948ac4` after the user decided in-game repair
  of a broken recipe is not a supported path. Eight UI findings from the
  same session are in `ui-v2-core-checkpoint-2026-09-13.md` for the UI
  owner. Open: Plan G (waits on UI slice 2A), the deferred items listed per
  plan (all on UI seams), the five constants question in Plan E's Status,
  and the closing critique re-run.

- 2026-09-14, final pass: `cleanup/stage-0` and `main` at `9e8247c` (Plans F
  to E merged, plus the segment-rotating trace). `./build.sh` clean,
  `tools/gate.sh push` green, `./install.sh` done. The batched in-game
  checkpoints (F, A, B, C, D, E and the UI core checkpoint) are handed to the
  user; Plan G still waits on UI slice 2A.

- 2026-09-13: the four-piece commit was superseded; the user committed the
  whole tree as `505f24c cleanup, ui in progress. not verified` (including
  `docs/.obsidian/`). That commit does not build on its own:
  `studio/Snapshot.h` includes `studio/Gesture.h`, which is still untracked
  in the UI wave's checkout, and `menu/RecipesPage.cpp` calls the two-argument
  `Manager::Watch` that exists only in the UI wave's uncommitted `Manager.h`.
  Critique branches are cut from it anyway and verified with untracked copies
  of the missing pieces; every in-game checkpoint waits for the UI wave to
  commit a building tree.
- 2026-09-13, F, `critique/f-tests-and-bounds`: implemented and native-verified;
  in-game checkpoint pending on a buildable base (see the plan's Status).
- 2026-09-14, A, `critique/a-error-contract`: implemented and native-verified
  on top of F.
- 2026-09-14, integration: the paused UI wave was committed as `8f7b8b5`
  (unverified, its in-game checklist unwritten, four new tidy findings
  absorbed into a regenerated baseline), the critique branch merged as
  `303d352` (one conflict in `RecipeEditor.cpp`, both sides kept;
  `paintsession_tests.cpp` updated to the wave's preflight semantics), the
  worktree `/tmp/beef-critique` removed, the DLL built and installed. The
  base now builds, so the fallback rules under Repository state no longer
  apply: work in the main checkout on a branch from `cleanup/stage-0`, and
  the F and A in-game checkpoints wait only on the user's run.
- 2026-09-14, tooling (`tools/fast-gates`, merged into `cleanup/stage-0`
  after B): `tests/run-native.sh` compiles the union of the selected
  suites' sources in parallel (`NATIVE_JOBS`, default 4): cold full run
  3 min 8 s and sanitized 5 min 11 s instead of about 10 each, warm 28 s.
  `tools/tidy.sh` treats a result as stale only when its own recorded
  headers changed (ninja's dependency log), so a header edit re-lints its
  includers, not all 107 files. The per-plan procedure above restores the
  sanitized run per plan; installs and in-game checkpoints stay batched.
- 2026-09-14, B, `critique/b-source-kinds`: B1, B2 and B4 implemented and
  native-verified (64 suites green, commit gate green on both commits, DLL
  builds); B3 deferred per the plan's UI classification. Tidy baseline
  regenerated (+3 intended findings). Sanitized suite, push gate, install and
  the in-game checkpoint wait for the single pass after the last plan.
- 2026-09-14, C, `critique/c-structure`: C1, C2, C4 and C5 step 2 implemented,
  plus C3 steps 1 and 2 (the coordinate steps, done ahead of UI slices 2B and
  3C). 67 suites green plain and sanitized, commit gate green on every commit,
  DLL builds, `tools/layers.sh` exits zero and is now the push gate's layer
  check. C3 step 3 (`EditChecks`) and C5 step 1 (`Page.h`) deferred to UI
  slices 2B and 2A; C5 step 3 (`ManagerShared`) left to Plan D as the plan
  directs. Tidy baseline regenerated for the moved code, 57 findings before
  and after, after fixing `tools/tidy.sh` to prune the cached result of a
  source that has been moved or deleted.
- 2026-09-14, E, `critique/e-docs`: E1 to E4 implemented. `README.md` and
  `docs/README.md` added, `CLAUDE.md` reduced to a pointer, fourteen history
  documents given a status header; the REQUIREMENTS, `deletions.md` and
  `REFERENCE.md` claims the critique listed corrected, with Paint's term
  templates moved under a new `## History` heading; `presets/regions.json`
  renamed to `presets/presets.json` with no fallback; the reason for each
  constant recorded under its module, five of them as "not recorded" with the
  search that found nothing; 38 of the 40 comment lines moved to `REFERENCE.md`
  or deleted, the owning-memory `NOLINT` restructured away, and a comment check
  added to the push gate. 66 suites green plain and sanitized, `tools/layers.sh`
  green, DLL builds, commit gate green on every commit, tidy baseline
  regenerated for the line shifts (57 findings before and after). The menu
  mechanics section body and the `menu/` comments are deferred; the in-game
  checkpoint joins the batched pass.
- 2026-09-14, D, `critique/d-naming`: every "safe now" rename plus the one
  "coordinate" rename (`Status::runtimeLab` -> `textureLab`, one line in
  `menu/Menu.cpp` and one in `studio/Snapshot.h`). `ActorState` -> `ActorPlan`,
  `LiveActor::structure` -> `plan`, `OutputIndex` -> `OutputId`,
  `SlotSource`/`LightSource` -> `SlotContributor`/`LightContributor`,
  `PartitionBake::slot` -> `bipedSlot` behind a new `enum class BipedSlot`,
  `ResourceSlots` -> `TargetPool`, `SlotWriter::binding_` -> `material_` (with
  `PbrMaterial::material_` -> `layout_`), `ManagerShared` split into
  `engine/Clock` and `engine/LiveActor.cpp` and deleted, `RuntimeTextures.h`
  -> `TextureLab.h` with its three sources, the glob matcher's indices, and
  the glossary in `docs/conventions.md`. 66 suites green plain and sanitized,
  `tools/layers.sh` green, DLL builds, commit gate green on every commit.
  Tidy baseline regenerated for the renamed files, 57 findings before and
  after. The `MenuState.h` extraction and `ScratchRebuilt` are deferred; the
  in-game checkpoint joins the batched pass.

What Plan B left for the later plans and for the UI owner:

- `Complete(table, count)` (`Core.h`) is the assert every enum table in
  `Words.h` carries; a new enum gets a `kFooCount` beside it in `Recipe.h`.
  `SourceKindId`/`kSourceKindWords` are the source-kind id and table;
  `SourceKindIdOf(kind)` reads the id. The extension checklist for a new
  source kind is in `docs/conventions.md` under the variants heading.
- `ParseSourceAlternative<T>` (`RecipeRead.cpp`) is where a new source kind's
  parser goes; `kSourceParsers` is built from it by alternative index.
- `tests/recipe/schema_tests.cpp` pins `schema/recipe.schema.json`'s enums to
  the tables; a table change fails it until the schema follows. `FunctionNames()`
  (`Expression.h`) is the published view of the expression function table.
- `TextureHandle` is opaque (`enum class` over `std::uintptr_t`); the UI's
  preview pinning must keep the snapshot `shared_ptr` alive, not the handle
  (contract in `REFERENCE.md`, studio). `TextureHandleOf` (engine,
  `ManagerSnapshot.cpp`) and `TextureOf` (menu, `MenuWidgets.cpp`) are the
  only conversions; compare a handle against `TextureHandle{}`.
- `studio/ApplicationRecord.h` holds the application record types; nothing
  under `studio/` includes `engine/` and the push gate now enforces it.
- Plan C's `MODULE_DEPS` removal: studio's entry is already `recipe mesh`
  and the two engine suites in `run-native.sh` list
  `src/studio/ApplicationRecord.cpp` explicitly.
- B3 (`SourceRow` as a variant of per-kind field records, `LayerRow::blend`,
  `RecipeRow::key`) and the five `MenuState.cpp` catch-alls over
  `RecipeEdit`/`Intent` wait on UI slices 2A/2D and 1B.

What Plan C left for the later plans and for the UI owner:

- `src/recipe/Visit.h` is the published recipe traversal. It holds the
  location vocabulary too (`ResourceKind`, `ResourceRef`, `OutputOwner`,
  `LayerOwner`, `ShellOwner`, `VariantOwner`, `RelationshipOwner`,
  `PropertyLocation`), moved out of `studio/Relationships.h` into the root
  namespace; `Relationships.h` keeps `Relationship` and `RelationshipsOf`.
  UI slice 2B extends `Visit.h`, not an anonymous namespace, which is what
  the UI plan's reuse audit asked for. A non-template helper added there
  needs `inline`.
- `tools/layers.sh` is the one layer graph and the push gate runs it. A new
  directory, or a widened edge, is an edit to its `ALLOWS` table. It also
  enforces that every include names its directory. Tell the UI owner it
  exists: `studio/` may not include `menu/` or `engine/`, which is the UI
  plan's own rule.
- `tests/run-native.sh` links every engine-free module into every suite and
  discovers suites in one loop. A new engine-free engine unit is one line in
  `SUITE_EXTRAS`. Suite names lost the `_tests` suffix (`recipe_recipe`, not
  `recipe_recipe_tests`).
- Plan D's renames land after these moves: `MeshReader` and `MeshCache` are
  in `src/render/`, `MeshEntry` with them; `Constants` is now
  `LayerConstants` in `render/ShaderConstants.h`; `Failed` and `DataOf` are
  in `render/D3DResult.h`; `VariantApplies`/`ApplyVariant` are in
  `recipe/Variants.cpp` and `IsAnimated` in `recipe/Vocabulary.cpp`.
  `engine/ManagerShared.{h,cpp}` were left for Plan D to empty and delete.
- Plan E documents against those paths. `REFERENCE.md`'s shader section now
  names `render/ShaderConstants.h` and `render/D3DResult.h`;
  `docs/conventions.md` gained a Gates bullet for `tools/layers.sh`.
  `docs/history/render-ownership.md`, `docs/history/engine-ownership.md`,
  `docs/history/engine-types-survey-2026-09-12.md` and `docs/history/buildup-plan.md`
  still cite `engine/MeshReader`; they are history, and Plan E decides
  whether to correct or mark them.
- `src/studio/Edits.cpp` is 1652 lines, not the plan's target of 1200. The
  residue is about 890 lines of `Edit` overloads and 210 of
  `DescribeVisitor`, which the plan says that file keeps. Splitting those is
  a design question, not a move; it belongs in a follow-up beside the
  `Manager` decomposition.

What Plan E left for Plan G and the final pass:

- `README.md` is the reading order and `docs/README.md` the index; `CLAUDE.md`
  points at them instead of carrying a second copy. A new document is one line
  in `docs/README.md`, in one of its four groups, or it is not indexed.
- The push gate fails on a comment in `src/` outside `_old`, `extern`, `cs`,
  `studio` and `menu`, and on anything but a `NOLINT` directive. Plan G writes
  render and engine code: a fact it cannot say in a name goes in `REFERENCE.md`
  under the module's heading, not in a comment, or `tools/gate.sh push` stops.
- `src/engine/RecipeStore.cpp:21` holds the tree's only `NOLINT`, with its
  reason under the engine heading in `REFERENCE.md`. A second one needs the same
  treatment and an entry in the gate's note.
- `REFERENCE.md`'s "Menu mechanics" section body, the two comments in
  `src/menu/MenuWidgets.cpp`, and deleting `studio` and `menu` from the gate's
  exclusion pattern all wait on the UI complete-editor checkpoint. They are in
  Plan E's `Deferred` list.
- Five constants have a `REFERENCE.md` line that says their reason is not
  recorded: `kMaxExpressionOps`' magnitude, `kMaxMaskDepth` 8, the flatness
  margins 0.02 and 0.98, the 0.05 floor under the normalisation mean, and the
  one-millisecond freeze epsilon. The batched question went to the user with
  Plan E's report; the answer replaces the "not recorded" sentence in each line.
- Plan E's in-game checkpoint joins the batched pass: after `./install.sh` the
  user deletes the stale `regions.json` from the plugin folder by hand and the
  `presets:` log line must report the same count from `presets.json`.
- `REFERENCE.md`'s preamble now says that `NOTES.md`, `ARCHITECTURE.md`,
  `reference/` and `decompiled/` are cited but not in the repository. The
  `NOTES n` numbers and the decompile line ranges stay as provenance; do not
  treat them as files to open.

What Plan D left for the later plans and for the UI owner:

- `docs/conventions.md` now carries a **glossary** under House rules: one
  line per word the whole codebase must use one way. Plan E documents
  against those words; a new name that means something already in the table
  is an edit to the table, not a second word.
- The names as they now stand, for Plan E's doc pass: `ActorPlan`,
  `OutputId`, `SlotContributor`/`LightContributor`, `BipedSlot`,
  `PartitionBake::bipedSlot`, `TargetPool`, `engine/Clock.h`,
  `engine/LiveActor.cpp`, `render/TextureLab.h` with
  `TextureLabLifecycle/Pass/Readback.cpp`, `Status::textureLab`,
  `PbrMaterial::layout_`.
- `REFERENCE.md` gained three entries this pass: the two biped-slot
  representations (mesh), `TargetPool`'s lease invariant (planners), and
  `RetireActorEffects`'s clearing order (engine). They replace comments
  that were deleted, so Plan E should not treat them as duplicates.
- Plan G reads the shell form in `studio/Forms.cpp`, which this pass touched
  in four lines (`BipedSlotNames` and the partition field's parse). Nothing
  in the shell form itself changed.
- `docs/checkpoints/cleanup-checkpoint-2026-09-13.md:36` still names `ResourceSlots`,
  a symbol that no longer exists. It is a dated checkpoint record; Plan E
  decides whether to correct it or leave it as history.
- Deferred to the UI complete-editor checkpoint: the `MenuState.h`
  extraction (`MenuState`, `FiringDraft`, `ResourceTab`, `State()` still in
  `studio/Intent.h`; `State()` is the last singleton under `studio/`) and
  `ScratchRebuilt`, which UI slice 3E owns.
