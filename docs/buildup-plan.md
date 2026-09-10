# Parallel buildup plan

Execution scaffolding, not canon. `REQUIREMENTS.md` governs; this file only
says how the new tree gets written by many agents at once. Delete it when
the buildup is done.

## Two rules that shape the parallelism

**Shape before fill.** A module's header — its data types and complete
function signatures — lands first, in one agent, and is frozen. Only then
do the agents that implement functions over it start, each owning disjoint
`.cpp` files. The shared header cannot be a collision point if it is
settled before anyone writes against it. This is "data first, then types,
then functions" turned into a scheduling rule.

**The new tree is the only build.** `src/_old` is never compiled, never on
the include path, and its tests never run. It stays on disk purely as the
behaviour oracle: agents read it and diff against it by eye, they do not
import it. Because it is off the include path, an old-style
`#include "Recipe.h"` fails to compile — the break enforces itself. Every
new include is directory-qualified (`#include "recipe/Recipe.h"`). All
tests are written fresh per module; the only things carried across are data
— `schema/recipe.schema.json` and the canonical recipe files — because
those are the format contract, not old test code.

There is no rebuildable plugin DLL until wave 3 produces `engine/` and
`render/`. The binary already installed in MO2 keeps the plugin playable
through the native waves; we simply stop rebuilding it. `CMakeLists.txt`
drops `src/_old` at wave 0 and builds only `src/` from then on.

## The dependency graph

```
wave 0  foundation      Core Identity PCH Settings SettingsFile; drop src/_old from CMake
wave 1  recipe/  mesh/   (parallel, pure)
wave 2  studio/  planners (parallel, pure)
wave 3  engine/  render/  (parallel, thin adapters — not native-testable; first DLL)
wave 4  menu/             (parallel by page)
wave 5  integration       build, install, one game checkpoint, delete src/_old
```

Waves 1–2 carry the correctness weight and run entirely native. Wave 3 is
the irreducible engine surface and the first time a DLL exists. The plan
front-loads every pure planner into wave 2 so wave 3 is thin wrappers,
which is where the buildup's risk is smallest.

## Wave 0 — foundation (1 agent)

Owns `src/main.cpp` stub, `Core.h`, `Identity.h`, `PCH.h`,
`Settings.{h,cpp}`, `SettingsFile.{h,cpp}`, the `CMakeLists.txt` rewrite
(drop `src/_old` entirely; a native/host target over `src/` and a DLL
target that is empty until wave 3), and the `tests/` directory mirror.
Everything downstream includes these. Done when the native target builds
empty and `tests/run-native.sh` runs zero suites green.

## Wave 1 — pure core (2 clusters, ~9 agents)

Both clusters run concurrently; directories are disjoint.

