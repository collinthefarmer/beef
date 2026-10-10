# engine/

The game-facing adapter layer. It hooks CommonLibSSE and SKSE events and a
per-frame vfunc. It tracks which actors wear which **pieces**, matches
**recipes** to those pieces, ticks their **signals**, and drives `render/` to
draw the result. It also owns the `RecipeStore` and `RecipeEditor` pipeline
that the menu edits recipes through. Its `ALLOWS` row in `tools/gate.py` is
`'engine': ADAPTER + ('engine',)`. The row lets it include every engine-free
directory, `render/`, `PCH.h`, `Identity.h`, `Settings.h` and `SettingsFile.h`.
Only `menu/`, `main.cpp` and `SettingsFile.cpp` may include it.

## What it owns

**Event and hook wiring.** `main.cpp` calls `InstallHooks` once at data load.
`InstallHooks` returns failure when the resolved `PlayerCharacter` vtable
address is zero, before it writes a slot. `main.cpp` enables effects through
`Manager::SetEmissivePathEnabled` and calls `RegisterEventSinks` only after the
hook succeeds. `HookProblem` gives the log and the menu the same actionable
text. The failure lasts until restart.

**The manager.** `Manager` is the singleton that owns the engine side of every
actor. It holds the **tick core**'s state (`world_`, an
`Orchestration::World`), one `LiveActor` per installed actor (`live_`), and
one `ModelTable` per observed actor (`models_`). It runs the core once per
tick and carries out the core's **commands**. It publishes the **snapshot**
that the menu reads. It owns `AnimationSubscriptions`, which keeps each
observed actor's animation graph apart from the recipe **bindings**. Its
`WarningHistory` (`warningHistory_`) resets in `Manager::Clear`. Log
suppression never removes the underlying diagnostics.

**The core boundary.** The engine makes no tick decision. `src/orchestration/`
makes every decision that reads or writes state shared across actors, and
`docs/components/orchestration.md` maps it. The engine gives the core
**facts** (`Orchestration::TickFacts`) and executes the commands that the core
returns (`Orchestration::Command`). Only `world_ = std::move(out.world)` in
`Manager::RunTick` writes `world_`. The heartbeat, the census, the snapshot
and the regression facts read `world_` after the tick. No executor reads it.

**Fact gathering.** `Manager::GatherFacts` (`ManagerFacts.cpp`) builds one
`TickFacts` per tick. It advances the 64-bit tick clock (`ClockAfter`,
`tickClock_`). It reads the applied catalog once (`AppliedCatalog`). It moves
last tick's command results (`results_`), the forget requests (`forgets_`)
and the pending memory sample (`pendingSample_`) into the facts.
`Manager::ClaimsOf` reports each geometry that another system took. For each
actor in `stale_`, `Manager::StaleActorOf` observes the actor
(`ObserveActor`), plans it (`Actors::PlanActor` with its live claims) and
reads its reinstall demand (`ReinstallDue`). It traces `actor_decide` with
`observe_us`, `plan_us`, `pieces` and `planned`. `Manager::SightingOf` gives
one `Sighting` for each actor of `world_` and each stale actor: the role,
loaded 3D, the view (`InViewOf`), the screen fraction (`ScreenFractionOf`),
the distance to the player and the name. `TickSettingsOf` copies the settings
that the core reads.

**Stale actors.** `ApplicationService` still coalesces sync requests and
posts them through the `SessionQueue`. Its callback is `Manager::NoteStale`,
which only records the actor and its tokens in `stale_`. The next gather
observes and plans the actor. The `Observe` command calls
`ApplicationService::SyncNow`, so the observed actor arrives as stale in a
later tick. An unload event (`QueueForget`), `RetireAll`, a failed
`MarkReinstall` and the regression retire add the actor to `forgets_`.
`Manager::ForgetRequests` sends a forget request for an actor that `world_`
holds. For any other actor it calls `ForgetActorResources` at once.

**The agreement check.** `Manager::CheckAgreement` runs before the core in
each tick. It maps each `LiveActor` to its `HeldResources` (`HeldOf`) and
compares them with the settled world (`Orchestration::SettledWorld`,
`Orchestration::Disagreements`). A changed set of findings for an actor gives
one `resource_disagreement` trace and one `LogOnce` warning
(`ReportDisagreement`). An executor that finds no resource for its command
reports through the same function, with a `command` field.
`disagreements_` keeps the last findings per actor, and
`disagreementCount_` feeds `resource_disagreements` on the `target_holders`
heartbeat.

**One executor per command.** `Manager::Execute` (`ManagerCommands.cpp`)
runs the commands in order and collects their results for the next tick.
An executor looks up its actor in `live_` by form id and keeps no engine
pointer past its call. It branches only on whether an engine call succeeded.

| Command | Executor | What it does |
|---|---|---|
| `TickActor` | `RunTickActor` (`ManagerTick.cpp`) | Ticks each instance's signals. When `drawStacks` is set, it renders the pieces (`RenderPieces`) with `concealSlots` and `hideShells`. It hides the lights when `hideLights` is set, then updates them. It finishes the applications when `finishApplications` is set. Without `concealSlots` it drops the first-draw list, which publishes the render. |
| `StartResize` | `RunStartResize` | Calls `StartResize` (`ActorResize.h`) and reports `ResizeStarted` or `ResizeNotStarted`. |
| `SwapResize` | `RunSwapResize` | Calls `FinishResize`, traces `resolution_swap` (`TraceSwap`) and reports `ResizeSwapped` or `ResizeNotSwapped`. |
| `AbandonResize` | `RunAbandonResize` | Drops the resize in flight (`DropResize`). |
| `ReleaseIntermediates` | `RunReleaseIntermediates` | Releases the attached render's intermediates (`ReleaseIntermediates`, `ActorBudget.h`) and traces `hidden_release` or `budget_release`. |
| `SetAppliedLevel` | `RunSetAppliedLevel` | Gives every instance environment the level (`SetAppliedResolution`). |
| `DrawUnpublished` | `RunDrawUnpublished` (`ManagerTick.cpp`) | Draws resize stacks (`DrawResize`) and first-draw slots (`DrawFirstSlots`) in the given order within `budgetUs`, and reports `UnpublishedDrawn` per actor. |
| `BuildAwaitedRender` | `RunBuildAwaitedRender` | Builds the render from a finished plan job (`Manager::BuildAwaitedRender`) and reports `Installed`, `InstalledWithoutRender` or `PlanAwaited`. |
| `FailAwaitedRender` | `RunFailAwaitedRender` | Fails the requested outputs with the job's problem (`Manager::FailAwaitedRender`). |
| `Install` | `RunInstall` | Retires a store entry that is still there, then runs `Manager::InstallLiveActor`. It reports `InstallFailed`, `InstalledWithoutRender`, `PlanAwaited` or `Installed`. |
| `Retire` | `RunRetire` | Retires the actor's resources (`RetireActorResources`) and reports `Retired`. |
| `Forget` | `RunForget` | Calls `ForgetActorResources`: stops the animation observation, abandons the applications, retires the resources, clears the reinstall demand and drops the models. |
| `Observe` | `RunObserve` | Calls `ApplicationService::SyncNow`. |
| `KeepModels` | `RunKeepModels` | Keeps the models of the given plan in `models_` (`KeepModels`). |
| `SettleApplications` | `RunSettleApplications` | Clears the reinstall demand, discards the equip flag as asked, prepares the actor's applications and reconciles its animation observation. |

