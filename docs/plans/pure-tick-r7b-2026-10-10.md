Status: done (2026-10-10) on branch `pure-tick`, commits b2464d8 to ee3624f plus the docs commit. Every recommendation in section 7 is decided as written; Q11 to Q15 record what the implementation decided. The baseline trace is the full suite of run 20261010T174638 at b8cebf4. Next: R6's docs pass and the in-game comparison of section 6 against that baseline.

# Pure tick R7b: the install program, engine part

**Before you start.** HEAD moved from b8cebf4 to 36d65a2 while I was reading. The two new commits touch only `tools/`, `tests/tools/`, `docs/build.md` and the plan doc, not `src/`, so every source file:line below still holds. One correction to the brief: `REFERENCE.md` has **no install-program section**. A search for `InstallProgram`, `PlanSurfaces`, `ActivateOutputs`, `RenderBuildAt` and `AttachmentsOf` finds nothing. The "engine runs the core" section is at `REFERENCE.md:3509-3564`. R7b has to write the install-program section.

**Core and commands.** The core (`src/orchestration`) and the command set need **no change**. `Install`, `BuildAwaitedRender`, `FailAwaitedRender` and `Installed{level,total,levelCosts,chargedBytes}` (`orchestration/Facts.h:59-64`, `Commands.h:59-76`) already carry everything. All new pure code goes in `planners/`.

---

## 1. How an install runs today

| # | Step | file:line |
|---|---|---|
| 1 | `Execute` dispatches `Install` to `RunInstall` and times it as install time. | `engine/ManagerCommands.cpp:123-143` |
| 2 | `RunInstall` looks up the actor (`InstallFailed` if it is gone). An existing store entry means a disagreement, and the entry is retired first. It counts the wait. | `ManagerCommands.cpp:273-295` |
| 3 | `InstallLiveActor` starts the `SyncMeter` and the `actor_install` trace. | `ManagerApply.cpp:1477-1484` |
| 4 | `LiveActorFor`: `LivePiecesOf` (geometry, property, PBR bind) and the geometry-count check. `BuildInstances` then calls `InstanceFor` per matched instance (recipe, variant graph, signals, environment, carried time). | `ManagerApply.cpp:1208-1229`, `201-251`, `1238-1297` |
| 5 | If that fails: `InstallFailed`. If `matches.placements` is empty: `InstalledWithoutRender` and no store entry. | `ManagerApply.cpp:1491-1508` |
| 6 | `PlaceInstances`: ticks signals at t=0 and opens the `actor_place` trace. | `ManagerApply.cpp:1299-1308` |
| 7 | `PlaceGeometries`, per flat geometry: `PlanGeometryPlacement` (view filter), `PreparePlacement` (copies the plan into `LiveGeometry`, resets `LivePlacement` outputs), then `PlaceOnGeometry`. | `ManagerApply.cpp:1340-1360`, `261-288` |
| 8 | `PlaceOnGeometry`: `InstallSurfaces` (material install, shell-owner choice, shell create), `MarkReplaced`, `CollectChainStacks`, and the `geometry_scope` trace. | `ManagerApply.cpp:1362-1389`, `290-353` |
| 9 | `CollectChainStacks`, per slot contribution: `LocateStackOutput`, `active=true`, `SurfaceProblem`, stack size (`RuntimeSizes`, `SlotStackSize`, `Compositor::StackSize`), `SideFor`, then a request and a chain link. | `ManagerApply.cpp:470-510`, `393-437`, `112-150`, `461-468` |
| 10 | `BuildActorRender`: clears the render and merged slots. No requests gives `RenderNotBuilt`. Otherwise `RenderPlanFor` (cache hit, verify, shared or submitted job, or a fresh build), then `RenderBuildAt`, a `RenderInstance`, `AttachRender`, `AttachStackOutputs` and `AttachMergedSlots`. On any error, `FailStackOutputs`. | `ManagerApply.cpp:890-942`, `838-888`, `589-659` |
| 11 | Sets `fullSizes`, adds the `actor_place` fields and emits. | `ManagerApply.cpp:1318-1328` |
| 12 | `KeepRender`: sets `awaitedPlan`, or level, holding and `StartFirstDraw`. | `ManagerApply.cpp:963-976`, `ActorResize.cpp:256-272` |
| 13 | `PlaceLightsOf`: `PlanActorLights`, light problems, then `PlaceLight` per shown contribution. | `ManagerApply.cpp:1391-1432`, `993-1083` |
| 14 | `ReportApplied` (log line plus the `installed` application trace), `RenderEventOf`, `live_[id]=…`, `FireEquip`. | `ManagerApply.cpp:1515-1527`, `978-991`, `1162-1176` |

