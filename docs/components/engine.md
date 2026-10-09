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

**The manager.** `Manager` is the singleton that owns every applied actor. It
marks actors **stale** and queues their forgetting through
`ApplicationService`. It
ticks every applied actor each frame and publishes the **snapshot** the menu
reads. It owns `AnimationSubscriptions`, which keeps each observed actor's
animation graph apart from the recipe **bindings**. Its `WarningHistory`
(`warningHistory_`) resets in `Manager::Clear`. Log suppression never removes
the underlying diagnostics.

**The text log.** `LogOnce` (`LogOnce.h`) writes a fault line the first time
its text occurs and suppresses each repeat. Stack diagnostics, texture-demand
errors, render-input errors, render-plan cache mismatches, light-placement
faults and fire triggers on a missing node go through the manager's history. `RecipeEditor` keeps its own
history for the diagnostics of an edit. `ReportApplied` writes one summary line
per install, reinstall, change or claim: the sync kind, the pieces, the
recipes, the geometries, the private and shared materials, and the shells. A
resolution swap writes no line by default. With `VerboseLogging`,
`ReportResize` writes one line with the new resolution level. `VerboseLogging`
also adds each piece, recipe, light and geometry of an apply, and the
retirement count.

**Per-actor render assembly.** `Manager::SyncActor` builds one `LiveActor` per
actor. `PlaceInstances` collects every **stack** request of the actor into one
`ActorStacks`. `BuildActorRender` turns those requests into one `RenderPlan`
(`BuildRenderPlan`, `planners/RenderPlan.h`) and one `RenderInstance`. Every
bound geometry of the actor shares that `RenderInstance` through
`GeometryInputs::render`. Each placed **output** receives a `RenderOutput` for
its step of the plan. A plan failure writes its text into every request's
`PlacedOutput::problem` through `FailStackOutputs`. A request that the plan
does not lower gets the problem `stack was not lowered`.
`PlaceInstances` keeps the full-resolution sizes of the stacks in
`LiveActor::fullSizes`. `Manager::InstallBoundOf` gives the finest level of
the install and the budget's headroom. For a reinstall, the finest level is
the target level. `SyncActor` reads the actor's planned bytes
(`PlannedBytesOf`) before `RetireActor`, and the headroom counts them as
freed. `BuildActorRender` chooses the level
after it has the plan (`ChooseInstallLevel`, `ActorBudget.h`). `PlaceInstances`
keeps that level in `LiveActor::appliedResolution` and
`TrackedActor::targetResolution` (`KeepInstallLevel`). It takes the replaced
bytes off `Manager::budget_` (`Released`) and charges its own bytes
(`Charged`). A render that is not built (`NoRender`) records no level.

**Plan builds on a worker.** On a plan cache miss, `BuildActorRender` submits
the plan build to one worker thread and does not build the plan in the sync
(`RenderPlanFor`, `ManagerApply.cpp`). The job input (`PlanBuildInput`,
`planners/RenderPlanJob.h`) owns its graphs, recipe outputs and identity
facts (`PlanInputOf`). `JobQueue` (`JobQueue.h`) is the generic queue: one
worker, owned inputs, a function pointer as the job, a bound of 16
unfinished jobs, and results that the main thread takes. `PlanJobBook`
(`PlanJobs.h`) maps each open job to its `RenderPlanKey`, so actors with the
same key share one job. The waiting actor keeps the job in
`LiveActor::awaitedPlan` and has no render instance. Its materials keep the
vanilla look, `RenderGeometry` hides its shells, and its applications stay
`prepared`. `Manager::DrainPlanJobs` runs in each tick before the actors
render. It stores each built plan in the cache. Each waiting actor then
collects its stacks again (`CollectActorStacks`) and runs `BuildActorRender`
from the cache (`RebuildAwaitedRender`). The install level is chosen then.
A failed job fails the actor's stack outputs (`FailAwaitedRender`). A stale
job, one that a load or a recipe reload removed from the book, is dropped,
and its actors ask again. Cache verification, a disabled cache, an
uncacheable key and a full queue build the plan in the sync.

**Resolution resize.** A change of resolution level rebuilds only the actor's
`RenderInstance` (`ActorResize.h`). It does not reinstall the actor, so
pieces, recipe instances, shells, materials, lights, signals and clocks stay.
`Manager::RequestLevelStep` records the next level as a **pending resize**
on the `LiveActor` (`RequestResize`). The next level is finer for a step up
and one level coarser for a step down (`NextLevelStep`). `Manager::AdvanceResizes` runs at the
start of each tick. It first swaps each finished resize (`FinishResize`). It
then starts at most one resize across all actors (`NextResize`,
`planners/ResolutionResize.h`). The pending actor with the largest screen
fraction resizes first. `StartResize` creates the new instance
(`ResizeInFlight`) from the current instance's plan, `GeometryInputs` and
graphs at the new sizes (`RenderInstance::AtSizes`). It gives each stack output of the actor a
`RenderOutput` on the new instance (`RenderOutput::DrawnBy`). When
`StartResize` fails, `AdvanceResizes` sets the tracked `targetResolution` back
to the applied level and logs `actor … keeps its resolution` once. `DrawResize`
runs in `Manager::DrawHidden` after the actor loop of each tick, for an actor
in view. It draws each stack of the new instance once, with no hidden layers
and no preceding stack. It shares the tick's hidden draw budget with the first
draws.
The old instance stays attached and visible during the resize.
`FinishResize` swaps when every stack has drawn once, when the actor is out of
view, or 60 frames after the resize began (`SwapDue`). In one call it moves
each new `RenderOutput` into its `PlacedOutput` or `MergedSlotStack`, sets
`GeometryInputs::render` on each geometry that held the old instance
(`SwapRender`), and sets `LiveActor::appliedResolution`. The old instance
goes when the last geometry lets go. When the actor's stack outputs no longer
match the resize, the resize goes back to pending. A reinstall replaces the `LiveActor`, so it drops a pending
or in-flight resize; `RetireActorEffects` drops it too. Each swap emits the
trace event `resolution_swap`.