**The text log.** `LogOnce` (`LogOnce.h`) writes a fault line the first time
its text occurs and suppresses each repeat. Stack diagnostics, texture-demand
errors, render-input errors, render-plan cache mismatches, light-placement
faults and fire triggers on a missing node go through the manager's history.
`RecipeEditor` keeps its own history for the diagnostics of an edit.
`ReportApplied` writes one summary line per install or reinstall: the sync
kind, the pieces, the recipes, the geometries, the private and shared
materials, and the shells. `Manager::EmitCoreRecords` writes the core's log
lines and trace records. A resolution swap writes no line by default. With
`VerboseLogging`, `TraceSwap` writes one line with the new resolution level.
`VerboseLogging` also adds each piece, recipe, light and geometry of an
apply, and the retirement count.

**Per-actor render assembly.** The `Install` executor
(`Manager::InstallLiveActor`, `ManagerApply.cpp`) builds one `LiveActor` per
actor (`LiveActorFor`) and runs the **install program**. The program's stages
are pure functions in `planners/InstallProgram.h`. The adapters that read and
write a `LiveActor` are in `InstallStages.h`. `PlanSurfaces` plans each
geometry's placements, stacks and bindings, and `KeepSurfacePlan` copies the
plan onto the `LiveActor`. `BindSurfaces` installs each material and shell. It
reads the geometry's slot targets into a `BoundSurfaces` right after that
bind. `ActivateOutputs` sets each output's `OutputState` and lists the
**stack** requests and the chains. `StackSizeOf` sizes each stack, and
`RenderBuildRequestOf` gives each stack a side. `BuildActorRender` turns the
request into one `RenderPlan` and one `RenderInstance`. Every bound geometry
of the actor shares that `RenderInstance` through `GeometryInputs::render`.
`AttachmentsOf` matches each stack to its plan output, and `ApplyAttachments`
gives each placed **output** a `RenderOutput`. A stack that the plan does not
lower fails its output with the problem `stack was not lowered`. A plan
failure fails every requested output (`FailedOutputs`). `LiveActor::fullSizes`
keeps the full-resolution sizes of the stacks. `RenderBuildAt` chooses the
level from the plan's level costs and the command's `InstallBound`.
`PlanLightsOf` gives each instance's light problem and the lights to place,
and `PlaceLights` places them. `UnpublishedSlotsOf` lists the slots of the
first draw, and `Installed::total` is their count. `KeepRender` stores the
awaited job, or the render level and a new first draw (`StartFirstDraw`).

**Plan builds on a worker.** On a plan cache miss, `BuildActorRender` submits
the plan build to one worker thread and does not build the plan in the
executor (`RenderPlanFor`, `ManagerApply.cpp`). `PlanRouteOf`
(`planners/InstallProgram.h`) chooses the route. The job input
(`PlanBuildInput`, `planners/RenderPlanJob.h`) owns its graphs, recipe outputs
and identity facts (`PlanInputOf`). `JobQueue` (`JobQueue.h`) is the generic
queue: one worker, owned inputs, a function pointer as the job, a bound of 16
unfinished jobs, and results that the main thread takes. `PlanJobBook`
(`PlanJobs.h`) maps each open job to its `RenderPlanKey`, so actors with the
same key share one job. The waiting actor keeps the job in
`LiveActor::awaitedPlan` and has no render instance. Its materials keep the
vanilla look, `RenderGeometry` hides its shells, and its applications stay
`prepared`. `Manager::DrainPlanJobs` runs first in each tick. It stores each
built plan in the cache and turns each finished job into a core fact
(`PlanJobDoneOf`). The core answers with a `BuildAwaitedRender` command, which
reads the actor's surface plan back from its `LiveActor` (`SurfacePlanOf`,
`BoundSurfacesOf`) and runs the install program again from
`ActivateOutputs`. The key is the job's key, so the build is a cache hit.
`RenderBuildAt` chooses the install level then. A `FailAwaitedRender` command
runs the same activation and fails every requested output with the job's
problem. A stale job, one that a load or a recipe reload removed from the
book, is reported as built, so its actors ask again. Cache verification, a
disabled cache, an uncacheable key and a full queue build the plan in the
executor.

**Resolution resize.** A change of resolution level rebuilds only the actor's
`RenderInstance` (`ActorResize.h`). It does not reinstall the actor, so
pieces, recipe instances, shells, materials, lights, signals and clocks stay.
The core decides when a resize starts, when it swaps and when it is
abandoned. `StartResize` creates the new instance (`ResizeInFlight`) from the
current instance's plan, `GeometryInputs` and graphs at the new sizes
(`RenderInstance::AtSizes`). It gives each stack output of the actor a
`RenderOutput` on the new instance (`RenderOutput::DrawnBy`). A stack that a
merged slot chain covers counts as drawn at the start
(`DrawnByMergedChains`). `DrawResize` draws each stack of the new instance
once, with no hidden layers and no preceding stack, inside the
`DrawUnpublished` executor. The old instance stays attached and visible during
the resize. `FinishResize` moves each new `RenderOutput` into its
`PlacedOutput` or `MergedSlotStack`, sets `GeometryInputs::render` on each
geometry that held the old instance (`SwapRender`), conceals the stack slots
(`ConcealStackSlots`) and sets `LiveActor::renderLevel`. It refuses the swap
when the actor's stack outputs no longer match the resize. The old instance
goes when the last geometry lets go. A reinstall replaces the `LiveActor`, so
it drops a resize in flight. `RetireActorEffects` drops it too.

**Hidden first draw.** `KeepRender` starts a first draw on every attached
render instance (`StartFirstDraw`), after an install and after a plan
arrives. The first draw lists the slots that `UnpublishedSlotsOf` gives
(`HiddenSlot`). While the core keeps the actor unpublished, its `TickActor`
conceals the slots, hides the shells and the lights, and does not finish the
applications in view. `DrawFirstSlots` draws slots (`RenderSlotChain`) inside
the `DrawUnpublished` executor until the draw pace reaches the command's
budget (`MayDrawStack`, `planners/DrawPacing.h`). The first `TickActor`
without `concealSlots` drops the list and publishes the render.

**Render target budget.** The core runs the budget. The engine measures and
acts. `Manager::SampleBudget` (`ManagerHeartbeat.cpp`) reads the DXGI local
segment through `VideoMemoryProbe` (`render/VideoMemory.h`), the setting
`RenderTargetBudgetMiB`, the measured target bytes and the planned holding of
the rendered actors (`PlannedTargets`). `RunTick` takes the sample when the
heartbeat is due and gives it to the core as `TickFacts::memory`. The
`ReleaseIntermediates` executor releases an actor's intermediate targets
except the mesh bakes. A budget step down and a radius change reach the
engine as `StartResize` and `Observe` commands. When the DXGI reading is
unknown, `SampleBudget` writes one warning.

