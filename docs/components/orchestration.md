# orchestration/

The **tick core**: the engine-free function that makes the decisions of one
tick. The engine gathers **facts**, calls the core once per tick, and carries
out the **commands** that the core returns. The core decides the render
lifecycle of each actor, the render target budget, install pacing and the
sync of stale actors. It holds no `RE::` pointer and calls no engine
function. It compiles natively and `ctest --preset native` runs its tests.
Its `ALLOWS` row in `tools/gate.py` is `'orchestration': ('Core.h',
'TraceTypes.h', 'recipe', 'mesh', 'planners', 'studio', 'actors',
'orchestration')`. Only `engine/` includes it.

## What it owns

**The world.** `World` is the core's whole state: one `ActorRecord` per
actor, the budget, the budget radius, the waiting installs, the sync log and
the recheck timers. The engine keeps it in `Manager::world_` and reads it
only as `const`.

**The render phase.** Each actor with installed effects has one
`RenderPhase`: `Empty`, `Planning`, `Drawing`, `Shown`, `Queued` or
`Running`. `PhaseFor` creates a phase from the first event of an install.
`OnEvent` applies an event that the engine reports. It matches the pair of
phase and event: each legal pair has its own case, and every other pair
leaves the phase as it was and marks the change `ignored`. The decision functions
(`PlanBuilt`, `FailPlan`, `WantLevel`, `ReleaseForBudget`, `SeeActor`,
`EndFirstDraw`) change a phase. Each returns a `PhaseChange`: the next phase
and the commands that it implies.

**The steps.** `Tick` runs five steps in a fixed order. Each step takes the
world and the facts and returns a `TickOutput`. `Tick` appends the commands,
trace records and log lines of each step in step order. The order of the
command list is the order in which the engine executes the commands.

**The contract with the engine.** One pass runs per tick. The result of a
fallible command is a fact of the next tick. The engine owns every resource
by actor id, and only a command creates or destroys one. The core returns
trace records and log lines; the engine writes them. The agreement check
compares the engine's resources with the core's phases before each tick.

**Boundary facts.** The engine parses its readings into facts before the
core sees them. `InViewOf` and `ScreenFractionOf` (`planners/ViewFacts.h`)
give the sighting. `ClockAfter` (`planners/TickClock.h`) gives the 64-bit
time. `Actors::PlanActor` gives the plan of a stale actor. The core converts
an `ActorId` to a 32-bit form id (`FormOf`, `ActorOf`) only at a planner call.

## Data

Every type below is in the namespace `Orchestration`.

### The world and its records

`World.h` holds the state that the core carries from one tick to the next.
A record keeps what one actor needs across ticks. An actor without a record
is unknown to the core.

| Type | Description | Declared in |
|---|---|---|
| `ActorId`, `FormOf`, `ActorOf` | The actor's id (`Actors::ActorId`) and the conversions to and from a 32-bit form id. | `World.h` |
| `Sighting` | One tick's sight of an actor: its role, whether its 3D is loaded, whether it is in view, its optional screen fraction, its optional distance to the player and its name. | `World.h` |
| `InstalledRender` | The render of an actor with installed effects: its `RenderPhase`, its `LevelCost` per level (`levelCosts`), and the frame it was first drawn in view (`renderedFrame`). | `World.h` |
| `ActorRecord` | One actor: its last `Sighting`, its actor-core state (`syncState`), the level and the **return bytes** kept from its last retire (`reinstallLevel`, `returnBytes`), and its optional `InstalledRender`. | `World.h` |
| `Timers` | The last waiting check and the last recheck. | `World.h` |
| `World` | The records by `ActorId`, the `BudgetState`, the optional `BudgetRadius`, the `WaitingInstall` list, the `Actors::SyncRecord` log and the `Timers`. | `World.h` |

### The render phase

`RenderPhase.h` holds the lifecycle of one actor's render. The budget and the
lifecycle step read it through the query functions, so no step inspects a
phase variant by hand. `PhaseChanges.h` changes it.

