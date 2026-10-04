Status: open. Review of branch `actor-core` at fe88771 against `main` at
e7eeccd, and its in-game acceptance run 20261003T215655. A2 and A4 passed
the in-game run 20261004T023455 with the new `unequip-one` case. Every other
item is implemented with native tests and passed the full in-game run
20261004T033046 (every case in 12 launches; the same three plugin-cost
budgets failed as in the baseline), except three, which were not done: A6's before-and-after comparison of first-person residue was
not added (the first-person body carries the effect in both views, so the
existing check already fails when it is missing); B10's duplicate-ref
rejection was not added; C6 keeps `itemID` and adds `enchantmentID` as
runtime form IDs beside the plugin-qualified refs. B6 was measured: refreshes
run on engine job threads, one at a time, never during `OnFrame`. In run
20261004T033046 the `soak-ward` and `soak-frost` GPU means (2.2 and 0.0 µs)
came with 3096 and 3090 timed ticks and none untimed, so they measure real
work, not dropped ticks.

# Actor core review, 2026-10-03

## Scope

The branch holds 12 commits over `main` (83 files, +2843/-749 lines):

| Commit | Content |
|---|---|
| caa5fa9 | Actor core plan; `Trace::Command` renamed `Trace::Operation`; `Regression::ActorFacts` renamed `RoleFacts` |
| 3c8427c | Actor core types (`src/actors/Actor.h`) |
| 1c32b67 | The planners' `ActorPlan` renamed `ActorMatches` |
| 6201662 | `PlanActor` and `RecipeCatalog` |
| b719f39 | `Reconcile`, `AfterCommand`, `Advance` |
| 8074790 | Per-piece model wait, eviction in `Advance`, facts without copies |
| fefa560 | Render plan cache (`RenderPlanCache`, `SameStructure`, `VerifyRenderPlanCache`) |
| 772a447 | Frame-cost phases, plugin spikes and game hitches; three-table regression report |
| 051275c | `Trace::Field` owns its name |
| a580eb2 | Stage 4a: `Refresh` runs through the actor core; `ActorObservation`; recipe fingerprints; `tracked_` |
| dd516b1 | Texture lab started at the main menu; observation without readbacks |
| fe88771 | Investigation timers removed; one `RebuildRender` per actor |

Four read-only reviewers read `git diff main...actor-core`, each with one
focus: the actor core and its engine adapter; the render plan cache; the
diagnostics and regression tools; rule and convention compliance and the
documents. Their reports follow the work items. No reviewer built or ran the
game; confidence is stated per finding.

## Acceptance run 20261003T215655

All 11 functional cases, `studio-reload` and the 10 soaks other than
`soak-hour` passed, each soak in its own launch. Three windows exceeded the
4000 µs plugin-cost budget, as in the baseline
(`regression-baseline-2026-10-03.md`), each by less than before.

| Window | Plugin cost, baseline | Plugin cost, now | Share of frame | GPU per frame | Arrival worst frame, baseline to now |
|---|---|---|---|---|---|
| `soak` steady | 3352 µs | 3048 µs | 7.9 % | 4470 µs | 41 to 36 ms |
| `soak-stack` steady | 6380 µs | 5474 µs (over) | 14.2 % | 5351 µs | 89 to 95 ms |
| `soak-ward` steady | 2189 µs | 2241 µs | 5.8 % | 2 µs (suspect) | |
| `soak-frost` steady | 992 µs | 1161 µs | 3.0 % | 0 µs (suspect) | |
| `soak-large` steady | 9452 µs | 8337 µs (over) | 21.4 % | 14103 µs | 47 to 52 ms |
| `soak-vanilla` steady | 3219 µs | 2911 µs | 7.5 % | 2809 µs | 51 to 51 ms |
| `soak-idle` steady | 2874 µs | 3607 µs (+25 %, unexplained) | 9.3 % | 3072 µs | |
| `soak-churn` steady | 2939 µs | 3120 µs | 8.1 % | 5319 µs | 33 ms now |
| `soak-fight` fight | 6558 µs | 5515 µs (over) | 14.2 % | 10523 µs | 92 to 108 ms |

The game ran at the 25.9 fps cap. The slowest refresh in any steady window
was 0.67 ms. Two readings need an explanation before they are trusted: the
`soak-idle` rise, and the near-zero GPU time of `soak-ward` and `soak-frost`
(see B5 under the tools review: ticks the GPU timer drops are not counted).

## Work items