**Install pacing.** `Manager::SyncActor` lets an install start only while
the frame's syncs spent less than 8 ms, or when no install ran in the frame
(`InstallMayStart`, `planners/InstallPacing.h`). `SyncStale` adds each sync's
time to `Manager::frameSyncs_`. An install that may not start is deferred:
the sync stops after `DecideActor`, records no state, retires nothing and
does not report its applications. The actor enters
`Manager::waitingInstalls_`. Retires and evictions always run. `OnFrame`
clears the frame's figure and then runs the waiting installs in order
(`RunWaitingInstalls`, `NextInstall`, `ApplicationService::SyncNow`): an
entry that waited 1 s or more, then the player, then actors in view by screen
fraction, then the others by age. A deferred install is not charged to the
budget until it runs.

**Hidden first draw.** `KeepActorRender` starts a first draw on every
attached render instance (`StartFirstDraw`, `ActorResize.h`), after an
install and after a plan arrives. The first draw lists each slot of each
geometry (`HiddenSlot`). While it runs, `RenderGeometry` writes the slots as
hidden and hides the shells, the lights are hidden, and `FinishApplications`
waits. `Manager::DrawHidden` runs after the actor loop. It orders the actors
in view that draw hidden (`HiddenDrawOrder`, `planners/DrawPacing.h`) and
draws slots (`DrawFirstSlots`, `RenderSlotChain`) and resize stacks until the
tick spent 4 ms (`MayDrawStack`). `AttachFirstDraw` ends the first draw when
every slot is drawn, or after 60 ticks in which the actor drew
(`FirstDrawDone`), and emits the trace event `first_draw`. An actor out of
view keeps waiting. A resize does not start during a first draw.

**Render target budget.** `Manager::FlushMetricsHeartbeat` runs the budget
once a second. `SampleBudget` reads the DXGI local segment through
`VideoMemoryProbe` (`render/VideoMemory.h`) and the setting
`RenderTargetBudgetMiB`. `UpdateBudget` computes the **allowance** and the
**pressure** with the pure functions of `planners/RenderBudget.h`. Each
pressure start and end writes one `info` line. Under pressure,
`RelieveBudget` builds one `BudgetActor` per rendered actor
(`BudgetActorOf`, `ActorBudget.h`) and applies the `BudgetResponse`: its
releases, then its next step (`Enough`, `AwaitResize`, `StepDown` or
`Exhausted`). Each release and step down names the actor by FormID. An actor
out of view releases its intermediate targets except the mesh bakes
(`ReleaseIntermediates`). Without pressure, an actor out of view for
`HiddenReleaseSeconds` (5 s by default) releases them too
(`ReleaseWhileHidden`). One
NPC steps down one level through the resize (`StepDownForBudget`). When
neither is left, `ShrinkBudgetRadius` sets or lowers the **budget radius**
(`Manager::budgetRadius_`) so that the farthest rendered NPC is evicted.
It marks that actor stale, so the next sync evicts it. Each heartbeat,
`GrowBudgetRadius` grows or clears the radius after the pressure ends
(`RadiusUnchanged`, `RadiusGrown`, `RadiusBlocked`, `RadiusCleared`). It
prices the ring with `EvictedActors` and `LargestReturnBytes`.
`ReportRadiusGrowth` writes the log line and the trace event. When the
radius has no candidate either, one `warning` names the largest holdings.
`EvictDistanceNow` gives the distance that `DecideActor` passes to `Advance`
and that `MarkRecheckStale` passes to `RangeChanged`.
`RequestLevelStep` admits each step up through `AdmitStepUp`. A step down
needs no admission. A blocked step
up waits and emits the trace event `resolution_blocked` once per blocked
episode (`LiveActor::stepUpBlockedFrom`). An episode ends when the actor's level
changes. Each heartbeat without pressure, `PreemptForBlockedStepUp` steps
down one smaller or out-of-view NPC for the largest blocked NPC in view
(`BlockedStepUps`, `BudgetActorsNow`), only when the candidates can cover
the requester's shortfall. `RelieveBudget` builds its
`BudgetActor` list with `BudgetActorsNow` too. When the DXGI reading is unknown, the plugin writes one warning and
the budget does nothing.

