Status: active (2026-10-12) on branch `pure-tick`. D1 and D2 are decided as recommended. Stage 1 is done: native suites, the Windows build and an in-game edit and drag run on 2026-10-12 (trace `...-1791814658775234.jsonl`) passed. That run's recipe planned in about 30 ms, so it exercised the gesture settle hold but not a cancel or a full queue; a drag on a heavy recipe still has to show them. Stage 2 is done; its design and record are `docs/plans/edge-fixes-stage2-swap-2026-10-12.md`. Stage 3 is done except the per-setting reapply scope: in trace `...-1791835525705069.jsonl` eight mute and solo toggles rebuilt nothing, and the player's first install after a load with `VerifyRenderPlanCache` on was `hit_verified` with no mismatch. The unchanged-Save path is not yet seen in game. Stage 4 is designed in `docs/plans/edge-fixes-stage4-per-piece-2026-10-20.md` and waits for decisions D6 to D8. Stages 2 to 4 are scoped, not designed; D3 waits for the stage 2 design. The findings come from the component-edge audit in `docs/plans/code-review-backlog-2026-10-11.md`, section "Component edges (audit 2026-10-12)".

# Edge fixes: plan queue, swap, invalidation, per-piece install

## Context

A **render** is everything the plugin draws for one actor: the plan, the render instance with its targets, the material bindings, the shells and the lights. A change on the authoring side or in the game reaches the screen through these steps today:

1. Something marks the actor stale: an equip, a 3D rebuild, a studio edit (`ChangeAndRebuildActors`), a view toggle or a setting.
2. The tick core's `Change` (`orchestration/Sync.cpp`) issues `Retire{actor}` and then `Install{actor}`. The retire throws away the whole render.
3. `RenderPlanFor` (`engine/ManagerApply.cpp`) looks up the actor's plan in the plan cache. On a miss it submits a **plan job** to the plan worker: one thread, no cancel, 16 slots (`planners/JobQueue.h`, `kPlanJobCapacity`).
4. While the job runs, the actor's phase is `Planning`. `TickActorFor` (`orchestration/Lifecycle.cpp`) conceals the slots and hides the shells, so the armor shows the vanilla look.
5. The plan arrives. The **first draw** prerenders every stack within the per-frame budget, and then the effect is published.

Example. In the studio session of trace `...-1791768470721915-2.jsonl`, one value edit on `SUM_EnchArmor_RestorationMystheal_FXS` reinstalled the player's 4 recipes on 8 pieces. The plan missed the cache and took 2.7 s of worker time for 532 steps. The first draw took 25 ticks. The armor showed the vanilla look for 4 to 6 s. During a drag, each 100 ms gesture step queued one more 2.7 s job, and the plan for the final value attached 37.5 s after it was submitted.

The fixes come in four stages, in order of risk to the release:

| Stage | Fixes | Size |
|---|---|---|
| 1. Plan queue | The game-thread freeze when the queue is full; superseded jobs that block the wanted one; one rebuild per gesture step | Small |
| 2. Swap, do not blank | The vanilla look during every reinstall (edit, Save, Revert, view toggle, setting, equip, 3D rebuild) | Large |
| 3. Narrow invalidation | Plan cache cleared on load and on every "rebuild all"; reinstalls for view changes that are applied per tick; a new graph on an unchanged Save | Medium |
| 4. Per-piece install | One equip reinstalls the whole actor | Large |

## Stage 1: plan queue

### 1a. Cancel a plan job that no actor awaits

**Today.** A reinstall retires the actor while its phase is `Planning{job}`. Nothing tells the plan worker, so the job runs to the end. When its result arrives, `RecordJob` finds no actor awaiting it and drops it. In the drag trace, jobs 22 to 35 ran 2.5 to 3.0 s each for nothing.

**Change.** The core knows which actors await which job (`AwaitedJob(phase)`). When the core drops a render in `Planning{job}` (`RetireForChange`, `ForgetRetiredRender`) and no other tracked actor awaits `job`, it issues a new command `CancelPlanJob{job}`. Another actor can await the same job through a shared key (`PlanRoute::kShareJob`), so the check covers every actor, not only the retired one.

The engine runs `CancelPlanJob` as follows:

- `JobQueue::Cancel(JobId)` removes the job if it is still queued and returns whether it did. A running job is not interrupted; its result arrives and is dropped as today.
- `Closed(planJobs_, job)` removes the book entry, so no later install shares a cancelled job.
- A trace record `render_plan_job` with `result: cancelled`.

**Tests (native).**

- `tests/orchestration/sync_tests.cpp`: a reinstall of an actor in `Planning{7}` issues `CancelPlanJob{7}` after `Retire`; with a second actor in `Planning{7}`, it issues none.
- `tests/engine/jobqueue_tests.cpp`: `Cancel` removes a queued job, returns false for a running or unknown job, and lowers `Unfinished()`.
- `tests/engine/planjobs_tests.cpp`: a cancelled job is no longer open for its key.

### 1b. Never plan on the game thread when the queue is full

**Today.** When `Submit` refuses with `kFull`, `RenderPlanFor` falls through to `StoredFreshRenderPlan`, a synchronous plan on the game thread. With this recipe that is a 2.7 s freeze. The drag trace reached 16 unfinished jobs, one short of it. With 1a, a drag keeps at most one queued job per actor, but a crowd of NPCs entering view at once can still fill 16 slots.

**Change.** A refusal no longer falls through to a synchronous plan. The install is deferred and retried on a later tick. This needs decision D1.

**Tests (native).** `tests/planners/installprogram_tests.cpp` (or the file that tests `PlanRouteOf`): a refused submit gives the deferred route, never `kFreshStored`. A core test for the retry, as D1 decides.

### 1c. One rebuild in flight per gesture

