# actors/

The **actor core**: the pure functions that decide what the plugin installs
on one actor. It reads what the engine adapter observed and returns the
actor's next phase and the commands that bring the engine side to the plan.
It is engine-free and holds no `RE::` pointer. Its `ALLOWS` row in
`tools/gate.py` is `'actors': ('Core.h', 'recipe', 'mesh', 'planners',
'studio', 'actors')`. The engine adapter (`engine/ActorObservation.cpp`,
`engine/ManagerApply.cpp`, `engine/ManagerTick.cpp`) calls it.
`ctest --preset native` runs its tests as the suites `actors_actor`,
`actors_planning`, `actors_reconcile` and `actors_lifecycle`.

## What it owns

The core owns four steps of one actor's update. `PlanActor` turns the
actor's facts, the loaded recipes and the studio view into a plan and
the matches the planners read. `Reconcile` compares the installed plan with
the plan and returns the commands that make them equal. `Advance` picks the
actor's next phase and calls `Reconcile` when the actor is present and not
waiting. `RecheckOf` tells the adapter when to observe an actor again
without an engine event.

The core is level-triggered. The same facts give the same plan and the same
commands. A reconcile of an installed plan that equals the plan gives no
command.

The core does not observe the engine and does not perform commands.
`ObserveActor` in `engine/ActorObservation.cpp` reads the engine into
facts. `Manager::Refresh` in `engine/ManagerApply.cpp` performs the result.
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
once per refresh, and `PlanActor` and `Advance` read them. A piece with no
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

### Phase and pending models

The **phase** is where an actor is in its lifecycle. `Advance` returns the
next phase, and the adapter stores it per actor. Only a `Present` actor
holds an installed plan and pending models.

| Type | Description | Declared in |
|---|---|---|
| `Unloaded` | The actor is not loaded for effects. Nothing is installed. | `Actor.h` |
| `Evicted` | The actor is beyond the eviction distance. Nothing is installed. | `Actor.h` |
| `Present{installed, pending}` | The actor is loaded and in range. It holds the installed plan and its pending models. | `Actor.h` |
| `Phase` | `std::variant<Unloaded, Evicted, Present>`. | `Actor.h` |
| `PendingModel{piece, sinceMs}` | A **pending model**: a worn piece of a present actor whose model has not attached, with the time the piece was first seen without a model. | `Actor.h` |
| `kModelWaitMs` | 1000 ms. While a third-person pending model is younger than this, an actor with an installed plan waits and gets no command. | `Lifecycle.h` |
| `kPendingRecheckMs` | 100 ms. The interval of the settling recheck, also added to the wait to set the end of settling. | `Lifecycle.h` |

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
| `Advanced{phase, commands}` | The result of `Advance`: the next phase and the commands. | `Actor.h` |

### Requests

The requests bundle the inputs of `PlanActor` and `Advance` by reference.
`RecipeCatalog` pairs each loaded recipe with its fingerprint. `Recheck`
is the answer to "observe this actor again without an event?".

| Type | Description | Declared in |
|---|---|---|
| `RecipeCatalog` | The loaded recipes and their fingerprints, one each. `RecipeCatalog::Of` returns none when the counts differ. `FingerprintOf` returns 0 for an unknown recipe. | `Planning.h` |
| `PlanRequest{facts, recipes, view}` | The input of `PlanActor`: the facts, the catalog, and the studio `View` (recipe set and isolation filter). | `Planning.h` |
| `AdvanceRequest{phase, facts, plan, nowMs, evictDistance}` | The input of `Advance`: the current phase, the facts, the plan, the clock, and the eviction distance setting. | `Lifecycle.h` |
| `Recheck` | `kNone`, `kSettling` or `kStuck`. A present actor with a pending model is **settling** while a pending model is younger than `kModelWaitMs` plus `kPendingRecheckMs`, and **stuck** after that. | `Lifecycle.h` |

## How an actor converges

