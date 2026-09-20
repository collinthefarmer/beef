# Critique follow-up handoff — 2026-09-14

For the administering agent who picks up after the 2026-09-13 critique
remediation. Read this, then `critique-handoff-2026-09-13.md` (the original
plan of record; its "Repository state" section describes a fallback period
that is over), then the plan file for whatever thread you take.

## Where the repository stands

- `cleanup/stage-0` and `main` both point at `1d92ce8`. The tree is clean.
  `origin/main` is 161 commits behind; the user pushes.
- Plans F, A, B, C, D and E are done: merged, push gate green, in-game
  checkpoints passed on 2026-09-14 (build `dff63dd0817a-f192084d1834f565`).
  Each plan file's Status block records the outcome.
- The UI v2 wave (stages 1 to 3) is committed as `8f7b8b5` and its
  complete-editor game check has been run once; eight findings are recorded
  in `ui-v2-core-checkpoint-2026-09-13.md` under "Game check findings".
- The installed build in the MO2 mod folder matches `main` in behaviour;
  nothing is waiting to be installed.
- Branches `critique/{f,a,b,c,d,e}-*`, `tools/fast-gates` and
  `tools/trace-rotation` are merged and kept only as history; delete at
  will.

## Decisions made on 2026-09-14 (do not reopen)

- **In-game repair of a broken recipe is not a supported path.** The plugin
  never writes a file its loader refuses, so hold-back means "refused
  visibly, never applied", nothing more. `7450b02` (Save re-validates) was
  reverted for this reason. Do not add repair affordances.
- **A newer `format` is held back, not unreadable** (`dff63dd`). The parse
  continues past the format error so the recipe reaches the menu.
- **Verification cadence per plan:** plain native suite, sanitized suite,
  commit gate, DLL build when engine, render or menu code changed. The push
  gate is optional per plan and mandatory once at the end. Installs and
  in-game checkpoints are batched. (`tests/run-native.sh` compiles in
  parallel; `tools/tidy.sh` re-lints only files whose recorded headers
  changed. Cold full native run about 3 minutes, sanitized about 5.)
- **Implementation agents run on Opus** by explicit override; this session
  ran on Fable 5.1.

## The loose threads, in the order to take them

### 1. Plan G: ShellPose fields

`critique-plan-g-shell-pose-2026-09-13.md`. Implements `offset`, `scale`,
`scalePoint`, `spin` and `spinAxis` in the shell pose alongside `inflate`
(decided 2026-09-13; the reader, writer, schema and form controls already
exist). **UI slice 2A is closed**, confirmed by a 2026-09-14 spot-check
against `cleanup/stage-0` at `1d92ce8` (dated note in
`docs/ui-v2-implementation-plan.md` section 5), so Plan G is unblocked. It
has its own in-game checkpoint.

### 2. The eight UI findings

All in `docs/checkpoints/ui-v2-core-checkpoint-2026-09-13.md`, each with the file,
the cause and the intended fix. They belong to the UI owner, not the
critique, but they came from the critique's checkpoint session and block
some of the deferred items below. In short:

1. Custom slider ranges keyed by ImGui widget id, which differs between the
   wide and narrow layouts (`menu/Tuning.cpp`).
2. Row rename unreachable: the new inspectors draw no name field
   (`menu/Workspace.cpp`; the old `ResourcePanels.cpp` tables are dead code).
3. Row removal unreachable, same cause.
4. Adding a layer (or output, or resource) does not select the new row;
   needs a follow-up subject beside `pendingIndexedEdit`.
5. A source cannot change kind to image, ripple or distance: `Edit(SetSource)`
   validates the blank record before storing it (`studio/Edits.cpp`,
   predates the wave).
6. The rename popup opens with stale text and hides the store's refusal;
   after a successful rename the selection does not follow the new id.
7. The recipe overview is blank for a recipe bound to no geometry.
8. A row the parser dropped is invisible in Studio; `RecipeRow::problems`
   is drawn only on the Recipes page.

### 3. Deferred critique items, all on UI seams

Every one waits for the UI complete-editor checkpoint to be accepted; each
is listed in its plan's Status block under `Deferred`:

- A: `FieldCheck` returning `Diagnostic`; the two result records' `error`
  fields to `std::optional<Diagnostic>`; `RecipeRow::heldBack` (the page
  can derive it from `HasRecipeErrors(row.problems)` meanwhile).
- B: B3 (`SourceRow` restructure, `LayerRow::blend`, `RecipeRow::key`);
  the five `MenuState.cpp` catch-alls.
- C: the `EditChecks` split out of `Edits.cpp`; deleting `studio/Page.h`;
  `Edits.cpp` is still 1652 lines, above the plan's 1200 target, and the
  residue (edit overloads, `DescribeVisitor`) is a design question.
- D: the `MenuState.h` extraction (`State()` is the last singleton under
  `studio/`); `ScratchRebuilt`.
- E: the `REFERENCE.md` "Menu mechanics" body; the two comments in
  `menu/MenuWidgets.cpp`; then remove `studio`/`menu` from the push gate's
  no-comment exclusion (`tools/gate.sh`, dated note).

### 4. Five constants with no recorded reason

Plan E searched `src/_old`, `git log -S`, and the pre-comment-strip tree
for each and found nothing. Each has a `REFERENCE.md` line that says the
reason is not recorded, written so the answer replaces one sentence. Ask
the user once, batched, and record the answer verbatim ("tuned by eye; no
derivation" is a valid answer):

1. `kMaxMaskDepth = 8` (`render/CompositorSource.cpp`).
2. The flatness margins `0.02f` and `0.98f` (same file).
3. The `0.05f` floor in the mid-grey normalisation (same file).
4. The one-millisecond freeze epsilon in `engine/ManagerTick.cpp`.
5. `kMaxExpressionOps = 256` (`recipe/Expression.h`), beyond its coupling
   to the shader's `float4 code[256]`.

### 5. Closing the effort

When G is done and the deferred lists are either done or re-deferred with a
named owner: run `tools/gate.sh push` on the final branch, then re-run the
critique with the same ten rubrics (`/critique` in Claude Code, skipping
negotiation) and append the scorecard with the date to
`critique-handoff-2026-09-13.md`, so the user can compare against the 6.5
both axes scored on 2026-09-13.

## Things that cost time to rediscover

- The recipe folder the game reads is the MO2 mod `SKSE Output - beef
  testing`, under `SKSE/Plugins/BetterEnchantmentEffects/recipes/`. Five
  `test-*.json` files from the checkpoint are still in its `user/` folder;
  the user may delete them.
- MO2's virtual file system means a recipe file cannot be edited or
  removed while the game runs, so any checkpoint step that needs one must
  happen between runs.
- The game log is `A:\home\Documents\My Games\Skyrim Special Edition\SKSE\
  BetterEnchantmentEffects.log` (WSL: `/mnt/a/home/Documents/My Games/...`);
  trace segments sit beside it, rotated in 32 MiB segments with the last
  two kept, and `tools/trace-report.py <segment>` stitches the siblings.
  Trace timestamps are UTC; log timestamps are local.
- 75 per cent of trace volume is one `shell` event per skin bone per
  pose; gating that on verbose logging would make the retained window
  cover far more of a session. Not done; a small change in
  `render/Shell.cpp` (`TraceSkin`).
- Inside `nix develop` the native compiler is `$NATIVE_CXX`; bare
  `clang++` on that PATH has no standard-library headers.
  `tools/format.sh` with no arguments reformats the whole tree; pass
  files.
- Agent sleep loops left running after a report re-notify the parent
  forever; stop the agent when its report is in.