`BuildAwaitedRender` (`ManagerCommands.cpp:235-257`, then `ManagerApply.cpp:1530-1552`) runs `CollectActorStacks` again (`512-522`), then `BuildActorRender`, `KeepRender` and `TracePlanJob`. `FailAwaitedRender` (`ManagerCommands.cpp:259-271`, then `ManagerApply.cpp:1331-1338`) runs `CollectActorStacks` again, then `FailStackOutputs`.

---

## 2. Each R7a stage against the engine code it duplicates

| Stage (`planners/InstallProgram.cpp`) | Engine duplicate | Do inputs and outputs match? | Adapter: what the engine gathers in, and what it does with the output |
|---|---|---|---|
| `PlanSurfaces` (267-294) | `PlaceGeometries` 1340-1360, `PreparePlacement` 261-288, the shell-owner part of `InstallSurfaces` 297-311, `ReplacerRecipeId`/`MarkReplaced` 315-353 | **Inputs:** `matches`, `RecipesOf(state)`, and the filter (same lambda as 1351-1353). The geometry order is the same: `LiveActorFor` already checks that the counts agree (1222). **Outputs:** `shellOwner` is an `InstanceId`, while `LiveGeometry::shellOwner` is `optional<size_t>`. `PlacementOutputs` holds `OutputState`, while `PlacedOutput` has `problem/active/rendered/renderFailed` flags (`LiveActor.h:34-41`). The engine never uses `OutputState` (grep finds nothing). The pure version lists only placements a geometry references; the engine sizes `placements` to `matches.placements.size()` (1344-1345). Owner check: the pure version wants the recipe in the store; the engine wants `instances[i].recipe`. These are equal unless `SharedRecipe` fails (inferred). | New `KeepSurfacePlan(LiveActor&, const SurfacePlan&)`: resize `placements`, then per `GeometryBinding` copy `placements=sources`, `plan`, `stackPlan=stacks`, `binding`, and per `PlacementOutputs` set `geometry` and the outputs. It must **not** store `placement.placed`: `PlacedRecipe::recipe` is a raw pointer into the catalog (`recipe/Merge.h:16-21`). |
| (binding, engine only) | `InstallSurfaces` 290-313 | — | New `BindSurfaces(LiveActor&, span<const GeometryBinding>, bool unique)`: `MaterialBinding::Install` when `binding.material`. When `binding.shell && shellOwner`, check the owner index is below `instances.size()` and `.recipe` is set, then `ShellBinding::Create`. Emits `geometry_scope`. |
| `ActivateOutputs` (296-333) | `CollectChainStacks` 470-510, `LocateStackOutput` 393-421, `SurfaceProblem` 423-437 | **Needs `BoundSurfaces`**, gathered after binding: `material=bound.material!=nullptr`, `shell=…`, and per plan slot `{surface, slot, ProblemText(TargetFor(bound,surface)->Problem(slot))}` when a target exists. **Needs `compiled`**: instance ids with `recipe && graph`, matching the engine checks at 401-410. **Output gap:** `StackRequest` has no texture size, `SurfaceOutput*`, `GeometryInputs` or enchantment. The engine must build those for each stack (next row and section 3). The order (geometry, slot, chain) matches the engine, so the sides and `full` come out identical. | `WriteOutputs(LiveActor&, span<const PlacementOutputs>)`. Then per `StackRequest`: the size (gap G1), plus a `StackInputs` record of the shared graph and recipe of `instances[IndexOf(instance)]`, `SurfaceOutputOf(recipe, output)`, and `GeometryInputs` with `scope{IndexOf(instance)+1, actor}` and `EnchantmentValuesOf`, all bounds-checked. |
| `RenderBuildRequestOf` (335-349) | `SideFor` 461-468, `fullSizes` 1318/1541 | Matches. A sizes/stacks length mismatch is defensive only. | `state.fullSizes = request.full`. On error: `FailedOutputs` and `RenderNotBuilt`. |
| `LevelCostsOf` / `RenderBuildAt` (351-390) | Already in use (`ManagerApply.cpp:924-929`) | Matches. | Unchanged. |
| *Plan key and plan input* (engine; the Open list keeps them there) | `PlanKeyOf` 661-678, `PlanStackRequestOf` 545-571, `PlanInputOf` 573-587 | Must take `(RenderBuildRequest, StackInputs)` instead of `ActorStacks`. The pure key is already `RenderPlanKeyOf`. The engine only gathers `StackKeyRequest` (material identity, mesh hash, value bindings from engine inputs), which correctly stays engine-side. The graph-pointer consistency check (554-559) becomes a bounds check, because the inputs now come from the instance itself. | `PlanKeyOf(const RenderBuildRequest&, span<const StackInputs>)` and `PlanInputOf(same)`. |
| `AttachmentsOf` (392-420) | `StackOutputFor` 595-606, `BoundAt` 608-612, `AttachStackOutputs` 614-633, `AttachMergedSlots` 635-659 | `a_bound` is the `GeometryBinding` span: `SurfacePlan.geometries` on install, the gathered plan on the awaited path. **Missing:** `StackAttachment` has no `animated`; the engine computes `IsAnimated(*graph, Output{*surface})` (624). The size comes from `render->SizeAt(request.stacks[i].side)`. The output's `placements` carry `kStackNotLowered`. | `ApplyAttachments(LiveActor&, const Attachments&, const RenderBuildRequest&, span<const StackInputs>, shared_ptr<RenderInstance>)`. Per stack: `RenderOutput` into `placements[...].outputs[...]`, resolved by `(placement, output)`. Per merged entry: `MergedSlotStack` on `LocateGeometry(geometry)`. Then `WriteOutputs` and `LogStackDiagnostics`. |
| `FailedOutputs` (422-430) | `FailStackOutputs` 589-593; `Manager::FailAwaitedRender` 1331-1338 | Matches. Active output plus problem becomes `OutputFailed{problem}`. | `WriteOutputs(state, FailedOutputs(activation.placements, activation.stacks, problem))`. |
| `PlanLightsOf` (432-470) | `PlaceLightsOf` 1391-1432, `LightSourceInstance`/`LightInstanceOf`/`LightGeometriesOf` 993-1033 | **Inputs:** the same. **Differences:** the engine overwrites `lightProblem=""` for sources without a light; a fresh `LiveInstance` already has `""`, so this is equal. The engine also needs `instance.signals` and non-null `bound.geometry` (1010, 1028); the pure version cannot know either. **Missing:** offset, bones and shadow, which the executor reads from `LightOutputOf(recipe, output)`. | `PlaceLights(LiveActor&, const LightProgram&, NiAVObject* root)`: set problems by instance index. Per `LightRequest`: map `geometries` to `LocateGeometry(...).bound.geometry` (skip null), skip if none are left or the instance has no signals, then `PlaceLightNodes` and `LightBinding::Create`. `LightProblemOf` (1035-1044) stays: it branches on engine results. |