**The frame.** `Manager::OnFrame` runs from the hook. It returns at once while
`ApplicationService::Loading` is true. It then reports and clears the frame's
sync time and runs the waiting installs (`RunWaitingInstalls`). Once a second it expires `CarriedTimes` and
sweeps animation observation. Every `Actors::kPendingRecheckMs` (100 ms),
`MarkWaitingStale` marks stale each actor that is **waiting**: it has a
pending model younger than the model wait plus one recheck. Every
`Actors::kRecheckMs` (1000 ms), `MarkRecheckStale` marks stale each tracked
actor with a stuck model or whose range changed (`RangeChanged`).
At the tick interval
(`Settings::TickIntervalMS`) it calls `Manager::DrainPlanJobs`, then
`Manager::Tick` for a nonempty actor set. The drain runs outside that
nonempty guard, so finished jobs free their queue places with no actor
loaded. Cache maintenance (`SweepBoundMeshes`) and `PublishSnapshot` run outside
that nonempty guard. The retirement of the last actor therefore cannot stop
mesh and material expiration. `ObserveRegression` runs last.

**Culling.** Each tick, `Tick` asks `ShownThisTick` whether an actor is in
the camera's view and stores the answer in `LiveActor::inView`. The player is
always in view. With the setting `CullOutOfView` off, every actor is in view
and no test runs. Otherwise `InCameraView` (`ActorObservation.h`) tests a sphere around the actor
against the camera frustum (`RE::NiCamera::BoundInFrustum`). The sphere sits
64 units above the actor's position, and its radius is the larger of the
actor's 3D bound and 96 units, times 1.25, plus 64. The inspected actor
always counts as in view. An actor out of view renders nothing that tick but
still updates its lights. `Metrics::CountActorView` counts each test for the
heartbeat (`actors_in_view`, `actors_out_of_view`). An application whose
actor is out of view when it would report `kPrepared` reports `kOutOfView`
instead (`FinishApplications`).

**Recipe CRUD.** `RecipeStore` holds the loaded `Recipe` list and does the
file I/O. `RecipeEditor` is the gesture, **edit** and undo layer that the
studio **intents** call through. It accepts document changes and queues actor
rebuilds. Application records report preparation and rendering separately.

**Republication.** `RecipeStore` keeps mutable documents apart from the
published **applied set**. Each republication, also on save, publishes a new
immutable `RecipeCatalog` (`AppliedCatalog`). `DecideActor` reads the
catalog once. A **debug recipe** is a file under the top-level
`recipes/debug` folder. `AppliedCatalog(DebugRecipes::kHidden)` leaves the
debug recipes out, and `DecideActor` asks for it while the
`ShowDebugRecipes` setting is off. `LoadedRecipes()` always lists them, so
the editor pages show them. `LiveActorFor` stores it in `LiveActor::catalog` with the
`ActorMatches` it was planned with, so each `RecipeId` indexes the catalog it
came from. `LiveInstance::recipe` comes from `SharedRecipe` and keeps the
catalog alive. `PlaceInstances` and `PlaceLightsOf` read `RecipesOf(state)`,
not `LoadedRecipes()`. `Manager::ChangeAndRebuildActors` runs its synchronous
mutation callback. It then asks each affected actor for a reinstall
(`MarkReinstall`); the actor keeps its effects until its sync replaces them.
Which actors it rebuilds follows
`ScopeOf` (`RebuildScope`, `recipe/Recipe.h`): when the edit names a recipe
and leaves every recipe's `MatchFacts` unchanged, only the actors applying
that recipe (`ActorsApplying`: a live instance of it, or a failed
application of it) are rebuilt, and the render plan cache is kept. Otherwise every
application actor is rebuilt and the render plan cache is cleared. The report
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
instance lights stay. `Manager::ClaimLostGeometries` runs for each live actor
on each tick. It finds each geometry whose material or shell another system
replaced (`TakenByAnotherSystem`). It retires that geometry's binding at once.
It adds a **claim** to the actor's state with `Actors::WithClaim` and marks the
actor stale. The next plan leaves the claimed geometry out. A claim ends when
its piece gets a new model or the game loads.

**Save.** `SaveRecipe` calls engine-free `WriteRecipeFile` with the
destination and an explicit import-promotion flag. An existing file under
`PluginIdentity::UserRecipeFolder()` keeps its destination, including nested paths.
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
retire and install of a sync. `Manager::SyncStale` reconciles
it against the new live state through `ReconcileAnimationEvents`.
`Manager::ForgetActor` ends observation and discovery even when the actor has no
`LiveActor`. `Manager::Clear` ends all registrations. `SweepAnimationEvents`
repairs changed or missing graphs once a second and retires abandoned
observation after pending application work ends.

**Native coverage.** Most of `engine/` is engine-facing and not
native-tested. `cmake/Native.cmake` compiles eight engine-free units into the
`BeefEngineServices` library for `ctest --preset native`:
`SessionQueue.cpp`, `ApplicationService.cpp`, `TextFile.cpp`,
`PluginEvents.cpp`, `RecipeFiles.cpp`, `InstanceTime.cpp`,
`MenuDependency.cpp` and `PlanJobs.cpp`. `engine_jobqueue` tests the
header-only `JobQueue.h`. Five suites add engine sources against test doubles:
`engine_wornkeys` (`WornKeys.cpp`, `EnchantmentEffects.cpp`),
`engine_animationsubscriptions` (`AnimationSubscriptions.cpp`,
`ManagerAnimation.cpp`), `engine_editorintegration` (`RecipeEditor.cpp`,
`RecipeStore.cpp`), `engine_hooks` (`Hooks.cpp`) and `engine_liveretirement`
(`LiveActor.cpp`). `engine_editorintegration` does not simulate actor
rebuilds or Skyrim form discovery; see
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

