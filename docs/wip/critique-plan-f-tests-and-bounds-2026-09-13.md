# Plan F: test harness and bounds checks — 2026-09-13

Status: implemented 2026-09-13 on branch `critique/f-tests-and-bounds`;
awaiting the in-game checkpoint.

- Base: `505f24c` on `cleanup/stage-0`. That commit includes
  `studio/Snapshot.h` with an `#include "studio/Gesture.h"`, but
  `src/studio/Gesture.{h,cpp}` are still untracked in the UI wave's checkout,
  so the base does not compile on its own. Verification here used an
  untracked copy of that pair; the copy is not committed. Nine committed
  UI-seam sources (`ManagerSnapshot.cpp`, `ContextRows.cpp`,
  `StudioPage.cpp`, `RuntimeTextures.h`, `RuntimeTexturesLab.cpp`,
  `TexturePreviews.*`, `RecipeSnapshot.cpp`, `Selection.cpp`) are also not
  clang-formatted at the base; they were left alone under the seam rule, so
  `tools/gate.sh push` fails its format check until the UI wave formats
  them.
- F1 done: `test::Check`/`Equal`/`Near`/`Skip` take a `std::source_location`
  and print `file:line: FAIL: what`, with both values for `Equal` and `Near`;
  `actorplanning_tests.cpp` is ten named scenarios including empty
  geometry, empty store and duplicate placement; `ExpectError` takes the
  expected message fragment and prints every diagnostic it saw on a miss;
  fixtures are globbed from `tests/fixtures/recipes` with a count floor of
  seven; `RUN_ARGS` is gone and `SUITE=<substring>` filters suites; the
  standard is C++23 on both sides and `REQUIREMENTS.md` says so; every test
  file is in the source style and `tools/format.sh` plus the commit gate
  cover `tests/`. Step 8: `SourceSampling.h` includes `render/Compositor.h`
  and `IsNonPlaceholderTexture` reads a D3D extent, so the pure halves moved
  to `src/planners/TextureIdentity.{h,cpp}` (`ImageCacheKey`,
  `IsPlaceholderExtent`) with `tests/planners/textureidentity_tests.cpp`;
  `IsNonPlaceholderTexture` stays in render and calls the planner predicate.
- F2 done except items 1 to 3, which are recorded for the expression cleanup
  owner at the end of
  `expression-cleanup-implementation-handoff-2026-09-13.md`. Item 4:
  `At` returns a pointer, `TypeOf(size_t)` an optional, `Inert` is true out
  of range, and `SignalState::Accept` returns on an out-of-range index. Item
  7: `ReadText`/`WriteText` moved to `src/engine/TextFile.{h,cpp}` (engine-free,
  tested by `tests/engine/textfile_tests.cpp`) with a 4 MiB cap recorded in
  `REFERENCE.md`; the store logs "does not exist", "cannot be opened",
  "larger than the ... cap" and "empty". Item 8 deviates from the letter of
  the plan: a lookup by recipe id breaks `RenameRecipe`, which changes the id
  before republishing, so `Republish` takes the index that `LoadedIndex`
  found and refuses (error log, no publish) when either list is shorter.
- Verified: `tests/run-native.sh` green (57 suites), `BEEF_SANITIZE=1` green,
  the nine plugin objects this plan touches compile under clang-cl (the DLL
  itself does not link at the base, see above),
  `SUITE=expression` runs only the three expression suites, a deliberately
  broken assertion prints `file:line` and both values,
  `tools/format.sh --check` passes for every file this plan touched.

Deferred: none.

Covers critique recommendations 9 (test harness failure output and style) and
10 (unchecked indices and the missing-file diagnostic). Runs first because it
makes every later plan's test failures diagnosable. Part F2 may be folded into
Plan A if the same files are already open there.

## UI rework impact

- **Safe now:** all of F1. The UI wave's new native tests under
  `tests/studio/` use `test::Check`; adding a defaulted
  `std::source_location` parameter keeps their calls compiling. The
  reformat in F1 step 7 must not touch files the UI wave modified while
  they are uncommitted; do it after they are committed, and expect the UI
  agents' files to already be in LLVM style.
- **Coordinate:** F2 items 1 to 3 (`Program::Check`, `std::get<float>`,
  `JoinOperands`) are in `src/recipe/Expression.cpp`, which the expression
  cleanup handoff and UI slice 3C both edit. Hand these three items to the
  expression cleanup owner as a note in their handoff file, and do not edit
  `Expression.cpp` here.
