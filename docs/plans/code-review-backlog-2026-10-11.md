Status: active (2026-10-11) on branch `pure-tick`. The findings of the user's code walk of the plugin (chapter 1, startup to one tick; chapter 2, the tick core through `RunSync`), collected for review in bulk. Nothing here is started unless marked; items that need the user's decision say so. Done and not listed: the budget and sync restructure (c7d4d4c), the render-phase state machine (5b1042c), the trace cleanup (e30c67b), `FindResize` (020c6f7), and the 2026-10-11 batch (the rename table, `ActorVisibility` and the nested `BudgetActor`, the traced `ApplyPhaseChange` in `Output.h`, `TickFacts` without `frameSyncs`, the install-order test, `Budget.cpp` in call-flow order).

# Code review backlog

## How the walk works

The user reads the code in an nvim pane that the `walk` skill drives. Each finding goes on this list unless the user asks for it at once. The style rules the findings apply are in the user's preferences: data-driven shapes over long imperative sequences, early returns so that the core logic is never inside an `if`, blank lines between the phases of a function, names short enough that declarations do not wrap, and a function named as a verb whose object is its first argument.

## Decisions for the user

| Item | Question | Recommendation |
|---|---|---|
| Install bounds | `InstallBoundsOf` returns the default bounds (full level, **no headroom**) for an NPC with no known level when `ScreenResolution` is off or the actor is not measured. Those installs ignore the render target budget, also under pressure. | Full level within the headroom: `{kFull, HeadroomOf(...)}`. |
| Application ledger | `docs/plans/application-ledger-2026-10-10.md`, Q1: accept the behaviour changes a to e? | Yes. |
| Staged tick output | Build the tick output in stages, or move it between stages as distinct types. The user is not convinced by the typed-changes design (each step returns a change record that one `Applied` function writes). | Discuss before a plan is drafted. |

## Next: the core files

1. **Call-flow order in `Sync.cpp` and `Lifecycle.cpp`.** Each public function first, its helpers beneath it in the order it calls them, and the anonymous namespace opening with forward declarations of the helpers. `Budget.cpp` has this order.
2. **One `Report…` function per reported event in `Sync.cpp` and `Lifecycle.cpp`.** Each writes the log line and the trace together, as the budget's reporters do. This is pass 2 of the trace cleanup.
3. **Guard, apply and report in `Sync.cpp` and `Lifecycle.cpp`.** Split the functions that check, change the world and report in one body, as `StepDownForBudget` does.

## Names still open

- `ResizeDraw::kOutOfView` and the `outOfViewReleaseDelay` setting still say "out of view" for "not shown".
- The test helpers `InView` and `OutOfView` build a `Sighting`; `Shown` and `Hidden` would match the field.

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
