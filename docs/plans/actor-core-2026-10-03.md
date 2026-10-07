Status: open. Stages 0 to 3 and 4a done on branch `actor-core`; 4b waits for a measurement that rebuilds still matter. Replaces the
refresh-and-rebuild lifecycle in `engine/Manager*.cpp` with a pure actor core
that reconciles each actor to a keyed plan. Completes the actor state tables
decided on 2026-09-09.

# Actor core

## Goal

An actor's installed effects always converge to its plan, whatever order the
engine events arrive in. A change rebuilds only the geometries and instances
it touches. Expensive installs spread over frames under a time budget.

Measured starting point (`docs/checkpoints/regression-baseline-2026-10-03.md`,
fight soak of 2026-10-03): a full rebuild of one actor with three recipes takes
40 to 78 ms in one frame; `PlaceInstances` takes 38 to 46 ms of it and the
retire 2 or 27 to 33 ms.

## Model

Events only **mark an actor stale**. Each frame the adapter takes stale actors
in order, **observes** each into `ActorFacts`, **plans** a keyed `ActorPlan`,
and **advances** the actor's state, which yields **commands**. The adapter
**admits** commands under a per-frame time budget and **performs** them. An
actor with commands left stays stale; the next frame recomputes them. No
command queue survives a frame.

The core is level-triggered: the same facts give the same commands, and a
reconcile of an actor whose installed state equals its plan gives none.

## Vocabulary

The naming pass of 2026-10-03. The glossary in `docs/conventions.md` gains
these words when stage 1 lands.

| Word | Meaning | Type |
|---|---|---|
| **piece** | One worn item and addon that can carry effects, as `WornArmorOf` decides | `PieceRef` (item, addon, first person) |
| **model** | The 3D the engine attaches for a worn piece (the biped `partClone`) | `ModelToken` |
| **geometry** | One `BSGeometry` inside a piece's model | `GeometryRef` (piece, name, ordinal among equal names) |
| **instance** | One recipe on one actor for one enchantment: signals, environment, clock, light | `InstanceRef` (recipe, fingerprint, enchantment, effect key) |
| **placement** | One instance on one geometry | `PlacementRef` |
| **plan** | What should be installed, as plain data | `ActorPlan` (keyed) |
| **installed** | What the engine side holds | `ActorState::installed` (an `ActorPlan`) |
| **facts** | One frame's observation of one thing | `ActorFacts` |
| **state** | The per-actor record: plan, installed plan, pending models and claims; an actor without one has no record | `ActorState` |
| **pending model** | A worn piece whose model has not attached, with the time it was first seen | `PendingModel` |
| **claim** | A geometry that another system took, while its piece keeps the claimed model | `Claim` (geometry, model) |
| **command** | An instruction the pure core issues for the engine side | `ActorCommand` |
| **stale** | Possibly out of date; observe again | `Manager::MarkStale` |

| Verb | Meaning |
|---|---|
| Observe | Read the engine into `ActorFacts` |
| Plan | Facts, recipes and the studio view to a keyed `ActorPlan` (`PlanActor`) |
| Advance | State, facts, plan and time to the next state and commands (`Transition`) |
| Reconcile | Installed state and plan to commands |
| Install, Retire | Add or undo engine state for an instance, a geometry or a placement |
| Perform | Carry out one command |
| Decide | Observe, plan and advance one actor without performing (`Manager::DecideActor`) |
| Sync | Decide one stale actor and perform the result (`Manager::SyncActor`) |
| Retire an actor | Undo everything installed on the actor and keep its record (`Manager::RetireActor`) |
| Forget | Retire the actor, stop its animation subscriptions and events, and erase its record (`Manager::ForgetActor`) |
| Admit | Let a command start this frame within the budget |

Identity types end in **Ref**, as `Studio::PieceRef`, `TextureRef` and
`ResourceRef` do. **Key** keeps its recipe meaning, a matching criterion
(`RecipeKey`, `KeyKind`, the `PieceKey` choices of a worn piece), and no
identity type uses it.

Dropped from the actor code: **refresh** (the application queue, its traces
and the budget fields say **sync**), **apply** for the act (the word keeps
its application-token meaning), **place** as a verb, **restore** for leaving
eviction, **live** (kept for heartbeat live targets and the engine's
`LiveActor` records in `Manager::live_`).

## Decisions

| Id | Decision | Reason |
|---|---|---|
| D1 | The pure core is the engine-free module `src/actors/`; the engine adapter keeps the name `Manager` and loses everything the core takes. | The conventions and the other modules already refer to `Manager`; a rename is a later, separate pass. |
| D2 | `Trace::Command` becomes `Trace::Operation`, and `Regression::ActorFacts` becomes `Regression::RoleFacts`. | **command** and **facts** each keep one meaning. |
| D3 | `GeometryRef` always carries the ordinal among geometries of equal name in the piece. | Names are not verified unique within a model; the ordinal costs nothing when they are. |
| D4 | A geometry's slot chain is rebuilt when any placement on that geometry is installed or retired. | The coarse rule is correct; a finer rule waits for a measurement. |
| D5 | The admission budget is time-based, 4 ms of performed commands per frame, as a setting. | A count does not bound a 40 ms install. |
| D6 | An application token completes when no command for its recipe remains on its actor and the outputs rendered. | Reconcile has no single "refresh done" moment. |
| D7 | `InstanceRef` carries the recipe fingerprint. | An edit replaces the instance (the live-mutation rule becomes structural); a save that changes nothing keeps it. |
| D8 | The planners' positional `ActorPlan` is renamed `ActorMatches`; `Actors::ActorPlan` is the only plan of an actor. `PlanActor` converts matches to the keyed plan. After the switch, `Geometry::lost` and `AnyLiveGeometry` go, and a keyed `MatchActor` is considered only if the conversion is awkward. | `MatchActor` produces which recipes matched which geometries; the planners build plans from it. |

