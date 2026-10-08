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
`PlaceInstances` keeps the full-resolution sides of the stacks in
`LiveActor::fullSides`.

**Resolution resize.** A change of resolution level rebuilds only the actor's
`RenderInstance` (`ActorResize.h`). It does not reinstall the actor, so
pieces, recipe instances, shells, materials, lights, signals and clocks stay.
`Manager::UpdateResolution` records the finer level as a **pending resize**
on the `LiveActor` (`RequestResize`). `Manager::AdvanceResizes` runs at the
start of each tick. It first swaps each finished build (`FinishResize`). It
then starts at most one build across all actors (`NextBuild`,
`planners/ResolutionResize.h`). The pending actor with the largest screen
fraction builds first. `StartResize` creates the new instance from the current
instance's plan, `GeometryInputs` and graphs at the new sides
(`RenderInstance::AtSides`). It gives each stack output of the actor a
`RenderOutput` on the new instance (`RenderOutput::DrawnBy`). `DrawResize`
runs after `RenderPieces` in each tick that renders the actor. It draws each
stack of the new instance once, with no hidden layers and no preceding stack.
The old instance stays attached and visible during the build.
`FinishResize` swaps when every stack has drawn once, when the actor is out of
view, or 60 frames after the build began (`SwapDue`). In one call it moves
each new `RenderOutput` into its `PlacedOutput` or `MergedSlotStack`, sets
`GeometryInputs::render` on each geometry that held the old instance, and sets
`LiveActor::resolution`. The old instance goes when the last geometry lets
go. When the actor's stack outputs no longer match the build, the build goes
back to pending. A reinstall replaces the `LiveActor`, so it drops a pending
or in-flight resize; `RetireActorEffects` drops it too. Each swap emits the
trace event `resolution_swap`.

**The frame.** `Manager::OnFrame` runs from the hook. It returns at once while
`ApplicationService::Loading` is true. Once a second it expires `CarriedTimes` and
sweeps animation observation. Every `Actors::kPendingRecheckMs` (100 ms),
`MarkWaitingStale` marks stale each actor that is **waiting**: it has a
pending model younger than the model wait plus one recheck. Every
`Actors::kRecheckMs` (1000 ms), `MarkRecheckStale` marks stale each tracked
actor with a stuck model or whose range changed (`RangeChanged`).
At the tick interval
(`Settings::TickIntervalMS`) it calls `Manager::Tick` for a nonempty actor
set. Cache maintenance (`SweepBoundMeshes`) and `PublishSnapshot` run outside
that nonempty guard. The retirement of the last actor therefore cannot stop
mesh and material expiration. `ObserveRegression` runs last.

**Culling.** Each tick, `Tick` asks `ShownThisTick` whether an actor is in
the camera's view and stores the answer in `LiveActor::inView`. The player is
always in view. With the setting `CullOutOfView` off, every actor is in view
and no test runs. Otherwise `InCameraView` tests a sphere around the actor
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
immutable `RecipeCatalog` (`AppliedCatalog()`). `DecideActor` reads the
catalog once. `LiveActorFor` stores it in `LiveActor::catalog` with the
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
and retires actor effects. `main.cpp` calls `AbortWaitingRegressionRequest` before
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
retire and install of a sync. `Manager::SyncStale` reconciles
it against the new live state through `ReconcileAnimationEvents`.
`Manager::ForgetActor` ends observation and discovery even when the actor has no
`LiveActor`. `Manager::Clear` ends all registrations. `SweepAnimationEvents`
repairs changed or missing graphs once a second and retires abandoned
observation after pending application work ends.