**The frame.** `Manager::OnFrame` (`ManagerTick.cpp`) runs from the hook. It
starts the texture lab on the first frame. It returns at once while
`ApplicationService::Loading` is true. It advances the regression run and
marks the heartbeat due once a second. `Manager::RunFrame` fires the due
equip finalizes, ticks the studio gesture and counts the frame. Once a second
it expires `CarriedTimes` and sweeps animation observation. At the tick
interval (`Settings::TickIntervalMS`) it calls `Manager::RunTick`, then
`SweepBoundMeshes`, `PublishSnapshot` and `ObserveRegression`. `RunTick` runs
also when no actor is installed, because installs happen only inside the
tick. In order, it drains the plan jobs (`DrainPlanJobs`), sweeps the
compositor's step outputs and the idle targets, samples the budget when the
heartbeat is due, gathers the facts, checks agreement, runs
`Orchestration::Tick`, emits the core's records, renders the previews and
executes the commands. `OnFrame` emits the heartbeat
(`Manager::EmitHeartbeat`, `ManagerHeartbeat.cpp`) after the frame.

**Culling.** `Manager::SightingOf` asks `InViewOf` (`planners/ViewFacts.h`)
whether an actor is in view. The player is always in view. With the setting
`CullOutOfView` off, every actor is in view and no test runs. Otherwise
`CameraViewOf` calls `InCameraView` (`ActorObservation.h`), which tests a
sphere around the actor against the camera frustum
(`RE::NiCamera::BoundInFrustum`). The sphere sits 64 units above the actor's
position, and its radius is the larger of the actor's 3D bound and 96 units,
times 1.25, plus 64. The inspected actor always counts as in view. The core
gives an actor out of view a `TickActor` without `drawStacks`, so it renders
nothing that tick but still updates its lights. `Metrics::CountActorView`
counts each camera test for the heartbeat (`actors_in_view`,
`actors_out_of_view`). An application whose actor is out of view when it
would report `kPrepared` reports `kOutOfView` instead (`FinishApplications`).

**Recipe CRUD.** `RecipeStore` holds the loaded `Recipe` list and does the
file I/O. `RecipeEditor` is the gesture, **edit** and undo layer that the
studio **intents** call through. It accepts document changes and queues actor
rebuilds. Application records report preparation and rendering separately.

**Republication.** `RecipeStore` keeps mutable documents apart from the
published **applied set**. Each republication, also on save, publishes a new
immutable `RecipeCatalog` (`AppliedCatalog`). `Manager::GatherFacts` reads the
catalog once per tick, and the `Install` executor uses the same catalog. A
**debug recipe** is a file under the top-level `recipes/debug` folder.
`AppliedCatalog(DebugRecipes::kHidden)` leaves the debug recipes out, and the
gather asks for it while the `ShowDebugRecipes` setting is off.
`LoadedRecipes()` always lists them, so the editor pages show them.
`LiveActorFor` stores the catalog in `LiveActor::catalog` with the
`ActorMatches` it was planned with, so each `RecipeId` indexes the catalog it
came from. `LiveInstance::recipe` comes from `SharedRecipe` and keeps the
catalog alive. `PlanSurfaces` and `PlanLightsOf` read `RecipesOf(state)`,
not `LoadedRecipes()`. `Manager::ChangeAndRebuildActors` runs its synchronous
mutation callback. It then asks each affected actor for a reinstall
(`MarkReinstall`). The actor keeps its effects until its reinstall replaces
them. An actor whose reinstall cannot be queued is added to `forgets_`.
Which actors it rebuilds follows
`ScopeOf` (`RebuildScope`, `recipe/Recipe.h`): when the edit names a recipe
and leaves every recipe's `MatchFacts` unchanged, only the actors applying
that recipe (`ActorsApplying`: a live instance of it, or a failed
application of it) are rebuilt, and the render plan cache is kept. Otherwise every
application actor is rebuilt, and the render plan cache and the plan job book
are cleared. The report
recipe id also scopes the diagnostics. A span from
`LoadedRecipes()` must not survive republication. Deferred editor requests capture owned ids and
edits and look documents up when they run.

**Instance slots.** `ActorMatches` owns instance identity and deduplication.
`LiveActor::instances` has one slot per planned instance. A slot stays empty
when preparation fails, so placements use the same indices in both tables.

**Load teardown.** `Manager::Clear` invalidates the `SessionQueue` generation
and retires actor effects. It empties the plan job book and drops the queued
plan jobs. A job that is running finishes, and the next drain drops its
result as stale. `main.cpp` calls `AbortWaitingRegressionRequest` before
`Manager::BeginLoad`. Posted callbacks borrow the singleton manager and
editor. Pending result objects keep their journal and report cancellation when
the queue discards their work. This relies on game-thread serialisation. The
generation check does not interrupt a callback that already runs.

**Operation journal.** `RecipeOperations.h` holds the engine-free
`FileOperationJournal` and the pending task guards that `RecipeEditor` uses.
Its readers hide pending edits and expose the pending state of file
operations. A late completion cannot overwrite a completed or canceled result.
Each file and edit list keeps at most 64 results and only the latest request
per recipe. Gesture publication requires an active matching request, so a
late publication cannot undo a cancellation.

**Application results.** An accepted edit is not a saved file or a rendered
result. File-operation results report persistence. Application records report
queued, prepared, rendered, unmatched, failed or canceled actor work. A
rendered record is adapter evidence. It does not prove the pixels shown in
game. Actor retirement reports the unfinished tokens of that `LiveActor` as
unmatched before it destroys the effects. This includes distance eviction
before the first render. `ApplicationService` checks both revision and actor
attempt, so newer rebuild attempts keep their state. Retirement does not
rewrite completed render or failure results.

**Application history.** `ApplicationService` keeps the newest
`kMaxTerminalApplicationRecipes` terminal recipe revisions and every record
with unfinished actors. `kMaxTerminalApplicationActors` caps the terminal
actor history. Recipe pruning checks each actor phase, because an aggregate
failure can still hold queued or prepared work. Supersession carries pending
and failed actors into the replacement, together with the manager's current
loaded and applied actors. Cancellation releases actor lists and old problem
text and keeps the token and canceled phase. `ApplicationService::Resume`
removes canceled history. These limits bound history, not the active workload.

**Geometry loss.** `RetireGeometry` retires a geometry's bindings before it
releases the geometry's inputs and prepared placement stacks. Placement
indices and output diagnostics stay. Sibling geometry resources and shared
instance lights stay. `Manager::ClaimsOf` runs in each tick's gather. It
finds each geometry whose material or shell another system replaced
(`TakenByAnotherSystem`). It retires that geometry's binding at once and
reports a **claim** (`ClaimedGeometry`). The core adds the claim to the
actor's state with `Actors::WithClaim` and asks for a new observation. The
next plan leaves the claimed geometry out. A claim ends when its piece gets a
new model or the game loads.

