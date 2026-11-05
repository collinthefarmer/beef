Status: accepted (2026-11-05) on branch `main`. It designs stage 4 of `docs/plans/edge-fixes-2026-10-12.md`. D6, D7 and D8 are decided as recommended; D6 is route 2, a restage driven by the core's geometry change. Nothing is implemented.

# Stage 4: restage an equip from the core's geometry change

## How it works today

- An equip, an unequip or a 3D rebuild marks the actor stale. `GatherFacts` observes the actor and plans it again, with `reinstall = kIfChanged`.
- `Actors::Advance` gets its commands from `CommandsFor` (`actors/Lifecycle.cpp:53`). For `kIfChanged` it runs `Reconcile(installed, plan)`. For `kAlways`, a studio edit, it retires everything and installs everything.
- `Reconcile` (`actors/Reconcile.cpp`) compares the two plans. It emits `RetirePlacement`, `RetireGeometry`, `RetireInstance`, `InstallInstance`, `InstallGeometry` and `InstallPlacement`, and `RebuildRender` when a placement changed. A geometry is kept when both plans hold the same `PlannedGeometry`: the same `GeometryRef` and the same `ModelToken` (`GeometryKept`).
- The commands are data. No engine executor exists for any of them.
- `Change` (`orchestration/Sync.cpp`) reads the command list only to decide that something changed. It issues `Retire{actor}` and then `Install{actor, ...}`.
- The engine then retires every resource of the actor and builds a new `LiveActor`: every instance, every geometry binding, every shell clone, every light, one render plan and one first draw. Every piece of the actor blanks until the first draw ends, about 1 s.
- Stage 2 added a **restage**: the shown `LiveActor` stays published while a staged `LiveActor` is built and drawn, and the swap hands the material and shell bindings over. `CanRestage` allows it only for `kAlways` with equal geometry lists, so an equip never restages.
- The restage engine code finds the geometry correspondence itself. `StagedActorOf` copies the published pieces. `HandOverBindings` pairs geometries by position and refuses a changed count (`engine/ActorRestage.cpp`).
- The model token is the biped part's `NiAVObject*` (`ModelFor`, `engine/ActorObservation.cpp:109`). A 3D rebuild gives each part a new node, so every geometry of the piece counts as new, also when the meshes did not change.

## Findings that shape the design

| # | Finding | Where |
|---|---|---|
| F1 | One `RenderInstance` and one render plan exist per actor. A new piece needs a new plan for the whole actor. | `AttachRenderPlan`, `engine/ManagerApply.cpp` |
| F2 | `PlacementId`, `GeometryId`, `HiddenSlot`, `shellOwner` and the stacks of `RenderPlanKey` are actor-wide vector indices. Adding or removing a piece moves them, so the plan cache misses for the pieces that stayed. | `planners/ActorMatches.h`, `planners/RenderPlanKey.h` |
| F3 | A recipe instance can span pieces that share an enchantment and a recipe. Its light and signal state belong to the instance. | `planners/ActorPlanning.cpp` |
| F4 | The budget, the render phase and the concealment during planning and drawing are per actor. | `orchestration/Facts.h`, `orchestration/Lifecycle.cpp` |
| F5 | Kept instances keep their clock and signals across a reinstall (`carriedInstances_`). | `engine/ManagerApply.cpp` |
| F6 | The shown render writes slots each tick through the shown actor's records, by the indices of F2. A change to the shown actor's pieces while that render is published moves those indices under it. A change under live records crashed once already: on 2026-09-22 a recipe store changed while live instances pointed into it. | `engine/ManagerTick.cpp` (`WriteSlots`); `REFERENCE.md`, the held-value passage |
| F7 | For `kAlways` the command list retires and installs everything, so it does not say which geometries are kept. The kept geometries are a fact of the two plans, whatever the reinstall kind. | `CommandsFor`, `actors/Lifecycle.cpp:53` |
| F8 | The material binding and the shell clone belong to one `BSGeometry` node. A kept geometry on a new node cannot keep its binding; it is bound again. | `MaterialBinding::Install`, `ShellBinding::Create` |

## Route 2

The core computes the geometry change once, from the two plans. The core decides from it whether a change restages. The engine builds the staged actor and runs the swap from that record. The shown actor stays unchanged until the swap (F6).

### Data

| Record | Fields | Meaning |
|---|---|---|
| `Actors::GeometryChange` | `kept`, `added`, `removed`: lists of `GeometryRef` | Each geometry of the installed plan and the new plan, in one of three lists. `GeometryKept` decides `kept`. `added` is in the new plan only, `removed` in the installed plan only. |
| `Orchestration::Restage` | `actor`, `facts`, `planned`, `bounds`, `change` | The restage command, with the `GeometryChange` added. |

`GeometryChangeOf(installed, plan)` is a pure function in `actors/Reconcile`. It reuses `GeometryKept`.

### Core