**Awaited path.** Today `CollectActorStacks` re-reads `LiveGeometry`. That keeps three behaviours:
- the same plan key, so the build is a cache hit (`REFERENCE.md:1565-1569`);
- geometries lost to a claim give no stacks, because `RetireGeometry` empties `plan` (`LiveActor.cpp:177-191`);
- the engine call `Compositor::StackSize` runs again.

Adapter: `SurfacePlanOf(const LiveActor&)` gathers `GeometryBinding{plan, sources, stackPlan, binding}` from each `LiveGeometry`, with `placed` left empty because `ActivateOutputs`/`AttachmentsOf` do not read it, plus `PlacementOutputs` from `LivePlacement`. Then run the same program as install from `BoundSurfacesOf` onward.

---

## 3. Gaps: engine decisions no R7a stage covers yet

| # | Decision | Where | Recommendation |
|---|---|---|---|
| G1 | Stack texture size: the texture-scale arithmetic, the slot resolution divisor, and the clamp over the base map with the flat-height exception | `RuntimeSizes` 117-143, `SlotStackSize` 145-150, `Compositor::StackSize`/`SizeOverBase` (`render/Compositor.cpp:17-21`, `158-170`) | **Add a pure stage.** It reads one actor's observation and the settings, and its result reaches the core through `levelCosts`. `struct StackSizeFacts { std::uint32_t nativeLargest = 0; std::optional<std::uint32_t> baseLargest; };` and `[[nodiscard]] TextureSize StackSizeOf(const StackSizeFacts &a_facts, Resolution a_resolution, TextureScale a_scale);`. The engine gathers `baseLargest` as none when the slot is height with `flatDisplacement`, when the base is a placeholder, or when the extent is unknown. All three give `requested` today: clamp(0, floor, ceiling) = floor. `Settings.h` is engine-free. Then delete `Compositor::StackSize` if nothing else calls it (only `ManagerApply.cpp:496` does). |
| G2 | First-draw slot list and count (`Installed::total`) | `StartFirstDraw` (`ActorResize.cpp:256-272`), `RenderEventOf` 988-990 | **Add.** The count reaches the core, and the agreement check compares it (`orchestration/Agreement.cpp:54-68`, `ManagerFacts.cpp:104-106`). `struct GeometrySlot { GeometryId geometry{}; std::size_t slot = 0; }; [[nodiscard]] std::vector<GeometrySlot> UnpublishedSlotsOf(std::span<const GeometryBinding> a_geometries);` The engine maps a flat `GeometryId` to `(piece, geometry)` for `HiddenSlot`. Then drop that part of the Open line. |
| G3 | Plan route: cache on or off, uncacheable, hit, verify, share an open job, submit, fresh fallback | `RenderPlanFor` 838-888, `AwaitPlanJob` 827-836 | **Extract only the choice:** `enum class PlanRoute { kFresh, kCached, kVerifyCached, kShareJob, kSubmitJob }; [[nodiscard]] PlanRoute PlanRouteOf(bool a_cacheOn, bool a_keyed, bool a_cached, bool a_verifies, bool a_jobOpen);`. The cache and job book stay in the engine, as a memo that changes timing, not results. Note the exception in REFERENCE: the cache is mutable shared state. The executor then branches only on `Submit` succeeding and on `SameStructure`. |
| G4 | "Nothing placed means no store entry and `InstalledWithoutRender`" | 1500-1508 | Leave it. It reads the command's own `planned.matches` (from `PlanActor`, a boundary function). Optional: `[[nodiscard]] bool PlacesNothing(const ActorMatches &)`. |
| G5 | "No stacks means no render" | 901-903 | Leave it as a test of `request.stacks.empty()` on the stage's output. |
| G6 | `animated` per stack attachment | 624 | Leave it in the executor: it is a property of the instance graph. Alternative: give `AttachmentsOf` a `std::span<const bool> a_animated`. |
| G7 | Instance building: which enchantment, variant graph, carried time | `EnchantmentForInstance` 99-110, `InstanceFor` 1247-1279, `CarryInstanceTime` 1281-1297 | Leave. This builds resources; no outcome reaches the core. |
| G8 | Light problem text and bone faults | 1035-1083 | Leave. It branches on engine results. |
| G9 | `ObserveOutput` (application observation) | `ManagerApplication.cpp:56-76` | Out of scope. R-5 wants it pure over `OutputState`; after step 2 it becomes trivial to move. |

