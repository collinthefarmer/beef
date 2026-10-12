Status: active (2026-10-11) on branch `pure-tick`. The findings of the user's code walk of the plugin (chapter 1, startup to one tick; chapter 2, the tick core through `RunSync`), collected for review in bulk. Nothing here is started unless marked; items that need the user's decision say so. Done and not listed: the budget and sync restructure (c7d4d4c), the render-phase state machine (5b1042c), the trace cleanup (e30c67b), `FindResize` (020c6f7), and the 2026-10-11 batch (the rename table, `ActorVisibility` and the nested `BudgetActor`, the traced `ApplyPhaseChange` in `Output.h`, `TickFacts` without `frameSyncs`, the install-order test, `Budget.cpp` in call-flow order), and the second batch (`Sync.cpp` and `Lifecycle.cpp` in call-flow order with one reporter per event, every `std::visit` as `Match`, `SameRenderValue` and `ExternalInputType` as `Match`), and install bounds with headroom for an NPC with no known level.

# Code review backlog

## How the walk works

The user reads the code in an nvim pane that the `walk` skill drives. Each finding goes on this list unless the user asks for it at once. The style rules the findings apply are in the user's preferences: data-driven shapes over long imperative sequences, early returns so that the core logic is never inside an `if`, blank lines between the phases of a function, names short enough that declarations do not wrap, and a function named as a verb whose object is its first argument.

## Decisions for the user

| Item | Question | Recommendation |
|---|---|---|
| Application ledger | `docs/plans/application-ledger-2026-10-10.md`, Q1: accept the behaviour changes a to e? | Yes. |
| Staged tick output | Build the tick output in stages, or move it between stages as distinct types. The user is not convinced by the typed-changes design (each step returns a change record that one `Applied` function writes). | Discuss before a plan is drafted. |

## Names still open

- **"Out of view" or "hidden".** The core's field is `shown`, but the opposite is spelled "out of view" in `ApplicationPhase::kOutOfView`, `ReleaseReason::kOutOfView`, `ResizeDraw::kOutOfView`, the invariant rule `kOutOfViewReleaseEarly` and the setting `outOfViewReleaseDelayMs`. One word for the opposite of shown is a decision for the user; the setting name may be read from the user's settings file.
- The test helpers `InView` and `OutOfView` build a `Sighting`. `Shown` cannot name a helper, because it is the phase type.
- `InViewOf` (`planners/ViewFacts`) computes `Sighting::shown`.

## Cleanups

- **`IsAny<Ts...>(variant)` with the next `Core.h` change.** It would replace the `Is<A>(v) || Is<B>(v) || …` chains in `TraitsOf` (`recipe/RecipeGraphLowering.cpp`). An edit to `Core.h` rebuilds most of the tree, so it waits for another reason to touch the file. `MaxBy` is dropped: the loop in `recipe/Resolve.cpp` returns early on a keyword miss, and the loop in `planners/RecipeTextureCache.h` filters first, so neither is a plain best-element fold.

- **`ScreenStep::up` is derivable.** `ScreenStep` (`orchestration/Lifecycle.cpp`) stores `up` beside `from` and `to`, so it can hold a contradiction such as `{kFull, kHalf, up = true}`. `ResolutionLevel` is ordered from `kFull` to `kEighth`, so `up` is `LevelIndex(to) < LevelIndex(from)`. Its only reader is `needsRoom` in `StepLevel`. Replace the field with a `StepsUp(const ScreenStep &)` helper and drop the third argument in `ScreenStepOf`.

- **"Prerender" for the unpublished draw.** An unpublished draw renders a render's textures before they are shown (the first draw, a running resize) under one time budget per tick. "Prerender" is the graphics term for this and has no other use in the code. Rename the family: `DrawUnpublished` to `Prerender`, `UnpublishedDraw` to `PrerenderProgress`, `UnpublishedDrawn` to `Prerendered`, `UnpublishedDrawOrder` and `UnpublishedDrawCandidate` to `PrerenderOrder` and `PrerenderCandidate`, `UnpublishedDraws` (it builds one command) to `PrerenderFor`, like `TickActorFor`, and the setting `UnpublishedDrawBudgetMs` to `PrerenderBudgetMs`. The setting name may be read from the user's settings file. Update the REFERENCE "First draw (7h)" section with the rename.