`Manager` holds one `LiveActor` per applied actor and hands per-actor queueing
to `ApplicationService`. `ApplicationService` owns the only runtime
`SessionQueue`. The menu reads the manager's state through `Manager::Snapshot`
and `Manager::Status`.

| Member | Description | Declared in |
|---|---|---|
| `Manager` | The singleton. It marks actors stale, queues their forgetting, holds `live_`, ticks each actor and owns the `RecipeEditor`. | `Manager.h` |
| `Manager::Status` | The debug overlay counts: actors, **pieces**, recipes, geometries, shells, lights and tick cost in ms. It also carries the `emissivePath`, `layoutVerified` and `textureLab` flags. | `Manager.h` |
| `Manager::Snapshot` | A `Studio::Snapshot` plus the `TextureRef`s that keep its texture handles alive. | `Manager.h` |
| `ApplicationService` | Tracks one `ApplicationRecord` per recipe-affecting change, keyed by recipe id and by actor id. | `ApplicationService.h` |
| `kMaxTerminalApplicationRecipes`, `kMaxTerminalApplicationActors` | The terminal history caps, 256 each. | `ApplicationService.h` |
| `ApplicationToken` | Names one change by recipe id, revision, actor id and attempt. | `studio/ApplicationRecord.h` |
| `ApplicationRecord` | One tracked change: its token, its phase, and the per-actor phases and problems. | `studio/ApplicationRecord.h` |
| `ApplicationPhase` | `kQueued`, `kPrepared`, `kRendered`, `kFailed`, `kUnmatched`, `kCancelled`. | `studio/ApplicationRecord.h` |
| `SessionQueue` | Serialises per-actor sync tasks onto the SKSE task interface. It holds the equip-finalize deadlines, the reinstall demands (`MarkReinstall`, `ReinstallDue`, `ClearReinstall`) and the load gate (`BeginLoad`, `Resume`). | `SessionQueue.h` |

### Texture-memory controls

Two controls cap texture memory for large crowds. Distance eviction keeps the
applied set near the player. Per-slot sizing shrinks each stack's render size
below the material's native size.