Each item names the evidence, the fix and the test that proves it. "Confirmed"
means confirmed by reading the code; "plausible" means the mechanism is read
but no failing case was run.

### A. Fix before merging

| Id | Finding | Evidence | Confidence | Fix | Proof |
|---|---|---|---|---|---|
| A1 | A reused render plan carries the storing actor's content keys, and the compositor's step-output cache, shared across actors, is keyed by them. A recipe with a node-distance source on a non-bone node lets actor B read actor A's distance bake. `SameStructure` compares only whether content keys are present, so verification cannot catch it. `REFERENCE.md` wrongly says the runtime never reads these identities. | `src/planners/RenderPlan.cpp:836-840` (content key from `producedKey`), `src/render/RenderInstance.cpp:1340-1355` (`ExecuteCached`), `src/render/CompositorDemand.cpp:51-55` (node position identity from pointers), `src/planners/RenderPlan.cpp:978-985` (`SameStructure`) | Plausible (mechanism read; no real recipe traced) | Put the node-position identity (geometry and root) into `RenderPlanKeyOf` whenever a graph reads `NodePositionInput`, or leave such steps without content keys. Make `SameStructure` compare content-key values after removing the actor term. Correct the `REFERENCE.md` paragraph. | Native test: two plans lowered with different node-position identities do not compare the same. In game: `soak-stack` with verification on reports no mismatch. |
| A2 | Observation and the legacy install decide an actor's pieces separately. `CollectPieces` has no inventory-worn filter, so in the 2 ms unequip window the rebuild re-installs a piece the core planned out, and no later observation removes it. The two also differ on the material check (`TextureSlotsValid` per material against one global `LayoutSanityCheck`), on armor with several attached addons (one model per armor against one piece per clone), and on dropping a whole piece after one failed material. An actor can then hold effects while its installed plan is empty, and `Advance` never retires it (`Retiring` from an empty installed plan issues no commands). | `src/engine/ManagerApply.cpp` `CollectPieces` (about line 1034); `src/engine/ActorObservation.cpp` `ObservePerson`, `GeometriesOf`, `WornArmorOf`; `src/actors/Lifecycle.cpp` `Retiring` | Inventory filter confirmed; the others plausible | Build the legacy install from the observed facts: the pieces, models and geometries the core planned, not a second biped walk. Until then, add the inventory-worn filter to `CollectPieces`. | In game: `soak-churn` and an unequip-one-of-two case; trace check that `applied_` and the installed plan list the same geometries after every refresh. |
| A3 | A wait swallows the equip event. An advance with no commands calls `applications_.TakeEquipped`, including one that only waits on a pending model, so `FireEquip` never fires when the model attaches and installs. On `main` the deferred path returned before `TakeEquipped`. | `src/engine/ManagerApply.cpp:852-856` | Confirmed | Consume the equip flag only when the advance has no pending model, or carry it until the next install. | In game: `equip-cycle` asserts the equip signal fires on a spawned NPC whose model attaches late. |
| A4 | `WornEnchantment` takes raw `RE::Actor *` and `RE::TESObjectARMO *` and dereferences both unchecked (rule 1). | `src/engine/ActorObservation.cpp:146`; callers there and in `ManagerApply.cpp:1052` | Confirmed | Take `RE::Actor &` and `const RE::TESObjectARMO &`. | Build. |
| A5 | Evicted actors stay evicted when `EvictDistance` becomes 0 or `PlayerOnly` changes at runtime: `QueueTrackedRefreshes` no longer queues them, though `Advance` would restore them. | `src/engine/ManagerTick.cpp:673` | Confirmed | Queue evicted actors whenever eviction is off. | Native: `Advance` restores an `Evicted` actor when the distance is 0. |
| A6 | The camera case's third-person `ExpectEffect{Player}` reads `residue`, which adds the first-person 3D, so it passes when only the first-person body carries the effect. `ExpectFirstPersonEffect` passes on any first-person residue, never compared with the count before the switch. | `src/regression/Catalog.cpp:88`, `src/engine/RegressionWorld.cpp:115-118, 145-148`, `src/regression/Steps.cpp:337, 425` | Confirmed | Add a third-person residue count to `RoleFacts` and expect it in third person; compare first-person residue after the switch with the count before it. | Native fault tests in `tests/regression/run_tests.cpp`. |
| A7 | Budget checks pass on missing data: a `_ratio_min` with a zero or missing baseline divides by 0.001; a window with no heartbeats measures 0; a window that never closes is dropped silently; a case run twice in one suite overwrites its own measurements. | `tools/regression_budgets.py:170-198` | Confirmed | Fail each of the four cases with a named message. | `tests/tools/regression_budgets_tests.py` cases for each. |

