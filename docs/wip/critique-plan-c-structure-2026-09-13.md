# Plan C: layering and file structure — 2026-09-13

Status: not started.

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