| Member | Description | Declared in |
|---|---|---|
| `Manager::tracked_`, `TrackedActor{state, models, targetResolution, returnBytes}` | One entry per actor with a record: its `Actors::ActorState` (plan, installed plan, pending models, claims), its `ModelTable`, its `targetResolution`, and its optional **return bytes**. `RetireActor` records the return bytes (`ReturnBytesOf`) before it retires an installed actor. An actor without an entry has no record. `Manager::Clear` empties the map. `ForgetActor` erases the actor's entry. | `Manager.h` |
| `Manager::DecideActor` | Observes the actor (`ObserveActor`), plans it (`Actors::PlanActor` with its live claims) and advances its state (`Actors::Advance`), the eviction and pending-model rules included. It performs nothing. It traces `actor_decide`. | `ManagerApply.cpp` |
| `Manager::RecordState` | Stores the next state in `tracked_`. No state erases the entry. | `ManagerApply.cpp` |
| `Manager::RetireActor` | Undoes every installed effect of the actor and empties its installed plan. It keeps the record. | `ManagerApply.cpp` |
| `Manager::ForgetActor` | Retires the actor, stops its animation subscriptions and events, and erases its record. | `ManagerApply.cpp` |
| `Manager::MarkWaitingStale`, `Manager::MarkRecheckStale` | Mark stale each waiting actor (every `Actors::kPendingRecheckMs`) and each actor with a stuck model or a changed range (every `Actors::kRecheckMs`). | `ManagerTick.cpp` |
| `RangeChanged` | Tells if `Actors::InRange` for the actor's `DistanceToPlayer` differs from "anything installed". It skips an actor with neither a plan nor an installed plan. | `ManagerTick.cpp` |
| `Loaded3D`, `DistanceToPlayer` | The engine facts that observation and the tick share: the actor is not deleted and has 3D; the distance to the player (none for the player). | `ActorObservation.h` |
| `EvictionFor`, `EvictionAction` | The pure decision: `kEvict` past the distance, `kRestore` inside 80% of it, `kNone` otherwise. The distance is `EvictDistance` or the smaller budget radius (`EvictDistanceNow`). | `planners/Eviction.h` |
| `Settings::evictDistance` | The eviction radius in game units. 0 disables eviction. `kMaxEvictDistance` (20000) caps it. | `Settings.h` |
| `RuntimeSizes` | Derives the base and maximum stack size from the material's largest native texture and `Settings::textureScale`. | `ManagerApply.cpp` |
| `SlotStackSize` | Divides the base size by `ResolutionDivisor` of the slot's `Resolution`. `CollectChainStacks` calls it before `Compositor::StackSize`. | `ManagerApply.cpp` |
| `DefaultSlotResolution`, `ResolutionDivisor` | The per-slot default `Resolution` and its divisor (`kFull` 1, `kHalf` 2, `kQuarter` 4). | `recipe/Recipe.h` |
| `SurfaceOutput::resolution` | An optional `Resolution` that overrides `DefaultSlotResolution` for that output's slot. | `recipe/Recipe.h` |
| `Settings::renderTargetBudgetMiB` | The cap on the render target allowance in MiB. 0 sets no cap. `kMaxRenderTargetBudgetMiB` (65536) caps it. A regression run can replace it (`RegressionRenderTargetBudgetMiB`). | `Settings.h` |
| `Settings::stepDownHoldSeconds`, `Settings::StepDownHoldMS` | How long an NPC stays small on screen before `RequestLevelStep` steps it down, in seconds (INI `StepDownHoldSeconds`, default 2, at most `kMaxStepDownHoldSeconds` 30). `LiveActor::smallOnScreen` holds the start. | `Settings.h` |
| `Settings::hiddenReleaseSeconds`, `Settings::HiddenReleaseMS` | How long an NPC stays out of view before `ReleaseWhileHidden` releases its intermediates, in seconds (INI `HiddenReleaseSeconds`, default 5, at most `kMaxHiddenReleaseSeconds` 60). `LiveActor::outOfView` holds the start. | `Settings.h` |
| `Compositor::ClearImages` | Empties the image store at load. `Manager::Clear` calls it and writes the count as `images` on the `clear_end` load event. | `render/Compositor.h` |
| `Manager::budget_`, `BudgetState` | The last allowance, the planned bytes and the `Pressure`: `kNone`, `kOver`, or `kOverWarned` after the exhaustion warning of the episode. | `Manager.h`, `planners/RenderBudget.h` |
| `Manager::SampleBudget`, `Manager::UpdateBudget`, `Manager::RelieveBudget`, `Manager::StepDownForBudget`, `Manager::AdmitStepUp` | Read the DXGI memory and the cap, update the pressure, apply the responses, step one NPC down, and admit or block one step up. | `ManagerTick.cpp` |
| `Manager::budgetRadius_`, `BudgetRadius` | The optional budget radius. No record means no radius. | `Manager.h`, `planners/BudgetRadius.h` |
| `Manager::ShrinkBudgetRadius`, `Manager::GrowBudgetRadius`, `Manager::ReportRadiusGrowth`, `Manager::FarthestTrackedDistance`, `Manager::EvictedActors`, `Manager::LargestReturnBytes`, `Manager::EvictDistanceNow` | Lower the radius past the farthest rendered NPC, grow, block or clear it after the dwell, write its log line and trace event, give the distance of the farthest tracked actor, list the evicted actors (a plan and nothing installed) with their distances and return bytes, give the largest recorded return bytes, and give the eviction distance with the radius applied. | `ManagerTick.cpp` |
| `LevelCostOf`, `CachedLevelCost`, `BudgetActorOf`, `HeldNow`, `InstancesOf`, `PlannedBytesOf`, `ReturnBytesOf`, `ReleaseIntermediates` | The bytes of a plan at one level with intermediates and with stacks only (`TargetsOf`), the same for the actor's attached plan (cached in `LiveActor::levelCosts`), the actor's facts for the budget with its distance to the player, the targets that the planned figure counts for the actor, the actor's render instances with a resize in flight, the bytes that the planned figure counts for the actor (0 before its first render), the bytes a return of the actor installs (with intermediates at its target level, else its planned bytes, none when 0), and the release of its intermediates. | `ActorBudget.h` |
| `InstallLevelChoice`, `ChooseInstallLevel` | The level of an install and the bytes charged to the budget (`chargedBytes`). `ChooseInstallLevel` prices the levels from the bound's finest level to eighth and calls `FittingLevel`. | `ActorBudget.h` |
| `Manager::InstallBoundOf`, `Manager::KeepInstallLevel` | The `InstallBound` of an install: the player's level without headroom, the target level with the headroom, or the ideal level with the headroom. The headroom counts the replaced bytes as freed. Then the level that the install built, kept on the tracked actor. | `ManagerApply.cpp` |

### Actor and worn-piece state

`Manager::SyncActor` fills one `LiveActor` per live actor from the equipped
pieces and the matched recipes. Each frame the tick advances each
**instance**'s signals in place and renders each bound geometry through its
bindings. Free functions in `LiveActor.h` resolve and retire these records
without the manager.