- `CanRestage` reads the phase and the `GeometryChange`. It restages when the actor has a shown render (`Shown` or `Restaging`), the new plan has placements, and at least one geometry is kept. It does not read the reinstall kind.
- An actor with no kept geometry retires and installs, as today. No shown pixel would survive the swap.
- `Change` issues `Restage` with the change. The core keeps the shown render's budget charge until the swap, as stage 2 does.
- The simulator's `Restage` executor reads `change`. A fault `restageRefused` still stands for an engine refusal.

### Engine

| Step | Rule |
|---|---|
| Build the staged pieces | `LivePiecesOf` gives the pieces of the new observation. A `kept` geometry takes the published geometry's record: its `inputs`, which hold the material as read before the plugin wrote it, and its identity. An `added` geometry keeps the observed record. |
| Check the kept bindings | For each `kept` geometry, the staged surface plan's `KeptBinding` (material, shell, shell settings) equals the published one. A difference refuses the restage, and `ReinstallFor` runs, as today. |
| Bind the added geometries | `BindSurfaces` binds the `added` geometries of the staged actor. The old render does not cover them, so they show their original material until the swap. |
| Draw | The staged render draws every stack of the actor (F1), inside the `DrawUnpublished` budget. |
| Swap | `HandOverBindings` walks `kept`. It moves each kept geometry's material binding and shell clone to the staged actor when the node is the same; otherwise the staged geometry is bound (F8). It conceals the published slots and retires the published actor. The `removed` geometries' bindings retire with it. |
| Abandon and retire | `RetireActorEffects` retires a staged actor before it is dropped, because a staged actor now owns the bindings of its `added` geometries. |
| Equip event | The swap fires `FireEquip` when `TakeEquipped` holds, as `InstallLiveActor` does. |

The pointer comparison in the swap checks an engine handle of a geometry that the core already named as kept. It does not find the correspondence.

### Plan key by identity

- `RenderPlanKey` names each stack by `GeometryRef` and recipe instance (`InstanceRef`) instead of by actor-wide index (F2).
- The kept pieces' stacks then hit the plan cache after an equip. The first draw still draws them (F1).

### Content model token (D7)

- The model token becomes the part's content: the geometry names, ordinals and mesh paths.
- A 3D rebuild with the same content keeps the token. `GeometryKept` then keeps every geometry, and the rebuild restages with every geometry kept on a new node (F8): the bindings are made again, and the plan key hits.
- A rebuild with different content restages with the changed geometries in `added` and `removed`.

## What route 2 does not change

- The first draw covers the whole actor (F1). An equipped piece shows about 1 s after the equip. The other pieces keep their shown render during that time.
- One render per piece (design B) would remove that draw. B changes the core's actor record and the engine's `LiveActor` (F3, F4), about 2 weeks. D6 holds it until a measurement shows that the whole-actor first draw is the cost that matters.

## Decisions

| # | Question | Decision (2026-11-05) |
|---|---|---|
| D6 | Which design? | Route 2 now: a restage driven by `GeometryChange`, with the plan key by identity. B only after a measurement. |
| D7 | Does a 3D rebuild with unchanged meshes keep its geometries? | Yes. The model token is the part's content. |
| D8 | Does an equip restage on every actor? | Yes, every actor, as D3 decided for edits. |

## Order of work

| # | Work | Checked by |
|---|---|---|
| 1 | `GeometryChange` and `GeometryChangeOf` in `actors/Reconcile`. | `actors/reconcile_tests` |
| 2 | `Restage.change`; `CanRestage` from the phase and the change; the simulator reads `change`. | `orchestration/sync_tests`, `orchestration/scenarios_lifetime_tests` |
| 3 | The engine steps in the table above. | Windows build; in game |
| 4 | The plan key by `GeometryRef` and `InstanceRef`. | `planners` plan key tests |
| 5 | The content model token. | `actors/lifecycle_tests`; in game |

Steps 1 and 2 are engine-free. Steps 3 to 5 each end with a Windows build; the in-game check runs once after step 5.

## Tests

- `actors/reconcile_tests`: an equip of one piece gives `kept` for the other pieces' geometries and `added` for the new piece; an unequip gives `removed`; an edit with the same geometries gives every geometry `kept`.
- `orchestration/sync_tests`: an equip of a shown actor restages; an actor with no kept geometry retires and installs; an actor that is planning or drawing its first render retires and installs. `AnUnforcedReinstallStillRetires` is replaced.
- `orchestration/scenarios_lifetime_tests`: an equip restages and swaps; a refused equip restage reinstalls.
- `planners`: the plan key's stacks for the kept pieces are equal before and after a piece is added.
- `actors/lifecycle_tests`: a content-equal 3D rebuild keeps its geometries.
- In game: the full suite, with `--timeout 3600` or one launch per soak case; an equip and an unequip of one piece on the player with no blank of the other pieces; a census with no `slot_only`, `unreached` or `unplanned` target.

## Size

About 2 days. Most of it is the engine steps and the plan key.
