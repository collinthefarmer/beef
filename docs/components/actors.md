# actors/

The **actor core**: the pure functions that decide what the plugin installs
on one actor. It reads what the engine adapter observed and returns the
actor's next state and the commands that bring the engine side to the plan.
It is engine-free and holds no `RE::` pointer. Its `ALLOWS` row in
`tools/gate.py` is `'actors': ('Core.h', 'recipe', 'mesh', 'planners',
'studio', 'actors')`. The engine adapter calls `PlanActor` when it gathers facts
(`engine/ManagerFacts.cpp`). The tick core calls `Advance`, `KindOf`,
`RecheckOf`, `InRange` and `WithClaim` (`orchestration/Sync.cpp`).
`ctest --preset native` runs its tests as the suites `actors_actor`,
`actors_planning`, `actors_reconcile` and `actors_lifecycle`.

## What it owns

The core owns four steps of one actor's update. `PlanActor` turns the
actor's facts, the loaded recipes and the studio view into a plan and
the matches the planners read. `Reconcile` compares the installed plan with
the plan and returns the commands that make them equal. `Advance` picks the
actor's next state and calls `Reconcile` when the actor is loaded, in range
and not waiting. `RecheckOf` tells the adapter when to observe an actor again
without an engine event.

The core is level-triggered. The same facts give the same plan and the same
commands. A reconcile of an installed plan that equals the plan gives no
command.

The core does not observe the engine and does not perform commands.
`ObserveActor` in `engine/ActorObservation.cpp` reads the engine into
facts. The tick core's sync step (`RunSync`, `orchestration/Sync.cpp`)
turns the transition into commands, and the engine's executors
(`engine/ManagerCommands.cpp`) perform them.
The core does not own recipe matching. `MatchActor` in
`planners/ActorPlanning.h` produces the matches, and `EvictionFor` in
`planners/Eviction.h` decides distance eviction.

## Data

Every type below is in the namespace `Actors`.

### Identity refs

A **ref** is a value that identifies one thing. The plan, the installed
plan and the commands name every entry by its ref, so two plans compare
entry by entry. Each ref compares by every field, and `Find` looks up a
plan entry by its ref.

| Type | Description | Declared in |
|---|---|---|
| `ActorId` | An actor's form ID as a distinct `enum class` over `std::uint32_t`. | `Actor.h` |
| `ModelToken` | The identity of one **model**, the 3D the engine attaches for a worn piece (the biped `partClone`). An `enum class` over `std::uint64_t`. The adapter mints it and holds the model. | `Actor.h` |
| `PieceRef{item, addon, firstPerson}` | One **piece**: one worn armor item, its optional armor addon, and the person (third or first) that wears it. | `Actor.h` |
| `GeometryRef{piece, name, ordinal}` | One **geometry**, a `BSGeometry` inside a piece's model. `ordinal` counts the earlier geometries of equal name in the piece. | `Actor.h` |
| `InstanceRef{recipe, fingerprint, enchantment, effectKey}` | One **instance**: one recipe on one actor for one enchantment. The recipe fingerprint makes an edited recipe a new instance. | `Actor.h` |
| `PlacementRef{instance, geometry}` | One **placement**: one instance on one geometry. | `Actor.h` |

### Facts

**Facts** are one observation of one actor. `ObserveActor` builds them
once per sync, and `PlanActor` and `Advance` read them. A piece with no
`model` has a worn item whose model has not attached yet.

| Type | Description | Declared in |
|---|---|---|
| `GeometryFacts{name, diffusePath}` | One geometry's name and diffuse texture path. | `Actor.h` |
| `PieceFacts{ref, itemID, enchantmentID, keys, model, geometries}` | One worn piece: its ref, the item and enchantment form IDs, its recipe matching keys (`WornPiece`), its model token if the model attached, and its geometries. | `Actor.h` |
| `ActorFacts{actor, loaded, distance, pieces}` | One actor: its ID, whether it is loaded for effects, its distance from the player (none for the player), and its pieces. | `Actor.h` |

### Plan

