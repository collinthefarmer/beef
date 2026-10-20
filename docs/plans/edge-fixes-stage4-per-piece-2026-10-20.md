Status: proposed (2026-10-20) on branch `pure-tick`. It designs stage 4 of `docs/plans/edge-fixes-2026-10-12.md`. Nothing is implemented. D6, D7 and D8 wait for the user.

# Stage 4: install only what an equip changes

## How it works today

- An equip, an unequip or a 3D rebuild marks the actor stale. `GatherFacts` observes the actor and plans it again, with `reinstall = kIfChanged`.
- `Actors::Advance` runs `Reconcile(installed, plan)`. `Reconcile` computes a per-placement diff: `RetirePlacement`, `RetireGeometry`, `RetireInstance`, `InstallInstance`, `InstallGeometry`, `InstallPlacement`, and `RebuildRender` when a placement changed.
- `Change` (`orchestration/Sync.cpp:537`) uses that diff only to decide that something changed. It drops the command list and issues `Retire{actor}` and then `Install{actor, ...}` with the whole actor.
- The engine retires every resource of the actor and builds a new `LiveActor`: every instance, every geometry binding, every shell clone, every light, one new render plan and one first draw.
- Stage 2 added a restage: the old render stays shown while a new `LiveActor` is built, and the geometry bindings are handed over. It runs only for a forced reinstall (`kAlways`, a studio edit) with the same geometries (`CanRestage`, `Sync.cpp:577`), so an equip never restages.
- A 3D rebuild gives each biped part a new `NiAVObject*`. The model token is that pointer (`ModelFor`, `engine/ActorObservation.cpp:109`), so every geometry of the piece counts as new, also when the meshes did not change.

## Findings that shape the design

| # | Finding | Where |
|---|---|---|
| F1 | One `RenderInstance` and one render plan exist per actor. A new piece needs a new plan for the actor, or a second render. | `AttachRenderPlan`, `engine/ManagerApply.cpp:853` |
| F2 | `PlacementId`, `GeometryId`, `HiddenSlot`, `RenderPlanKey` and `shellOwner` are actor-wide vector indices. Adding or removing a piece moves them, so the plan cache misses for the pieces that stayed. | `planners/ActorMatches.h:48`, `planners/RenderPlanKey.h` |
| F3 | A recipe instance can span pieces that share an enchantment and a recipe. Its light and signal state belong to the instance, not to a piece. | `planners/ActorPlanning.cpp:80` |
| F4 | The budget (level, `chargedBytes`, `levelCosts`), the render phase and the concealment during planning and drawing are per actor. | `orchestration/Facts.h:85`, `orchestration/Lifecycle.cpp:611` |
| F5 | A per-geometry retire exists (`RetireGeometry`, `engine/LiveActor.cpp:51`). The restage hand-over matches bindings by position and needs equal geometry counts. | `engine/ActorRestage.cpp:78` |
| F6 | Kept instances already keep their clock and signals across a reinstall (`carriedInstances_`). | `engine/ManagerApply.cpp:1248` |

The cost of a whole-actor reinstall is the plan, the first draw (about 1 s) and the shell clones. Since stage 2 the user sees no blank on an edit, but an equip still blanks every piece of the actor until the new first draw ends.

## Two designs

### A. Restage on an equip, with bindings matched by geometry

- `CanRestage` accepts a `kIfChanged` change. The engine builds the new `LiveActor` beside the old one, as stage 2 does.
- The hand-over matches bindings by `GeometryRef` instead of by position. A kept geometry passes its material binding and its shell clone to the new `LiveActor`; a removed geometry is retired with `RetireGeometry`; a new geometry is bound.
- The plan and the first draw still cover the whole actor, because of F1.
- The plan key names stacks by `GeometryRef` and recipe instance instead of by index, so the kept pieces' stacks hit the plan cache (F2).

Result: no blank on an equip, no new shell clones for kept pieces, and a cheaper plan. The first draw stays whole-actor. The core changes are small: `CanRestage` and the hand-over rule. The engine changes are the matched hand-over and the plan key.

### B. One render per piece

- Each piece gets its own `RenderInstance`, plan, phase, budget charge and concealment scope. An equip installs one piece and retires one piece; the other pieces are untouched.
- An instance that spans pieces (F3) becomes one instance per piece, or stays shared with its light placed again on a change.
- The core's per-actor budget, phase and draw spreading become per piece (F4). The simulator, the contract tests and about 15 suites change.

Result: the cheapest equip, at the cost of a rewrite of the core's actor record and the engine's `LiveActor`.

## Rebinding after a 3D rebuild

The model token can be the model's content instead of its pointer: the geometry names, ordinals and mesh paths of the part. A rebuild with the same content then keeps the token, `Reconcile` keeps every geometry, and the engine rebinds the kept bindings to the new nodes. The rebind reuses the hand-over from design A. A rebuild with different content still reinstalls the piece.

## Decisions for the user

| # | Question | Options | Recommendation |
|---|---|---|---|
| D6 | Which design? | (a) A, restage with matched bindings. (b) B, one render per piece. (c) A now; B only if a measurement shows the whole-actor first draw is still the cost that matters. | (c). A removes the blank and most of the cost with a small core change. B is a rewrite whose value is unmeasured. |
| D7 | Does a 3D rebuild with unchanged meshes keep its geometries? | (a) Yes, the model token is the part's content. (b) No, keep the pointer identity. | (a). The rebind reuses A's hand-over. |
| D8 | Does an equip restage on every actor? | (a) Every actor, as D3 decided for edits. (b) The player and the inspected actor only. | (a). It matches D3, and the budget already allows two renders per actor while staging. |

## Tests

- `actors/reconcile_tests`, `actors/lifecycle_tests`: an equip of one piece keeps the other pieces' geometries; a content-equal rebuild keeps its geometries.
- `orchestration/sync_tests`: `AnUnforcedReinstallStillRetires` becomes "an equip restages"; a restage that cannot match a binding falls back to `ReinstallFor`.
- `planners`: the plan key of an actor is equal before and after a piece is added, for the stacks of the pieces that stayed.
- In game: the full suite against the 2026-10-10 baseline, and an equip of one piece on Belrand with no blank of the other pieces.

## Size

Design A with the rebind: about 2 days, most of it in the engine hand-over and the plan key. Design B: about 2 weeks, with the simulator and contract changes.