**recipe/** — shape agent lands `Recipe.h`, `Words.h`, `Expression.h`,
`Signals.h`, `Merge.h`, `Importer.h` with full signatures and native test
skeletons. It is the critical path: everything above depends on `Recipe.h`.
Format 1 is frozen, so it reads `src/_old/Recipe.h` and
`schema/recipe.schema.json` for the target shape and writes the header
fresh. Then fill agents, disjoint by file:
- `Recipe.cpp` / `Vocabulary.cpp` / `Resolve.cpp` — the recipe core: record
  accessors, variants and `IsAnimated` (`Recipe.cpp`); the format's static
  vocabulary and atomic text forms over `Words.h` (`Vocabulary.cpp`); piece
  matching and resolution (`Resolve.cpp`). Every symbol is declared in
  `Recipe.h`; these back it.
- `RecipeRead.cpp` / `RecipeWrite.cpp` — verified by round-trip against the
  canonical recipe files (byte-identical read-back).
- `Expression.cpp` — parser and evaluator, bounded depth/op/stack, fed
  garbage in tests.
- `Signals.cpp` — graph compile and per-tick evaluation behind the
  `SignalEnvironment` seam.
- `Merge.cpp` — matching and the slot/light plans (a pure planner reused by
  wave 3).
- `Importer.cpp` — vanilla effect shader to recipe, reproducing the frozen
  importer's output (read `src/_old/Importer.cpp` for the defaults).

**mesh/** — shape agent lands `Mesh.h`, `TextureSize.h`, `Islands.h`,
`MaterialClusters.h`, `MeshFacts.h`. Then fill agents: `Mesh.cpp` (decode +
bakes over decoded data), `Islands.cpp`, `MaterialClusters.cpp`,
`MeshFacts.cpp`. Written fresh; the frozen `Analysis.cpp` and `Mesh.cpp`
are read for the algorithms, not imported.

Per-module done: zero-warning native build, its suite passes, tidy shows
only what it added. The layer inversions are resolved by construction —
`ParsePresets` is not written here (it belongs to studio/), and the
importer's parser lives in `recipe/Importer`.

## Wave 2 — studio and the planners (2 clusters)

**studio/** (pure) — shape agent lands `Snapshot.h`, `View.h`, `Intent.h`,
`Forms.h`, and the record headers. Fill agents, disjoint by file:
`Edits.cpp` (apply-or-refuse, tested with a round-trip-and-undo property
per edit), `FieldCheck.cpp`, `MenuState.cpp` (the `Reduce` reducer), the
view-model builders (`Rows`/`Panels`/`Board`/`Selection`/`Names`),
`History`, and the mask editor (`Region`/`Presets`/`TermTemplates`/
`PaintSession`). Depends on recipe/ + mesh/ from wave 1.

**planners/** (pure) — the decision halves of the engine modules, written
and native-tested now so wave 3 holds no logic: the compositor's stack plan
(static-vs-animated classification, layer order, the cut at the highest
`replace`), the binding's install/restore diff, and the manager's decision
functions (match, place, retire) over an `ActorState` defined on opaque
handles rather than `RE::` pointers. Each is data-in/data-out with a native
test against fixtures. Merge from wave 1 is the first of these.

## Wave 3 — engine adapters (2 clusters, thin, not native-testable)

**engine/** — `EngineForms`, `Environment`, `Events`, `Hooks`,
`RecipeStore`, `MeshReader` (fetch only; decode is mesh/), and the
`Manager` shell with `ActorApply`/`ActorTick`/`RecipeCommands`/
`SnapshotBuild` wiring the wave-2 planners to real handles and the task
queue. One agent owns `Manager` and integrates last, since it is the point
the others meet.

**render/** — `PBRMaterial.h` (layout compile-asserted against
`src/cs/BSLightingShaderMaterialPBR.h`), `RuntimeTextures` (D3D pass
execution), `Compositor` execution over the wave-2 stack plan, `Binding`
writer over the wave-2 install/restore diff.

This wave produces the first DLL. Verification is a zero-warning build only;
correctness rides the wave-5 checkpoint. The signal that the split held:
each engine/render `.cpp` is much smaller than its frozen counterpart, and
the logic lives in wave-2 tests.

## Wave 4 — menu surface (parallel by page)

Shape agent lands `MenuWidgets` (every drawing mechanic) and `FormDraw`
first. Then one agent per page: `BoardPage`, `ContextRows`, `StackPanel`,
`ResourcePanels`, `PaintPanel`, `StudioPage`, plus `Menu` registration and
`Intents` collection. Each page reads the immutable snapshot and posts
intents; none touches engine state. Build-verified.

## Wave 5 — integration (1 owner, serial)

1. Full native suite; zero-warning Release build with `-j 4`;
   `./install.sh`.
2. One in-game checkpoint, aimed at exactly the surface native tests cannot
   reach: the material merge (two recipes on one geometry compose, no
   "dropping … replaced by another system"), a static and an animated
   stack, a light, a shell, and the studio editing a live recipe. Hand the
   user the log lines to watch.
3. Once the checkpoint passes, delete `src/_old`. It was reference only, so
   this is `rm -r`, not a build change — nothing has linked it since wave 0.

## Orchestration mechanics

- **Worktree isolation per agent.** Each agent works in its own git
  worktree so parallel writes never collide; an integration pass per wave
  merges the owned files back and runs the full native suite plus
  `tools/tidy-baseline.sh --check`.
- **Barrier only at the shape step and at each wave edge.** Within a wave,
  fill agents run free once their cluster's header is in.
- **Each agent brief carries:** its exclusive file set, the frozen
  counterpart(s) to read for reference (never to import), the relevant
  `docs/wip/clusters.md` (fix-as-you-write) and `docs/wip/deletions.md`
  (do-not-write, and the "looked dead, keep" list) entries, the three rules
  from `CLAUDE.md`, and its exact verification command.
- **Never dispatch two agents whose file sets overlap.** The directory
  split makes this automatic across clusters; within a cluster the
  file-per-agent rule enforces it.
- **Build discipline:** `./build.sh Release -j 4` only; never a build and
  clang-tidy at once; work inside `nix develop`.

## Where the risk sits

- `recipe/Recipe.h` is the critical path and every wave-2+ module waits on
  it. It is also the best-specified file in the tree (frozen header to read
  + frozen schema + canonical recipes), so the shape agent has an exact
  target.
- The planner extraction in wave 2 depends on `ActorState` being defined
  over opaque handles. If a planner needs a real `RE::` pointer to decide,
  the split is wrong and that logic belongs in the wave-3 shell.
- Wave 3 is the first integration of untested engine code and the first
  DLL. The mitigation is the whole shape of this plan: thin adapters, and
  every decision they make already green in a native test before the DLL
  exists.