**Native coverage.** Most of `engine/` is engine-facing and not
native-tested. `cmake/Native.cmake` compiles seven engine-free units into the
`BeefEngineServices` library for `ctest --preset native`:
`SessionQueue.cpp`, `ApplicationService.cpp`, `TextFile.cpp`,
`PluginEvents.cpp`, `RecipeFiles.cpp`, `InstanceTime.cpp` and
`MenuDependency.cpp`. Five suites add engine sources against test doubles:
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
| `Manager::tracked_`, `TrackedActor{state, models}` | One entry per actor with a record: its `Actors::ActorState` (plan, installed plan, pending models, claims) and its `ModelTable`. An actor without an entry has no record. `Manager::Clear` empties the map. `ForgetActor` erases the actor's entry. | `Manager.h` |
| `Manager::DecideActor` | Observes the actor (`ObserveActor`), plans it (`Actors::PlanActor` with its live claims) and advances its state (`Actors::Advance`), the eviction and pending-model rules included. It performs nothing. It traces `actor_decide`. | `ManagerApply.cpp` |
| `Manager::RecordState` | Stores the next state in `tracked_`. No state erases the entry. | `ManagerApply.cpp` |
| `Manager::RetireActor` | Undoes every installed effect of the actor and empties its installed plan. It keeps the record. | `ManagerApply.cpp` |
| `Manager::ForgetActor` | Retires the actor, stops its animation subscriptions and events, and erases its record. | `ManagerApply.cpp` |
| `Manager::MarkWaitingStale`, `Manager::MarkRecheckStale` | Mark stale each waiting actor (every `Actors::kPendingRecheckMs`) and each actor with a stuck model or a changed range (every `Actors::kRecheckMs`). | `ManagerTick.cpp` |
| `RangeChanged` | Tells if `Actors::InRange` for the actor's `DistanceToPlayer` differs from "anything installed". It skips an actor with neither a plan nor an installed plan. | `ManagerTick.cpp` |
| `Loaded3D`, `DistanceToPlayer` | The engine facts that observation and the tick share: the actor is not deleted and has 3D; the distance to the player (none for the player). | `ActorObservation.h` |
| `EvictionFor`, `EvictionAction` | The pure decision: `kEvict` past the distance, `kRestore` inside 80% of it, `kNone` otherwise. | `planners/Eviction.h` |
| `Settings::evictDistance` | The eviction radius in game units. 0 disables eviction. `kMaxEvictDistance` (20000) caps it. | `Settings.h` |
| `RuntimeSizes` | Derives the base and maximum stack size from the material's largest native texture and `Settings::textureScale`. | `ManagerApply.cpp` |
| `SlotStackSize` | Divides the base size by `ResolutionDivisor` of the slot's `Resolution`. `CollectChainStacks` calls it before `Compositor::StackSize`. | `ManagerApply.cpp` |
| `DefaultSlotResolution`, `ResolutionDivisor` | The per-slot default `Resolution` and its divisor (`kFull` 1, `kHalf` 2, `kQuarter` 4). | `recipe/Recipe.h` |
| `SurfaceOutput::resolution` | An optional `Resolution` that overrides `DefaultSlotResolution` for that output's slot. | `recipe/Recipe.h` |

### Actor and worn-piece state

`Manager::SyncActor` fills one `LiveActor` per live actor from the equipped
pieces and the matched recipes. Each frame the tick advances each
**instance**'s signals in place and renders each bound geometry through its
bindings. Free functions in `LiveActor.h` resolve and retire these records
without the manager.

| Record | Description |
|---|---|
| `LiveActor` | One applied actor: its handle, the `RecipeCatalog` it was planned against, its `ActorMatches`, its pieces, instances and **placements**, and the `ApplicationToken`s of its in-flight applications. It also holds its resolution level, its full-resolution sides (`fullSides`) and its `LiveResize`. `RecipesOf` returns the catalog's recipes. |
| `LiveResize` | The resize state of one actor: `ResizeIdle`, `ResizePending` (the level) or `ResizeBuild`. |
| `ResizeBuild` | A resize in flight: its `ResizeProgress` (the old level, the new level and the frame the build began), the new `RenderInstance`, and one `ResizedStack` per stack output of the actor. |
| `ResizedStack` | One stack output on the new instance and whether it has drawn once. |
| `ResizeSwap` | What `FinishResize` reports: the two levels, the frames from build to swap, the stack count, the drawn count and the `TargetCost` of the new instance. |
| `LivePiece` | One worn armor clone: its `Actors::PieceRef` (`ref`), its `ModelToken` (`model`), the armor form id and name, the optional addon `FormKey`, the enchantment form id and its `LiveGeometry`s. |
| `LivePieceId` | A typed piece index (`enum class`, `std::size_t`). |
| `LiveGeometry` | One bound geometry: its `Actors::GeometryRef` (`ref`, set by `LivePiecesOf` through `Actors::GeometryRefOf`), engine geometry and shader-property pointers, `GeometryInputs`, `MaterialBinding`, `ShellBinding`, its `GeometryPlan`, `GeometryStackPlan` and `BindingPlan`, and its `PlacementId`s. Its `lost` flag marks it retired. |
| `LiveInstance` | One recipe matched onto the actor: the `Recipe` pointer, enchantment, effect scope, `RecipeGraph`, `SignalState` and `ActorEnvironment`. It also holds the optional `LightBinding`, the light problem text, and the timing (`startMS`, `lastTime`). |
| `LivePlacement` | The outputs bound onto one geometry: a `GeometryId` and one `PlacedOutput` per placed output. |
| `PlacedOutput` | One placed output: its recipe output index, its `RenderOutput` (`stack`), its `problem` text and its `active`, `rendered` and `renderFailed` flags. |
| `ResolvedPlacement` | The placement index and instance index that `ResolvePlacement` returns. |
| `ActorEnvironment` | The `SignalEnvironment` a `LiveInstance` ticks against. It reads actor values, actor state, the enchantment and effect shaders from the game. |

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
| `TickFrame` | The settings, view, time and resume flag of one tick. | `ManagerTick.cpp` |
| `SlotRenderView` | The view, whether any **layer** is hidden, and `Settings::publishEffects`. | `ManagerTick.cpp` |
| `SlotChain` | The rendered outputs of one slot: their `SurfaceOutput`s, resolved values, the final texture and a shown flag. | `ManagerTick.cpp` |
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
| `RecipeStoreStatus` | The load counts: loaded, with errors, held back, unresolved, imported. `LoadRecipes` and `GetRecipeStoreStatus` return it. | `RecipeStore.h` |
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
  │   RenderPlanCache hit, or CollectLayerDemands ──▶ BuildRenderPlan
  │                                                              planners/RenderPlan.h
  │   one RenderInstance per actor, AttachRender, AttachStackOutputs
  ▼