- **`ScheduleResizes` names a value only to return it.** `starting` (`orchestration/Lifecycle.cpp`) is returned on the next line. Return `{swapping, StartNextResize(a_out, RunningResizes(a_out.world, swapping))}` directly; clang-format wraps it once.

- **`Issue` becomes `AddCommand`.** `Output.h` has three helpers that each append to one list of `TickOutput`: `Issue` (commands), `AddTrace` and `AddLog`. Name the first `AddCommand` so the three read as one set. The tick core uses it in `Sync.cpp`, `Budget.cpp` and `Lifecycle.cpp`; the `Issue` in `regression/Steps.cpp` is a different function and keeps its name.

- **`SwapsDue` reads as a verb.** "Swaps due" can be read as "it swaps the due ones". Rename the set `ActorsReadyToSwap(world, frame)` and the per-actor predicate `SwapDueFor` to `IsReadyToSwap(record, frame)` (`orchestration/Lifecycle.cpp`). The planner predicate `SwapDue` keeps its name.
- **`SwapDueFor` looks the phase up twice.** `ResizeProgressOf` answers only for `Running`, so `UnpublishedDrawOf`, which also accepts `Drawing`, adds a case that cannot happen there. One `Get<Running>` gives both the resize and `running->draw`.

- **Predicate prefixes.** A yes/no function starts with `Is`, `Has` or `Can`; `May…` becomes `Should…`. A function that acts and returns success (`AdmitStepUp`) is not a predicate. The tick core and planners have about 60 predicates to rename, for example `SwapDue`, `SmallFor`, `RenderLoaded`, `AwaitsJob`, `ScreenStepDownDue`, `MayRelease`, `MayDrawStack`, `MayEvict`, `MayStepDown`, `MayPreempt`. List them by shape before renaming, then sweep the rest of `src/`.

- **`StartNextResize` looks the chosen actor up again.** `NextResize` returns a bare form id, so `StartNextResize` (`orchestration/Lifecycle.cpp`) finds the record again and checks its render and target level, which `ResizeCandidateOf` already checked. Let the candidate carry the target level and `NextResize` return the chosen candidate; the re-lookup goes.
- **`ResizeCandidate::pending` is always true.** `ResizeCandidateOf` is the only producer outside the tests and it sets `pending = true`; it filters non-queued actors out instead. Delete the field and its check in `NextResize` (`planners/ResolutionResize.cpp`), and the test case "an actor without a pending level does not resize". With the field gone, the loop in `NextResize` is a plain best-element search: `std::ranges::min_element(a_candidates, ResizesBefore)`.

## Component edges (audit 2026-10-12)

Three read-only audits of the edges between components, after the studio-edit blank above. Traces: `...-1791768470721915-2.jsonl` (studio session), `...-1791770642202300.jsonl` (five loads). The findings share three causes.

**Discard before replace.** Every path below retires the actor's render and installs a new one. While the new one plans and draws, `TickActorFor` conceals the slots and hides the shells, so every effect on the actor shows the vanilla look. A resize already builds beside the old render and swaps; one swap path for installs would cover all of these paths.