### B. Fix in the same pass

| Id | Finding | Evidence | Confidence | Fix |
|---|---|---|---|---|
| B1 | First-person pieces count as pending. If the first-person biped lists items without a first-person model (helmets, boots), the player has a permanently pending piece; every new such equip holds the player's commands for 1 s, and the player is re-observed every second for good. `main` checked only the third-person biped. | `src/actors/Lifecycle.cpp` `PendingOf`; `src/engine/ActorObservation.cpp` `ObservePerson` | Plausible | Treat a missing model as pending only on the third-person biped, or only for items that have a first-person addon. |
| B2 | While eviction is on, every tracked actor is re-observed every second in one frame, with two `GetInventory` calls per worn piece; `tracked_` keeps every loaded actor ever refreshed, including those without effects. `main` read only distances of applied actors. | `src/engine/ManagerTick.cpp:673` | Plausible (cost unmeasured) | Re-observe only for distance (a position check) on the second cadence, or spread the actors over frames; measure in `soak-large` with eviction on. |
| B3 | The hitch counter counts a load screen as one interval, because `lastFrameMicros` is not reset while loading; the once-a-second heartbeat runs inside the frame timer, so its own cost enters `frame_us`, the 8 ms spike count and the share. | `src/diagnostics/Metrics.cpp:96`, `src/engine/ManagerTick.cpp:618-634` | Confirmed | Reset the frame interval on load; emit the heartbeat outside the frame timer. |
| B4 | The plan cache stores every graph of the storing actor (`LiveGraphs`), not the graphs its plan points into, so up to 64 entries keep dead actors' variant graphs alive until a load or a rebuild-all. | `src/engine/ManagerApply.cpp:701, 708` | Confirmed | Store only the graphs named in the plan's requests. |
| B5 | GPU time counts only timed `RenderTick` spans; dropped or discarded ticks are left out, so the mean reads low (likely the near-zero `soak-ward` and `soak-frost` GPU figures); spans drained at a window-begin flush can belong to the previous window. | `tools/regression_budgets.py:75` | Medium | Report dropped ticks beside the mean, or scale by timed ticks. |
| B6 | Refreshes run from the applications queue, likely outside `OnFrame`, so the plugin share leaves refresh time out; `refresh_us_mean` still shows it. | `src/engine/ManagerApply.cpp:48` | Low to medium (thread not traced) | Trace which thread runs the queue; if outside, add refresh time to the share. |
| B7 | The stall report names the last completed step, not the step that hung, and lists windows left open by an earlier failed step. | `tools/regression_budgets.py:235-255` | Medium | Name the next step of the case; list only the window open at the stall. |
| B8 | `kPhaseCount = 8` is written by hand beside the enum and `kPhaseFields` is aligned by position with no check; `CountPhase` drops an added phase silently and `Snapshot::Of` indexes unchecked. | `src/diagnostics/Metrics.h:10-20, 54, 112`; `src/engine/ManagerTick.cpp` `kPhaseFields` | Confirmed | A `Named` table with `static_assert(Complete…)`, indexed through it. |
| B9 | The hitch test sleeps 60 ms and expects to stay under the 100 ms threshold (fails on a loaded machine), and depends on running first. | `tests/diagnostics/metrics_tests.cpp:246-263` | Medium | Inject the clock, or test the bins through a pure function. |
| B10 | Native test gaps in `tests/actors/lifecycle_tests.cpp`: an evicted actor restoring with distance 0; `Unloaded` from `Evicted` with no commands; an unequip retire held during another piece's wait; `sinceMs` near `UINT32_MAX`; duplicate refs in a plan (first-match `Find` would retire and reinstall on every refresh). | `tests/actors/` | Confirmed | Add the tests; reject duplicate refs in `PlanActor`. |

### C. Naming and documentation