| Type or function | Description | Declared in |
|---|---|---|
| `PlanJobId` | The id of a plan job, as the core sees it. | `RenderPhase.h` |
| `Held`, `Freed`, `Intermediates` | Whether a shown render holds its intermediate targets. A held render carries the `Hold` of its time out of view (`outOfView`). | `RenderPhase.h` |
| `UnpublishedDraw` | A draw before publication: the slot or stack count (`total`), the drawn count, the frame it began and the ticks that drew. | `RenderPhase.h` |
| `FailedLevel` | A level whose resize did not start or did not swap, and the wanted level at that time (`wantedThen`). | `RenderPhase.h` |
| `Empty` | An install that built no render, or a plan job that failed. It has no level. | `RenderPhase.h` |
| `Shown` | A published render: its level, its `Intermediates`, the hold of a small screen fraction (`smallOnScreen`), the level of a blocked step up (`stepUpBlockedFrom`) and an optional `FailedLevel`. | `RenderPhase.h` |
| `Planning` | An install that waits for a plan job. | `RenderPhase.h` |
| `Drawing` | A new render that draws before publication: its level, its `UnpublishedDraw` and a level wanted meanwhile (`next`). | `RenderPhase.h` |
| `Resize` | A level change: the old and new levels (`from`, `to`), the attached render's `IntermediateHolding` and an optional `FailedLevel`. | `RenderPhase.h` |
| `Queued`, `Running` | A resize that waits to start, and a resize that draws its `UnpublishedDraw`. Both carry a `Resize`. | `RenderPhase.h` |
| `RenderPhase` | The variant of the six phases. | `RenderPhase.h` |
| `Level`, `ResizeIn` | The name of `ResolutionLevel` inside the core, and the `Resize` of a `Queued` or `Running` phase. | `RenderPhase.h` |
| `TargetLevel`, `AppliedLevel`, `HoldingOf` | The level the actor is going to, the level its attached render has, and its holding. | `RenderPhase.h` |
| `MayRelease`, `MayStepDown`, `MayEvict` | The budget's three questions: release holds in `Shown` with held intermediates, a step down only in `Shown`, an eviction in every phase except `Queued` and `Running`. | `RenderPhase.h` |
| `UnpublishedDrawOf`, `ResizeProgressOf`, `AwaitedJob`, `FailedLevelOf`, `ScreenStepDownDue`, `PhaseName` | The unpublished draw, the running resize's progress, the awaited job, the refused level, whether the screen step-down hold is due, and the trace name of a phase. | `RenderPhase.h` |
| `PhaseChange` | The next phase, its commands, and `ignored` when the phase cannot take the event. | `PhaseChanges.h` |
| `EventContext`, `SeenThisTick` | The frame and wanted level that `OnEvent` reads; the sight, small flag, wanted level, time, release delay and releasable bytes that `SeeActor` reads. | `PhaseChanges.h` |
| `PhaseFor`, `OnEvent`, `PlanBuilt`, `FailPlan`, `WantLevel`, `ReleaseForBudget`, `SeeActor`, `EndFirstDraw` | The phase transitions. Every command that a phase change implies comes from one of them. | `PhaseChanges.h` |

### Facts and results

`Facts.h` holds what the engine tells the core in one tick. Each fact is
parsed at the engine boundary. A result of a command arrives in the tick
after the command.

| Type | Description | Declared in |
|---|---|---|
| `TickSettings` | The settings the core reads: the eviction distance, screen sizing, `publishEffects`, `verboseLogging`, the unpublished-draw budget, the out-of-view release delay and the screen step-down hold. | `Facts.h` |
| `StaleActor` | An actor to sync: its `Actors::ActorFacts`, its `Actors::PlannedActor` and its reinstall demand. | `Facts.h` |
| `ClaimedGeometry` | A geometry that another system took, with its `Actors::Claim`. The engine has already retired it. | `Facts.h` |
| `PlanJobBuilt`, `PlanJobFailed`, `PlanJobDone` | A finished plan job, built or failed with a problem. | `Facts.h` |
| `PlanAwaited`, `Installed`, `InstalledWithoutRender` | The outcome of an install or an awaited build: a job to wait for; a render with its level, first-draw slot count (`total`), level costs and charged bytes; or no render. | `Facts.h` |
| `UnpublishedDrawn` | The drawn count after an unpublished draw, and whether the actor drew in this tick. | `Facts.h` |
| `ResizeStarted`, `ResizeNotStarted`, `ResizeSwapped`, `ResizeNotSwapped` | The outcome of a resize start or swap. | `Facts.h` |
| `RenderEvent`, `ActorEvent` | The variant of the eight events above, and an event with its actor. | `Facts.h` |
| `InstallFailed`, `Retired`, `CommandResult` | An install that built nothing, a finished retire, and the variant of all results. | `Facts.h` |
| `TickFacts` | One tick's facts: time, frame, `TickSettings`, one `Sighting` per actor, last tick's results, finished jobs, stale actors, claims, the optional `BudgetSample`, the frame's sync time and the forget requests. | `Facts.h` |