**Save.** `SaveRecipe` calls engine-free `WriteRecipeFile` with the
destination and an explicit import-promotion flag. An existing file under
`Identity::UserRecipeFolder()` keeps its destination, including nested paths.
Every other source saves to `<id>.json` in the user folder, so shipped and
imported source files stay. Only an imported-folder source has its import
metadata cleared. The store adopts the user destination only after a
successful write. Save writes a prepared copy without temporary paint masks
and returns the saved document only after the write succeeds. A refusal
leaves the working recipe and import metadata unchanged.

**Diagnostics across edits.** `RecipeStore` keeps file-decoding diagnostics
apart from model checks. Edits and undo refresh model diagnostics and keep the
file problems. A successful save clears the old decoding diagnostics and
revalidates. A failed save keeps them. Reload replaces them with the new file
result. Invalid batches and no-op edits leave history and revisions unchanged.

**Revert, rename and delete.** Revert calls engine-free `ReadRecipeFile`
before it changes store state. Rename and delete call `RenameRecipeFile` and
`DeleteRecipeFile` before they change or unpublish a document. A failure
returns a recipe diagnostic that names the path. Rename refuses an existing
destination. Files outside the user folder stay on disk. `RecipeEditor`
advances history, view and revision only after the store accepts the
operation. `studio/DocumentRevisions` owns revision lookup, advance and reset.
Reload and game-load cancellation advance the shared epoch, so old indexed
edits, gestures and paint assignments cannot target a reused recipe id.

**Duplicate definitions.** `LoadFile` uses `AppendDefinition`. A later
decoded file with the same id replaces the earlier record and moves to the
later traversal position. This holds also when recipe-level validation holds
the record back. Unreadable files are skipped.

**Menu preflight.** `CheckMenuFramework` checks loaded-module presence and the
`MenuFrameworkExports` inventory before menu callbacks register. It reports
missing exports by name. It does not check ABI or version compatibility.

**Animation observation across rebuilds.** Observation survives the
`Retire` and `Install` of a reinstall. The `SettleApplications` executor
reconciles it against the new live state through `ReconcileAnimationEvents`.
`Manager::ForgetActorResources` ends observation and discovery even when the
actor has no `LiveActor`. `Manager::Clear` ends all registrations.
`SweepAnimationEvents` repairs changed or missing graphs once a second and
retires abandoned observation after pending application work ends.

