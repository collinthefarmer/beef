# Plan C: layering and file structure — 2026-09-13

Status: done. In-game checkpoint passed 2026-09-14 on build `dff63dd0817a-f192084d1834f565-Release`:
stacks with animated, ripple and material-clusters sources rendered as
before; no `TextureLab` or `RenderTargetPool` failure in the log or the
trace (0 presenter rejections, 0 lease rejections).

Implemented and verified natively 2026-09-14 on
`critique/c-structure`, branched from `cleanup/stage-0` at `4d2dc16`.
Five commits, one per step group. Install and the in-game checkpoint are
batched into the single pass after the last plan.

| Commit | Step |
|---|---|
| `8233030` | C1: `MeshReader` moves to `src/render/` |
| `8ddcc08` | C2: `tools/layers.sh`, the push gate, `tests/run-native.sh` |
| `4dfa74d` | C3 steps 1 and 2: `recipe/Visit.h`, `RenameInExpression` |
| `6453b63` | C4: `ShaderConstants.h`, `D3DResult.h`, `MeshCache` |
| `83fe74b` | C5 step 2: `Recipe.cpp` split |
| `eb77f40` | the tidy-cache prune and the regenerated baseline |

### What was done

**C1.** `src/engine/MeshReader.{h,cpp}` are now `src/render/MeshReader.{h,cpp}`
(`git mv`, history follows). `Compositor.h` and `CompositorSource.cpp` were
the only includers, as the finding said; `ReadMesh`, `IdentityOf`,
`CompareWithGpu`, `NodeBindPosition` and `ToRootSpace` have no caller under
`src/engine/` or `src/menu/` (`Studio::ReadMesh` in `Intent.h` is an
unrelated intent record). Step 2's question is answered by C4: once
`MeshEntry` left `Compositor.h`, nothing there named a `MeshReader`
declaration, so the header now reaches it through `render/MeshCache.h`.

**C2.** `tools/layers.sh` holds one row per directory listing what it may
include, checks every `#include "..."` under `src/` against it, and also
carries the engine-free rule the Plan B purity grep held (no `RE::` symbol
in `recipe`, `mesh`, `planners`, `studio`, `diagnostics`; `PCH.h` is an
adapter-only root header, so the table covers that half). It reports
file, line and the offending include, and exits non-zero on any. The push
stage of `tools/gate.sh` runs it in place of the grep.

Its first run found no layer edge beyond the `MeshReader` one C1 had just
removed (the `Snapshot.h` edge was already gone with Plan B4), and four
includes that name no directory, against `REQUIREMENTS.md:144`'s "src is
the only include root": `recipe/Merge.cpp`, `render/RuntimeTexturesLab.cpp`,
`render/ShaderSource.cpp`, `render/PBRMaterial.cpp`. All four are fixed in
the same commit. `tools/layers.sh` now exits zero.

`tests/run-native.sh` has no `MODULE_DEPS`. `MODULES` is `recipe mesh
planners studio diagnostics`, every suite links the union of their sources,
and the engine, diagnostics and settings suites are discovered by the same
loop, naming only their extra sources in a `SUITE_EXTRAS` table at the top.
Suite names drop the `_tests` suffix, which the hand-written engine suites
already lacked, so `recipe_recipe_tests` is now `recipe_recipe` and
`SUITE=studio` still selects exactly the 31 studio suites.

**C3 steps 1 and 2.** `src/recipe/Visit.h` publishes the traversal toolkit
that was unreachable in `Edits.cpp`'s anonymous namespace: the `VisitRef` /
`VisitParam` / `VisitVector` skeleton, `LiteralOf`, `ScalarDefault`, the
per-kind `VisitSignalParams`, `VisitSourceParams`, `VisitSurfaceParams`,
`VisitLightParams`, `VisitOutputParams` and `VisitShellParams`,
`ForEachParam`, `LocatedVisitor`, `RefVisitor` and the five `ForEach*`
reference walks.