### Commands

`Commands.h` holds what the core asks of the engine. An executor carries out
a command and decides nothing. A command that can fail reports a result for
the next tick.

| Type | Description | Declared in |
|---|---|---|
| `TickActor` | Tick the actor's signals and apply the core's flags: `drawStacks`, `concealSlots`, `hideShells`, `hideLights`, `finishApplications`. | `Commands.h` |
| `StartResize`, `SwapResize`, `AbandonResize` | Build the render at a new level, attach it, or drop it. | `Commands.h` |
| `ReleaseIntermediates`, `ReleaseReason` | Release the attached render's intermediates, out of view (`kOutOfView`) or for the budget (`kBudget`), with the bytes the core expects to free. | `Commands.h` |
| `SetAppliedLevel` | Give every instance environment of the actor a level. | `Commands.h` |
| `DrawUnpublished` | Draw unpublished slots and stacks of the listed actors, in order, within `budgetUs`. | `Commands.h` |
| `BuildAwaitedRender`, `FailAwaitedRender` | Build the render from a finished plan job within an `InstallBound`, or fail the actor's outputs with the job's problem. | `Commands.h` |
| `Install`, `InstallKind` | Install the actor from its facts and plan, as a first install or a reinstall, with its `InstallBound` and its replaced bytes. | `Commands.h` |
| `Retire`, `Forget`, `Observe`, `KeepModels`, `SettleApplications` | Undo the actor's effects and keep its tracking; drop everything held for it; observe it again; keep the models of a plan; end its equip and reinstall applications as flagged. | `Commands.h` |
| `Command` | The variant of the fifteen commands. | `Commands.h` |
| `LogLevel`, `LogRecord`, `TraceRecord` | One log line and one trace event that the engine writes for the core. | `Commands.h` |

### The steps and their helpers

`Tick.h` declares the steps. `Budget.h`, `Sync.h` and `Lifecycle.h` declare
the helpers that tests and the engine's heartbeat also read.

| Type or function | Description | Declared in |
|---|---|---|
| `TickOutput` | The next `World`, the commands, the trace records and the log lines. | `Tick.h` |
| `RecordSyncResults`, `RecordRenderResults`, `RunBudget`, `RunSync`, `RunLifecycle`, `Tick` | The five steps and their composition. | `Tick.h` |
| `PlannedBytesOf` | The bytes the budget counts for one rendered actor or for the world: the level cost at the applied level and holding, plus the target level's full cost while a resize runs. | `Budget.h` |
| `InstallBoundOf` | The bound of an install: the target level, else the reinstall level, else the wanted level, with `HeadroomOf` the budget. The player gets no headroom. | `Budget.h` |
| `BudgetActors`, `BudgetActorOf`, `BudgetActorsOf` | The planners' `BudgetActor` for each rendered actor, and whether any actor resizes. | `Budget.h` |
| `BlockedStepUp`, `BlockedStepUpsOf` | Each live actor in view with a blocked step up, with its preemption request, largest screen fraction first. | `Budget.h` |
| `EvictedActorsOf`, `LargestReturnBytes`, `FarthestTrackedDistance` | The evicted actors with their distances and return bytes, the largest return bytes, and the distance of the farthest actor with a state. | `Budget.h` |
| `PlannerMs`, `ReturnBytesOf`, `RangeChanged` | The 32-bit planner time, the return bytes of a record, and whether an actor's range differs from what it has installed. | `Sync.h` |
| `ActorSightOf`, `TickActorFor`, `EventName` | The planners' `ActorSight` of a sighting, the `TickActor` flags of a phase, and the trace name of an event. | `Lifecycle.h` |

### The agreement check

`Agreement.h` compares the core's phases with the resources that the engine
holds. The engine runs it at the start of each tick and reports each finding
as `resource_disagreement`.