- **Coordinate:** F2 item 4 changes `SignalGraph::At`, `TypeOf(size_t)` and
  `Inert` signatures. Callers include the signal detail panels
  (`src/studio/Panels.cpp`) that UI slice 2E re-presents. Do it now while
  2E has not started; the compiler lists every caller.
- **Safe now:** F2 items 5 to 8 (render guards, `ReadText`, `Republish`).

## Findings addressed

- `tests/test_support.h:25-32`: `Check` prints only the caller's label. No
  file, no line, no expected-versus-actual.
- `tests/test_support.h:17`: global counters with one `main` per file; a crash
  forfeits every later check.
- `tests/run-native.sh:58`: `RUN_ARGS` is forwarded to every binary and no
  test reads `argv`. Filtering is a dead feature.
- `tests/run-native.sh:15`: tests compile with `-std=c++23`. Check whether
  `CMakeLists.txt` sets the same standard.
- `tests/recipe/recipe_tests.cpp:51-55`: `ExpectError` asserts only
  `HasErrors()`, so a rejection for the wrong reason passes.
- `tests/recipe/recipe_tests.cpp:83-86`: seven fixture names hard-coded
  instead of globbed from `tests/fixtures/recipes`.
- `tests/recipe/recipe_tests.cpp:17-31` versus
  `tests/planners/actorplanning_tests.cpp:39-47`: two house styles (tabs and
  Allman versus two-space LLVM).
- `tests/planners/actorplanning_tests.cpp:61-258`: a 200-line `main` with
  order-coupled state and no scenario names; no empty-geometry, empty-store or
  duplicate-placement case.
- `src/render/SourceSampling.h:5-10`: `ImageCacheKey` and
  `IsNonPlaceholderTexture` are pure and untested.
- `src/recipe/Expression.cpp:576,587`: `Program::Check` indexes `refs_` and
  `curves_` unchecked while `Evaluate` at `:680,685-688` bounds-checks.
- `src/recipe/Expression.cpp:52,80`: `std::get<float>` guarded only by a
  prior `TypeOf` comparison; `Get<T>` exists in `Core.h`.
- `src/recipe/Expression.cpp:138-148`: `JoinOperands` writes `operands[i-1]`
  into a three-element array with no clamp.
- `src/recipe/Signals.h:73,76,81`: public `noexcept` `At`, `TypeOf(size_t)`
  and `Inert` index `nodes_` unchecked; `Signals.cpp:1152` `Accept` likewise.
- `src/render/Binding.cpp:192`: `SetFeature` dereferences `material_`
  without `MaterialAttached()`.
- `src/render/RuntimeTexturesPass.cpp:498,527`: `BakeMesh` guards
  `borrowedContext_` but dereferences `borrowedDevice_`.
- `src/engine/RecipeStore.cpp:45-50`: `ReadText` cannot tell a missing file
  from an empty one and has no size cap; a missing file surfaces as "not a
  JSON object" from `RecipeRead.cpp:1541-1544`.
- `src/engine/RecipeStore.cpp:436-444`: `Republish` derives an index by
  pointer arithmetic across two parallel vectors and only logs a mismatch.

## F1: harness

Decisions made here. Do not re-open them.

1. **File and line without macros.** Change `test::Check` to
   `Check(bool ok, std::string_view what, std::source_location loc =
   std::source_location::current())` and print `file:line: FAIL: what`. Add
   `Equal(const A &actual, const B &expected, std::string_view what,
   std::source_location loc = current())` that prints both values through
   `std::format` when both are formattable, and falls back to the label
   otherwise. Add `Near` overloads that report the two floats. Keep `Skip`
   counting as a failure.
2. **Scenario functions.** Split
   `tests/planners/actorplanning_tests.cpp` `main` into named scenario
   functions in the style of `tests/engine/sessionqueue_tests.cpp`
   (`LoadAndCoalesce`, `SubmissionFailure`). Each scenario builds its own
   state. Add three scenarios: empty geometry list, empty recipe store, and
   duplicate placement of one recipe on one geometry. Read
   `src/planners/ActorPlanning.h` to confirm the entry points before writing
   them.
3. **`ExpectError` names the reason.** Give it a `std::string_view
   a_expectedMessageFragment` parameter and assert that some error diagnostic's
   message contains it. Update every call site with the fragment the test
   actually intends.
4. **Glob fixtures.** Replace the seven hard-coded names with a directory
   listing of `Fixtures()/"recipes"` sorted by name, and assert the count is
   at least seven so a missing fixture still fails.