The plan's rule was that nothing moved may depend on studio types. The
dependency turned out to be the owner and property records the traversal
reports with, and they are recipe vocabulary, not editor vocabulary: they
name a row and a field of a `Recipe`, the same thing `Reporter`'s `where`
names. So `ResourceKind`, `kResourceKindNames`, `ResourceRef`,
`OutputOwner`, `LayerOwner`, `ShellOwner`, `VariantOwner`,
`RelationshipOwner` and `PropertyLocation` move from
`studio/Relationships.h` into `recipe/Visit.h` in the root namespace, and
`Relationships.h` keeps `Relationship` and `RelationshipsOf`. Every studio
user reads those names unqualified from inside `Studio` and is unchanged;
the one file that qualified them, `menu/RelationshipPanel.cpp`, drops the
`Studio::` prefix on them (18 lines, mechanical; that file is named by no
UI slice).

Two small changes were needed to make the move compile:

- `VisitSurfaceParams` read a layer's default opacity from
  `Studio::DefaultLayer()`. It now default-constructs a `Layer`, as the
  neighbouring `VisitLightParams` and `VisitShellParams` already do for
  their defaults. `Layer::opacity` is `1.0f` in the struct and
  `DefaultLayer()` set it to `1.0f`, so the value is identical.
- `LiteralOf(const Param &)` and `ScalarDefault` are not templates, so in a
  header they need `inline`. Without it every studio translation unit that
  reaches `Visit.h` emits a definition and the native link fails on
  multiple definitions.

`RenameInExpression` and `ExpressionRename` move from `studio/Edits.{h,cpp}`
to `recipe/Expression.{h,cpp}`. The expression cleanup pass had finished, so
no hand-off was needed. `studio/TermTemplates.cpp` gains the explicit
`recipe/Expression.h` include it was getting transitively, and `Edits.h`
loses a `std::span` use it never included `<span>` for.

The test moved as a split rather than a whole file. Only the first eight
checks in `tests/studio/expressionrename_tests.cpp` are about the text
surgery; the rest exercise `KeepEdits`, `MaterialiseTerm` and `BuildTerm`,
which are studio. The eight are now `tests/recipe/expressionrename_tests.cpp`
and pull only `recipe/Expression.h`; the studio suite of the same name keeps
the rest. Moving the file whole would have put `studio/` includes in a
`tests/recipe` suite, which is the edge this plan exists to remove.

**C4.** `src/render/ShaderConstants.h` declares `LayerConstants` (the
`Constants` rename), `ProgramConstants`, `RippleConstants` and
`ClassifyConstants` with their `static_assert`s once;
`RuntimeTexturesLab.cpp` and `RuntimeTexturesPass.cpp` include it and drop
their byte-identical copies. `src/render/D3DResult.h` holds `Failed` and
`DataOf`, replacing four copies of `Failed` (Lab, Pass, Readback,
`RenderTargetPool`) and three of `DataOf` (Pass, Readback,
`RenderTargetPool`). `MeshEntry` and `MeshCache` move from `Compositor.h`
to `render/MeshCache.h`, and the four methods with their `NameOf` and
`LogRead` helpers from `CompositorBake.cpp` to `render/MeshCache.cpp`.

Step 4: the `RenderedMask` forward declaration at the top of `Compositor.h`
stays. The cycle is between the two records themselves — `PreparedSource`
holds a `std::shared_ptr<RenderedMask>` and `RenderedMask` holds a
`std::vector<PreparedSource>` — so moving the `Prepared*` records into a
`render/Prepared.h` would carry the forward declaration along rather than
remove it. Moving `MeshCache` out does not touch that pair.

**C5 step 2.** `recipe/Recipe.cpp` is 92 lines: `MakeDiagnostic`, the
`Recipe::Find*` lookups, the `*Where` names, and the error predicates over
a `LoadResult`. `VariantApplies` and `ApplyVariant` are
`src/recipe/Variants.cpp` (27 lines); the four `IsAnimated` overloads, the
`AnimationQuery` walk behind them and the `RefOf` / `CollectRefs` helpers
only they use are at the end of `Vocabulary.cpp` (852 lines). Their
declarations were already grouped in `Recipe.h` and did not move.

