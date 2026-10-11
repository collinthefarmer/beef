Status: active (2026-10-11) on branch `pure-tick`. The findings of the user's code walk of the plugin (chapter 1, startup to one tick; chapter 2, the tick core through `RunSync`), collected for review in bulk. Nothing here is started unless marked; items that need the user's decision say so. Done during the walk and not listed: the budget and sync restructure (c7d4d4c), the render-phase state machine (5b1042c), the trace cleanup (e30c67b), `FindResize` (020c6f7).

# Code review backlog

## How the walk works

The user reads the code in an nvim pane that the `walk` skill drives. Each finding goes on this list unless the user asks for it at once. The style rules the findings apply are in the user's preferences: data-driven shapes over long imperative sequences, early returns so that the core logic is never inside an `if`, blank lines between the phases of a function, names short enough that declarations do not wrap, and a function named as a verb whose object is its first argument.

## Decisions for the user

| Item | Question | Recommendation |
|---|---|---|
| Install bounds | `InstallBoundOf` returns the default bounds (full level, **no headroom**) for an NPC with no known level when `ScreenResolution` is off or the actor is not measured. Those installs ignore the render target budget, also under pressure. | Full level within the headroom: `{kFull, HeadroomOf(...)}`. |
| Application ledger | `docs/plans/application-ledger-2026-10-10.md`, Q1: accept the behaviour changes a to e? | Yes. |
| Staged tick output | Build the tick output in stages, or move it between stages as distinct types. The user is not convinced by the typed-changes design (each step returns a change record that one `Applied` function writes). | Discuss before a plan is drafted. |

## Next: the core files

1. **Renames that waited for the phase merge.** Planner type `BlockedStepUp` becomes `WaitingStepUp`. Phase field `stepUpBlockedFrom` becomes `waitingForRoomFrom` (`planners/RenderBudget`, tests).
2. **One pass over `orchestration/Budget.cpp`, then `Sync.cpp` and `Lifecycle.cpp`:**
   - `BudgetActor` holds nested records: `visibility` (an `ActorVisibility`), `distance`, `level`, `resizing`, `costs` (`BudgetCosts` moves to `planners/RenderBudget.h`).
   - `ActorSight` becomes `ActorVisibility`. One `VisibilityOf` replaces `ActorSightOf` (Lifecycle) and `SightOf` (Budget). `WantedLevel(sight)` becomes `LevelFor(visibility)`: it can return a finer, the same or a coarser level, so it is not a step-up level.
   - `StepDownForBudget`, `ReportRadiusGrowth` (a `Match` with one reporter per growth result), `ShrinkBudgetRadius` and `MakeRoomForWaitingStepUp` split into guard, apply and report.
   - One `Report…` function and one record per reported event write the log line and the trace together. This is pass 2 of the trace cleanup.
   - Every `.cpp` in call-flow order: a public function, then its helpers beneath it in the order it calls them. The anonymous namespace opens with forward declarations of the helpers.
3. **Trace every phase change.** Phase changes that the budget makes (a step-down through `WantLevel`, a release through `ReleaseForBudget`) go through `ApplyPhaseChange` in `Budget.cpp`, which does not emit `actor_phase` or `actor_phase_ignored`. The lifecycle's `Apply` does. One `ApplyPhaseChange(output, actor, render, change, event)` in `orchestration/Output.h` would trace both. Trace counts in tests can change.
4. **`TickFacts`.** `frameSyncs` looks vestigial: since R4 the engine always passes `FrameSyncs{}`. Confirm and remove it. Group the fields by meaning: time and settings, observations, last tick's results, requests.
5. **The waiting-install queue ordering.** `InstallOrder` and `NextInstall` depend on `Precedes` being a strict weak ordering. Add a test that pins this.

## Renames (`tools/rename.py`)

| Today | Proposed | Note |
|---|---|---|
| `Sighting::inView` | `shown` | A draw policy: the frustum test, forced true for the player, with culling off and for the studio-inspected actor. Every consumer reads it that way. Drop the redundant player check in `Refreshed` (`Sync.cpp`). |
| `ActorRecord` | `TrackedActor` | 19 files. Matches the lookup `TrackedActor(world, id)`. |
| `TickFacts::memory` | `budgetSample` | Its type is `BudgetSample`. "budget" alone is `World::budget`. |
| `InstallBound`, `InstallBoundOf`, `bound` | `InstallBounds` {`finestLevel`, `headroom`}, `InstallBoundsOf`, `bounds` | The user's choice: it holds two bounds. |
| `Actors::Recheck` {`kNone`, `kWaiting`, `kStuck`} | `ModelLoad` {`kNone`, `kLoading`, `kLate`} | The engine reads it too. A late model is still retried every second, so "stuck" overstates. Rename `Timers::lastWaitingCheck` and `lastRecheck` to match `DueChecks::loadingModels` and `slowChecks`. |

## Cleanups

- **`Match` in place of `std::visit`.** 10 sites: `regression/Steps.cpp` (5), `regression/RunFile.cpp`, `regression/Run.cpp`, `Settings.cpp` (2), `engine/RegressionRun.cpp`. `std::visit` can throw `bad_variant_access`.
- **Two-variant `Match`.** `SameRenderValue` (`render/RenderInstance.cpp`). Read `TraitsOf` (`recipe/RecipeGraphLowering.cpp`) and `ExternalInputType` (`planners/RenderPlanLowering.cpp`) to see whether they branch on (kind, source) pairs.
- **`MaxBy`.** The best-element loops in `recipe/Resolve.cpp` and `planners/RecipeTextureCache.h` pick an element, not a value, so `MaxOf` does not fit them.
- **Test prose.** Some test messages and local variables still say "live" for the shown phase.

## Large plans, on hold

| Plan | Scope |
|---|---|
| `docs/plans/application-ledger-2026-10-10.md` | `ApplicationService` and `SessionQueue` shrink to a progress ledger (plain record, native-tested) and a session queue. Six hops of the stale path go. |
| `docs/plans/effects-runtime-2026-10-10.md` | `Manager` splits into named parts under a root, `EffectsRuntime`. A load resets each part from a declared table; the frame runs each part's work from a declared cadence table. |

The effects-runtime plan also answers these walk findings:

- `OnFrame` buries the hot path, `RunFrame`, under the one-time texture-lab start, the load gate and side duties.
- `RunFrame` does domain work (lab timings, texture sweeps, previews, equip second looks, gestures, animation expiry, mesh sweep, snapshot, regression) that belongs to the owners.
- About half of `RunTick` is instrumentation: bare scope blocks for `SectionTimer`, heartbeat sampling, the GPU-timed tick. The cadence table's `timed` column and self-timing steps remove it; the GPU-timed tick wraps `Execute` in one named call.

## To investigate

- **The equip second look.** On equip, `QueueEquipFinalize` marks the actor stale again 100 ms later. The reason ("the game replaces the worn mesh after the equip event") comes from the decompile summary and was never verified; REFERENCE states it as fact. Pending-model rechecks and `NiNodeUpdateEvent` may make it redundant. Test an equip run with it off, then keep it with the real reason or delete it, and correct the REFERENCE row.
- **Recovery fps.** After the frost and fight soak cases, frame rate recovered to only 0.49 to 0.82 of the baseline. Compare with a run from before R4.