| Id | Finding | Fix |
|---|---|---|
| C1 | `REFERENCE.md` (around line 752) describes the `observe_slow` trace that fe88771 removed. | Delete the sentence. |
| C2 | No `docs/components/actors.md` for `src/actors/`, and no `actors/` heading in `REFERENCE.md` for the facts behind `kModelWaitMs`. | Write the component sheet from the template; add the heading. |
| C3 | The glossary's **phase** means actor lifecycle only, but `Metrics::Phase`, `GesturePhase`, `ApplicationPhase` and the regression run phases use it. | Rename `Metrics::Phase` (for example `FrameSection`) or widen the entry. |
| C4 | The plan drops **refresh**, but new code still uses it: `QueueTrackedRefreshes`, `RefreshMeter`, the `refresh_begin` trace, `REFERENCE.md:711`. | Rename to stale or observe words, or move the drop to stage 4b in the plan. |
| C5 | `RenderPlanCache` and `RenderPlanKeyOf` add a third meaning of **key**; the glossary's "never key" line in **ref** contradicts the content-key and cache-key rows. | Add a render-plan-key row; narrow the **ref** row. |
| C6 | The glossary's **facts** row lists only regression types; `ActorFacts`, `PieceFacts`, `GeometryFacts` are missing. `PieceFacts::itemID` duplicates `ref.item`. | List the actor types; derive `itemID` where `Studio::PieceRef` is built. |
| C7 | The plan's vocabulary lists the type `InstalledActor`; the code uses `Present::installed` of type `ActorPlan`. Stage 1 lists "keys" among the types. | Correct both cells. |
| C8 | `docs/components/planners.md:54-57` heads a section "The actor plan" and says "`MatchActor` builds the plan", against the glossary (matches are not a plan). | Say "matches". |
| C9 | The command-to-operation rename is unfinished: `docs/conventions.md:88` ("tags the events of one command with a command id") and `docs/components/diagnostics.md:99` ("Command tagging"). | Say "operation". |
| C10 | `docs/components/engine.md:42` says every 100 ms queues "each actor whose armor model is still pending"; the code queues only actors inside the wait window and checks other pending actors once a second. | Correct the sentence. |
| C11 | `PhaseName` and `Reconcile` use `std::visit(Overloaded…)`, against the conventions' "Dispatch with `Match`"; `Lifecycle.cpp` uses `get_if`/`holds_alternative` instead of `Get`/`Is`; `PhaseName` has external linkage, no declaration and no word table. | Use `Match`; add a `Phase` word table asserted against `variant_size_v`. |
| C12 | `QueueTrackedRefreshes` repeats the lifecycle rule (`kModelWaitMs + kArmorModelCheckMS`) in the adapter and takes two booleans. | Have the core report when to observe the actor next, or use an enum for the two cadences. |
| C13 | `RenderPlanKeyOf` falls back to a geometry pointer that nothing holds while the entry lives. With A1 fixed the structure is the same, but the plan's rule "identity is compared only while held" still applies. | Hold the geometry in the entry, or skip caching on the fallback. |
| C14 | `MaterialIdentityOf` embeds texture names; a name containing `|` or `;` could make two keys equal (also needs pointer and generation to match). | Length-prefix the names. |
| C15 | `tools/trace-report.py` no longer recognises `command` events in traces written before the rename (shows operation 0). | Accept both names. |

## Verification after the fix pass

1. Native tests for every A and B item marked with a test, and `ctest --preset
   native`.
2. In game: the functional cases, `soak-churn`, `soak-fight`, `soak-idle` and
   `soak-stack` with `--verify-render-plans`.
3. Explain the `soak-idle` rise and the near-zero GPU figures before the
   numbers are recorded as the new baseline.

## Reviewer reports

The four reports as delivered, lightly reformatted.

### Actor core and engine adapter

Scope: actor core, ActorObservation, and the Refresh, AdvanceActor,
SettlePhase, InstalledPlanOf, RetireEffects, Retire and QueueTrackedRefreshes
paths, plus tests/actors. Read only.

1. Bug: a wait swallows the equip event. `src/engine/ManagerApply.cpp:852-856`.
   When an advance has no commands, the code calls
   `applications_.TakeEquipped(actorID)`, including advances that only wait on
   a pending model. On main, the deferred path returned before `TakeEquipped`.
   An NPC equips its first enchanted piece with `partClone` still null; the
   finalize refresh at +100 ms consumes the flag; when the model attaches and
   installs, `FireEquip` never fires. The same happens when an installed actor
   waits on a new piece. Confirmed by reading.