### One tooling fix the moves exposed

`tools/tidy.sh` caches one result per source path and never removed the
result of a source that had been deleted or moved.
`tools/tidy-baseline.sh` reads the whole cache directory, so after C1 the
baseline carried `src/engine/MeshReader.cpp` and `src/render/MeshReader.cpp`
alike and reported 60 findings where clang-tidy had found 57. A full
`tools/tidy.sh` run now prunes results with no source behind them (a
targeted run does not, since it does not know the whole source list). Plan D
renames across every module and would have hit this on every move.

### Deferred

- **C3 step 3, the `EditChecks` split.** `CheckText`, `CheckCurveText`,
  `CheckCtx`, `CheckScalarRef`, `CheckVectorRefSignal`,
  `CheckVectorRefParts` and `CheckVectorRef` stay in `Edits.cpp`. UI slice
  2B owns the reference traversal in that file; the split is a pure move
  and costs nothing to wait for.
- **C5 step 1, deleting `studio/Page.h`.** `Fields.cpp` is UI slice 2A's
  seam. `Page.h` is still vestigial (no file under `src/menu/` uses it;
  `menu/Frame.h` is the real per-frame bundle), so delete it when 2A is
  done.
- **C5 step 3, deleting `engine/ManagerShared.{h,cpp}`.** Plan D owns the
  rename that empties it, and C ran first, so the files stay as the plan
  directs.
- **B3 and the `MenuState` catch-alls** are Plan B's deferrals and were not
  picked up.

### Acceptance

| Check | Result |
|---|---|
| `tools/layers.sh` exits zero | yes: "layers: every include stays inside the graph" |
| `tests/run-native.sh` has no dependency table and passes | yes: `MODULE_DEPS` is gone; 67 suites green, exit 0 |
| `SUITE=studio` runs only studio suites | yes: 31 suites, all `studio_*` |
| `BEEF_SANITIZE=1 tests/run-native.sh` | yes: 67 suites green, exit 0, no ASan or UBSan report |
| `grep -rn 'struct alignas(16)' src/render` shows each struct once | yes: four, all in `render/ShaderConstants.h` |
| `grep -rn 'Failed(std::int32_t' src/render` shows one definition | yes: `render/D3DResult.h:8` |
| `src/studio/Page.h` does not exist | no: deferred with C5 step 1 |
| `src/render/MeshReader.h` exists, `src/engine/MeshReader.h` does not | yes |
| `wc -l src/studio/Edits.cpp` under 1200 | no: 1652, down from 2060 |
| DLL builds (`./build.sh Release -j 4`) | yes, exit 0 |
| Tidy baseline | regenerated, 57 findings before and after; `--check` matches. Sources 108 to 110 (`render/MeshCache.cpp`, `recipe/Variants.cpp`) |
| `tools/gate.sh push` | not run; batched into the pass after the last plan |
| In-game checkpoint | not run; batched |

The 1200-line target for `Edits.cpp` is not reachable from this plan's
moves. The traversal took 408 lines out and `RenameInExpression` 46; the
deferred `EditChecks` split is about 105 more, which would leave roughly
1545. What remains is what the plan says `Edits.cpp` keeps: about 890 lines
of `Edit` overloads and 210 of `DescribeVisitor`. Splitting those is a
design question (one file per subject area, or a table), not a move, and
belongs in a follow-up.

### In-game checkpoint (pending, batched)

Load a save, render a stack with an animated layer, a ripple source and a
material-clusters source, and confirm all three draw as before. Log lines:
the `TextureLab` pass creation lines, and any `Failed` report from
`RenderTargetPool`. A silent constant-buffer size mismatch is what a header
split in D3D constant layouts would hide, so the three sources are the
point of the check.