PlaceLightsOf, then live_[actorID] = LiveActor                 ManagerApply.cpp

PlayerCharacter::Update hook (every frame)                      Hooks.cpp
  │
  ▼
Manager::OnFrame                                                ManagerTick.cpp
  │   once a second: CarriedTimes::Expire, SweepAnimationEvents
  │   every kPendingRecheckMs: MarkWaitingStale
  │   every kRecheckMs: MarkRecheckStale
  ▼
Manager::Tick                                                   ManagerTick.cpp
  │   not Loaded3D: ForgetActor
  │   ClaimLostGeometries; TickInstance per LiveInstance
  ▼
AdvanceResizes: FinishResize, NextBuild, StartResize              ActorResize.cpp
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
| The manager | `Manager.h`, `Manager.cpp` (construction, queue calls, `Clear`, load, the menu's idle-camera hold), `PhaseTrace.h` (`PhaseTrace`: times named phases with `Measure` and emits one metrics or application trace event), `ManagerApply.cpp` (`SyncStale`, `SyncActor`, `InstallActor`, `DecideActor`, `RetireActor`, `ForgetActor`, live pieces from observation, placement, `BuildActorRender`, lights), `ManagerApplication.cpp` (`ChangeAndRebuildActors`, application bookkeeping), `ManagerTick.cpp` (`OnFrame`, `MarkWaitingStale`, `MarkRecheckStale`, `Tick`, `UpdateResolution`, `AdvanceResizes`, `ClaimLostGeometries`, `RenderGeometry`, slot writes, `UpdateLights`), `ActorResize.h`/`.cpp` (the resolution resize of one actor), `ManagerAnimation.cpp` (animation events), `ManagerEvents.cpp` (`Fire`, `FireAt`, `QueueEvent`, `QueueBroadcast`), `ManagerInspection.cpp` (`RequestMesh`), `ManagerSnapshot.cpp` (`GetStatus`, `BuildSnapshot`, `PublishSnapshot`, `Watch`), `LogOnce.h`/`.cpp` (`LogOnce`: a fault line once per text, through a `WarningHistory`) |
| Application and session bookkeeping | `ApplicationService.h`/`.cpp` (token tracking, rejection handling, history pruning), `SessionQueue.h`/`.cpp` (per-actor task serialisation) |
| Actor and worn-piece state | `ActorObservation.h`/`.cpp` (`ObserveActor`, `ModelTable`, `ModelOf`, `MaterialLayout`), `LiveActor.h`/`.cpp` (live records, `RetireGeometry`, `ResolvePlacement`), `Environment.h`/`.cpp` (`ActorEnvironment`), `WornKeys.h`/`.cpp` (`WornKeysOf`), `EnchantmentEffects.h`/`.cpp` (`EnchantmentValueFor`), `InstanceTime.h`/`.cpp` (`CarriedTimes`) |
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
