Status: active (2026-10-12) on branch `pure-tick`. It designs stage 2 of `docs/plans/edge-fixes-2026-10-12.md`. D3 and D4 are decided as recommended; D5 is decided as (a), signal state carries across the swap. Done: the full native suite and the Windows build pass, and an in-game run on 2026-10-12 (trace `...-1791820872560376-2.jsonl`) restaged and swapped 62 times with no first draw and 0 resource disagreements; 2 edits that changed a shell binding fell back to a reinstall, as designed. Beyond the design, a restage while restaging restages again, and the engine falls back by retiring and installing in the same command (`ReinstallFor`), so the core needs no refusal event.

# Stage 2: restage a render instead of retiring it

## Context

A **reinstall** replaces an actor's render. Today it runs in two steps:

1. The core's `Change` (`orchestration/Sync.cpp`) issues `Retire{actor}`. The engine (`RetireActorResources`) restores every material, detaches every shell, removes every light and drops the `RenderInstance`.
2. The core issues `Install{actor}`. The engine (`InstallLiveActor`, `engine/ManagerApply.cpp:1281`) reads the pieces, binds the materials and shells (`BindSurfaces`, `:1199`), plans the render, places the lights and starts the first draw.

The bindings are installed before the plan. While the phase is `Planning` and then `Drawing`, `TickActorFor` (`orchestration/Lifecycle.cpp:563`) conceals the slots and hides the shells and lights, so the armor shows the vanilla look.

Example: one studio value edit on a 532-step recipe shows the vanilla look for the 2.7 s plan plus about 1 s of first draw.

A **resize** already avoids a blank. The actor keeps its published `RenderInstance` and builds a second one at the new level in `LiveActor::resize` (`ResizeInFlight`, `engine/LiveActor.h:103`). The second instance holds its own copy of every input, so it draws without the bindings (`engine/ActorResize.cpp:191`). `SwapResize` (`FinishResize`, `ActorResize.cpp:232`) moves the new stacks into place in one tick. The pieces, material bindings, shells, lights and instances are shared and never touched.

## Findings that shape the design

