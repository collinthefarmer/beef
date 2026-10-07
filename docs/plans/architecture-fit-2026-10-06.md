Status: open. W1 to W10 done (W1 to W4 on 2026-10-06, W5 to W10 on 2026-10-07; uncommitted); the other items are not started. Raised by a read-only review on
2026-10-06 of the actor core (commits 1c32b67 to 828b364), the performance
work (772a447, dd516b1, 0e62ab8, 48938f0 and the uncommitted editor-refresh
changes) and the uncommitted recipe-language additions.

# Architecture fit

## Goal

The recent work keeps one owner for each piece of state. Pure decisions live
in engine-free modules that native tests reach. The documents describe the code
as it stands.

## Findings that stay in other plans

| Finding | Plan |
|---|---|
| The engine keeps two state tables per actor: `tracked_` (the keyed `ActorPlan`) and `live_` (a `LiveActor` built from the positional `ActorMatches`). `SyncActor` treats any non-empty command list as a full rebuild. | `actor-core-2026-10-03.md`, stage 4b |
| `Manager` still holds the snapshot publisher and the regression facade. | `actor-core-2026-10-03.md`, stage 6 |

## Work items

The order is by improvement per unit of work. W1 comes first because it
changes uncommitted code before that code is committed.

| Item | Work | Evidence | Done when |
|---|---|---|---|
| W1 | Publish each applied set as one immutable `RecipeCatalog` (`recipe/RecipeCatalog.h`) in place of the parallel vectors `g_recipes`, `g_fingerprints` and `g_snapshots`. A `LiveActor` holds the catalog it was planned against, so its `RecipeId`s index that catalog. `InstanceFor` takes its recipe through `SharedRecipe`; `PlaceInstances` and `PlaceLightsOf` read `RecipesOf(state)`. `RecipeCatalog::Of` cannot fail. | `RecipeStore.cpp:41-44`; `ManagerApply.cpp:1207`, `:1274`, `:965-970`; `Planning.cpp:107-114` | Native tests pass. No code indexes recipes by `ActorMatches` order. |
| W2 | Move the render-plan key and cache into `planners/` (`RenderPlanKey`, `RenderPlanCache`, `GraphsNamedBy`). Derive the key from the identities of the graph's static external inputs, the same identities the content keys use, instead of a hand-kept list. Positions and graph pointers stay in the key, because a plan stores `GeometryId`/`PlacementId` indices and graph pointers. Fixes a missed input: `ItemHashInput` entered content keys but not the plan key. | `ManagerApply.cpp:647-746`; review finding A1 was a bug in this key | Native tests cover the key: equal gear shares a plan; a changed placement, geometry, size, material, mesh, chain or read item hash does not. |
| W3 | Done 2026-10-06. `tests/planners/templatecost_tests.cpp` imports `SyntheticTiled` through the shipped `fill.json`, lowers it at a 2048² base and asserts that it builds (the field programs pack) and stays within 88 MiB of **targets** per wearer and geometry (`TargetCostOf`, `planners/TargetCost.h`). Measured: current template 31 steps, 15 targets at 1024², 80.0 MiB, 37.3 MiB shareable; the template at 48938f0 23 steps, 7 targets, 141.3 MiB, 134.7 MiB shareable. Two items share as much as two copies of one item. | The NPC in the 2026-10-06 VRAM report held about 12 placements (2 recipes on 6 to 7 geometries); 12 × 80 MiB is about 1 GiB. | The test passes; a template that grows past 88 MiB fails it. |
| W4 | Done 2026-10-06. Make one gesture-hold record with one clock. The mailbox holds the time of the last apply. One function clears `held`. | `held` (studio mailbox), `gestureStarted` (`FileOperationJournal`), `gestureAppliedMS_` (`RecipeEditor.h:90`); `held` is cleared in `TakeGestureDelivery`, `TickGesture` and `QueueGestureUpdate` (`RecipeOperations.h:80`); `TickGesture` mixes `steady_clock` and `NowMS()` (`RecipeEditor.cpp:103-121`) | Native tests cover the hold. A drag delivers one update per `kGestureApplyInterval`. |
| W5 | Done 2026-10-07. The per-actor record is `Actors::ActorState` (plan, installed plan, pending models, claims); `Phase`, `Unloaded`, `Evicted` and `Present` are deleted. `Manager::tracked_` maps an actor to `TrackedActor{state, models}`, and an actor without an entry has no record. `Advance` returns a `Transition`: no state when the actor is not loaded, or when plan, installed plan, pending models and claims are all empty. `InRange` derives the hysteresis from what is installed: with something installed an actor stays in range to 1.0 × `EvictDistance`; with nothing installed it comes in only inside 0.8 ×. Out of range, the state keeps the plan and the claims and nothing is installed. The tick's `ClaimLostGeometries` retires a binding whose `StillOwned()` fails, records a **claim** (`GeometryRef` and the piece's `ModelToken`) into the state with `WithClaim` and marks the actor stale; `PlanActor` leaves claimed geometries out; `Reconcile` retires them as any other removal; `LiveClaims` ends a claim when its piece gets a new model, and a load clears it. The tick no longer forgets an actor whose geometries are all lost (`!Alive`). `SyncActor` forgets an actor whose transition has no state; the tick forgets one that is not `Loaded3D`. Shared engine facts `Loaded3D` and `DistanceToPlayer` serve observation and the tick; the one-second recheck interval is `Actors::kRecheckMs`. `Geometry::lost`, `AnyLiveGeometry` and `RecipesOfInactiveInstances` are deleted. | `ManagerTick.cpp:28`, `:75-88`, `:746-784`, `:831-846`; `ActorObservation.cpp:216-225`; `ManagerApply.cpp:1064-1071`; `planners/ActorMatches.h:23`, `:65` | Native tests cover claims in planning, reconcile, expiry and `Tracked`. `lifecycle`, `unload` and `soak-churn` pass in game. |
| W6 | Done 2026-10-07. Derive every list of expression functions from the function table: the UI help in `Forms.h`, the `REFERENCE.md` grammar and the schema test. Delete the unused `rand` and `randrange` rows. Move `randomCalls_` from `Program` to `ExpressionParser`. Build the `Hashed` program without parsing a string at run time, or report a diagnostic when it fails. Put `kMaxShuffleTotal` and `kShuffleOutside` with the other limits at the top of `Expression.h`. Give the numeric helpers (`ShufflePosition`, `HashUnit`, `RandomInRange`, `HueColor`, `ShiftHue`) their own header. Remove the default argument from `BindContext`. | `Expression.cpp:46-47`, `:557-562`; `Expression.h:151`, `:179-180`; `Forms.h:124-127`; `REFERENCE.md:211`, `:215`; `RecipeGraphLowering.cpp:152-158`, `:420` | A test fails when a function is missing from any list. Native tests pass. |
| W7 | Done 2026-10-07. Measured with `soak-enchanted` against a build that marks `EnchantmentSignal` changing: steady plugin time 1032 µs per frame against 1179, signal evaluation 32 µs against 117, program evaluations 169 against 215 per second; stack draws unchanged (about 2040 per second), because the fill template's stacks also read time. Settle the `rand()` seeding model and make its seeds static. Decide how `itemHash` and `chartCount` are modelled and how `rand()` is salted (see "Decisions needed"). Stop treating the seeds as changing: the enchantment id and effect hashes are fixed for an instance, but `EnchantmentSignal` is marked changing, so every `rand()` step re-runs each tick and is never shared. Key the seed identities by the enchantment and effect, not the actor instance, so actors with equal enchantments and items share static steps. | `RecipeGraphLowering.cpp:152-185` (`InstanceIdentity`, `PieceIdentity`, `RowIdentity`), `:713-720` (`TraitsOf` changing set); `CompositorDemand.cpp` (`EnchantmentSignal` identity carries the instance string); the 17:31 trace of 2026-10-06 (200 to 800 stack draws per second at rest) | The decisions are recorded in `recipe-program-pipeline-2026-09-25.md`. Native tests show a `rand()` step is static and content-keyed, and that two actors with one enchantment share it. `soak-churn` and a crowd soak measure stack executions and target bytes against the 2026-10-07 run. |
| W8 | Done 2026-10-07 except committing `IdleCamera`; the `Sweep` rename was dropped because every cache in the repo uses that verb for renew-and-evict, and the `ReportApplied` logging stays because it is verbose-only. Small cleanups, each independent. See "W8 list". | See the list | Native tests and the commit gate pass. |
| W9 | Done 2026-10-07. Bring the documents up to date. See "W9 list". | See the list | Each document names only symbols that exist. |
| W10 | Done 2026-10-07 (`soak-enchanted`, `Dress::kEnchantedCuirasses`, `ShowAllRecipes`); first run 2026-10-07 passed (run 20261007T032055-1). Add a regression soak whose crowd wears vanilla armor under vanilla enchantments whose effect shaders an imported recipe covers, without the demo keyword, so only imported recipes match. It measures W7 (static `rand()` steps) before and after, and gives render-performance stage 7 a repeatable enchanted crowd. | On 2026-10-07 `soak-vanilla` and `soak-churn` applied only `arcane-circuit`; no imported recipe, and so no `rand()` step, ran on the crowd. | The soak passes; its trace shows imported recipes rendered on every crowd member; its steady window is recorded against a run on the W6 build. |
| W11 | Add `--steady SECONDS` to `tools/regression-run.py`. The run file carries the value; the plugin's hold step uses it in place of every soak's `steady` duration, and shortens `baseline` and `recovery` to 10 s, so an A/B comparison takes about 2.5 minutes instead of 5. The full soaks stay the checkpoint gate, because the budgets and the leak check are written against their windows. | On 2026-10-07 the W7 A/B needed two 5-minute `soak-enchanted` runs, of which only the steady window was compared; the heartbeat's per-second samples were flat within about 5 % across that window. | Native tests cover the run-file field and the shortened windows; a quick `soak-enchanted` run reports the same steady numbers as the full one within run-to-run variation. |