## Engine rules the core must keep

Each rule became code after a defect or an in-game finding (`REFERENCE.md`,
section "Engine events, hooks and the manager"). Each gets a native test where
the core owns it, and an in-game case where only the engine shows it.

| Rule | Owner | Test |
|---|---|---|
| The armor model attaches after the equip event | `Advance`: each pending model holds an installed actor's effects for 1 s from when that piece was first seen; a model that never attaches stops holding after its own limit | Native; `unload`, `soak-fight` |
| The unequip sync runs before the biped changes | Observe: a piece without the inventory worn mark is not observed | Native; `soak-churn` |
| Model and geometry identity is compared only while held | Adapter: `ModelToken` backed by an `NiPointer` | `soak-churn`, `soak-waves` |
| A live recipe change is atomic with the actors that use it | `InstanceRef` fingerprint; the recipe store swaps before actors are marked stale | Native; `load-edit` |
| A load clears every actor and cancels work | Every actor is forgotten before any other step | `load-*` cases |
| First-person pieces belong to the player, when the setting is on | Observe | `camera` |
| Distance eviction and return, with hysteresis | `Advance` through `InRange`: an actor with something installed stays to 1.0 of the distance, one with nothing installed comes in inside 0.8 of it; the facts carry the distance (none for the player) | Native |
| The studio view's recipe set and isolation filter matches | `PlanActor` input | Native; `isolation`, `soak-stack` |
| Equips that change no piece cost no install | `Reconcile` returns nothing | Native; `soak-fight` sync count |

## Review of stages 1 to 3

A review on 2026-10-03 found four deviations, fixed before stage 4. The model
wait was one timer per actor, so a model that never attaches disabled the wait
for every later equip; it is now one `PendingModel` per piece. `inRange` could
not express eviction hysteresis; the facts now carry the distance and
`Advance` applies `EvictionFor`. `PlanActor` decided lifecycle; only `Advance`
does. The facts held derivable copies (a piece's enchantment beside its keys, a
full geometry ref and identity per geometry); a geometry fact is now its name
and diffuse path, and the core derives its ref, ordinal and identity.

## Rebuild cost, measured before stage 4

Timing inside a rebuild on 2026-10-03 showed one actor's render is planned as
one unit (`BuildActorRender`), so any placement change rebuilds it.
`BuildRenderPlan` took 34 to 39 ms of the 45 ms place phase (lowering 14.5 ms,
finishing 13.8 ms, demand collection about 6.6 ms); surface installs took
7.8 ms. The render plan cache (`REFERENCE.md`, engine section) now reuses the
plan for actors in the same gear: a reused plan costs about 4 ms. The other
remaining spike is a render release of about 50 ms on the first retire of a
batch, while the next retires take 0.1 ms; its cause is not yet found. Stage 4
keeps the command set; `RebuildRender`, issued once per actor whose
placements changed, replaced the per-geometry `ChainGeometry` the same day.

## Stage 4 split (decided 2026-10-03)

4a: the core decides each actor's lifecycle and the engine still rebuilds an
actor whole when the core issues any command. 4b: perform the commands one by
one, keeping unchanged pieces' bindings and instances; it waits until a
measurement shows rebuilds matter after 4a and the render plan cache. 4a
passed the functional cases, `studio-reload` and `soak-churn` in game on
2026-10-03 (run 20261003T194412, three launches); `EvictDistance` is 6000 in
the test INI, so `unload` exercised eviction. Of 1587 refreshes in
`soak-churn`, 1479 needed no command.

## Stages

| Stage | Work | Done when |
|---|---|---|
| 0 | Branch `actor-core`; this plan; the D2 renames | Native tests and the commit gate pass |
| 1 | `src/actors/` types: refs, facts, phases, installed state, commands; the layer in `tools/gate.py`; glossary entries | The header states the data first and compiles natively |
| 2 | `PlanActor`: keyed plan from facts through `MatchActor` and `ViewedRecipes` | Native tests, including the studio filters |
| 3 | `Reconcile` and `Advance` | Native tests for every row of the rules table the core owns, and for idempotence |
| 4 | Adapter: observe, perform, admission; `Manager` drives the core; the old refresh, sweeps and containers are deleted | Windows build; `lifecycle`, `equip-cycle`, `unload`, `soak-churn` pass in game |
| 5 | Acceptance | The full suite passes, and each soak's steady cost and arrival spike are at or below the 2026-10-03 baseline |
| 6 | Split the remaining `Manager` concerns: snapshot publisher, regression facade | Full suite passes |