| Trigger | Where | Evidence |
|---|---|---|
| Studio edit, undo, gesture step, Save, Revert | `RecipeEditor` → `ChangeAndRebuildActors` | Trace: 4 to 6 s blank per edit. |
| Equip or unequip of one piece | `Change` (`orchestration/Sync.cpp`) | `Reconcile` (`actors/Reconcile.cpp`) computes a per-placement diff, but `Change` uses only "commands non-empty" and issues `Retire{actor}` + `Install{actor}`; `Install` carries no diff. |
| A 3D rebuild with the same meshes (RaceMenu, another mod's `NiNodeUpdateEvent`) | `ModelFor` (`engine/ActorObservation.cpp`) | A model token is the `NiAVObject*` identity, so `GeometryKept` fails and every placement reinstalls. How often this happens is unverified. |
| View changes (mute, layer isolation, solo piece, isolate, pin, recipe set) | `RecipeEditor::UpdateView` and friends | Mute, layer isolation and solo piece are already applied per tick (`HiddenLayers`, `PieceShown` in `ManagerTick.cpp`) and need no rebuild. |
| Seven settings with `reapply=true`, and the Reload INI and Re-apply buttons | `ReapplyAll` (`ManagerApply.cpp`) | The buttons reapply without checking `SettingsDiffer`. |

**Plan worker queue.** One worker thread, no cancel, 16 slots (`engine/JobQueue.h`, `kPlanJobCapacity`).

- A superseded plan job keeps running. During one drag, revisions 4493 to 4507 submitted jobs 23 to 33 at 100 to 150 ms intervals; jobs 22 to 35 ran 2.5 to 3.0 s each and were dropped stale; job 36, the wanted plan, attached 37.5 s after submission.
- A gesture applies every 100 ms (`kGestureApplyInterval`), and each step is a full reinstall and plan.
- `RebuildAll` resets `planJobs_` but never calls `planQueue_.DropQueued()`; only a load does.
- **Freeze risk:** a submit refused as full falls through to `StoredFreshRenderPlan` (`ManagerApply.cpp`), a synchronous plan on the game thread, about 2.7 s for this recipe. The drag trace reached 16 unfinished jobs.
- Fix: drop an actor's queued jobs when it opens a new one; coalesce gesture rebuilds until the open job finishes or the gesture ends; never plan synchronously when the queue is full.

**Coarse invalidation and identity.**

- `RebuildAll` clears the plan cache (`renderPlans_.Clear()`), and so does every load (`Manager::Clear`). Cache entries hold their graphs by `shared_ptr`, so the keys stay valid. The trace's 16 misses all follow a `clear_begin`. Check that no plan input outside `RenderPlanKey` depends on the catalog before keeping the cache.
- An empty recipe id means `RebuildAll` (`ScopeOf`, `recipe/Resolve.cpp`), and view changes, settings, create, rename, duplicate, delete and paint begin and end all pass an empty id.
- Save and Revert reset the graph even when the content is unchanged (`RefreshRecipeDerivedState`, `RecipeStore.cpp`). Keep the graph when `ContentFingerprint` is unchanged; an undo to a known state could reuse its graph the same way.
- The plan key is the whole actor, built partly from positional ids (`PlacementId`, `GeometryId`), and one stack without a mesh hash makes the whole key empty. Two actors with the same gear share a plan only if their piece order matches.
- Every edit copies every loaded recipe twice in `RebuildApplied`; during a drag this runs every 100 ms.
- `InvalidatePreviews` bumps one global generation on any actor's install, retire or plan arrival, so every studio preview renders again.

**Ordering kept only by call order.** `MutableRecipe` writes `g_loaded`, but the rebuild scope reads the published catalog, so each editor action must call `RefreshRecipeDerivedState` after its mutation by hand. A `ReplaceRecipe(id, Recipe)` store call would make this one step. `RevertRecipe` calls `FinishActiveGesture(true)` outside its rebuild, which can start a second reinstall first.

**Per-tick overhead.**

- `CheckAgreement` copies `TickFacts` and the world and reruns `RecordSyncResults` and `RecordRenderResults` (`SettledWorld`, `Agreement.cpp`), then `Tick` runs both again.
- `SightingOf` allocates the actor's name string per actor per tick.
- `SetSettings` serializes the full INI twice and forces the trace on, once per slider frame.
- `step_stale` fills its 32-per-second cap in 888 of 944 seconds with time and signal inputs that change by design.
- Rendering reads `GetSettings()` under a mutex per geometry (`ManagerTick.cpp`, `CompositorBake.cpp`) instead of `TickContext.settings`.

**Design question.** `SeeShown` frees an actor's held intermediates 5 s out of view with no budget condition. Actor 760200 released 18 steps more than 10 times, as little as 10 to 20 s apart, with no budget pressure in the session. Release out of view only under pressure, or keep it as a deliberate memory policy and say so in REFERENCE.

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
- **A studio edit blanks the effect for 4 to 6 seconds.** Trace `BetterEnchantmentEffects-trace-1791768470721915-2.jsonl`, recipe `SUM_EnchArmor_RestorationMystheal_FXS`: each `editor.EditRecipe` or `UndoRecipe` marks the player stale and reinstalls all four of the player's recipes. The render plan misses the cache and its job takes 2.6 to 2.9 s of worker time for 532 steps. While the phase is `Planning`, `TickActorFor` sets `concealSlots` and `hideShells`, so the armor shows the vanilla look. A second edit within that time waits 1.0 to 1.7 s for the first job. The first draw then takes about 1 s (25 ticks, 44 stacks). The source preview thumbnails render only after the plan attaches (5.8 s, 40.6 s, 55.5 s after the first edit), so the thumbnail and the effect appear together; selecting a source does not release the effect. Two questions: keep the old render visible until the new one is drawn, as a resize does; and why this plan costs about 5 ms a step when the player's 51-step plan in trace `1791770642202300` costs about 0.3 ms a step (the 2026-10-05 editor refresh measured 16.2 ms of plan for Vigilant). Why the whole actor: `ChangeAndRebuildActors` reinstalls every actor applying the recipe (render, bindings, shells, lights), and the render plan is cached whole per actor under a `RenderPlanKey` of every `StackKey` and `ChainKey`. `StackKey::graph` is a pointer, and an edit compiles a new `RecipeGraph`, so the edited recipe's stacks always miss and the whole plan misses with them. When the match facts do not change (`ScopeOf` already tells), a value edit changes only the edited recipe's stacks and the slot chains that contain them. Three fixes, smallest first: (1) build the new render beside the old one and swap when drawn, as a resize does; (2) cache planned stacks by `StackKey` and rebuild only the changed stacks and their chains; (3) replace the recipe instance in place without a reinstall, keeping bindings and the other recipes' shells.
- **Skyrim Performance Solution (SPS) corrupts enchanted armor textures.** Partly done 2026-10-12 (uncommitted): `RenderTarget::Texture` also checks that our view and texture are still in the presenter's renderer data, `SlotWriter::WriteTexture` writes the original when the texture it is given is no longer ours (before, it skipped the write and left the taken-over presenter in the slot), the first loss is logged and each slot change is traced (`slot_texture_lost`), and `docs/alpha-package.md` documents the exclusion. Open: confirm in game; presenters without files. Reported 2026-10-12: with SPS on, armor slots show wrong textures (a normal map in the diffuse slot, the original combined with another texture). SPS 1.0.50 streams textures "straight from DDS files" through a DDS loader hook and, by default, also streams clothing and armor of characters (`bStreamClothing = true`). It shrinks a texture in VRAM and later reloads it by path, with a RAM cache. Our render targets are shown through presenters: real files `textures\BetterEnchantmentEffects\slots\slot_NN.dds` whose renderer texture we point at our target (`RenderTargetPool::LoadPresenter`). To SPS a presenter looks like an ordinary armor DDS, and we recycle presenters between targets, so a shrunk or reloaded presenter can show a stale composite of another slot or actor. Inferred, not confirmed: `SPS_TextureSizes.txt` has no `slots` path, and our `presenter_rejected`/`renderer_replaced` check runs only when a target is acquired, so a takeover in use would go unnoticed. Confirm: add `betterenchantmenteffects\slots\` to SPS `sExclude` (or set `bStreamClothing = false`) and compare. Fixes: detect a presenter whose renderer texture or view changed while in use and drop the target with a log line naming the conflict; document the SPS exclusion; the REQUIREMENTS future spike "presenter textures without files" removes the file path that makes presenters streamable.
- **Recovery fps.** After the frost and fight soak cases, frame rate recovered to only 0.49 to 0.82 of the baseline. Compare with a run from before R4.