A **plan** (`ActorPlan`) is what should be installed on one actor, as plain
data named by refs. `PlanActor` makes it from the facts. The **installed**
plan is what the engine side holds for the actor, in the same shape.
`Reconcile` compares the two.

| Type | Description | Declared in |
|---|---|---|
| `PlannedInstance{ref}` | One instance to install. | `Actor.h` |
| `PlannedGeometry{ref, model}` | One geometry to install, with the token of the model that holds it. A new model token makes a different geometry entry. | `Actor.h` |
| `PlannedPlacement{ref, matchedBy, outputs, priority}` | One placement to install: the recipe key that matched it, the recipe outputs it places (`OutputPlacement`), and its priority. | `Actor.h` |
| `ActorPlan{instances, geometries, placements}` | One actor's plan. Equality compares every entry. | `Actor.h` |
| `PlannedActor{plan, matches}` | The result of `PlanActor`: the plan and the `ActorMatches` it came from. The adapter builds the engine state from the matches. | `Planning.h` |

### State, pending models and claims

The **state** (`ActorState`) is the per-actor record. `Advance` returns the
next state, and the adapter stores it per actor. An actor without a state
has **no record**. `Advance` returns no state when the plan, the installed
plan, the pending models and the claims are all empty.

| Type | Description | Declared in |
|---|---|---|
| `ActorState{plan, installed, pending, claims}` | One actor's record: the last plan, the installed plan, the pending models and the claims. | `Actor.h` |
| `PendingModel{piece, sinceMs}` | A **pending model**: a worn piece whose model has not attached, with the time the piece was first seen without a model. | `Actor.h` |
| `Claim{geometry, model}` | A **claim**: a geometry that another system took, with the token of the piece's model at that time. | `Actor.h` |
| `kModelWaitMs` | 1000 ms. While a third-person pending model is younger than this, an actor with an installed plan waits and gets no command. | `Lifecycle.h` |
| `kPendingRecheckMs` | 100 ms. The interval of the waiting recheck. It is also added to `kModelWaitMs` to set the end of waiting. | `Lifecycle.h` |
| `kRecheckMs` | 1000 ms. The interval of the stuck and range recheck. | `Lifecycle.h` |

An **evicted** actor has a state with a plan and an empty installed plan.

### Commands

A **command** is an instruction the core issues for the engine side to
perform. `Reconcile` orders them: retire placements, geometries and
instances first, then install instances, geometries and placements.
`AfterCommands` applies a command list to an installed plan, which gives
the installed plan after the engine performs them.

| Type | Description | Declared in |
|---|---|---|
| `InstallInstance{instance}`, `RetireInstance{ref}` | Add or undo the engine state of one instance. | `Actor.h` |
| `InstallGeometry{geometry}`, `RetireGeometry{ref}` | Add or undo the engine state of one geometry. | `Actor.h` |
| `InstallPlacement{placement}`, `RetirePlacement{ref}` | Add or undo one placement. | `Actor.h` |
| `RebuildRender` | Rebuild the actor's render. `Reconcile` issues it last, once, when a placement changed and the plan still has placements. | `Actor.h` |
| `ActorCommand` | The `std::variant` of the seven commands above. | `Actor.h` |
| `Transition{state, commands}` | The result of `Advance`: the next state (none when the actor has no record) and the commands. | `Actor.h` |

### Requests

The requests bundle the inputs of `PlanActor` and `Advance` by reference.
`PlanRequest` reads the applied `RecipeCatalog` (`recipe/RecipeCatalog.h`). `Recheck`
is the answer to "observe this actor again without an event?".