**Native coverage.** Most of `engine/` is engine-facing and not
native-tested. `cmake/Native.cmake` compiles eight engine-free units into the
`BeefEngineServices` library for `ctest --preset native`:
`SessionQueue.cpp`, `ApplicationService.cpp`, `TextFile.cpp`,
`PluginEvents.cpp`, `RecipeFiles.cpp`, `InstanceTime.cpp`,
`MenuDependency.cpp` and `PlanJobs.cpp`. `engine_jobqueue` tests the
header-only `JobQueue.h`. Six suites add engine sources against test doubles:
`engine_wornkeys` (`WornKeys.cpp`, `EnchantmentEffects.cpp`),
`engine_animationsubscriptions` (`AnimationSubscriptions.cpp`,
`ManagerAnimation.cpp`), `engine_editorintegration` (`RecipeEditor.cpp`,
`RecipeStore.cpp`, `LogOnce.cpp`), `engine_hooks` (`Hooks.cpp`),
`engine_liveretirement` (`LiveActor.cpp`) and `engine_installstages`
(`LiveActor.cpp`, `InstallStages.cpp`). The tick core itself is native-tested
under `tests/orchestration/`. `engine_editorintegration` does not simulate
actor rebuilds or Skyrim form discovery; see
[the harness boundary](../build.md#native-authoring-integration).

## Data

### Event and hook wiring

These declarations bring game and plugin events into the manager. The sinks
and the hook forward each event to a `Manager` queue call. The plugin-event
parser is engine-free, so the native suite feeds it malformed messages.

| Member | Description | Declared in |
|---|---|---|
| `RegisterEventSinks` | Registers the equip, object-loaded, hit and node-update sinks. Each sink calls `Manager::MarkStale`, `Manager::QueueEquipFinalize` or `Manager::QueueEvent`. | `Events.h` |
| `InstallHooks` | Installs the `PlayerCharacter::Update` vfunc hook that calls `Manager::OnFrame` every frame. | `Hooks.h` |
| `HookProblem` | Returns the hook failure text, or an empty view when the hook succeeded. | `Hooks.h` |
| `PluginEventMessage` | The raw message another SKSE plugin sends: `version`, `actor`, `id`, `type` and three `value` floats. | `PluginEvents.h` |
| `PluginEventType` | The closed payload tag: `kScalar`, `kVec2`, `kVec3`. | `PluginEvents.h` |
| `PluginEvent` | A parsed message: the target actor (0 for a broadcast) and its `EventRecord`. | `PluginEvents.h` |
| `ParsePluginEvent` | Checks version, bounded id (`kPluginEventIdMax`), type tag and finite components, then returns a `PluginEvent`. `main.cpp` queues the result per actor or as a broadcast. | `PluginEvents.h` |
| `AnimationSubscriptions` | Owns actor handles, graph references and registrations for animation-graph events. `Reconcile` follows a replaced graph; `Stop` and `Clear` detach. | `AnimationSubscriptions.h` |
| `AnimationRegistration` | One observed actor id and an atomic `active` flag that detaching clears. | `AnimationSubscriptions.h` |
| `AnimationEvent` | One animation tag and payload with its registration. `Current` is false after the registration detaches. | `AnimationSubscriptions.h` |

### The manager and application bookkeeping

`Manager` holds the tick core's world, one `LiveActor` per installed actor
and the queues that feed the next tick. It hands per-actor queueing to
`ApplicationService`, which owns the only runtime `SessionQueue`. The menu
reads the manager's state through `Manager::Snapshot` and `Manager::Status`.

| Member | Description | Declared in |
|---|---|---|
| `Manager` | The singleton. It runs the tick core, executes its commands, holds the resources of each actor and owns the `RecipeEditor`. | `Manager.h` |
| `Manager::world_` | The core's `Orchestration::World`. Only `RunTick` writes it, with the core's output. | `Manager.h` |
| `Manager::live_` | The per-actor resource store: one `LiveActor` per actor with installed effects. | `Manager.h` |
| `Manager::models_` | One `ModelTable` per observed actor. `KeepModels` prunes it, and `ForgetActorResources` erases it. | `Manager.h` |
| `Manager::results_` | The results of this tick's commands. The next gather moves them into `TickFacts::results`. | `Manager.h` |
| `Manager::stale_` | The actors that `NoteStale` recorded, with their application tokens. The next gather consumes them. | `Manager.h` |
| `Manager::forgets_` | The actors to forget. The next gather turns them into forget requests or forgets them at once. | `Manager.h` |
| `Manager::heartbeatDue_`, `Manager::pendingSample_` | The heartbeat cadence and the memory sample that the next tick gives to the core. | `Manager.h` |
| `Manager::tickClock_` | The 64-bit tick clock built from 32-bit `NowMS` readings (`TickClock`, `ClockAfter`). | `Manager.h`, `planners/TickClock.h` |
| `Manager::disagreements_`, `Manager::disagreementCount_` | The last agreement findings per actor, and the count of reported disagreements since the game started. | `Manager.h` |
| `GatheredFacts` | One tick's `TickFacts`, the application tokens of each stale actor, the catalog, the waiting installs before the tick and the observation time. | `Manager.h` |
| `TickContext` | What every executor reads: the settings, the studio view, the inspected actor, the time, the resume flag and the `GatheredFacts`. | `Manager.h` |
| `CommandResults` | The results that one executor reports. | `Manager.h` |
| `HeartbeatReading` | The drained metrics, the memory sample and the interval of one heartbeat. | `Manager.h` |
| `Manager::Status` | The debug overlay counts: actors, **pieces**, recipes, geometries, shells, lights and tick cost in ms. It also carries the `emissivePath`, `layoutVerified` and `textureLab` flags. | `Manager.h` |
| `Manager::Snapshot` | A `Studio::Snapshot` plus the `TextureRef`s that keep its texture handles alive. | `Manager.h` |
| `ApplicationService` | Tracks one `ApplicationRecord` per recipe-affecting change, keyed by recipe id and by actor id. `SyncNow` runs an actor's sync at once; its callback records the actor as stale. | `ApplicationService.h` |
| `kMaxTerminalApplicationRecipes`, `kMaxTerminalApplicationActors` | The terminal history caps, 256 each. | `ApplicationService.h` |
| `ApplicationToken` | Names one change by recipe id, revision, actor id and attempt. | `studio/ApplicationRecord.h` |
| `ApplicationRecord` | One tracked change: its token, its phase, and the per-actor phases and problems. | `studio/ApplicationRecord.h` |
| `ApplicationPhase` | `kQueued`, `kPrepared`, `kRendered`, `kFailed`, `kUnmatched`, `kCancelled`. | `studio/ApplicationRecord.h` |
| `SessionQueue` | Serialises per-actor sync requests onto the SKSE task interface. It holds the equip-finalize deadlines, the reinstall demands (`MarkReinstall`, `ReinstallDue`, `ClearReinstall`) and the load gate (`BeginLoad`, `Resume`). | `SessionQueue.h` |

### Texture-memory controls

Two controls cap texture memory for large crowds. Distance eviction keeps the
applied set near the player. Per-slot sizing shrinks each stack's render size
below the material's native size. The core decides eviction, levels and the
budget. The engine gives it the measured facts.

| Member | Description | Declared in |
|---|---|---|
| `Loaded3D`, `DistanceToPlayer` | The engine facts that observation and the sighting share: the actor is not deleted and has 3D; the distance to the player (none for the player). | `ActorObservation.h` |
| `EvictionFor`, `EvictionAction` | The pure decision: `kEvict` past the distance, `kRestore` inside 80% of it, `kNone` otherwise. The distance is `EvictDistance` or the smaller budget radius (`EffectiveEvictDistance`). | `planners/Eviction.h`, `planners/BudgetRadius.h` |
| `Settings::evictDistance` | The eviction radius in game units. 0 disables eviction. `kMaxEvictDistance` (20000) caps it. | `Settings.h` |
| `StackSizeFacts`, `StackSizeOf`, `StackSizeFactsOf`, `ScaleResolutionOf` | The size of one stack: the largest native map side of the material and the side of the slot's base map, if usable. `StackSizeOf` divides the largest side by the texture scale and the slot's `Resolution` and clamps the result over the base map. `StackSizeFactsOf` gathers the facts from the engine textures, and `ScaleResolutionOf` converts `Settings::textureScale`. | `planners/InstallProgram.h`, `ManagerApply.cpp`, `InstallStages.h` |
| `DefaultSlotResolution`, `ResolutionDivisor` | The per-slot default `Resolution` and its divisor (`kFull` 1, `kHalf` 2, `kQuarter` 4). | `recipe/Recipe.h` |
| `SurfaceOutput::resolution` | An optional `Resolution` that overrides `DefaultSlotResolution` for that output's slot. | `recipe/Recipe.h` |
| `Settings::renderTargetBudgetMiB` | The cap on the render target allowance in MiB. 0 sets no cap. `kMaxRenderTargetBudgetMiB` (65536) caps it. A regression run can replace it (`RegressionRenderTargetBudgetMiB`). | `Settings.h` |
| `Settings::unpublishedDrawBudgetMs` | The time that one tick may spend on unpublished draws, from 1 to 16 ms (`kMinUnpublishedDrawBudgetMs`, `kMaxUnpublishedDrawBudgetMs`). The core passes it in `DrawUnpublished::budgetUs`. | `Settings.h` |
| `Manager::SampleBudget` | Reads the DXGI memory, the cap, the measured target bytes and the planned holding (`PlannedTargets`) into a `BudgetSample`. | `ManagerHeartbeat.cpp` |
| `PlannedTargets` | The targets and bytes that the rendered actors' attached plans hold, with intermediates as held. The heartbeat writes it as `engine_estimated_bytes`. | `ManagerHeartbeat.cpp` |
| `HeldNow`, `InstancesOf`, `ReleaseIntermediates` | The targets that the planned figure counts for an actor, the actor's render instances with a resize in flight, and the release of its intermediates. | `ActorBudget.h` |

### Actor and worn-piece state

The `Install` executor fills one `LiveActor` per actor from the observed
pieces and the planned matches. Each `TickActor` advances each
**instance**'s signals in place and renders each bound geometry through its
bindings. Free functions in `LiveActor.h` resolve and retire these records
without the manager.

| Record | Description |
|---|---|
| `LiveActor` | One installed actor: its handle, its full-resolution sizes (`fullSizes`), its optional `ResizeInFlight` (`resize`), its optional `FirstDraw` (`firstDraw`), the worker job it waits for (`awaitedPlan`), the level of its attached render (`renderLevel`) and that render's `IntermediateHolding` (`holding`). It also holds the `RecipeCatalog` it was planned against, its `ActorMatches`, its pieces, instances and **placements**, and the `ApplicationToken`s of its in-flight applications. `RecipesOf` returns the catalog's recipes. |
| `ResizeInFlight` | A resize in flight: the new level (`to`), the new `RenderInstance`, and one `ResizedStack` per stack output of the actor. |
| `ResizedStack` | One stack output on the new instance and whether it has drawn once. |
| `ResizeSwap` | What `FinishResize` reports: the two levels, the stack count, the drawn count and the `TargetCost` of the new instance. |
| `FirstDraw` | A first draw in progress: one `HiddenSlot` per slot. |
| `HiddenSlot` | One slot of a first draw: its piece, geometry and slot indices, and whether it has drawn once. |
| `LivePiece` | One worn armor clone: its `Actors::PieceRef` (`ref`), its `ModelToken` (`model`), the armor form id and name, the optional addon `FormKey`, the enchantment form id and its `LiveGeometry`s. |
| `LivePieceId` | A typed piece index (`enum class`, `std::size_t`). |
| `LiveGeometry` | One bound geometry: its `Actors::GeometryRef` (`ref`, set by `LivePiecesOf` through `Actors::GeometryRefOf`), engine geometry and shader-property pointers, `GeometryInputs`, `MaterialBinding`, `ShellBinding` and shell owner, its `GeometryPlan`, `GeometryStackPlan`, merged slot stacks and `BindingPlan`, and its `PlacementId`s. Its `lost` flag marks it retired. |
| `MergedSlotStack` | One merged slot chain on a geometry: the surface and slot, its chain links and layer starts, and its `RenderOutput`. |
| `LiveInstance` | One recipe matched onto the actor: the `Recipe` pointer, enchantment, effect scope, `RecipeGraph`, `SignalState` and `ActorEnvironment`. It also holds the optional `LightBinding`, the light problem text, and the timing (`startMS`, `lastTime`). |
| `LivePlacement` | The outputs bound onto one geometry: a `GeometryId` and one `PlacedOutput` per placed output. |
| `PlacedOutput` | One placed output: its recipe output index, its `RenderOutput` (`stack`) and its `OutputState` (`state`): inactive with a problem, pending, rendered, or failed with a problem. |
| `ResolvedPlacement` | The placement index and instance index that `ResolvePlacement` returns. |
| `ActorEnvironment` | The `SignalEnvironment` a `LiveInstance` ticks against. It reads actor values, actor state, the enchantment and effect shaders from the game. It holds the applied resolution level, which starts at full and which `SetAppliedResolution` sets, and returns it as the `resolutionLevel` actor state. |

### Per-actor render assembly

These records serve the install program and the tick's slot writes. The
`Install`, `BuildAwaitedRender` and `FailAwaitedRender` executors use the
first group to build one render plan per actor. `TickActor` and
`DrawUnpublished` use the second group to render each **slot**'s chain and
write it to the material.

| Record | Description | Declared in |
|---|---|---|
| `LocatedGeometry`, `LocateGeometry` | A geometry found by `GeometryId`: its piece, its `LiveGeometry` and the id. | `InstallStages.h` |
| `StackInputs` | One stack's engine inputs: the instance's recipe and `RecipeGraph`, its `SurfaceOutput`, and the geometry's `GeometryInputs` with the instance scope and enchantment values. | `ManagerApply.cpp` |
| `ActorRenderRequest` | One actor's render request: the surface plan's geometries, the `OutputActivation`, the `RenderBuildRequest`, one `StackInputs` per stack and the `GeometryInputs` of every geometry. `BuildActorRender` reads it. | `ManagerApply.cpp` |
| `SurfacePlanOf`, `KeepSurfacePlan`, `WriteOutputs`, `CompiledInstancesOf`, `BoundSurfacesOf` | Read a `LiveActor` into the install program's records and write the results back. `KeepSurfacePlan` copies no `GeometryPlacementPlan::placed`. | `InstallStages.h` |
| `RenderOutcome`, `RenderNotBuilt`, `PlacedRender` | The result of `BuildActorRender`: the `RenderBuild`, the `Orchestration::PlanAwaited` job, or `RenderNotBuilt` when no stack or no plan gave a render instance. `PlacedRender` adds the slots of the first draw. | `Manager.h` |
| `JobQueue`, `JobId`, `JobDone`, `JobRefusal`, `JobTiming` | The generic worker queue, a job's id, a finished job with its result or error text, the reason a job was refused (`kFull`, `kNoWorker`), and the queue wait and work time. | `JobQueue.h` |
| `PlanQueue`, `kPlanJobCapacity` | The plan build queue and its bound of 16 unfinished jobs. | `PlanJobs.h` |
| `OpenPlanJob`, `PlanJobBook`, `BookAfterClose`, `OpenJobFor`, `Opened`, `Closed` | The open plan jobs with their keys, graphs and submitting actor; the job for a key; the book with a job added or removed, and the closed job. | `PlanJobs.h` |
| `PlanJobDoneOf`, `PlanJobIdOf`, `JobIdOf` | A finished job as the core's `PlanJobBuilt` or `PlanJobFailed` (a stale job counts as built), and the conversions between `JobId` and `Orchestration::PlanJobId`. | `PlanJobs.h` |
| `TickFrame` | The settings, view, time and resume flag of one tick. | `ManagerTick.cpp` |
| `SlotRenderView` | The view, whether any **layer** is hidden, and `Settings::publishEffects`. | `ManagerTick.cpp` |
| `RenderFlags` | The `TickActor` flags that rendering reads: `concealSlots` and `hideShells`. | `Manager.h` |
| `SlotChain` | The rendered outputs of one slot: their `SurfaceOutput`s, resolved values, the final texture, a shown flag, and a pending flag when a stack returned `kPending`. | `ManagerTick.cpp` |
| `DrawPace`, `MayDrawStack`, `Drew` | The unpublished draw time and stack count of the current tick, and the rule that draws another stack within a budget. | `planners/DrawPacing.h` |
| `SlotWrite` | The texture, scalars and colour that `WriteSlot` writes to one `SlotTarget`. | `ManagerTick.cpp` |

### Instance time

These declarations keep an instance's clock across a rebuild. `Manager` stores
the time of a retired instance and gives it to the matching new instance.

| Member | Description | Declared in |
|---|---|---|
| `CarriedTimes` | A bounded store keyed by `CarriedTimeKey`. It holds a phase and a retirement time per entry. `Take` consumes an entry; `Expire` drops old entries. | `InstanceTime.h` |
| `CarriedTimeKey` | Actor id, recipe id, enchantment form id and effect scope. | `InstanceTime.h` |
| `kCarryWindowMS`, `kMaxCarriedInstanceTimes` | The carry window (2000 ms) and the entry cap (4096). | `InstanceTime.h` |
| `ClockOffsetMS` | Converts carried seconds at a speed into a checked millisecond offset. | `InstanceTime.h` |
| `InstanceSpeed` | Multiplies the animation, view and recipe clock speeds. | `Clock.h` |

### CRUD and results

The free functions in `RecipeStore.h` own the loaded `Recipe` list and its
file I/O. `RecipeEditor` is the layer that studio intents call for every
edit. Both report through small result records that the menu reads back.

| Member | Description | Declared in |
|---|---|---|
| `RecipeStoreStatus` | The load counts: loaded, with errors, held back, unresolved, imported, debug. `LoadRecipes` and `GetRecipeStoreStatus` return it. | `RecipeStore.h` |
| `RecipeOrigin` | One recipe's file path and its load `Diagnostic`s. `OriginOf` returns it. | `RecipeStore.h` |
| `RecipeEditor` | Drives gestures, edits, undo and redo, save and reload, paint sessions and view commands. | `RecipeEditor.h` |
| `FileOperationJournal` | The mutex-guarded journal of file results, edit results and the active gesture. | `RecipeOperations.h` |
| `PendingFileOperation`, `PendingRecipeEdit`, `PendingGestureTask` | Guards that report cancellation from their destructor when their task never runs. | `RecipeOperations.h` |
| `Studio::FileOperationResult` | The outcome of one save or revert: request id, recipe, `FileAction`, `FileOperationState`, written path and error. | `studio/FileOperation.h` |
| `Studio::RecipeEditResult` | The outcome of one edit request: request id, recipe and error. | `studio/EditResult.h` |

### Regression fixture

These declarations back the unattended in-game regression run. The run
submits a request, and the manager reports a `Regression::RequestState` when
the actor's application finishes.

| Member | Description | Declared in |
|---|---|---|
| `RegressionRequest`, `RequestKind` | One request: id, actor, kind (apply or retire), previous attempt, dispatched flag and state. `BeginRequest`, `Pending` and `StateOf` start it and read it by id, so an old id never sees a newer result. | `RegressionRequest.h` |
| `SubmitRegressionRequest`, `RegressionRequestState`, `AbortRegressionRequest` | Start, read and abort the one current request. | `RegressionRequests.h` |
| `SoloRegressionRecipe`, `EndRegressionSolo` | Solo the recipe under test in the studio, and restore the isolation from before. | `RegressionRequests.h` |
| `AbortWaitingRegressionRequest` | Aborts the current request on game load when it is still waiting. | `RegressionRequests.h` |
| `RegressionFixture` | The demo cuirass form. | `RegressionRequests.h` |
| `ReadRegressionRun`, `FinishRegressionLoad`, `AdvanceRegressionRun` | Read the run file, start the run after its save loads, and advance it once per player update. | `RegressionRun.h` |
| `RunWorld`, `Observe`, `Execute`, `ReleaseWorld`, `ForgetWorldAfterLoad`, `QuitGame` | The game side of a run: the spawned actors and crowd, the return marker, the current request and the started studio work; one `Regression::Observation` per frame; one `Regression::Command` carried out; the spawned actors and marker deleted at the end or forgotten after a load. | `RegressionWorld.h` |
| `Manager::RegressionActivity` | Pending applications, an open paint session, an active gesture, pending file operations, and the outcome of the edit, gesture and file operation the run started. | `Manager.h` |
| `Manager::RegressionRecipe` | Whether a recipe is loaded and unsaved, and its first layer's opacity when that is a number. | `Manager.h` |
| `Manager::StartRegressionDuplicate`, `StartRegressionSave`, `StartRegressionDelete` | Start the editor's duplicate, save and delete for the regression's scratch recipe. | `Manager.h` |
| `Manager::StartRegressionEdit`, `StartRegressionGesture`, `StartRegressionPaint` | Start the same editor work the menu starts: an opacity edit, an opacity slider gesture left open, a paint preview with the recipe's first key. | `Manager.h` |
| `WearerFacts`, `Manager::RegressionActors` | For each asked actor, from one copy of the application snapshot: whether the manager holds live state, and the newest application revision that rendered the fixture. | `Manager.h` |
| `Manager::QueueRegression` | Adds a retire request's actor to `forgets_`, or marks an apply request's actor stale on the session queue. | `Manager.h` |
| `Manager::ObserveRegression` | Reads a newer application record for the actor and finishes the request as `PASS`, `FAIL`, `BLOCKED` or `ABORTED`. A retire passes once neither `forgets_`, `live_` nor `world_` holds the actor, and fails after 10 ticks (`kRetireTicks`). | `Manager.h` |
| `Manager::SoloInEditor`, `Manager::EndSoloInEditor` | Isolate the recipe under test and restore the earlier isolation. | `Manager.h` |

## How an actor and a recipe edit flow

```
(a) a game event reaches render

RE::TESEquipEvent ──▶ EventSink::ProcessEvent                  Events.cpp
  │
  ▼
Manager::MarkStale ──▶ ApplicationService::MarkStale            Manager.cpp, ApplicationService.cpp
  │   SessionQueue::MarkStale posts a task onto the SKSE task interface  SessionQueue.cpp
  ▼
ApplicationService::RunActor ──▶ Manager::NoteStale (stale_)    ApplicationService.cpp, ManagerFacts.cpp

PlayerCharacter::Update hook (every frame)                      Hooks.cpp
  │
  ▼
Manager::OnFrame ──▶ RunFrame                                   ManagerTick.cpp
  │   once a second: CarriedTimes::Expire, SweepAnimationEvents
  │   at the tick interval:
  ▼
Manager::RunTick                                                ManagerTick.cpp
  │   DrainPlanJobs: RenderPlanCache::Store, PlanJobDoneOf      ManagerApply.cpp, PlanJobs.cpp
  │   heartbeat due: SampleBudget                               ManagerHeartbeat.cpp
  ▼
Manager::GatherFacts                                            ManagerFacts.cpp
  │   ClaimsOf, ForgetRequests
  │   StaleActorOf per stale actor: ObserveActor, Actors::PlanActor
  │   SightingOf per actor: InViewOf, ScreenFractionOf          planners/ViewFacts.h
  ▼
Manager::CheckAgreement ──▶ Orchestration::Disagreements        ManagerFacts.cpp
  ▼
Orchestration::Tick ──▶ world_, commands, records               orchestration/Tick.cpp
  ▼
EmitCoreRecords, then Manager::Execute per command              ManagerFacts.cpp, ManagerCommands.cpp
  │
  ├─ Install ──▶ InstallLiveActor                               ManagerApply.cpp
  │     LiveActorFor: LivePiecesOf, the plan's matches, BuildInstances
  │     PlaceInstances: PlanSurfaces ──▶ KeepSurfacePlan ──▶ BindSurfaces
  │       RenderRequestOf: ActivateOutputs, StackSizeOf, RenderBuildRequestOf
  │       BuildActorRender: PlanKeyOf, PlanRouteOf; a cache hit, or
  │         PlanInputOf ──▶ PlanQueue::Submit (the actor waits in awaitedPlan)
  │         RenderBuildAt, AttachRender, AttachmentsOf ──▶ ApplyAttachments
  │     KeepRender (UnpublishedSlotsOf ──▶ StartFirstDraw)
  │     PlanLightsOf ──▶ PlaceLights; live_[actorID] = LiveActor
  │
  ├─ TickActor ──▶ RunTickActor                                 ManagerTick.cpp
  │     TickInstance per LiveInstance
  │     RenderPieces ──▶ RenderGeometry
  │       RenderInstance::BeginFrame, UpdateRenderInputs
  │       WriteSlots ──▶ RenderSlotChain ──▶ Compositor::Render   render/Compositor.cpp
  │       WriteSlot into the SlotTarget; PoseShell                render/Binding.cpp
  │     UpdateLights, FinishApplications                        ManagerApplication.cpp
  │
  └─ DrawUnpublished ──▶ DrawResize, DrawFirstSlots              ActorResize.cpp, ManagerTick.cpp
  ▼
results_ for the next tick; OnFrame emits the heartbeat         ManagerHeartbeat.cpp

(b) a menu edit reaches an applied recipe

studio EditRecipe intent ──▶ IntentPerformer                    menu/Menu.cpp
  │   Manager::Editor().EditRecipe posts a task
  ▼
RecipeEditor::ApplyEdits                                        RecipeEditor.cpp
  │   MutableRecipe(id) ──▶ Studio::PrepareEdits                  RecipeStore.cpp, studio/Edits.cpp
  ▼
Manager::ChangeAndRebuildActors                                 ManagerApplication.cpp
  │   callback: history push, move the prepared Recipe in,
  │             RefreshRecipeDerivedState(id)                     RecipeStore.cpp
  │             (revalidates, resolves forms, drops the cached
  │              RecipeGraph, republishes)
  │   ScopeOf, ApplicationService::Begin
  │   ApplicationService::MarkReinstall per actor
  ▼
rejoins (a) at Manager::NoteStale; the core retires and installs the
actor, and PlanActor reads the edited Recipe from the new RecipeCatalog
```

## The files

| Concern | Key files |
|---|---|
| Event sinks and hooks | `Events.h`/`.cpp` (equip, object-loaded, hit and node-update sinks), `PluginEvents.h`/`.cpp` (the inter-plugin message contract and parser), `Hooks.h`/`.cpp` (the `PlayerCharacter::Update` hook), `AnimationSubscriptions.h`/`.cpp` (animation-graph observation) |
| The manager | `Manager.h`, `Manager.cpp` (construction, queue calls, `Clear`, load, the menu's idle-camera hold), `PhaseTrace.h` (`PhaseTrace`: times named phases with `Measure` and emits one metrics or application trace event), `ManagerTick.cpp` (`OnFrame`, `RunFrame`, `RunTick`, the `TickActor` and `DrawUnpublished` executors, `RenderGeometry`, slot writes, `UpdateLights`), `ManagerFacts.cpp` (`NoteStale`, `GatherFacts`, `SightingOf`, `StaleActorOf`, `ClaimsOf`, `ForgetRequests`, `CheckAgreement`, `EmitCoreRecords`), `ManagerCommands.cpp` (`Execute` and the other command executors), `ManagerHeartbeat.cpp` (`EmitHeartbeat`, `FlushMetricsHeartbeat`, `SampleBudget`, `PlannedTargets`, `ReportRenderMemory`), `ManagerApply.cpp` (`InstallLiveActor`, `BuildAwaitedRender`, `FailAwaitedRender`, `RetireActorResources`, `ForgetActorResources`, live pieces from observation, `PlaceInstances`, `BuildActorRender`, `DrainPlanJobs`, lights), `InstallStages.h`/`.cpp` (the install program's adapters over a `LiveActor`), `ManagerApplication.cpp` (`ChangeAndRebuildActors`, application bookkeeping), `ActorResize.h`/`.cpp` (the resolution resize and the first-draw list of one actor), `ActorBudget.h`/`.cpp` (the held targets and the release of one actor), `TargetCensus.h`/`.cpp` (the `target_holders` census), `JobQueue.h` (the generic worker queue), `PlanJobs.h`/`.cpp` (the plan job book and the job facts), `ManagerAnimation.cpp` (animation events, `RetireAll`), `ManagerEvents.cpp` (`Fire`, `FireAt`, `QueueEvent`, `QueueBroadcast`), `ManagerInspection.cpp` (`RequestMesh`), `ManagerSnapshot.cpp` (`GetStatus`, `BuildSnapshot`, `PublishSnapshot`, `Watch`), `LogOnce.h`/`.cpp` (`LogOnce`: a fault line once per text, through a `WarningHistory`) |
| Application and session bookkeeping | `ApplicationService.h`/`.cpp` (token tracking, rejection handling, history pruning), `SessionQueue.h`/`.cpp` (per-actor task serialisation) |
| Actor and worn-piece state | `ActorObservation.h`/`.cpp` (`ObserveActor`, `ModelTable`, `ModelOf`, `MaterialLayout`), `LiveActor.h`/`.cpp` (live records, `RetireGeometry`, `SetAppliedResolution`, `ResolvePlacement`), `Environment.h`/`.cpp` (`ActorEnvironment`), `WornKeys.h`/`.cpp` (`WornKeysOf`), `EnchantmentEffects.h`/`.cpp` (`EnchantmentValueFor`), `InstanceTime.h`/`.cpp` (`CarriedTimes`) |
| Recipe CRUD | `RecipeStore.h`/`.cpp` (load, publish, mutate, save, `RefreshRecipeDerivedState`), `RecipeFiles.h`/`.cpp` (checked read, write, rename and delete), `RecipeOperations.h` (the operation journal and task guards), `RecipeEditor.h`/`.cpp` (gestures, edits, undo, paint, view) |
| Form and game-object lookups | `EngineForms.h`/`.cpp` (`FormKeyFor`, `LookupForm`, `ShaderFor`, `RecordFrom`), `GameObjectService.h`/`.cpp` (game-object and animation-event catalogs), `InputCatalog.h`/`.cpp` (`BuildActorValueSamples`), `Tweaks.h`/`.cpp` (`EditorIdOf`, `TweaksEditorIdsAvailable`) |
| Regression fixture | `RegressionRequests.h`/`.cpp` (the request store and the manager's regression calls), `RegressionRequest.h` (`RegressionRequest`), `ManagerRegression.cpp` (the manager's regression observations and studio work), `RegressionRun.h`/`.cpp` (the unattended run's file, load and result lines over `regression/Run.h`), `RegressionWorld.h`/`.cpp` (its observations and commands) |
| Small utilities | `Clock.h`/`.cpp` (`NowMS`, `InstanceSpeed`), `TextFile.h`/`.cpp` (`ReadText`, `WriteText`, bounded by `kMaxTextFileBytes`; `CreateText` and `SetAside`, which never replace a file), `MenuDependency.h`/`.cpp` (`CheckMenuFramework`) |

## See also

- `REFERENCE.md` → *Engine events, hooks and the manager*, *The engine runs
  the core*, *The install program*, *Bindings*,
  *Recipe CRUD travels one pipeline*, *Menu dependency preflight*,
  *Regression requests* and *Unattended regression runs*.
- `docs/components/orchestration.md`, the tick core that the engine runs.
- `docs/components/planners.md` → *InstallProgram*, the install program's
  pure stages.
- `docs/conventions.md` → *Component ownership* (`Manager`, `RecipeEditor`,
  *Load lifecycle*) and *Runtime identities and editor commits* (*Edit
  commits*, *Application results*).
- `docs/conventions.md` → *Diagnostics* and *The JSON boundary*, which
  `RecipeStore` and `RecipeEditor` reuse from `recipe/`.
