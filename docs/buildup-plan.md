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

The shape agent also produces a complete `.cpp` ownership map: every
function declared in the header maps to exactly one owning `.cpp` before any
fill starts. A function with no assigned home is the failure this rule
prevents. Wave 1 shipped without a home for the recipe core — the
`Words.h`-driven vocabulary, the atomic text forms, `Find*`, `IsAnimated` —
and it surfaced as undefined symbols at merge, forcing `Recipe.cpp` to be
written on the spot as a six-responsibility catch-all that then had to be
split into `Vocabulary.cpp` and `Resolve.cpp`. A downstream module's shape
(studio's `Reduce`, the view-model builders, the planners) must not leave a
linchpin file unowned.

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

## The per-cluster loop: dispatch, merge, inspect, refactor+reduce

A cluster is not done when its fill agents return. Wave 1 proved the fills
produce a working-but-rough tree, not final code; four stages carry it the
rest of the way. Every cluster in every wave runs the same loop. The
per-wave sections below give each cluster's stage 1; the four stages here
apply to all of them. The two clusters in a wave run the loop
independently; the barrier is the wave edge.

**1. Dispatch.** Shape agent lands the frozen header and the `.cpp`
ownership map; fill agents then implement, disjoint by file, each in its own
worktree. Barrier at the shape step only — fills run free once the header is
in. This is the stage the per-wave breakdowns describe.

**2. Merge.** Collect each worktree's owned files back to the integration
branch. Reconcile any file two agents both edited — in wave 1 that was
`REFERENCE.md`, where several fills each added their module's heading. Then
run a link / undefined-symbol check across the merged tree: it catches any
function the shape step failed to assign an owner. Wave 1's recipe core
surfaced exactly here, as undefined symbols, and `Recipe.cpp` was written at
merge to satisfy them (which the refactor stage then split). Only once the
tree links do you build and run the suite.

**3. Inspect.** Establish the state of the merged cluster against the gates:

- Zero-warning native build (`./build.sh Release -j 4`; more jobs OOM-kill
  WSL).
- Full `tests/run-native.sh`.
- `tools/tidy.sh --force` then `tools/tidy-baseline.sh --check` against
  `docs/wip/tidy-baseline.txt`. `tidy.sh` reads
  `build/clangd/compile_commands.json`; run `tools/compile-db.sh` first if a
  source file was added or removed this cluster.
- Triage the findings. Separate the accepted-by-policy ones — the flat
  per-texel / per-tick switches and the arity-floor generic helpers
  (`NamedRows`, `CheckVector`), recorded in `docs/wip/clusters.md` and
  carried in the baseline — from the actionable ones the refactor stage will
  clear. A finding is accepted only if `clusters.md` says so.

Formatting is gated earlier and separately: `.githooks/pre-commit` and the
Claude `PreToolUse` gate in `.claude/settings.json` both run
`tools/format.sh --check` on staged sources, so no unformatted commit
reaches merge. A module is done, per `REQUIREMENTS.md`, only when four things
hold together: zero-warning build, green native suite, tidy baseline showing
only what the module meant to add, and the frozen counterpart gone from the
build and from `src/_old`. It is not done while both copies are in the tree.

**4. Refactor+reduce.** Reduce what the fills left rough, using the
`docs/conventions.md` patterns (each cites a real helper by name — grep
before writing a variant):

- Split any catch-all `.cpp` by concern. Wave 1 split `Recipe.cpp` into
  `Vocabulary.cpp` (the `Words.h` vocabulary and atomic forms), `Resolve.cpp`
  (piece matching), and `Recipe.cpp` (record accessors, variants,
  `IsAnimated`).
- Reduce a high-complexity function by per-kind dispatch — a function per
  variant alternative behind an enum-ordered table with a count assert
  (`kSignalParsers` / `kSourceParsers`, so `SignalFrom` / `SourceFrom` index
  a table instead of enumerating kinds inline) — or by named phases for a
  pipeline (`SignalGraph::Compile` became `ParseCurves` → `RegisterNodes` →
  `ResolveRefs` → `OrderNodes` → `InferTypes` → `CheckReferenceTypes` →
  `PropagateInert`).
- Keep an exhaustive `Match` of tiny arms flat. Do not table a per-texel
  switch: `clusters.md` rejects it because the switch compiles to an inlined
  jump table and an indirection would cost more than it cleans.
- Lift any idiom that recurs across the cluster into the shared vocabulary
  (`Core.h` / `Recipe.h`) rather than duplicating it. Wave 1 lifted
  `Reporter` (the one diagnostic sink), the `Reader` / `Writer` binders,
  `ReadRows`, and `Recipe::Find*`.

Re-baseline (`tools/tidy.sh --force && tools/tidy-baseline.sh`) whenever the
reduction changes the accepted findings, so the baseline stays the exact set
the cluster meant to add. `conventions.md` now exists, so a fill built on the
established vocabulary arrives closer to its floor and this stage is smaller
than it was in wave 1 — but it is still a stage, not an afterthought.

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

Both clusters run the full loop after these fills merge; the recipe core is
the worked example throughout. The layer inversions are resolved by
construction — `ParsePresets` is not written here (it belongs to studio/),
and the importer's parser lives in `recipe/Importer`. Done is the four-part
criterion from the loop's inspect stage.

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

This wave produces the first DLL. It runs the loop like any other cluster,
but the inspect stage has no native suite to run — correctness rides the
wave-5 checkpoint — so done here is the zero-warning DLL build plus a clean
tidy baseline. The signal that the split held: each engine/render `.cpp` is
much smaller than its frozen counterpart, and the logic lives in wave-2
tests.

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
  worktree so parallel writes never collide. The merge, inspect, and
  refactor+reduce stages of the loop reconcile the owned files back onto the
  integration branch and carry the cluster to done.
- **Barrier only at the shape step and at each wave edge.** Within a wave,
  fill agents run free once their cluster's header is in.
- **Each agent brief carries:** its exclusive file set, the frozen
  counterpart(s) to read for reference (never to import), `docs/conventions.md`
  (the canon patterns to build on, so a fill does not regrow the wave-1
  monsters and arrives near its floor), the relevant `docs/wip/clusters.md`
  (fix-as-you-write) and `docs/wip/deletions.md` (do-not-write, and the
  "looked dead, keep" list) entries, the three rules from `CLAUDE.md`, and
  its exact verification command.
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