5. **Suite filter.** Delete `RUN_ARGS`. Add `SUITE=<substring>` to
   `tests/run-native.sh`: when set, only suites whose name contains it build
   and run. Document it in the script's usage line at the top.
6. **Language standard.** Read `CMakeLists.txt` for `CXX_STANDARD`. If it is
   20, change the test flag to `-std=c++20` and fix whatever breaks. If it is
   23, leave the flag and record the standard in `REQUIREMENTS.md` where the
   build is described. Do not leave the two disagreeing.
7. **One test style.** Check for a `.clang-format` under `tests/`. The
   source style is LLVM two-space. Reformat the tab-and-Allman test files
   (`tests/recipe/*`, `tests/settingspublication_tests.cpp`, others found by
   inspection) to the source style with `clang-format` from the dev shell.
   Delete any `tests/.clang-format` that pins the other style. Make
   `tools/format.sh` cover `tests/` so the gate keeps it that way.
8. **`SourceSampling` tests.** Read `src/render/SourceSampling.h` and its
   `.cpp`. If they compile without `PCH.h` or `RE::` types, add
   `tests/render/sourcesampling_tests.cpp` and an explicit `build_and_run`
   line in `run-native.sh` like the engine suites. If they do not, move the
   two pure functions to `src/planners/` (they concern cache identity, not
   D3D) and test them there. State which you did in the Status block.

## F2: bounds and diagnostics

1. **`Program::Check`.** Bounds-check `node.index` against `refs_` and
   `curves_` and return `std::unexpected("malformed program")` on
   overflow, matching `Evaluate`.
2. **`std::get<float>` at `Expression.cpp:52,80`.** Replace with `Get<float>`
   and a zero fallback, so the guard is local rather than an unstated
   invariant.
3. **`JoinOperands`.** Take a `std::span<const ValueType>` and return early
   when the size is not two or three. Add a test that feeds it one and four.
4. **`SignalGraph` accessors.** Change `At(size_t)` to return `const Signal
   *` (null when out of range), `TypeOf(size_t)` to return
   `std::optional<ValueType>`, and `Inert(size_t)` to return `true` when out
   of range. Fix `Accept` at `Signals.cpp:1152` the same way. Update every
   caller; the compiler will list them. Out-of-range is treated as inert,
   which is the stated policy for malformed input.
5. **`SetFeature`.** Add the `MaterialAttached()` guard inside the function
   so it does not depend on its single caller.
6. **`BakeMesh`.** Guard `borrowedDevice_` alongside `borrowedContext_`.
7. **`ReadText`.** Return `std::expected<std::string, std::string>`.
   Distinguish "does not exist", "cannot be opened" and "larger than the
   cap" from an empty file. Cap at 4 MiB and record the reason for the number
   in `REFERENCE.md` under the engine heading (a recipe is a few kilobytes;
   the cap stops a stray binary from being read into memory). Route the
   failure through the same diagnostic path `LoadFile` uses so the message
   names the real cause.
8. **`Republish`.** Replace the pointer-arithmetic index with a lookup by
   recipe id. If the id is not found, that is an invariant violation; log at
   error level and return without publishing.

## Acceptance

- `tests/run-native.sh` green, then `BEEF_SANITIZE=1 tests/run-native.sh`
  green.
- A deliberately broken assertion prints a `file:line` prefix and, for
  `Equal`, both values.
- `SUITE=expression tests/run-native.sh` runs only the expression suites.
- `tools/format.sh --check` covers `tests/` and passes.
- New tests exist for: `JoinOperands` with one and four operands,
  `SignalGraph::At` out of range, `Program::Check` with a corrupted ref
  index, `ReadText` on a missing path and on a file over the cap (write the
  file into a temp directory under the test output dir, not into fixtures),
  and the three planner scenarios.
- Tidy baseline regenerated only if findings changed intentionally.

## In-game checkpoint

F2 items 5 and 6 change render code and item 7 and 8 change the recipe
store. After the build and install, ask the user to load a save with an
enchanted worn item and confirm in the log that recipes load with the same
counts as before and that the effect renders. Also ask them to place an
empty file and a non-JSON file in the recipes folder and confirm the log
names "empty" and "not a JSON object" respectively, and to remove a recipe
file while the game runs and confirm the log names "does not exist" on the
next reload.

## Out of scope

Coverage for `RecipeStore`, `RecipeEditor` and the `Manager*` units stays
absent. They are bound to SKSE logging and engine forms and need an adapter
seam that is not in this plan.