```
engine event (Events.cpp)                                  engine/
  │  Manager::QueueRefresh(actor)  marks the actor stale    Manager.cpp
  ▼
Manager::Refresh(actor) ── AdvanceActor                    engine/ManagerApply.cpp
  │
  │  ObserveActor({actor, settings, ...}, models, layout)   engine/ActorObservation.cpp
  │    ActorFacts: loaded, distance (none for the player),
  │    pieces with refs, keys, model tokens, geometries
  ▼
  │  RecipeCatalog::Of(recipes, fingerprints)               actors/Planning.cpp
  │  PlanActor({facts, catalog, view})                      actors/Planning.cpp
  │    MatchActor over the pieces that have a model ──▶ matches
  │    matches converted to the ActorPlan by refs   ──▶ plan
  ▼
  │  Advance({phase, facts, plan, nowMs, evictDistance})    actors/Lifecycle.cpp
  │    not loaded            ──▶ Unloaded, retire all installed
  │    InRange false          ──▶ Evicted,  retire all installed
  │    pending models from the facts' pieces without a model
  │    installed and a third-person pending model
  │      younger than kModelWaitMs ──▶ Present, no command
  │    else Reconcile(installed, plan) ──▶ Present, commands  actors/Reconcile.cpp
  ▼
Manager::Refresh, after AdvanceActor                       engine/ManagerApply.cpp
  │  no commands: RetireEffects if not Present; SettlePhase
  │  commands:    RetireEffects(actor)
  │               installed = AfterCommands(installed before, commands)
  │               SettlePhase(actor, phase)  drops an actor that holds nothing
  │               LiveActorFor(actor, facts, matches)
  │                 engine state from the observed model handles
  │                 and the plan's matches
  ▼
installed plan == plan; the next refresh with the same facts issues nothing


Rechecks without an event                                  engine/ManagerTick.cpp
  every kPendingRecheckMs (100 ms)
    QueueSettlingActors: RecheckOf(phase) == kSettling ──▶ QueueRefresh
  every second
    QueueSecondChecks:   RecheckOf(phase) != kNone
                         or InRange(phase, distance) disagrees with the phase
                                                       ──▶ QueueRefresh
```

`Manager::Refresh` does not yet perform the commands one by one. A refresh
with any command retires the actor's effects and installs them again whole.
The commands set the new installed plan through `AfterCommands`.

## The files

| File | What it owns |
|---|---|
| `src/actors/Actor.h`/`.cpp` | The refs, facts, plan, phase, pending model and command types, and the three `Find` lookups by ref. |
| `src/actors/Planning.h`/`.cpp` | `RecipeCatalog`, `PlanRequest`, `PlannedActor`, `PlannedPieces` (the pieces with a model) and `PlanActor`. |
| `src/actors/Reconcile.h`/`.cpp` | `Reconcile`, `AfterCommand` and `AfterCommands`. |
| `src/actors/Lifecycle.h`/`.cpp` | `kModelWaitMs`, `kPendingRecheckMs`, `Recheck`, `AdvanceRequest`, `Advance`, `InRange` and `RecheckOf`. |
| `tests/actors/fixtures.h` | Shared builders for refs and placements (`Piece`, `Geo`, `Instance`, `Placed`). |
| `tests/actors/actor_tests.cpp` | Ref equality by every field, and `Find` by ref. |
| `tests/actors/planning_tests.cpp` | Plans from facts: pieces without a model, the studio view, first-person pieces, addons, and determinism. |
| `tests/actors/reconcile_tests.cpp` | Convergence for every pair of plans, command order, and which entries a change touches. |
| `tests/actors/lifecycle_tests.cpp` | Phase changes, eviction hysteresis, the model wait per piece, rechecks, and a clock wrap. |

## See also

- `REFERENCE.md` → *Engine events, hooks and the manager* — the refresh
  through the actor core, observation of worn pieces and models, the model
  wait and the settling and stuck rechecks, and distance eviction.
- `docs/plans/actor-core-2026-10-03.md` → *Vocabulary*, *Decisions* and
  *Engine rules the core must keep*.
- `docs/conventions.md` → *Glossary* — **ref**, **plan**, **matches**,
  **model**, **installed**, **phase**, **command**, **stale**.