**Today.** A drag applies its newest value every 100 ms (`kGestureApplyInterval`, `studio/Gesture.h`). Each application calls `ChangeAndRebuildActors`, which reinstalls every actor applying the recipe and starts a plan job. With 1a, the older jobs are cancelled, but each step still retires and reinstalls the actor.

**Change.** `ProcessGesture` (`engine/RecipeEditor.cpp`) applies a gesture step only when the previous step's application has settled: no actor in its scope is still `Planning` or in its first draw. Until then the gesture mailbox keeps only the newest value, which it already does. On release, the final value applies at once, as today. The editor asks the runtime through one query, for example `Manager::Settling(recipeID) -> bool`, read from the application ledger (`applications_.PendingFor`).

**Tests (native).** `tests/studio/gesture_tests.cpp`: a mailbox that is not allowed to apply keeps the newest value and applies it once allowed; a release applies at once.

### In-game check for stage 1

Run in the studio on the player with a heavy recipe (`SUM_EnchArmor_RestorationMystheal_FXS`):

1. Drag a value slider for about 5 s, then release.
2. Make three quick edits, then undo twice.

Look for these trace records (`tools/trace-report.py`):

- `render_plan_job` with `result: cancelled` during the drag;
- no `render_plan_job` with `result: refused`, and no `render_plan_cache` `miss` that is not followed by a `submitted` job (that pattern was the synchronous plan);
- `first_draw` for actor `20` within about 4 s of the release (one plan plus one first draw), compared with 37.5 s before.

The log should show no `edit refused` lines.

## Stage 2: swap, do not blank (scoped)

**Goal.** A reinstall keeps the old render visible until the new render's first draw ends, then swaps in one tick. A resize already works this way: the old level stays published while `Running` draws the new one, and `SwapResize` publishes it.

**What makes it large.** The old and the new render want the same geometries:

- Material bindings install into the geometry's material. Two bindings on one geometry at once are not supported. The swap has to move the binding from the old render to the new one, or the new render must stage without binding until the swap.
- The first draw renders slots "with the real filter and base" (`REFERENCE.md`, "First draw (7h)"). It is not yet known whether that needs the new binding to be installed.
- Shells are cloned per install. A staged render needs its own shell clones hidden until the swap, or reuses the old ones.
- During staging the actor holds two sets of targets. The budget has to charge both, as it does for a resize's intermediates.

**Next step.** A design document for stage 2 after stage 1 lands, starting from the resize path (`ActorResize.cpp`, `Running`, `SwapResize`). It needs decision D3.

## Stage 3: narrow invalidation (scoped)

Each item stands alone.

- Keep the plan cache across a load. `Manager::Clear` calls `renderPlans_.Clear()`. Cache entries hold their graphs, so keys stay valid. Prove it with one load run under `VerifyRenderPlanCache=true`.
- Keep the plan cache on `RebuildAll` (`ManagerApplication.cpp`). First check that no plan input outside `RenderPlanKey` depends on the catalog.
- Apply mute, layer isolation and solo piece with no rebuild. They are already applied per tick (`HiddenLayers`, `PieceShown`). Only isolation recipe or output, pin and recipe set feed the install.
- Keep a recipe's graph when its `ContentFingerprint` is unchanged (Save, Revert, an undo to a known state).
- "Reload INI" reapplies only when `SettingsDiffer`; each `reapply` setting gets the narrowest scope (actors that enter or leave scope for `PlayerOnly`, `ThirdPerson`, `FirstPerson`, `ShowDebugRecipes`).
- `InstanceGraph` compiles a new graph per install for a recipe with a variant. Cache the variant graph by recipe fingerprint and variant name.

## Stage 4: per-piece install (scoped)

`Reconcile` (`actors/Reconcile.cpp`) already computes a per-placement diff (`RetirePlacement`, `InstallPlacement`, `RetireGeometry`). `Change` uses it only as "something changed". Stage 4 passes the diff to the engine, so one equip installs one piece. After stage 2 the user no longer sees the whole-actor reinstall, so stage 4 is about cost (plan, first draw, shell clones), not looks. Also in scope: a 3D rebuild with the same meshes rebinds the existing render to the new nodes, because a model token is the `NiAVObject*` identity today (`ModelFor`, `engine/ActorObservation.cpp`).

## Decisions for the user

| # | Question | Options | Recommendation |
|---|---|---|---|
| D1 | What does an install do when the plan queue is full? | (a) Report `InstallFailed{"plan queue full"}`. Today a failed install makes the actor gone (`SelectGone` in `Sync.cpp`), so it is forgotten and nothing retries it until its next equip or 3D change; (a) needs a new retry rule. (b) Defer the install like an install over the per-frame budget: the actor goes back on the waiting list (`sync_deferred`) and installs on a later tick, when the queue has room. The core needs one new fact, the queue's free slots. (c) Keep the synchronous plan, but only for the player. | (b). It reuses the waiting list and the install pacing, and the actor never loses its effect for good. |
| D2 | Does a gesture step wait for the previous step to settle (1c), or only for its plan? | (a) Settled: plan built and first draw ended. (b) Plan built only. | (a). The first draw is about 1 s, and with (b) every step still blanks the armor for that long. |
| D3 | Does the stage 2 swap apply to every actor or only where the user is looking? | (a) Every actor. (b) The player and the inspected actor only; NPCs keep retire-then-install. | (a) if the budget allows two renders per actor during staging; decide after the stage 2 design measures it. |

## Regression set

- Stage 1: `ctest --preset native -R "^(orchestration_sync|orchestration_lifecycle|engine_jobqueue|engine_planjobs|studio_gesture)"`, the Windows build, and the in-game check above.
- Stages 2 to 4: the full in-game suite against the 2026-10-10 baseline, as each stage's design states.