## Decisions (settled 2026-10-07 in W7: piece source kind, `rand([salt])`, all three seeds allowed)

| Question | Options | Why it matters |
|---|---|---|
| How are `itemHash` and `chartCount` modelled? | Keep them as `BakeKind` alternatives. Or make a separate per-piece source or signal kind. | They are per-piece values, not mesh bakes. The lowering diverts them to `ExternalInput`, which leaves dead arms in `Mesh.cpp` and `ValueIdentity.cpp`. The words become permanent at the format freeze. |
| How is `rand()` salted? | Keep the call's position in the expression. Or require an explicit salt argument. | With the position as the salt, reordering the `rand()` calls in an expression changes every value they produce. |
| Which inputs may seed `rand()` in a static step? | Allow the item hash. Or allow only the enchantment and effect hashes in steps that do not animate. | The Compositor shares a static step across actors only when its content is equal (render-performance stage 5, peak 8.6 GB to 2.2 GB). A seed from the worn item makes every item's static steps unique, so equal enchantments on different items no longer share targets. W3 measured `SyntheticTiled` through the current template: two items share 37.3 MiB, the same as two copies of one item, so on that fixture the item seeds cost no sharing. |

## W8 list

| Cleanup | Evidence |
|---|---|
| Warn when `areaBias` is set without `pick`, because the lowering ignores it then. | `RecipeGraphLowering.cpp:392-395` |
| Build the `KeyPositionBake` identity through the `ValueIdentity` encoding helpers, not by string concatenation. | `RenderPlanLowering.cpp:566-575` |
| Extract a pure `ScopeOf(before, after, recipe)` that returns a two-variant rebuild scope. It replaces `bool matchingKept` and the empty-string sentinel. | `ManagerApplication.cpp:166-185` |
| Give `StepOutputCache::Sweep` a name that says it renews and evicts. Add a native test of its retention rule. | `StepOutputCache.cpp:42-50` |
| Skip `InCameraView` and its metric when `cullOutOfView` is false. | `ShownThisTick` in `ManagerTick.cpp` |
| Move the Stopwatch and trace code out of `FreshRenderPlan`, `BuildActorRender`, `SyncActor` and `PlaceInstances` into a scoped timer helper. Remove the per-piece logging in `ReportApplied`. | `ManagerApply.cpp:698-799`, `:960-993`, `:1206-1232` |
| Done in W5 (2026-10-07): `Geometry::lost`, `AnyLiveGeometry` and `RecipesOfInactiveInstances` are deleted. | `planners/ActorMatches.h:23`, `:65`; `ActorMatches.cpp:44`, `:159` |
| Rename `RegressionActorFacts`, which collides with `Actors::ActorFacts`. | `Manager.h:44` |
| Done in W5 (2026-10-07): `RefOf` became `Actors::GeometryRefOf`, which checks the index and returns no ref past the end. | `Planning.cpp:28` |
| Make `g_nextModelToken` per-`Manager` state, or state why it is global in `REFERENCE.md`. | `ActorObservation.cpp:17` |
| Commit `IdleCamera` and move its function-local `static` hold out of `Menu.cpp`. | `Menu.cpp:332`; `src/engine/IdleCamera.{h,cpp}` are untracked |

## W9 list

| Document | Change |
|---|---|
| `docs/components/engine.md` | Add culling, `kOutOfView`, the scoped rebuild and `IdleCamera`. |
| `docs/components/render.md` | Add `StepOutputCache` retention and the lazy pipeline capture. |
| `docs/components/planners.md` | `:17` says `MatchActor` builds the "actor plan"; it builds `ActorMatches`. The flow at `:329` and `:332` names the matches parameter `plan`. The opcode table at `:240-245` lacks opcodes 45 to 49. |
| `docs/components/recipe.md` | `:256` says `BindContext` binds time, x and mean; it also binds identity. |
| `REFERENCE.md` | `:211` omits `rand`. `:215` lists only `shuffle` of the new functions. The `hueShift` parity note says "same arithmetic"; the CPU and GPU agree to rounding only, because the function uses sin and cos. |