---

## 4. Deletion list

All in `src/engine/ManagerApply.cpp` unless marked.

| Delete | Lines |
|---|---|
| `RuntimeSizes`, `SlotStackSize` (replaced by G1 plus a facts gather) | 112-150 |
| `PreparePlacement` (replaced by `KeepSurfacePlan`) | 261-288 |
| Shell-owner decision inside `InstallSurfaces` (becomes `BindSurfaces`) | 297-311 |
| `ReplacerRecipeId`, `MarkReplaced` | 315-353 |
| `LocatedStackOutput`, `ActorStackRequest`, `ActorStacks` (+ forward declaration in `Manager.h:59`) | 356-378 |
| `LocateStackOutput`, `SurfaceProblem` | 393-437 |
| `RecordGeometry`, `SideFor`, `CollectChainStacks`, `CollectActorStacks` | 450-522 |
| `FailStackOutputs`, `StackOutputFor`, `BoundAt`, `AttachStackOutputs`, `AttachMergedSlots` | 589-659 |
| Consistency branch of `PlanStackRequestOf` (rewritten over `StackInputs`) | 553-559 |
| The `Get<RenderPlan>` null branch ("no render plan", unreachable) | 919-923 |
| `LightSourceInstance`, `LightInstanceOf`, `LightGeometriesOf` | 993-1033 |
| `Manager::PlaceGeometries`, `Manager::PlaceOnGeometry` (replaced by the program) | 1340-1389 (+ `Manager.h:315-319`) |
| `Manager::PlaceLightsOf` (replaced by `PlaceLights`) | 1391-1432 (+ `Manager.h:320`) |
| `Manager::FailAwaitedRender` body (rewritten), `Manager::PlaceInstances` (folded into the install executor) | 1299-1338 (+ `Manager.h:306-314`) |
| `Compositor::StackSize`, `SizeOverBase`, `BaseMapFor` if unused after G1 | `render/Compositor.cpp:13-21`, `158-170` |
| Slot enumeration in `StartFirstDraw` (built from G2's list) | `ActorResize.cpp:261-270` |
| `PlacedOutput::problem/active/rendered/renderFailed` (replaced by `OutputState state`, step 2) | `LiveActor.h:37-40` |

`BuildActorRender` (890-942) and `RenderPlanFor` (838-888) stay, rewritten over the new types.

---

## 5. Order of work

The Windows build stays green after every step. Each step ends with `ctest --preset native` and `cmake --build --preset windows-release`.

| Step | Content | Native tests added |
|---|---|---|
| 1 | Pure gaps in `planners/InstallProgram.*`: `StackSizeOf`, `UnpublishedSlotsOf`, `PlanRouteOf`. Nothing calls them yet. | `installprogram_tests.cpp`: the scale divisors; flat height giving requested; a placeholder base giving requested; clamping to floor and to ceiling; equal sizes sharing a side through `RenderBuildRequestOf`; a lost geometry (empty stack plan) giving no slots; every route. |
| 2 | `PlacedOutput` holds `OutputState state`. Update `ManagerTick.cpp:164-165` (`AfterRender`), `ManagerTick.cpp:439-446` (`FailGeometryOutputs` uses `AfterLostGeometry`), `LiveActor.cpp:200-208` (`AfterRetire`), `ManagerApplication.cpp:60-72`, `RegressionRequests.cpp:70-71` (`Is<OutputRendered>`), `ManagerSnapshot.cpp:186` (`ProblemOf`), and `PreparePlacement`, `CollectChainStacks`, `FailStackOutputs`, `AttachStackOutputs` as they stand. | Update `tests/engine/liveretirement_tests.cpp`. `outputstate_tests.cpp` already covers the transitions. |
| 3 | New `src/engine/InstallStages.{h,cpp}`: `SurfacePlanOf`, `KeepSurfacePlan`, `WriteOutputs`, `CompiledInstancesOf`, `BoundSurfacesOf`, kept free of `RE::` calls. Register as a native suite with `tests/engine/lifetime` stubs, like `engine_liveretirement` (`cmake/Native.cmake:51-54`). Run `python3 tools/compile-db.py`. | Round trip `KeepSurfacePlan` then `SurfacePlanOf`. A retired geometry gives no stacks after `ActivateOutputs` and keeps its problem. `WriteOutputs` ignores unknown placements. The `Binding.h` stub needs `Problem`. |
| 4 | Render subprogram: `ActivateOutputs`, `StackSizeOf` plus the engine facts, `RenderBuildRequestOf`, `PlanKeyOf`/`PlanInputOf` over `StackInputs`, `RenderPlanFor` with `PlanRouteOf`, `RenderBuildAt`, `ApplyAttachments`/`FailedOutputs`. Switch **all three** callers in the same change: `PlaceOnGeometry` collection, `BuildAwaitedRender`, `FailAwaitedRender`. Delete the collection and attach group. | None new; the stage tests already exist. |
| 5 | Placement and binding: `PlanSurfaces`, `KeepSurfacePlan`, `BindSurfaces`, then step 4. Delete `PreparePlacement`, `MarkReplaced`, `PlaceGeometries`, `PlaceOnGeometry`. | — |
| 6 | Lights: `PlanLightsOf` then `PlaceLights`. Delete `PlaceLightsOf` and its helpers. | — |
| 7 | First draw from `UnpublishedSlotsOf`; `RenderEventOf` takes its count. | — |
| 8 | Deletion sweep, `tools/compile-db.py`, docs. Add a new REFERENCE section "The install program (`planners/InstallProgram.*`, `engine/InstallStages.*`)" covering the stage order, the awaited re-gather, `placed` never being stored, the plan-route memo exception, and `animated` in the executor. Add a row to the boundary table. Update `docs/components/engine.md:43-80`, `448-449`, `539-566`; `planners.md:462`; `render.md:24`, `258`; `REFERENCE.md:948-949`, `1565-1571`; the Open line in the plan. | — |

**Risky seams.**

| Seam | Risk | Mitigation |
|---|---|---|
| Raw pointers in pure output | `GeometryPlacementPlan::placed[].recipe` points into the catalog; a `RebuildAll` swaps the catalog. | Use `SurfacePlan` only inside one executor call. `KeepSurfacePlan` copies no `placed`. |
| `LiveGeometry*`/`PlacedOutput&` held across steps | Today `ActorStacks::bound` and `LocatedStackOutput::placed` are raw references into a local `LiveActor` that is later moved (1523). | Every adapter resolves by `GeometryId`/`PlacementId` with bounds checks (`LocateGeometry`, `ResolvePlacement`, `OutputAt`) at the moment it writes. No pointer is kept across a stage. |
| Lost geometries on the awaited path | A claim between install and plan arrival would build a stack on a geometry whose `inputs.geometry` is null. | Gather from the current `LiveGeometry`; a lost geometry's `plan` is empty. Step 3 has a test for it. |
| Shared `RenderInstance` | `inputs.render` is set on every geometry; `RenderOutput` reaches the render through `render_.lock()` (`Compositor.cpp:174`). | Keep the order: build `GeometryInputsOf(state)` (flat, every geometry) **before** `AttachRender`, as at 931-935. |
| Shell owner index | `instances[owner].recipe->shell` | Check `owner < instances.size()` and `recipe` before the read, as at 305-306. |
| Plan-key identity | A different key on the awaited path means a cache miss and a resubmit loop. | `StackInputs` must reproduce `scope.context = instance+1`, enchantment and inputs exactly. Check in game with `--verify-render-plans`. |
| Traces | The names must stay. A search of `tools/`, `src/regression` and `tests/tools` finds no consumer of `actor_install`, `actor_place`, `actor_render`, `render_plan_job` or `geometry_scope` fields. Tools read `metrics action=sync` (`tools/trace-report.py:88`), heartbeat `sync_us`/`sync_max_us`, and `resource_disagreement`/`actor_phase_ignored` (`tools/regression_budgets.py:336`). | Keep every field: `live_us/place_us/lights_us/report_us`; `geometry_us/render_us/geometries/level/finest_level/replaced_bytes/headroom_bytes/admitted_bytes`; `plan_us/attach_us/plan`; `render_plan_job` result, job, level, finest_level, admitted_bytes, shared, requests, inputs_us, unfinished. `geometry_us` will now also time activation. Keep `SyncMeter`. |
| Filter source | `editor_.CurrentView()` vs `TickContext::view` | Keep `editor_.CurrentView()`, as today. |

---

## 6. In-game checks that prove R7b

First run a baseline of the full catalog at HEAD before step 4, then the same after step 8. Compare per case.

| Case | Proves |
|---|---|
| `lifecycle`, `equip-cycle`, `unequip-one` | Install and retire, `FireEquip`, outputs reaching `rendered` |
| `isolation`, `studio-save`, `studio-reload` | Reinstall, the view filter, stale jobs, the awaited re-gather |
| `burst`, `soak-waves`, `soak-churn` | Plan jobs and the awaited path under a crowd |
| `soak-large`, `soak-distance` | `InstallBound` headroom fit (`admitted_bytes`) |
| `resonant-ward`, `winterglass`, `arcane-circuit`, `soak-stack` | Shells, merged chains, lights (inferred from the names) |
| Any one case with `--verify-render-plans` (`tools/regression-run.py:290`) | The plan key is unchanged: `render_plan_cache result=hit_verified`, never `mismatch` |

Lines to look for:
- No `resource_disagreement` and no `actor_phase_ignored`; R-7 fails the launch on either. The agreement check also catches a wrong first-draw count.
- The install summary `actor … installed: N piece(s), M recipe(s) applied to G geometries (P private material(s), S shared, H shell(s))` matches the baseline per actor, and so do the verbose `apply armor … material=… shell…` lines.
- `render_plan_job result=submitted`, then `done`, then `attached` with `level`; and `render_plan_cache result=hit` on the awaited rebuild. No second `submitted` for the same actor.
- `actor_phase` goes `planning` to `drawing` to `shown`; `first_draw end=drawn`.
- No new `stack was not lowered` or `light bone … is not on the wearer` warnings. Studio rows still show `replaced by recipe X` and `actor-wide lights replaced by recipe X`.
- The `actor_install`, `actor_place` and `actor_render` field sets are unchanged.
- `live_instances` returns to 0 in `target_holders` after cleanup.

---

## 7. Open questions

| # | Question | Recommendation |
|---|---|---|
| Q1 | Should `PlacedOutput` adopt `OutputState` (R-5) inside R7b? | **Yes, as step 2.** Otherwise a lossy translation between flags and states sits in the executor, and the awaited write-back would have to keep `renderFailed` by hand. |
| Q2 | Awaited path: gather again, or store the install's `RenderBuildRequest`? | **Gather again** (`SurfacePlanOf`). It keeps the cache-hit rule and claim safety, and needs no pure filter for lost geometries. |
| Q3 | Should G1 stack size be pure? | **Yes.** It feeds `levelCosts` and the charge. |
| Q4 | Should G2 first-draw slots be pure, against the Open line? | **Yes.** `Installed::total` is a core fact checked by agreement. Update the Open line; only the plan-key gathering stays in the engine. |
| Q5 | Plan route | Extract `PlanRouteOf`, keep the stores in the engine, and record the memo exception in REFERENCE. |
| Q6 | Stack `animated` | Leave it in the executor (graph property); note it in REFERENCE. |
| Q7 | `LiveGeometry` duplicates `GeometryBinding` (placements, plan, stackPlan, binding) | Not in R7b. Replacing them with one `GeometryBinding` member touches `ManagerTick`, `ActorResize`, `ManagerSnapshot` and `TargetCensus`. Make it a later `tools/rename.py` pass. |
| Q8 | Where the adapters live | New `engine/InstallStages.*`, free of `RE::` calls, natively tested with the lifetime stubs. This also shrinks `ManagerApply.cpp` (1588 lines). |
| Q9 | Sequencing against R6 | The plan says R6 is next. Take R6's full-suite run as the baseline trace and start R7b only after it, so a difference can be blamed on R7b. |
| Q10 | Core or command change | None needed. If Q4 is taken, only the plan's Open line and the boundary table change. |
| Q11 | `PlanRouteOf` as proposed has five routes and five bool arguments | **Decided in step 1.** The engine has three fresh routes today: the cache off (no trace, no store), `uncacheable` (trace, no store), and a verifying miss (fresh plan, stored, trace `miss`). Five routes would make the executor branch on the cache setting again. `PlanRoute` has seven: `kFresh`, `kUncacheable`, `kCached`, `kVerifyCached`, `kShareJob`, `kSubmitJob`, `kFreshStored`. The facts arrive as one `PlanRouteFacts` record, so two bools cannot swap places at a call. A refused submission still falls back to a stored fresh plan; that branch is on an engine call's success. |
| Q12 | `StackSizeOf` cannot take `TextureScale` | **Decided in step 1.** The gate's layer graph (`tools/gate.py`) forbids `planners/` from including `Settings.h`. `StackSizeOf` takes the scale as a `Resolution` (`kFull`, `kHalf`, `kQuarter` divide by 1, 2 and 4, as `TextureScale` does), and the engine converts `TextureScale` at the boundary (`ScaleResolutionOf`, `engine/InstallStages.*`). |
| Q13 | How `OutputState` keeps today's observations (step 2) | **Decided in step 2.** An output that holds a stack is always `Pending`, `Rendered` or the render failure; an output with any other problem has no stack. So a render outcome changes the state only of an output with a stack (`RecordRender`, `engine/ManagerTick.cpp`), and a lost geometry fails only outputs with a stack. Today the flags of an output without a stack were set but never read, so no observation changes. `ObserveOutput` reads the stack to tell a render failure (reported once the actor has rendered) from a placement or surface problem (reported at once), as today. `RetireGeometry` treats a render failure as pending before `AfterRetire`, so a retire still names itself (`StateBeforeRender`, `engine/LiveActor.cpp`). **One visible difference:** the studio output row shows the render failure "the output could not be rendered; reapply to retry" (`ProblemOf`, `ManagerSnapshot.cpp`), where today it showed no problem. The plan's step 2 names `ProblemOf` for this row. |
| Q14 | When the engine reads each geometry's slot targets | **Decided in step 3.** Today a geometry's `SurfaceProblem` reads its targets right after that geometry binds and before the next one binds. A later geometry's bind can detach an earlier geometry's material when the two share a property. So the install executor gathers each geometry's `BoundSurfaces` right after binding it (`BoundSurfacesOf(LiveGeometry &, GeometryId)`), and only the awaited path gathers them all at once (`BoundSurfacesOf(LiveActor &)`), as `CollectActorStacks` does today. |
| Q15 | Fold `Manager::PlaceInstances` into the install executor? | **Decided in step 8: no.** `PlaceInstances` stays as the executor's named placement step: it ticks the signals at t=0, runs the program up to `BuildActorRender` and writes the `actor_place` trace. Folding it into `InstallLiveActor` would put two phase traces in one function. It now returns `PlacedRender`, the outcome with the first draw's slots. |