| Type | Description | Declared in |
|---|---|---|
| `PlanRequest{facts, recipes, view, claims}` | The input of `PlanActor`: the facts, the catalog, the studio `View` (recipe set and isolation filter), and the actor's claims. | `Planning.h` |
| `AdvanceRequest{state, facts, plan, nowMs, evictDistance, reinstall}` | The input of `Advance`: the current state (none when the actor has no record), the facts, the plan, the clock, the eviction distance (`EffectiveEvictDistance`: the setting or the smaller budget radius), and the reinstall demand. | `Lifecycle.h` |
| `Reinstall` | `kIfChanged` or `kAlways`. A **reinstall** (`kAlways`) gives an in-range actor commands that retire everything installed and install the whole plan, without a model wait. A rebuild asks for it. | `Lifecycle.h` |
| `Recheck` | `kNone`, `kWaiting` or `kStuck`. An actor with a pending model is **waiting** while a pending model is younger than `kModelWaitMs` plus `kPendingRecheckMs`, and **stuck** after that. | `Lifecycle.h` |

### Functions over the state

| Function | Description | Declared in |
|---|---|---|
| `Advance` | Returns the `Transition` for one actor. The section *How an actor converges* gives its rules. | `Lifecycle.h` |
| `InRange(installed, distance, evictDistance)` | Tells if an actor is in range. An actor with something installed stays in range out to 1.0 × `evictDistance`. An actor with nothing installed comes in range only inside 0.8 × `evictDistance`. An actor with no distance (the player) is always in range. An `evictDistance` of 0 or less puts every actor in range. | `Lifecycle.h` |
| `AnythingInstalled(state)` | Tells if a state has a non-empty installed plan. | `Lifecycle.h` |
| `RecheckOf(state, nowMs)` | Returns the `Recheck` of a state from its pending models. | `Lifecycle.h` |
| `LiveClaims(claims, facts)` | Keeps the claims whose piece still has the claimed `ModelToken`. A claim ends when its piece gets a new model (a re-equip or a 3D reload). | `Lifecycle.h` |
| `WithClaim(state, claim)` | Returns the state with the claim added once. | `Lifecycle.h` |
| `GeometryRefOf(piece, index)` | Returns the `GeometryRef` of a piece's geometry at an index, or none when the index is out of bounds. Planning and the adapter's `LivePiecesOf` share this identity rule. | `Planning.h` |

## How an actor converges

```
engine event (Events.cpp)                                  engine/
  │  Manager::MarkStale(actor) ──▶ SessionQueue task        Manager.cpp, SessionQueue.cpp
  │  ApplicationService::RunActor ──▶ Manager::NoteStale    engine/ManagerFacts.cpp
  ▼
next tick: Manager::StaleActorOf(actor)                    engine/ManagerFacts.cpp
  │  ObserveActor({actor, settings, ...}, models, layout)   engine/ActorObservation.cpp
  │    ActorFacts: loaded (Loaded3D), distance (DistanceToPlayer,
  │    none for the player), pieces with refs, keys, model tokens, geometries
  │  AppliedCatalog(debug)  one immutable catalog per tick  engine/RecipeStore.cpp
  │  LiveClaims(record.syncState.claims, facts)             actors/Lifecycle.cpp
  │  PlanActor({facts, catalog, view, claims})              actors/Planning.cpp
  │    MatchActor over the pieces that have a model ──▶ matches
  │    a claimed geometry resolves no recipe: it keeps its
  │    position in the matches and gets no placement
  │    matches converted to the ActorPlan by refs   ──▶ plan
  │  ReinstallDue ──▶ StaleActor{facts, planned, reinstall}
  ▼
RunSync ── SyncStale ── Decide                              orchestration/Sync.cpp
  │  Advance({syncState, facts, plan, nowMs,                actors/Lifecycle.cpp
  │           EffectiveEvictDistance, reinstall})
  │    not loaded            ──▶ no state, retire all installed
  │    claims = LiveClaims(state.claims, facts)
  │    InRange false         ──▶ state {plan, nothing installed, claims},
  │                              retire all installed
  │    pending models from the facts' pieces without a model
  │    installed and a third-person pending model
  │      younger than kModelWaitMs ──▶ no command
  │    else Reconcile(installed, plan) ──▶ commands         actors/Reconcile.cpp
  │    next installed = AfterCommands(installed, commands)
  │    plan, installed, pending and claims all empty ──▶ no state
  │  KindOf ──▶ the sync log (World::syncLog)               actors/SyncLog.cpp
  ▼
RunSync, after Decide                                       orchestration/Sync.cpp
  │  an install that InstallMayStart refuses: KeepModels,
  │    the waiting installs, trace sync_deferred
  │  no state:    Forget
  │  no commands: KeepModels, syncState = state, SettleApplications
  │  commands:    KeepModels, Retire (when something was installed),
  │               syncState = state,
  │               Install (when AnythingInstalled(state)), SettleApplications
  ▼
engine executors                                            engine/ManagerCommands.cpp
  │  Retire ──▶ RetireActorResources
  │  Install ──▶ InstallLiveActor ──▶ LiveActorFor
  │    engine state from the observed model handles and the plan's
  │    matches; on failure InstallFailed, and the core forgets the actor
  ▼
installed plan == plan; the next sync with the same facts issues nothing


Rechecks without an event (ObserveDue)                     orchestration/Sync.cpp
  every kPendingRecheckMs (100 ms)
    RecheckOf(state) == kWaiting                          ──▶ Observe
  every kRecheckMs (1000 ms)
    RecheckOf(state) != kNone or RangeChanged(state)      ──▶ Observe
  every tick
    a claim from Manager::ClaimsOf (the geometry is already
    retired) ──▶ WithClaim (RecordSyncResults), Observe
    the next waiting install                              ──▶ Observe
```