| Record | Description |
|---|---|
| `LiveActor` | One applied actor: its handle, the `RecipeCatalog` it was planned against, its `ActorMatches`, its pieces, instances and **placements**, and the `ApplicationToken`s of its in-flight applications. It also holds its applied resolution level (`appliedResolution`), its full-resolution sizes (`fullSizes`), its `LiveResize` and its optional `FirstDraw`. `awaitedPlan` names the worker job whose plan the actor waits for. For the render target budget it holds the bytes of its plan per level (`levelCosts`), whether its intermediates are released (`intermediates`), and the level at which a step up is blocked (`stepUpBlockedFrom`). `RecipesOf` returns the catalog's recipes. |
| `LiveResize` | The resize state of one actor: `ResizeIdle`, `ResizePending` (the level) or `ResizeInFlight`. |
| `ResizeInFlight` | A resize in flight: its `ResizeProgress` (the old level, the new level and the frame the resize began), the new `RenderInstance`, and one `ResizedStack` per stack output of the actor. |
| `ResizedStack` | One stack output on the new instance and whether it has drawn once. |
| `ResizeSwap` | What `FinishResize` reports: the two levels, the frames from the start of the resize to the swap, the stack count, the drawn count and the `TargetCost` of the new instance. |
| `FirstDraw` | A first draw in progress: the frame it began, the ticks in which the actor drew a stack, and one `HiddenSlot` per slot. |
| `HiddenSlot` | One slot of a first draw: its piece, geometry and slot indices, and whether it has drawn once. |
| `LivePiece` | One worn armor clone: its `Actors::PieceRef` (`ref`), its `ModelToken` (`model`), the armor form id and name, the optional addon `FormKey`, the enchantment form id and its `LiveGeometry`s. |
| `LivePieceId` | A typed piece index (`enum class`, `std::size_t`). |
| `LiveGeometry` | One bound geometry: its `Actors::GeometryRef` (`ref`, set by `LivePiecesOf` through `Actors::GeometryRefOf`), engine geometry and shader-property pointers, `GeometryInputs`, `MaterialBinding`, `ShellBinding`, its `GeometryPlan`, `GeometryStackPlan` and `BindingPlan`, and its `PlacementId`s. Its `lost` flag marks it retired. |
| `LiveInstance` | One recipe matched onto the actor: the `Recipe` pointer, enchantment, effect scope, `RecipeGraph`, `SignalState` and `ActorEnvironment`. It also holds the optional `LightBinding`, the light problem text, and the timing (`startMS`, `lastTime`). |
| `LivePlacement` | The outputs bound onto one geometry: a `GeometryId` and one `PlacedOutput` per placed output. |
| `PlacedOutput` | One placed output: its recipe output index, its `RenderOutput` (`stack`), its `problem` text and its `active`, `rendered` and `renderFailed` flags. |
| `ResolvedPlacement` | The placement index and instance index that `ResolvePlacement` returns. |
| `ActorEnvironment` | The `SignalEnvironment` a `LiveInstance` ticks against. It reads actor values, actor state, the enchantment and effect shaders from the game. It holds a copy of the actor's `appliedResolution`, which `SetAppliedResolution` sets, and returns it as the `resolutionLevel` actor state. |

### Per-actor render assembly

These records live in the anonymous namespaces of `ManagerApply.cpp` and
`ManagerTick.cpp`. A sync uses the first group to build one render plan per
actor. The tick uses the second group to render each **slot**'s chain and
write it to the material.