| # | Fact | Evidence |
|---|---|---|
| F1 | The first draw does not read the material binding. "The real filter and base" are the view's `LayerFilter` and the preceding stack in the slot chain. | `DrawFirstSlots`, `RenderSlotChain`, `RenderMergedChain` (`engine/ManagerTick.cpp:256-353, 485-511`) call no `TargetFor`. |
| F2 | Two material bindings on one geometry are unsound. With `uniqueMaterial` (the default) the second install copies the current material, which carries the first binding's writes, and the first binding then loses ownership and skips its restore. Without it, the second writer records the first writer's texture as its original. | `MaterialBinding::Install` (`render/Binding.cpp:476-491`), `BeginWrite` (`:197-214`), `Attached` (`render/PBRMaterial.cpp:57`). |
| F3 | A shell attaches visibly at creation, and a second shell clone on one geometry, with its own skin palette lease, is untested. A second `LightBinding` doubles the actor's lights. | `ShellBinding::Create` (`render/Shell.cpp:333-353`), `LightBinding` (`render/Light.cpp:221`). |
| F4 | `GeometryInputs` (the material's diffuse, normal, RMAOS and displacement) are captured when the pieces are read, before the binding. A new install read while the old binding is installed would capture the old render's written targets as its originals. | `MaterialInputs::From` (`render/CompositorSource.cpp:18-28`), `LivePiecesOf` (`ManagerApply.cpp:141-190`). |
| F5 | A studio edit, Save, Revert, a view toggle and a setting reapply keep every geometry: a model token is the `NiAVObject*`, and nothing in the scene changes. An equip changes the geometry set, and a 3D rebuild replaces every node, so the old render has nothing left to show. | `ChangeAndRebuildActors` (`ManagerApplication.cpp:174-201`), `ModelFor` (`ActorObservation.cpp:112-126`). |

F2 and F3 rule out building a full second `LiveActor` beside the first. F1 and F4 show that the resize's way (a second render instance that shares the pieces and bindings) carries over.

## Design: restage like a resize

A **restage** builds new instances, placements, stacks and a new `RenderInstance` beside the published ones, keeps the pieces, bindings, shells and lights, and swaps in one tick when the new render is drawn. It applies when the edit leaves the **binding plan** unchanged: the same geometries take a material binding, the same geometries take a shell, and the shell settings are equal. Anything else falls back to retire-then-install, as today.

### Core

| Item | Shape |
|---|---|
| Phase | `Restaging{Shown shown; RestageStep step}`, where `RestageStep` is `std::variant<RestagePlanning{PlanJobId job}, RestageDrawing{UnpublishedDraw draw}>`. The actor stays published at `shown.level` the whole time. |
| Command | `Restage{actor, facts, planned, bounds}` replaces `Retire` + `Install` in `Change` when the record is `Shown` and the stale reason is a reinstall with unchanged geometries (F5). |
| Events | `Restaged{total, levelCosts}` (the engine built and attached the new instance; the first draw starts), `RestageAwaited{job}`, `RestageRefused{reason}` (the binding plan differs, or the budget refused), `RestageSwapped`, `RestageNotSwapped` (a claim took a geometry). |
| Swap | `SwapRestage{actor}`, issued by the same rule as `SwapResize`: drawn, out of view, or 60 ticks (`SwapDue`). |
| Fallback | On `RestageRefused` or `RestageNotSwapped`, the core issues `Retire` + `Install` on the next tick. |
| `TickActorFor` | `Restaging` acts like `Shown`: draw the stacks, publish, no concealment. The prerender order includes a `RestageDrawing` actor (rename item: prerender, backlog). |
| Budget | The staged render is charged like `RunningResizeBytes` while it exists, and the old render's bytes are released at the swap (D3). |
| Plan jobs | Stage 1's `CancelPlanJob` covers `RestagePlanning`; a second restage of the same actor cancels the first one's job and restarts. |

### Engine

| Item | Shape |
|---|---|
| Staged state | `LiveActor::restage: std::optional<RestageInFlight>`, holding the new `instances`, `placements`, `stackPlan`, `mergedSlots` and `RenderInstance`. The published fields stay untouched until the swap. |
| `RunRestage` | Rebuilds the surface plan (`PlanSurfaces`) from the new match and compares its binding plan with the published one. If it differs: `RestageRefused`. Otherwise it builds the render request from the **existing** bound surfaces and the **existing** pieces' `GeometryInputs` (so F4 cannot occur), and plans or awaits as an install does. |
| Draw | `DrawUnpublished` draws a `RestageInFlight` like a `ResizeInFlight`. |
| `RunSwapRestage` | Checks the stacks still match their geometries (as `StacksMatch`), moves the staged fields into the published ones, re-points `inputs.render`, writes every slot back to its original and lets the next `TickActor` publish, as `FinishResize` does. The old instances' animation times carry over through `carriedTimes_`, as on a reinstall. |
| Ledger | The application tokens stay pending on the actor and finish at the swap. |

### Triggers

| Trigger | Today | After stage 2 |
|---|---|---|
| Studio value edit, gesture step, Save, Revert, undo | Retire + install, blank | Restage, no blank |
| A studio edit that adds or removes an output on a new geometry, or changes shell settings | Retire + install, blank | Restage refused, then retire + install, blank |
| View toggles, setting reapply | Retire + install, blank | Restage (stage 3 removes most of these rebuilds anyway) |
| Equip of one piece | Retire + install, blank | Unchanged; stage 4 |
| 3D rebuild | Retire + install, blank | Unchanged; the old nodes are gone (F5) |

## Signal state across the swap (D5)

A recipe's **signal state** is the memory of its stateful signal rows, held in `SignalState` (`recipe/Signals.h`): a wave's phase, a trigger's live firings and counts, a hold's held value, a counter's and an accumulator's value, and the previous value of a rate or a smooth. A new instance starts with empty state today, so a restage or a reinstall restarts every wave, drops every live firing and zeroes every counter. `carriedTimes_` carries only the animation clock.

Stateful operations exist only as named signal rows (`RecipeGraph::NameOf`, `recipe/RecipeGraphLowering.cpp`), so the state can be matched across an edit by name:

- `SignalState` exports a **signal memory**: one entry per stateful row, keyed by the row's name, holding the row's state.
- A new `SignalState` adopts a signal memory: each entry whose name exists in the new graph with the same operation kind replaces the fresh state. An entry with no such row, or with another kind, is dropped. A new row starts fresh.
- An edit to a row's parameters keeps its state (a wave keeps its phase at its new frequency). An edit that renames a row or changes its kind starts it fresh.
- The restage builds its instances from the published instances' memories, matched by recipe, enchantment and effect scope (the `CarriedTimeKey` fields). The plain reinstall path (equip, 3D rebuild) carries the memory through the same store as the animation clock, so stage 2 fixes the restart there too.

The memory is plain data and the matching is engine-free, so `tests/recipe/signals_tests.cpp` covers it: a kept row keeps its state, a renamed row and a changed kind start fresh, a removed row is dropped, and a trigger keeps its live firings.

## Tests

- Core (`tests/orchestration/sync_tests.cpp`, `lifecycle_tests.cpp`, `phasechanges_tests.cpp`): a reinstall of a `Shown` actor issues `Restage`, not `Retire`; `Restaging` publishes in `TickActorFor`; the swap follows `SwapDue`; `RestageRefused` and `RestageNotSwapped` fall back to retire and install; a restage while restaging cancels the first plan job.
- Simulator (`tests/orchestration/sim`): the shell models a staged render; invariants for "one staged render per actor", "a restaging actor is published", and agreement rows for the new phase (`Agreement.cpp`, `FindingsFor`).
- Engine-free planner: the binding-plan comparison (`planners/InstallProgram`), with cases for an added output, a removed geometry and changed shell settings.
- In game: the stage 1 drag and edit run, now expecting no vanilla look at any point; a recipe edit that adds a shell, expecting the fallback; the full suite against the 2026-10-10 baseline.

## Size

About the size of the R4 resize work: one new phase and its machine, two commands, five events, one engine in-flight record and two runners, agreement rows and simulator support. The engine part reuses `ActorResize.cpp` almost line for line.

## Decisions for the user

| # | Question | Options | Recommendation |
|---|---|---|---|
| D3 (decided: a) | Which actors restage? | (a) Every actor; the staged render must be admitted by the budget like a step up, and a refusal falls back to retire and install. (b) The player and the inspected actor only. | (a). The budget already decides this for resizes, and an NPC without headroom only loses what it loses today. |
| D4 (decided: a) | What does a restage do when the binding plan differs? | (a) Fall back to retire and install (a blank, as today). (b) Hand the bindings over at the swap: move each kept `MaterialBinding` and re-create only the changed shells. | (a) first. (b) touches the scene-graph code (F2, F3) and is worth it only if added outputs or shell changes turn out to be common edits. |
| D5 | Does the staged render start from the old render's signal state? | (a) Yes: copy the signal and trigger state into the new instances, so an animation continues across the swap. (b) No: new instances start fresh, and `carriedTimes_` carries only the animation clock, as on a reinstall today. | Decided: (a), matched by row name and kind (section "Signal state across the swap"). |