| Type or function | Description | Declared in |
|---|---|---|
| `UnpublishedCount`, `HeldRender`, `HeldResize`, `HeldResources` | What the engine holds for one actor: the attached render's level and holding, the first draw's counts, the resize's level and counts, and the awaited job. | `Agreement.h` |
| `Disagreement`, `Disagreements` | One finding for one actor, and every finding between a world and the held resources. | `Agreement.h` |
| `SettledWorld` | The world after this tick's results are recorded, without the finished jobs and without the results that abandon a resize. Both sides then describe the same moment. | `Agreement.h` |

## How a tick flows

```
Manager::RunTick                                           engine/ManagerTick.cpp
  │  DrainPlanJobs ──▶ PlanJobDone                           engine/ManagerApply.cpp
  │  GatherFacts ──▶ TickFacts                               engine/ManagerFacts.cpp
  │  CheckAgreement: SettledWorld, Disagreements             Agreement.cpp
  ▼
Tick(world, facts)                                         Tick.cpp
  │
  ├─ 1 RecordSyncResults                                    Sync.cpp
  │     charge Installed::chargedBytes, reset the state of an
  │     InstallFailed, erase the render of a Retired, add claims
  ├─ 2 RecordRenderResults                                  Lifecycle.cpp
  │     sightings; each ActorEvent through PhaseFor or OnEvent;
  │     each finished job through PlanBuilt or FailPlan; rendered frames
  ├─ 3 RunBudget (only with a memory sample)                Budget.cpp
  │     NextBudget; under pressure RelieveBudget: releases, one
  │     step down, or ShrinkBudgetRadius; else PreemptForBlockedStepUp;
  │     GrowBudgetRadius
  ├─ 4 RunSync                                              Sync.cpp
  │     forget unloaded, failed and requested actors;
  │     SyncStale per stale actor (waiting installs first):
  │       Actors::Advance, defer, settle, or Retire and Install;
  │     ObserveDue: rechecks, claims, the next waiting install
  └─ 5 RunLifecycle                                         Lifecycle.cpp
        swaps and at most one resize start; per loaded actor
        SeeActor and the screen level step; first-draw ends;
        one TickActor per actor; one DrawUnpublished
  ▼
TickOutput{world, commands, trace, logs}
  │  world_ = out.world; EmitCoreRecords                    engine/ManagerFacts.cpp
  ▼
Manager::Execute: one executor per command, in order       engine/ManagerCommands.cpp
  │  results_ ──▶ TickFacts::results of the next tick
```

## The files

| File | What it owns |
|---|---|
| `World.h` | `Sighting`, `InstalledRender`, `ActorRecord`, `Timers`, `World`, `FormOf`, `ActorOf`. |
| `RenderPhase.h`/`.cpp` | The render phase variant and its queries. |
| `PhaseChanges.h`/`.cpp` | `PhaseChange` and the phase transitions. |
| `Facts.h` | `TickSettings`, the stale actors, claims, job outcomes, render events, results and `TickFacts`. |
| `Commands.h` | The commands, `LogRecord` and `TraceRecord`. |
| `Tick.h`/`.cpp` | `TickOutput`, the step declarations and `Tick`. |
| `Sync.h`/`.cpp` | `RecordSyncResults` and `RunSync`: forgets, the actor-core sync, install deferral, the sync log and the rechecks. |
| `Lifecycle.h`/`.cpp` | `RecordRenderResults` and `RunLifecycle`: phase events, plan jobs, resize scheduling, the screen level step, admission of step ups, first-draw ends, `TickActor` and the unpublished draw order. |
| `Budget.h`/`.cpp` | `RunBudget`: pressure, releases, step downs, preemption, the budget radius, the estimate and the install bound. |
| `Agreement.h`/`.cpp` | The agreement check. |
| `tests/orchestration/*_tests.cpp` | Unit tests per step and per phase change, and the scenario suites over hundreds of ticks. |
| `tests/orchestration/sim/` | The simulator shell that executes commands with counters and failure injection, the invariants checked after every tick, and the contract table of each step. |

## See also

- `REFERENCE.md` → *orchestration*, *The lifecycle step*, *The sync step*,
  *Boundary facts*, *The agreement check*, *The engine runs the core* and
  *The install program*.
- `docs/components/engine.md`: the fact gathering and the command executors.
- `docs/components/planners.md`: the level, budget, radius, pacing and
  install-program decisions that the core calls.
- `docs/components/actors.md`: `Actors::Advance`, which the sync step calls.
- `docs/conventions.md` → *Component ownership* → `Manager`, *Functional
  core, thin adapter* and *Gates* → *Layers*.