| Record | Description | Declared in |
|---|---|---|
| `LocatedGeometry` | A geometry found by `GeometryId`: its piece, its `LiveGeometry` and the id. | `ManagerApply.cpp` |
| `LocatedStackOutput` | One slot contribution resolved to its placement, recipe, `RecipeGraph`, `SurfaceOutput` and `PlacedOutput`. | `ManagerApply.cpp` |
| `ActorStackRequest` | One `StackTextureRequest` with its geometry and located output. | `ManagerApply.cpp` |
| `ActorStacks` | Every stack request of one actor, with the per-geometry `GeometryInputs` and bound geometries. `BuildActorRender` reads it. | `ManagerApply.cpp` |
| `ActorRender`, `NoRender` | The result of `BuildActorRender`: the chosen `InstallLevelChoice`, the `PlanAwaited` job, or `NoRender` when no request or no plan gave a render instance. | `ManagerApply.cpp` |
| `PlanAwaited` | The worker job whose plan the actor waits for. | `PlanJobs.h` |
| `JobQueue`, `JobId`, `JobDone`, `JobRefusal`, `JobTiming` | The generic worker queue, a job's id, a finished job with its result or error text, the reason a job was refused (`kFull`, `kNoWorker`), and the queue wait and work time. | `JobQueue.h` |
| `PlanQueue`, `kPlanJobCapacity` | The plan build queue and its bound of 16 unfinished jobs. | `PlanJobs.h` |
| `OpenPlanJob`, `PlanJobBook`, `OpenJobFor`, `Opened`, `Closed` | The open plan jobs with their keys, graphs and submitting actor; the job for a key; the book with a job added or removed. | `PlanJobs.h` |
| `PlanArrival`, `AwaiterStep`, `ArrivalOf`, `StepFor` | The stale rule: a result is built, failed or stale (`ArrivalOf` takes the closed job, if the book had one, and the job's result), and a live actor ignores it, rebuilds from the cache, or fails its outputs. | `PlanJobs.h` |
| `TickFrame` | The settings, view, time and resume flag of one tick. | `ManagerTick.cpp` |
| `SlotRenderView` | The view, whether any **layer** is hidden, and `Settings::publishEffects`. | `ManagerTick.cpp` |
| `SlotChain` | The rendered outputs of one slot: their `SurfaceOutput`s, resolved values, the final texture, a shown flag, and a pending flag when a stack returned `kPending`. | `ManagerTick.cpp` |
| `FrameSyncs`, `InstallMayStart`, `Synced` | The sync time and the install count of the current frame, and the rule that starts an install. | `planners/InstallPacing.h` |
| `WaitingInstall`, `NextInstall`, `Deferred`, `Removed`, `WaitedMS` | A deferred install with its role, view, screen fraction and first deferral time; the next entry to run; the list with an entry added or removed; the wait of an entry. | `planners/InstallPacing.h` |
| `DrawPace`, `MayDrawStack`, `Drew` | The hidden draw time and stack count of the current tick, and the rule that draws another stack. | `planners/DrawPacing.h` |
| `HiddenDrawCandidate`, `HiddenDrawOrder` | An actor that draws hidden, by FormID, role and screen fraction, and the order of the draws as FormIDs. | `planners/DrawPacing.h` |
| `FirstDrawProgress`, `FirstDrawEnd`, `FirstDrawDone` | The drawing ticks, slots and drawn slots of a first draw, and the rule that ends it. | `planners/DrawPacing.h` |
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
| `Manager::QueueRegression` | Forgets the request's actor or marks it stale on the session queue. | `Manager.h` |
| `Manager::ObserveRegression` | Reads a newer application record for the actor and finishes the request as `PASS`, `FAIL`, `BLOCKED` or `ABORTED`. | `Manager.h` |
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
ApplicationService::RunActor ──▶ Manager::SyncStale             ApplicationService.cpp, ManagerApply.cpp
  │
  ▼
Manager::SyncActor                                              ManagerApply.cpp
  │   DecideActor: ObserveActor, Actors::PlanActor, Actors::Advance
  │   no state: ForgetActor and stop
  │   no commands: RecordState and stop
  │   commands: RetireActor, RecordState; if anything is installed,
  │   LiveActorFor: LivePiecesOf, the plan's matches, BuildInstances
  ▼
Manager::PlaceInstances                                         ManagerApply.cpp
  │   per geometry: PreparePlacement, PlaceOnGeometry
  │     InstallSurfaces, MarkReplaced, CollectChainStacks
  │     (SlotStackSize, Compositor::StackSize) into ActorStacks
  ▼
BuildActorRender                                                ManagerApply.cpp
  │   PlanKeyOf ──▶ RenderPlanKeyOf               planners/RenderPlanKey.h
  │   RenderPlanCache hit, or a miss:
  │     open job for the key, or PlanInputOf ──▶ PlanQueue::Submit  JobQueue.h, PlanJobs.h
  │     (the actor waits in LiveActor::awaitedPlan)
  │     verification or a refused job: BuildPlanFrom in the sync  planners/RenderPlanJob.h
  │   one RenderInstance per actor, AttachRender, AttachStackOutputs
  ▼
PlaceLightsOf, then live_[actorID] = LiveActor                 ManagerApply.cpp

PlayerCharacter::Update hook (every frame)                      Hooks.cpp
  │
  ▼
Manager::OnFrame                                                ManagerTick.cpp
  │   once a second: FlushMetricsHeartbeat (SampleBudget, UpdateBudget,
  │     RelieveBudget, GrowBudgetRadius), CarriedTimes::Expire,
  │     SweepAnimationEvents
  │   every kPendingRecheckMs: MarkWaitingStale
  │   every kRecheckMs: MarkRecheckStale
  ▼
Manager::DrainPlanJobs                                          ManagerApply.cpp
  │   each finished job: RenderPlanCache::Store; each waiting actor:
  │   CollectActorStacks ──▶ BuildActorRender, or FailStackOutputs
  ▼
Manager::Tick                                                   ManagerTick.cpp
  │   not Loaded3D: ForgetActor
  │   ClaimLostGeometries; TickInstance per LiveInstance
  ▼
AdvanceResizes: FinishResize, NextResize, StartResize              ActorResize.cpp
  ▼
RenderPieces ──▶ RenderGeometry                                 ManagerTick.cpp
  │   RenderInstance::BeginFrame, UpdateRenderInputs
  │   WriteSlots ──▶ RenderSlotChain ──▶ Compositor::Render        render/Compositor.cpp
  │   WriteSlot into the SlotTarget; PoseShell                    render/Binding.cpp
  │   DrawResize for an actor with a resize in flight             ActorResize.cpp
  ▼
UpdateLights, FinishApplications                                ManagerTick.cpp, ManagerApplication.cpp

(b) a menu edit reaches an applied recipe

studio EditRecipe intent ──▶ IntentPerformer                    menu/Menu.cpp
  │   Manager::Editor().EditRecipe posts a task
  ▼
RecipeEditor::ApplyEdits                                        RecipeEditor.cpp
  │   MutableRecipe(id) ──▶ Studio::PrepareEdits                  RecipeStore.cpp, studio/Edits.cpp
  ▼
Manager::ChangeAndRebuildActors                                 ManagerApplication.cpp
  │   ApplicationService::Begin, RetireActor per actor
  │   callback: history push, move the prepared Recipe in,
  │             RefreshRecipeDerivedState(id)                     RecipeStore.cpp
  │             (revalidates, resolves forms, drops the cached
  │              RecipeGraph, republishes)
  │   ApplicationService::MarkStale per actor
  ▼
rejoins (a) at Manager::SyncStale; PlanActor reads the edited
Recipe from LoadedRecipes()
```

## The files

| Concern | Key files |
|---|---|
| Event sinks and hooks | `Events.h`/`.cpp` (equip, object-loaded, hit and node-update sinks), `PluginEvents.h`/`.cpp` (the inter-plugin message contract and parser), `Hooks.h`/`.cpp` (the `PlayerCharacter::Update` hook), `AnimationSubscriptions.h`/`.cpp` (animation-graph observation) |
| The manager | `Manager.h`, `Manager.cpp` (construction, queue calls, `Clear`, load, the menu's idle-camera hold), `PhaseTrace.h` (`PhaseTrace`: times named phases with `Measure` and emits one metrics or application trace event), `ManagerApply.cpp` (`SyncStale`, `SyncActor`, `InstallActor`, `DecideActor`, `RetireActor`, `ForgetActor`, live pieces from observation, placement, `BuildActorRender`, lights), `ManagerApplication.cpp` (`ChangeAndRebuildActors`, application bookkeeping), `ManagerTick.cpp` (`OnFrame`, `RunWaitingInstalls`, `DrainPlanJobs` call, `MarkWaitingStale`, `MarkRecheckStale`, `Tick`, `DrawHidden`, `AttachFirstDraw`, `RequestStepUp`, `AdvanceResizes`, `ClaimLostGeometries`, `RenderGeometry`, slot writes, `UpdateLights`), `ActorResize.h`/`.cpp` (the resolution resize and the hidden first draw of one actor), `ActorBudget.h`/`.cpp` (the render target budget's facts and release for one actor), `JobQueue.h` (the generic worker queue), `PlanJobs.h`/`.cpp` (the plan job book and the stale rule), `ManagerAnimation.cpp` (animation events), `ManagerEvents.cpp` (`Fire`, `FireAt`, `QueueEvent`, `QueueBroadcast`), `ManagerInspection.cpp` (`RequestMesh`), `ManagerSnapshot.cpp` (`GetStatus`, `BuildSnapshot`, `PublishSnapshot`, `Watch`), `LogOnce.h`/`.cpp` (`LogOnce`: a fault line once per text, through a `WarningHistory`) |
| Application and session bookkeeping | `ApplicationService.h`/`.cpp` (token tracking, rejection handling, history pruning), `SessionQueue.h`/`.cpp` (per-actor task serialisation) |
| Actor and worn-piece state | `ActorObservation.h`/`.cpp` (`ObserveActor`, `ModelTable`, `ModelOf`, `MaterialLayout`), `LiveActor.h`/`.cpp` (live records, `RetireGeometry`, `SetAppliedResolution`, `ResolvePlacement`), `Environment.h`/`.cpp` (`ActorEnvironment`), `WornKeys.h`/`.cpp` (`WornKeysOf`), `EnchantmentEffects.h`/`.cpp` (`EnchantmentValueFor`), `InstanceTime.h`/`.cpp` (`CarriedTimes`) |
| Slot writes | `SlotWrites.h`/`.cpp` (`SlotWrite`, `EmptyWrite`, `WriteSlot`, `ConcealStackSlots`): the one place that writes a stack slot, shown or not shown; the tick's render (`WriteSlots`) and a resize swapped out of view both call it |
| Recipe CRUD | `RecipeStore.h`/`.cpp` (load, publish, mutate, save, `RefreshRecipeDerivedState`), `RecipeFiles.h`/`.cpp` (checked read, write, rename and delete), `RecipeOperations.h` (the operation journal and task guards), `RecipeEditor.h`/`.cpp` (gestures, edits, undo, paint, view) |
| Form and game-object lookups | `EngineForms.h`/`.cpp` (`FormKeyFor`, `LookupForm`, `ShaderFor`, `RecordFrom`), `GameObjectService.h`/`.cpp` (game-object and animation-event catalogs), `InputCatalog.h`/`.cpp` (`BuildActorValueSamples`), `Tweaks.h`/`.cpp` (`EditorIdOf`, `TweaksEditorIdsAvailable`) |
| Regression fixture | `RegressionRequests.h`/`.cpp` (the request store and the manager's regression calls), `RegressionRequest.h` (`RegressionRequest`), `ManagerRegression.cpp` (the manager's regression observations and studio work), `RegressionRun.h`/`.cpp` (the unattended run's file, load and result lines over `regression/Run.h`), `RegressionWorld.h`/`.cpp` (its observations and commands) |
| Small utilities | `Clock.h`/`.cpp` (`NowMS`, `InstanceSpeed`), `TextFile.h`/`.cpp` (`ReadText`, `WriteText`, bounded by `kMaxTextFileBytes`; `CreateText` and `SetAside`, which never replace a file), `MenuDependency.h`/`.cpp` (`CheckMenuFramework`) |

## See also

- `REFERENCE.md` → *Engine events, hooks and the manager*, *Bindings*,
  *Recipe CRUD travels one pipeline*, *Menu dependency preflight*,
  *Regression requests* and *Unattended regression runs*.
- `docs/conventions.md` → *Component ownership* (`Manager`, `RecipeEditor`,
  *Load lifecycle*) and *Runtime identities and editor commits* (*Edit
  commits*, *Application results*).
- `docs/conventions.md` → *Diagnostics* and *The JSON boundary*, which
  `RecipeStore` and `RecipeEditor` reuse from `recipe/`.