Covers critique recommendations 5 (the render-to-engine include, the
duplicated layer table) and 7 (split `Edits.cpp`, hoist the GPU constant
structs). Runs after Plan B so the moves out of `Edits.cpp` are pure moves.

## UI rework impact

- **Safe now:** C1 (`MeshReader`), C2 (layers script, `run-native.sh`; the
  UI wave's tests under `tests/studio/` are auto-discovered and unaffected),
  C4 (render headers), C5 step 2 (`Recipe.cpp` split).
- **Coordinate, and do it first:** C3 steps 1 and 2, moving the traversal
  templates to `src/recipe/Visit.h` and `RenameInExpression` to
  `Expression.h`. UI slice 2B says "extend existing reference traversal with
  owning property locations" and 3C says "add source-location support to the
  existing parser". If the move lands before 2B and 3C start, they extend a
  published recipe-layer header instead of the anonymous namespace of
  `Edits.cpp`, which is what the UI plan's reuse audit asks for. If either
  has started, hand the move to that agent. `Expression.h` is also under
  the expression cleanup handoff; agree the move with that owner in the
  same message.
- **Defer:** C3 step 3 (`EditChecks` split) until 2B is done; it is a pure
  move and costs nothing to wait. C5 step 1 (`Page.h` deletion): the UI
  wave's `Workspace.h` and `RecipeActions.h` take `menu/Frame.h`, so `Page.h`
  is still vestigial, but `Fields.cpp` is in slice 2A's seam. Delete it when
  2A is done.
- **Layers script:** `menu/` may include everything, so the UI's new menu
  files pass. `studio/` may not include `menu/` or `engine/`; the UI plan's
  own rule ("engine-free decisions in `studio/`, drawing stays in `menu/`")
  is the same rule, so the script also guards the UI work. Tell the UI owner
  it exists.

## Findings addressed

- `src/render/Compositor.h:4` and `src/render/CompositorSource.cpp:4`
  include `engine/MeshReader.h`. These are the only two includers. Every
  render translation unit transitively pulls the engine adapter. `MeshReader`
  reads `RE::BSGeometry` into `MeshData`; nothing about it is manager or
  event logic.
- `tests/run-native.sh:20-26` hand-maintains a module dependency table that
  duplicates the CMake globs at `CMakeLists.txt:80-85` and already disagrees
  with the tree (it omitted studio's engine edge, which Plan B removes).
- `src/studio/Edits.cpp` (1990 lines) holds four concerns: sixty `Edit`
  overloads (`:273-1159`); reference validation (`:104-207`: `CheckText`,
  `CheckCurveText`, `CheckCtx`, `CheckScalarRef` and siblings); a recipe
  traversal toolkit (`:1159-1544`: `ForEachParam`, `VisitSignalParams`,
  `VisitSourceParams`, `VisitSurfaceParams`, `RefVisitor`,
  `ForEachImageRef`) in an anonymous namespace, unreachable by anyone else;
  and `RenameInExpression` (`:1918`, declared in `Edits.h:313-316`), which
  is expression-text surgery.
- `src/render/RuntimeTexturesLab.cpp:19-61` and
  `src/render/RuntimeTexturesPass.cpp:44-85` define byte-identical
  `ProgramConstants`, `RippleConstants`, `ClassifyConstants` and `Constants`
  with duplicated `static_assert`s. `Failed()` and `DataOf()` are copied into
  all three `RuntimeTextures*.cpp` files and `Failed()` again in
  `RenderTargetPool.cpp:13`.
- `src/studio/Page.h:12` is vestigial: no file under `src/menu/` uses it and
  its only consumer is `src/studio/Fields.cpp:389`. `src/menu/Frame.h:10` is
  the real per-frame bundle. Both carry an `ActorOf` accessor.
- `src/recipe/Recipe.cpp:73-215` is a leftovers file: variant application
  and the `IsAnimated` overloads.
- `src/render/Compositor.h:187`: `MeshCache` is mesh residency, not
  compositing, implemented in `CompositorBake.cpp:66`.
- `src/engine/Manager.h:27-154` is a god class, and the eight-file split is
  uneven (34 lines to 826). Recorded here; not in this plan. See Out of scope.

## Decisions

- `MeshReader` moves to `src/render/`. It is engine-facing, so it stays out
  of the native library, and render is its only consumer.
- `run-native.sh` compiles every native directory into every suite. The
  per-source object cache already keys on the source path, so the cost is
  link time only. The dependency table is deleted. The intended layer graph
  lives in one script, `tools/layers.sh`, which the push gate runs.
- The traversal toolkit becomes `src/recipe/Visit.h`. It walks `Recipe`
  parameters; that is a recipe concern, and templates live in headers.
  `RenameInExpression` moves to `src/recipe/Expression.h` and `.cpp`, where
  `REQUIREMENTS.md` already says the tokeniser for the editor lives.
- Reference validation becomes `src/studio/EditChecks.h` and `.cpp`.
- GPU constant layouts become `src/render/ShaderConstants.h`. `Failed` and
  `DataOf` become `src/render/D3DResult.h`.
- `Page.h` is deleted; `Fields.cpp` takes what it needs from `Frame.h`'s
  studio-side equivalent or a two-field record local to `Fields.h`. Read
  `Fields.cpp:389` first to see which members it uses.
- `Recipe.cpp` splits: variant application goes to `src/recipe/Variants.cpp`
  with its declarations grouped in `Recipe.h`; `IsAnimated` overloads go to
  `Vocabulary.cpp`, which already holds the predicates over recipe rows.
- `MeshCache` moves to `src/render/MeshCache.h` and `.cpp`.

## Steps

### C1: the render-to-engine edge

1. `git mv src/engine/MeshReader.h src/render/MeshReader.h` and the `.cpp`.
   Fix the two includes. Grep for `ReadMesh`, `IdentityOf`, `CompareWithGpu`,
   `NodeBindPosition`, `ToRootSpace` across `src/engine/` and `src/menu/` to
   confirm no other caller; if one exists, it includes the render header,
   which is the correct direction. Run `tools/compile-db.sh`.
2. Check whether `Compositor.h` needs the header at all or only
   `CompositorSource.cpp` and `MeshCache` do. Include it only where a
   declaration is used.

### C2: one layer graph

1. Write `tools/layers.sh`. It encodes the intended graph from
   `REQUIREMENTS.md:126-141`:
   - `recipe` includes only `Core.h` and its own directory.
   - `mesh` may include `recipe`.
   - `planners` may include `recipe`, `mesh`.
   - `diagnostics` includes only `Core.h` and its own directory.
   - `studio` may include `recipe`, `mesh`, `planners`, `diagnostics`.
   - `render` may include all of the above plus `PCH.h`; not `engine`, not
     `menu`.
   - `engine` may include all of the above plus `render`; not `menu`.
   - `menu` may include everything.
   It greps `#include "` lines per directory, excluding `_old`, `extern` and
   `cs`, and prints every edge outside the table with file and line. Exit
   non-zero on any. Run it; whatever it prints beyond the `MeshReader` edge
   and the `Snapshot.h` edge (fixed in Plan B) is a finding: fix it in this
   plan if it is a move, or list it in the Status block if it needs design.
2. Add `tools/layers.sh` to the push stage of `tools/gate.sh`. Replace the
   purity grep Plan B added with this script.
3. In `tests/run-native.sh`, delete `MODULE_DEPS`; set `MODULES=(recipe mesh
   planners studio diagnostics)` and collect sources from all of them for
   every suite. Keep the auto-discovery of `tests/<mod>/*_tests.cpp`. Move
   the engine and settings suites into the same loop by listing their extra
   sources in a small table at the top rather than five hand-written
   `build_and_run` lines, so a new engine-free engine unit is one line.

### C3: split `Edits.cpp`

1. Create `src/recipe/Visit.h`. Move the six traversal templates there
   verbatim, in the `BetterEnchantmentEffects` root namespace. They must not
   depend on studio types; if one does, that dependency is a studio concern
   and stays behind as a thin wrapper.
2. Move `RenameInExpression` to `Expression.h` and `Expression.cpp`. Move
   `tests/studio/expressionrename_tests.cpp` to
   `tests/recipe/expressionrename_tests.cpp`.
3. Create `src/studio/EditChecks.h` and `.cpp` with the reference-validation
   functions from `Edits.cpp:104-207`. Declare them in the header, types
   first (`CheckCtx`), then functions.
4. `Edits.cpp` keeps the `Edit` overloads, `Apply`, `PrepareEdits` and the
   `*Where` helpers. Target under 1200 lines; report the final count.
5. Run `tools/compile-db.sh`; regenerate the tidy baseline if function-size
   findings moved between files (they will; the counts should not grow).

### C4: render headers

1. Create `src/render/ShaderConstants.h` holding the four constant structs
   and their `static_assert`s once. Rename the bare `Constants` to
   `LayerConstants`, since that is what its members describe. Both
   `RuntimeTexturesLab.cpp` and `RuntimeTexturesPass.cpp` include it and
   drop their copies.
2. Create `src/render/D3DResult.h` with `constexpr bool Failed(std::int32_t)
   noexcept` and `DataOf`. Replace the four copies.
3. Move `MeshCache` to `src/render/MeshCache.h` and `.cpp`.
4. Check `Compositor.h:43` and `:104`: `RenderedMask` is forward-declared to
   break a cycle with `PreparedMask`. If moving `MeshCache` out and the
   `Prepared*` records into `src/render/Prepared.h` removes the cycle, do it;
   if not, leave the forward declaration and say so in the Status block.

### C5: small removals

1. Delete `src/studio/Page.h` after rehoming what `Fields.cpp:389` uses.
2. Split `Recipe.cpp` as decided.
3. Delete `src/engine/ManagerShared.h` and `.cpp` after Plan D moves its
   four functions (Plan D owns that rename; if C runs first, leave them).

## Acceptance

- `tools/layers.sh` exits zero.
- `tests/run-native.sh` has no dependency table and passes; `SUITE=studio`
  still runs only studio suites.
- `grep -rn 'struct alignas(16)' src/render` shows each constant struct
  once.
- `grep -rn 'Failed(std::int32_t' src/render` shows one definition.
- `src/studio/Page.h` does not exist. `src/render/MeshReader.h` exists;
  `src/engine/MeshReader.h` does not.
- `wc -l src/studio/Edits.cpp` is under 1200.
- Gate push green, tidy baseline regenerated with the same or fewer
  findings; record the before and after counts.

## In-game checkpoint

Only moves and header splits touched render and engine, but a header split
in D3D constant layouts is exactly where a silent size mismatch would hide.
Ask the user to load a save, render a stack with an animated layer, a ripple
source and a material-clusters source, and confirm all three draw as before.
Log lines: the `TextureLab` pass creation lines and any `Failed` reports
from `RenderTargetPool`.

## Out of scope

`Manager` stays a singleton with eight implementation files. Decomposing it
means deciding which of actor lifecycle, recipe matching, placement, tick,
lights, inspection and snapshot publication become free functions over
`LiveActor`, and each step needs its own in-game verification. Write it up
as a follow-up plan after the six critique plans are done, using
`src/engine/ManagerShared.h:1-15` (free functions over `LiveActor`) as the
pattern and `ManagerApply.cpp:1-436` (the anonymous-namespace helper library)
as the first extraction. The same follow-up covers `GetSettings()` reading a
file-scope global at `src/SettingsFile.cpp:126` and `Manager::Tick` reaching
for `editor_.CurrentView()` and `TextureLab::GetSingleton()` while taking
settings explicitly.