2. Bug: the legacy install does not apply the inventory-worn filter.
   `CollectPieces` (around line 1034) has no `InventoryWorn` check;
   observation has one (`ActorObservation.cpp:339`). The actor unequips helmet
   A while wearing B; the core plans without A and issues commands;
   `LiveActorFor` still finds A in the biped and re-installs it; the core
   records "installed without A"; when the biped drops A, observation is
   unchanged, so there is no rebuild, and A's instance keeps ticking. On main,
   the `WornEntry` comparison caught the biped change. Confirmed by reading.
3. Risk: observation and the legacy install pick different geometries.
   `GeometriesOf` requires `TextureSlotsValid` on every material and drops the
   whole piece on one failure; the legacy path checks once globally. `ModelFor`
   caches the empty result for the model's lifetime. A piece the core never
   plans gets effects anyway when another piece triggers a rebuild. Plausible.
4. Risk: an armor with several attached addons. `WornArmorOf` keeps one model
   per armor; `CollectPieces` one piece per distinct clone. The core does not
   know the second addon's geometries; a reattach of it is never detected.
   Plausible.
5. Risk: first-person pieces can block the player's updates. `PendingOf`
   treats first-person pieces as pending; main checked only
   `GetBiped(false)`. If the first-person biped lists items without a
   first-person model, the player has a permanently pending piece. Plausible;
   depends on how the engine fills the first-person biped.
6. Risk: an actor with empty installed state is never retired. `Advance` to
   `Retiring(Unloaded/Evicted, installed)` issues no commands when installed is
   empty, so `Refresh` never calls `RetireEffects`. After 2 to 4, an actor can
   have `applied_` content while installed is empty; eviction or turning
   effects off then leaves those effects. Confirmed mechanism; needs one of the
   divergences.
7. Risk: frame spike every second. `QueueTrackedRefreshes` queues every
   tracked actor every second while eviction is on; each observation calls
   `GetInventory` twice per worn piece; `tracked_` holds every loaded actor
   ever refreshed. Plausible; cost not measured.
8. Minor bug: evicted actors can stay evicted when `EvictDistance` is set to 0
   or `playerOnly` changes at runtime. Confirmed by reading.
9. Test gaps: evicted actor restoring with distance 0; `Unloaded` from
   `Evicted`; an unequip retire held during another piece's wait; `sinceMs`
   near `UINT32_MAX`; duplicate refs in a plan.
10. Nit: REFERENCE.md describes `observe_slow`, which no code emits.

Clean: the `ModelFor` reference; `TrackedActor&` in `AdvanceActor`;
`NiPointer` handling in `ModelTable`; time arithmetic; fingerprint alignment;
`ChangeAndRebuildActors`; `Reconcile` and `AfterCommands`; eviction
hysteresis; the rules table, except the unequip rule, which only observation
keeps.

### Render plan cache

One real cross-actor bug: a reused plan carries the storing actor's content
keys, and the runtime reads them. Every other input is captured by the key or
makes the key unique to one actor. Read only.

1. Bug: a distance bake can be shared across actors. `contentKey` is built
   from `producedKey.identity.canonical` (`src/planners/RenderPlan.cpp:836-840`);
   `ExecuteCached` keys the compositor's shared step-output cache by it
   (`src/render/RenderInstance.cpp:1340-1355`). `NodePositionInput`'s identity
   is the geometry pointer, root pointer and node name
   (`src/render/CompositorDemand.cpp:51-55`); `RenderPlanKeyOf` captures
   neither pointer. A node position does not count as changing
   (`RecipeGraphLowering.cpp:663`), so the `BakeMeshStep` from
   `LowerDistanceOperation` gets a content key, and with bake buffers as its
   only input `CacheKeyFor` appends no operands. If the node is not a skin
   bone, `NodeBindPosition` falls back to `RootNodePosition`, which depends on
   the actor's skeleton and pose; B reuses A's plan and gets A's distance
   texture. Harmless when the node is a skin bone. Plausible; the bake's
   canonical identity was not traced through a real recipe
   (`ValueIdentity.cpp:188-191` does pull in external identities).
2. Risk: verification cannot catch 1. `SameStructure`
   (`src/planners/RenderPlan.cpp:978-985`) checks only that `producedKey` and
   `contentKey` are present. Confirmed. Every other field is compared;
   `displayName` is skipped and appears only in `step_stale` traces.
3. Nit: `Store` keeps every graph of the storing actor, not only those its plan
   points into (`ManagerApply.cpp:701,708`); variant graphs then stay alive
   until a load or a rebuild-all. Confirmed.
4. Nit: `MaterialIdentityOf` embeds texture names; `|` or `;` in a name could
   make keys equal, also needing pointer and generation to match. Negligible.