The tick core does not perform the commands one by one. A sync with any
command retires the actor's effects and installs them again whole. The
commands set the new installed plan through `AfterCommands`.

## The files

| File | What it owns |
|---|---|
| `src/actors/Actor.h`/`.cpp` | The refs, facts, plan, state, pending model, claim and command types, and the three `Find` lookups by ref. |
| `src/actors/Planning.h`/`.cpp` | `PlanRequest`, `PlannedActor`, `GeometryRefOf`, `PlannedPieces` (the pieces with a model) and `PlanActor`. |
| `src/actors/Reconcile.h`/`.cpp` | `Reconcile`, `AfterCommand` and `AfterCommands`. |
| `src/actors/Lifecycle.h`/`.cpp` | `kModelWaitMs`, `kPendingRecheckMs`, `kRecheckMs`, `Recheck`, `Reinstall`, `AdvanceRequest`, `Advance`, `InRange`, `AnythingInstalled`, `RecheckOf`, `LiveClaims` and `WithClaim`. |
| `src/actors/SyncLog.h`/`.cpp` | `SyncKind`, `SyncKindName`, `KindOf`, `SyncRecord`, `Recorded` and `SyncSummary`: what a sync did, derived from the transition, and the last 2 s of syncs (at most 64) that the burst line names. |
| `tests/actors/fixtures.h` | Shared builders for refs and placements (`Piece`, `Geo`, `Instance`, `Placed`). |
| `tests/actors/actor_tests.cpp` | Ref equality by every field, and `Find` by ref. |
| `tests/actors/planning_tests.cpp` | Plans from facts: pieces without a model, the studio view, first-person pieces, addons, claimed geometries, and determinism. |
| `tests/actors/reconcile_tests.cpp` | Convergence for every pair of plans, command order, and which entries a change touches. |
| `tests/actors/lifecycle_tests.cpp` | State changes, the forgotten actor, eviction hysteresis derived from what is installed, the model wait per piece, rechecks, a clock wrap, and claims. |

## See also

- `REFERENCE.md` → *Engine events, hooks and the manager*: the sync
  through the actor core, observation of worn pieces and models, the model
  wait and the waiting and stuck rechecks, claims, and distance eviction.
- `REFERENCE.md` → *The sync step*, and `docs/components/orchestration.md`:
  the tick core that calls `Advance`.
- `docs/plans/actor-core-2026-10-03.md` → *Vocabulary*, *Decisions* and
  *Engine rules the core must keep*.
- `docs/conventions.md` → *Glossary* — **ref**, **plan**, **matches**,
  **model**, **installed**, **state**, **claim**, **command**, **stale**,
  **sync**, **decide**, **retire**, **forget**.