Safe: size settings (`texture.size` is in the key); view and `OutputShown`
(placements and requests are in the key); variant graphs (pointer in the key,
graph held by the entry); raw graph pointers in the plan (a subset of the
key's); eviction and replacement (copies returned); mesh pointer fallback
(unique key); recipe edits (`ChangeAndRebuildActors`, graph reset); material
identity (pointer, generation, name); `producedKey` (read nowhere else);
thread safety (main-thread tasks, atomic run flag).

### Diagnostics and regression tools

Python tool tests pass (19 of 19); C++ not built. `main` is already at
e7eeccd, so the camera, crowd, arena, fighter and idle-camera code was
reviewed as it stands on the branch.

1. Bug: the camera case's third-person `ExpectEffect{Player}` reads `residue`,
   which adds first- and third-person residue; the first-person 3D stays loaded
   in third person, so it passes when only the first-person body has the effect.
2. Test gap: `ExpectFirstPersonEffect` passes on any first-person residue,
   never compared with the count before the switch.
3. Risk: `OnFrame` returns while loading and nothing resets `lastFrameMicros`,
   so the first frame after a load records the load screen as one interval.
4. Risk: the once-a-second heartbeat runs inside `frameTimer`; its JSON events
   enter the next heartbeat's `frame_us`, 8 ms spike count and share.
5. Risk: budget checks pass on missing data: `_ratio_min` with a missing or
   zero baseline; a window with no heartbeats; an unclosed window dropped; a
   case appearing twice overwriting itself.
6. Risk: refreshes timed by `RefreshMeter` may run outside `OnFrame`, leaving
   them out of the share. Thread not traced.
7. Risk: the stall report names the last completed step, not the hung one, and
   lists windows left open by failed steps.
8. Risk: `gpu_us_mean` leaves out dropped or discarded ticks and can take spans
   from the previous window at a window-begin flush.
9. Nit: each phase's `*_max_us` is the longest single call, not the longest
   frame.
10. Nit: `kPhaseCount = 8` written by hand with no `static_assert`.
11. Test gap: the hitch test sleeps 60 ms against a 100 ms threshold and
    depends on running first.
12. Nit: `tools/trace-report.py` does not recognise `command` events from
    traces written before the rename.

Clean: heartbeat field count (47 of 64; `step_kinds` at most 33); owned field
names; cleanup (`DespawnCrowd` returns the player from the arena;
`ReleaseWorld` restores `fAutoVanityModeDelay`; `EndRunEarly` quits, and the
setting lives in memory only; faction and aggression changes apply only to
deleted spawned references); atomics (as on main); units; window-edge timing.

### Rules, conventions and documents

Greps and reading only. No comments in code, no `auto` in a signature, no
declaration without a definition; the deleted symbols have no references
left.

1. `WornEnchantment` takes raw pointers and dereferences both unchecked
   (rule 1). `src/engine/ActorObservation.cpp:146`.
2. `RenderPlanKeyOf` and `MeshIdentityOf`: the pointer fallback is not held by
   the cache (rule 1, and the plan's rule "identity is compared only while
   held").
3. Two observation paths (inferred): inventory-worn filter, deduplication by
   armor against by clone, `TextureSlotsValid` against `LayoutSanityCheck`.
4. `QueueTrackedRefreshes` repeats the lifecycle rule and takes two booleans.
5. `PhaseName` and `Reconcile` use `std::visit(Overloaded…)` against "Dispatch
   with `Match`"; `get_if`/`holds_alternative` instead of `Get`/`Is`;
   `PhaseName` has external linkage and no word table.
6. `kPhaseCount` maintained by hand; `kPhaseFields` aligned by position;
   `Snapshot::Of` unchecked.
7. Glossary, phase: used for metrics, gestures, applications and run phases.
8. Glossary, refresh: still used in new code.
9. Glossary, key against ref: a third meaning of key.
10. Glossary, facts: actor types missing; `itemID` duplicates `ref.item`.
11. No `docs/components/actors.md`.
12. `REFERENCE.md:759-761` describes the removed `observe_slow`; no `actors/`
    heading.
13. The plan's `InstalledActor` and "keys" cells.
14. `docs/components/planners.md:54-57` "The actor plan".
15. The command-to-operation rename unfinished in `conventions.md:88` and
    `diagnostics.md:99`; `engine.md:42` misstates the 100 ms cadence.

The README reading list matches the branch.
