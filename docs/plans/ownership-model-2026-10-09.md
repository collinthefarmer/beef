Status: active (2026-10-09); decisions D1 to D8 made. Stage 0 is done: `engine/SlotWrites.*` is committed after `camera cull isolation`, `soak-fight`, `soak-fight-orbit`, `soak-orbit` and `soak-distance` passed 5 of 5 with the census clean. Stage 1 is next. Raised by the leak fixes of 2026-10-08 and 2026-10-09 and by three read-only audits on 2026-10-09.

# Ownership model

## Goal

Every render target and every reference into the game's objects has one
stated **owner**. The census checks that statement on every heartbeat. Each
release follows from an owner's transition. No release depends on a later
event that may never come.

## How the area works today

### Reference counting with local release rules

The plugin keeps resources alive through `std::shared_ptr`, `RE::NiPointer`
and `TextureRef`. Each holder releases on its own local event. No type states
which holder is responsible for a resource.

| Resource | Holders today | Release event |
|---|---|---|
| Computed target of a step | `RenderInstance` (`RenderExecution` step values), `RenderOutput`, `StepOutputCache`, slot `written`, `MaterialInputs` | Last `shared_ptr` drops; the deleter returns it to the pool (`RenderTargetPool::Recycle`) |
| Idle target | `RenderTargetPool` (`ResourcePool<RenderTarget>`, 16 targets, 64 MiB, 5 s) | Oldest-out or idle expiry (1391bd1) |
| Target shown in a material or shell slot | `SlotWriter` group `written` | **The next slot write**, which never comes for an actor out of view (46b3969) |
| Target the game may still render after a restore | `SlotWriter::RetiredTextures()` | `SweepRetiredMaterialTextures`, every frame |
| Game texture | `TextureRef(RE::NiSourceTexture *)` makes an `NiPointer`; held by `MaterialInputs`, `Compositor::images_`, previews | Last holder drops |
| Presenter texture read from a material | `TextureRef(RE::NiSourceTexture *)` looks it up in `TextureLeases` and takes a strong reference to the target | Last holder drops |
| Actor 3D | `LiveGeometry` (geometry, property), `GeometryInputs` (geometry, root), `MaterialBinding`, `ShellBinding`, `SkinPaletteLease`, `LightBinding`, `TrackedActor::models`, `MeshEntry` | Retire, next sync, or mesh sweep after 30 s |
| Preview draw | `ConsumptionLeases` ticket (limit 2048) | The ImGui callback calls `Consumed()` |

### The leaks of 2026-10-08 and 2026-10-09

Each leak got its own local rule. All are on `main`.

| Commit | Leak | Local rule |
|---|---|---|
| 934c2e4 | A resize's hidden draw executed merge-covered chain links; their stack outputs were never released (25 to 40 extra 1024² targets per resize; the 1024 presenter slots ran out) | Draw only rendered stacks in a resize |
| 46b3969 | An out-of-view resize swap left the old stack targets in material and shell slots (356 MiB in `soak-fight`) | `ConcealStackSlots` on an out-of-view swap |
| 1391bd1 | Scratch targets stayed until load; the idle pool never aged | Draw-scoped leases; oldest-out and 5 s expiry |
| 90f8462 | Material analysis records held game textures to protect a raw-pointer key | Key by `TextureIdentity` |
| 63dfa9c | An actor installed out of view never reported out-of-view during a pending first draw | A check on the flag combination |

### The census

`target_holders` (`engine/TargetCensus.cpp`, `planners/TargetHolders.h`)
classifies every live target by its first holder: `instance`, `output`,
`slot`, `material_input`, `retired`, `cache`, `preview`, `pool`, `resident`,
`unreached`. It also writes `unplanned` (step outputs the estimate leaves
out) and `slot_only` (targets that only a slot holds, per actor). On
2026-10-09 the full in-game suite passed 16 of 16 and the census was clean:
`unreached` 0, `unplanned` empty, `slot_only` empty. That result is the bar
for every stage.

### The starting point

The working tree holds `engine/SlotWrites.h` and `engine/SlotWrites.cpp`
(`SlotWrite`, `EmptyWrite`, `WriteSlot`, `ConcealStackSlots`). The tick's
render (`WriteSlots`) and the out-of-view swap (`FinishResize`) now write a
hidden slot through the same `EmptyWrite`. `REFERENCE.md` and
`docs/components/engine.md` already describe it.

## Verification of the audit findings

The main session confirmed items 1, 2, 3 and 9 before this plan. This plan
read the code for the others on 2026-10-09.

| Item | Result | Evidence |
|---|---|---|
| 1. TextureLab means keyed by raw pointer | Confirmed | `luminance_` and `channelMeans_` (`TextureLab.h:491-493`) key by `RE::NiSourceTexture *` with no pin and no generation. A failed render stores 0.5 forever (`TextureLabReadback.cpp:438-475`). The only caller is `MeasureFlatDisplacement` (`CompositorSource.cpp:8`). `MeanLuminance` has no caller. |
| 2. Failed `StartResize` keeps the target level | Confirmed | Both failure paths set `ResizeIdle` (`ActorResize.cpp:166`, `:176`). `AdvanceResizes` only logs (`ManagerTick.cpp:1160-1166`). `RequestLevelStep` steps from `TrackedActor::targetResolution`, which still names the failed level. |
| 3. `Compositor::images_` never cleared | Confirmed | No clear call exists. `Manager::Clear` clears step outputs, meshes and materials only. A failed load stores an empty `TextureRef` for the session. |
| 4a. Release paths disagree on first draws | Confirmed | `ReleaseWhileHidden` excludes `firstDraw` and a non-idle resize. `BudgetActorOf` sets only `resizing`, so `RespondToPressure` can release intermediates during a first draw. |
| 4b. Intermediates survive a swap | Confirmed by reading | `FinishResize` does not reset `LiveActor::intermediates`. A swap while `kReleased` installs a new instance whose hidden draw executed every step. `ReleaseWhileHidden` then skips the actor, because `intermediates` is not `kHeld`, and `BeginFrame` does not run out of view. The budget counts the actor as released. The same holds for a reinstall out of view. Not yet observed in a trace. |
| 4c. Call sites | 75 phase-field references on 72 lines; 91 references with the level-step fields | `awaitedPlan` 9, `firstDraw` 17, `resize` 18, `intermediates` 8, `targetResolution` 9, `appliedResolution` 11, `hiddenSinceMS` 3; plus `smallSinceMS` 5, `stepUpBlockedFrom` 5, `renderedFrame` 6. Files: `ManagerTick.cpp`, `ActorResize.cpp`, `ManagerApply.cpp`, `ActorBudget.cpp`, `ManagerApplication.cpp`, `TargetCensus.cpp`, `LiveActor.cpp`. The earlier estimate of 55 to 60 was low. |
| 5. Shell ownership check after a 3D replacement | Confirmed by reading | `SkinPaletteLease::StillOwned` compares the source skin, root parent and transform arrays with objects that the lease itself pins (`SkinPalette.cpp:197-232`). `ShellBinding::StillOwned` compares `clone_->parent` with the pinned `parent_`. No check reaches the actor's current 3D. Release then depends on the next sync retiring the geometry. |
| 6. Observed model table pins armor 3D | Confirmed by reading | `ObservedModel` and `ObservedGeometry` hold `NiPointer`s (`ActorObservation.h:17-28`). `KeepModels` erases them only at the actor's next sync. `Tick` checks `Loaded3D` only for actors in `live_`, so a tracked actor without a live entry keeps its models until `QueueForget` or the next load. |
| 7. Content keys embed addresses | Confirmed by reading | `IdentityFactsOf` puts `&graph`, `inputs.geometry` and `inputs.root` addresses into the facts (`CompositorDemand.cpp:16-21`). `NodePositionInput` keys on geometry and root (`IdentityFacts.cpp:44-46`). `MeshIdentityOf` falls back to the geometry address. The step output cache holds none of these objects. A collision needs a reused address inside the retention window; none is observed. |
| 8. Preview draw tickets survive `ClearPreviews` | Confirmed | `ClearPreviews` clears `previews_` only (`TexturePreviews.cpp:176`). A ticket ends only when its ImGui callback runs (`MenuWidgets.cpp:100-104`). The census does not collect tickets, so a stranded ticket counts as `unreached`. |
| 9. Hygiene | Confirmed | `FailGeometryOutputs` sets `renderFailed` and leaves `rendered` (`ManagerTick.cpp:435`). `MaterialBinding::Install` keeps the original material only in a local (`Binding.cpp:476`). `InstanceGraph` compiles a fresh graph per variant install (`ManagerApply.cpp:94-99`), and the plan key compares graph addresses. `planners/RecipeTextureCache.h` and `planners/ResourceCache.h` have no user outside their tests. |
| 10a. Lights pin bones | Confirmed, inside the model | `LightBinding` holds bone and scene `NiPointer`s. `LiveInstance` owns it, so `LiveActor` owns it. Only the 3D replacement check of item 5 is missing. |
| 10b. Shell emissive `NiColor` | Unresolved | `ConfigureProperty` allocates it with `RE::malloc` (`Shell.cpp:291`). Its release depends on the `BSLightingShaderProperty` destructor. `REFERENCE.md` has no line for it. |
| 10c. Copied `boneVertData` | Unresolved | `CopySkinData` allocates each bone's weights with `RE::malloc`. Their release depends on the `NiSkinData` destructor. `REFERENCE.md` states the layout only. |
| 10d. Alpha property without a constructor | Refuted | `REFERENCE.md` (Bindings) records that the engine exposes no `NiAlphaProperty` constructor. The vtable and zero refcount are written by hand on purpose, and the `NiPointer` releases it through the engine's destructor. |

The audit of 2026-10-09 found five more facts.

| Fact | Where |
|---|---|
| `MeshCache` keys by `RE::BSGeometry *` and keeps an `NiPointer` to the geometry in each entry, so a cache owns actor 3D for 30 s after the last bind. `MeshEntry` also travels into `RenderInstance` as a `RenderValue`. | `MeshCache.h`, `MeshCache.cpp:79` |
| `ConsumptionLeases` hands the ImGui callback a raw `Ticket *`. Freeing a ticket before its callback runs is a use after free. | `ConsumptionLeases.h:27-37` |
| `TextureLab::sampleWarned_` is an unbounded set of raw texture pointers, used only to log once. | `TextureLab.h:494` |
| `RenderPlanKey` hashes the `RecipeGraph` address. The plan cache stores the graphs beside the plan, so the address stays valid. | `RenderPlanKey.cpp:57`, `RenderPlanCache` |
| `REFERENCE.md` (Generated texture references) still says material-analysis records retain `TextureRef`; 90f8462 changed that. | `REFERENCE.md:3335` |

## The target model

### Reference kinds

Every reference to a target or a game object has one of three kinds.

| Kind | Meaning | Rules |
|---|---|---|
| **Owner** | Keeps the object alive for one purpose. | Exactly one owner per purpose. The owner's transitions end the object. |
| **Lease** | A time-bound strong reference that an owner grants and ends. | The holder cannot extend or copy it. The owner records every lease it grants. |
| **Identity** | A comparable, non-owning name of an object. | Stable against address reuse. Every cache key is an identity. Reading a raw game pointer makes an identity, never an owner. |

| Owner | Owns |
|---|---|
| `RenderInstance` | Its computed targets |
| `LiveActor` | Every reference into the actor's 3D: bindings, shells, palette leases, lights, observed models of installed pieces, and the leases that `RenderInstance` and `MaterialInputs` use |
| `RenderTargetPool` | Idle targets |
| `StepOutputCache` | Shared step outputs |
| Retired list | Targets that a game material may still render |
| `Compositor` image store | Images the plugin loaded by path |

| Lease | Granted by | Ended by |
|---|---|---|
| Presenter slot | `RenderTargetPool` | Target teardown |
| Draw-scoped target | `RenderInstance` | End of the draw (1391bd1) |
| Preview ticket | `TexturePreviews` | Callback, or `ClearPreviews` through an id lookup |
| **Display lease** (`Lease<RenderTarget>`): one per material or shell slot | `LiveActor` when it attaches, swaps or retires an instance, in view or not | The same `LiveActor` transitions |
| Game texture lease for `MaterialInputs` | `LiveActor`, from the bound material | Retire of the geometry |

| Identity | Shape |
|---|---|
| `Identity<RenderTarget>` | Target address and acquisition generation (exists as `TargetId` plus `Generation`) |
| `Identity<NiSourceTexture>` | Lowercased texture name and extent (D5); a texture without a name has no identity and is not cached |
| `Identity<BSGeometry>` | Renderer buffer identity (`MeshIdentity`), plus a per-`LiveActor` install token |
| `Identity<RecipeGraph>` | Recipe id, recipe revision and variant id |
| Content key | A string built only from the identities above and values |

The types make a wrong kind fail to compile. A store whose value type is an
owner cannot take a lease, and a store key cannot be an owner. The names are
an open decision (D1).

### Bounded retention

One **retained store** replaces `ResourcePool` and `RetainedCache`. A
**retention policy** states every bound: maximum count, maximum bytes,
maximum idle age, eviction order (oldest-out), and a keep predicate. The
store returns evicted values to the caller, so the caller destroys them
outside any lock. No store has an unstated "never".

| Store | Policy today | Policy in the model |
|---|---|---|
| Target pool | 16, 64 MiB, 5 s, oldest-out | Same, as a stated policy |
| Step output cache | 4096; evict after 500 ms unless another holder exists | Same; "held elsewhere" becomes the keep predicate |
| Material records | 64 unused, 30 s | Same |
| Mesh cache | 30 s, no count | 30 s and a count bound (D4) |
| Image cache | Never | Count, idle age, failures kept 10 s (D4) |
| TextureLab means | Never, raw pointers | Count, identity key, failures kept 10 s |
| Retired list | Swept every frame, no count | Count bound with a warning line |
| Warning history | 1024 keys, 128 KiB | Same, as a stated policy |

### Lifecycle as one phase variant

An actor's render state is one **phase**. Pure planner functions take the
phase and an event and return the next phase and a list of **effects**. The
engine applies the effects. Values that follow from the phase are computed,
not stored.

```
AwaitingPlan { job }
FirstDrawing { draw, level }
Live         { level, Held | Released{since} }
Resizing     { from, to, Pending | InFlight{progress} }
```

The target level is `Resizing::to` or the level of the other phases. The
applied level is `Resizing::from` or the level of the other phases. A failed
resize returns to `Live{from}`, so the target level cannot stay at the failed
level. A swap enters `Live{to, Held}`, because the new instance holds its
intermediates. Only `Live` may release intermediates, so both release paths
ask one question. `PlacedOutput` follows the same pattern: one output state
replaces `active`, `rendered`, `renderFailed` and `problem`.

### Condition hold

One **hold** value records since when a condition is true.
`Update(condition, now)` returns the new hold. `Due(now, delay)` answers
whether the condition held for the delay. It replaces `SmallSince`,
`HiddenSince`, `HiddenReleaseDue`, the pool's `idleSince` and the 500 ms
step idle rule.

### The census invariant

Every live target is owned by an instance, the pool, the step output cache,
the retired list, the image store or the resident set. A slot holds at most
one display lease. `material_input`, `slot_only` and `unreached` are empty. The census
writes the violation count on every heartbeat.

## Stages

Every stage builds for Windows, keeps `ctest --preset native` green, and
stops at an in-game checkpoint for the user. The commands are those in
`CLAUDE.md`. "Suite" means the full in-game suite through
`tools/regression-run.py`. The bar is 16 of 16 with the census clean.

| Stage | Subject | Sessions | Risk |
|---|---|---|---|
| 0 | Land `SlotWrites` | 0.5 | Low |
| 1 | Utilities: hold, retained store, identity types | 1.5 | Low |
| 2 | Confirmed quick fixes: means, failed resize, image cache | 1 | Low |
| 3 | Actor render phase and output state | 3 | High |
| 4 | Display leases for slots | 2 | High |
| 5 | Game-object ownership under `LiveActor` | 2 | Medium to high |
| 6 | Cache-key identities and hygiene | 2 | Medium |
| 7 | The census invariant as a gate | 0.5 | Low |

### Stage 0: land `SlotWrites`

| Field | Content |
|---|---|
| Work | Commit the working-tree change as it stands. |
| Files | `engine/SlotWrites.h`/`.cpp` (new, 98 lines), `ManagerTick.cpp` (-63 lines), `ActorResize.cpp` (-16 lines), `REFERENCE.md`, `docs/components/engine.md` |
| Deletes | The two copies of the hidden write: `WriteSlots`' inline empty write and `ActorResize.cpp`'s `ConcealStackSlots`, which wrote only the texture. |
| Native tests | None; the code is engine-facing. |
| Risks | A concealed slot now also gets fallback scalars. A shell slot written out of view must not show a black emissive. |
| Checkpoint | `soak-fight` and `soak-fight-orbit`: `slot_only` empty; no `resolution_swap` leaves an actor with stale slot targets. Suite 16 of 16. |

### Stage 1: utilities

| Field | Content |
|---|---|
| Work | 1a: `planners/Hold.h` with `Hold`, `Update`, `Due`, one time type (`std::uint64_t` ms). Adopt it in `SmallSince` (`ResolutionLevel.*`), `HiddenSince` and `HiddenReleaseDue` (`RenderBudget.*`). 1b: `planners/RetainedStore.h` with `RetentionPolicy` and `Evicted`. Adopt it in `RenderTargetPool` (3 call sites) and `Compositor::materials_` (4 call sites). 1c: `planners/References.h` with `Owned<T>`, `Lease<T>` and `Identity<T>` (D1), and the four identity shapes `Identity<RenderTarget>`, `Identity<NiSourceTexture>`, `Identity<BSGeometry>` and `Identity<RecipeGraph>`. Later stages adopt them. 1d: the settings `StepDownHoldSeconds` (default 2) and `HiddenReleaseSeconds` (default 5) with their `SettingDesc` rows (D3); the step-down and hidden-release holds read them. |
| Files | New: `Hold.h`, `RetainedStore.h`, `Identities.h`. Changed: `ResolutionLevel.*`, `RenderBudget.*`, `ManagerTick.cpp` (2 sites), `RenderTargetPool.*`, `Compositor.h`, `CompositorBake.cpp`. |
| Deletes | `planners/ResourcePool.h`, `planners/RetainedCache.h` (keeps `StepOutputRetention` as the step cache's keep predicate until stage 6), `planners/RecipeTextureCache.h`, `planners/ResourceCache.h`, their four test files. |
| Native tests | `hold_tests`: start, reset, due at the boundary, clock going backwards. `retainedstore_tests`: count, bytes, age, oldest-out, keep predicate, zero-capacity, oversize value, evicted values returned and not destroyed inside. The pool and material cases of the deleted tests move here. `identities_tests`: equality, ordering, hash. |
| Risks | The pool's lock: eviction must still destroy targets outside `Pool::lock`, because the destructor restores presenter renderer data. A change of time type in `SmallSince` must keep `kStepDownHoldMS` (2 s). |
| Checkpoint | `soak-churn` and `soak-hour --quick 600`: `pool_targets` ≤ 16 and returns to 0 within 5 s of idle; `resolution_level` steps at the same screen fractions as before. Suite 16 of 16. |

### Stage 2: confirmed quick fixes

| Field | Content |
|---|---|
| Work | 2a: TextureLab channel means move into a retained store keyed by `Identity<NiSourceTexture>` and channel (count 256, D5). A failed render is kept for 10 s only, so a later call tries again. Delete `MeanLuminance` and `luminance_`. 2b: `AdvanceResizes` restores the tracked target level to the applied level when `StartResize` fails (one caller, two failure paths). Stage 3 removes the stored target level; this fix covers the time between. 2c: `Compositor::images_` becomes the image store with a policy of 64 entries, 30 s idle and failed loads kept 10 s (D4). A failed load is kept for 10 s only. `Manager::Clear` clears it. |
| Files | `TextureLab.h`, `TextureLabReadback.cpp` (2 functions), `TextureLabLifecycle.cpp` (`Clear`), `CompositorSource.cpp` (1 caller), `ManagerTick.cpp` (`AdvanceResizes`), `Compositor.h`/`.cpp` (`LoadImage`), `Manager.cpp` (`Clear`). |
| Deletes | `MeanLuminance`, `luminance_`, the raw-pointer `channelMeans_` map, the unbounded `images_` map. |
| Native tests | A pure `LevelAfterFailedResize` in `planners/ResolutionResize.h`, tested for both failures. The image and mean policies are covered by `retainedstore_tests`. |
| Risks | `MeasureFlatDisplacement` runs inside `MaterialInputs::From`. A retry after a failure must not repeat every tick: the store keeps a failure for a short expiry (10 s) instead of forever. A texture whose name is empty gets an identity from its address and extent only. |
| Checkpoint | `load-apply` then `studio-reload` twice in one launch: the trace shows the image store emptied at load (`load` event). `soak-distance`: no actor stays at a coarser level after `actor … keeps its resolution`. Suite 16 of 16. |

### Stage 3: actor render phase and output state

| Field | Content |
|---|---|
| Work | 3a: `planners/ActorPhase.h` with the phase variant, the events (plan awaited, plan arrived, plan failed, first draw done, level wanted, resize started, resize failed, swapped, hidden, in view, budget release, retire) and `Step(phase, event) -> {phase, effects}`. Derived queries: `TargetLevel`, `AppliedLevel`, `MayRelease`, `DrawsHidden`, `ReleasedBytes`. `Live` holds the level, the holding (`Held` or `Released{since}`), the step-down `Hold` and `stepUpBlockedFrom`, so both reset when a new `Live` starts (D6). `LiveActor::renderedFrame` becomes `std::optional<std::uint64_t>` in place of the 0 sentinel (D6). The engine keeps the resources in `LiveActor` beside the phase tag: the in-flight `RenderInstance` and resized stacks, and the `FirstDraw` slot list. 3b: `TrackedActor` keeps one `reinstallLevel`, written only by the retire effect, so a reinstall still starts at the decided level. 3c: `planners/OutputState.h`: `Inactive`, `Pending`, `Rendered`, `Failed{problem}` replaces the four `PlacedOutput` fields. |
| Files and call sites | Phase: 72 lines (`ManagerTick.cpp` 31, `ActorResize.cpp` 13, `ActorBudget.cpp` 12, `ManagerApply.cpp` 11, `ManagerApplication.cpp` 1, `TargetCensus.cpp` 2, `LiveActor.cpp` 2), plus 16 for `smallSinceMS`, `stepUpBlockedFrom` and `renderedFrame`, which move into the `Live` phase or stay as level-step state (D6). Output state: 12 sites in `LiveActor.cpp`, `ManagerApplication.cpp`, `ManagerTick.cpp`, `RegressionRequests.cpp`, `RegressionWorld.cpp`, `RegressionRun.cpp`, and `tests/engine/liveretirement_tests.cpp`. |
| Deletes | `LiveActor::awaitedPlan`, `firstDraw` as an independent optional, `resize`, `intermediates`, `hiddenSinceMS`, `appliedResolution` as a stored field (derived; `SetAppliedResolution` becomes an effect that updates `ActorEnvironment`), `TrackedActor::targetResolution`, `DrawsHidden` and the two release guards. `PlacedOutput::active`, `rendered`, `renderFailed`, `problem`. |
| Subsumes | 63dfa9c (the flag combination becomes the `FirstDrawing` phase out of view); stage 2b; the first-draw release disagreement; intermediates surviving a swap. |
| Native tests | `actorphase_tests`: a table of every phase and event pair with its next phase and effects; target level equals applied level after a failed resize; no release in `FirstDrawing`, `AwaitingPlan` or `Resizing`; a swap from `Released` ends `Held`; retire from every phase emits the reinstall level. `outputstate_tests`: a lost geometry fails a rendered output. Update `liveretirement_tests`. |
| Risks | The plan worker: `AwaitingPlan` keeps only the `JobId`; `DrainPlanJobs` must match it as before, and a stale arrival must not move another phase. `FinishResize` swaps a `shared_ptr<RenderInstance>` that the geometries share; the swap effect must run before the release effect in the same tick. The budget charge reads `ReleasedBytes`; a wrong derivation over-admits NPCs. |
| Checkpoint | Suite 16 of 16. `soak-fight-orbit`: `hidden_release` and `budget_release` never name an actor whose `first_draw` has not ended; after each `resolution_swap` of an out-of-view actor, the next `hidden_release` names it within 5 s. `soak-enchanted`: `estimated_bytes` stays within 5 % of measured live bytes without pool and scratch. `resolution_levels` counts match the actors' phases. |

### Stage 4: display leases for slots

| Field | Content |
|---|---|
| Work | 4a: `planners/DisplayLeases.h`: a pure lease book keyed by (geometry, surface, slot) that grants, swaps and ends leases and refuses a second lease per slot. 4b: `LiveActor` owns the book. The stage 3 effects drive it: attach (first draw done) grants; swap ends the old lease and grants a lease to the new stack, or writes `EmptyWrite` when the new stack has not drawn; retire ends every lease. In view or not, the effect is the same. 4c: `WriteSlot` takes a display lease or a conceal, not a `TextureRef`. `SlotWriter` keeps the written texture as an `Identity<RenderTarget>` for comparison and restore decisions. 4d: on restore, a texture the game material may still render moves to the retired list as before; the retired list gets its count bound. 4e: the census gets a `display_lease` holder in place of `slot` and writes `lease_violations`. |
| Files and call sites | `SlotWrites.*` (2 writers), `Binding.h`/`.cpp` (`SlotWriter` groups, `written`, `published_`, `RetainPublishedTextures`: about 15 sites), `Shell.cpp` (1 `WriteTexture`), `ManagerTick.cpp` (`WriteSlots`, `RenderGeometry`), `ActorResize.cpp` (`FinishResize`), `LiveActor.cpp` (`RetireGeometry`, `RetireActorEffects`), `TargetCensus.cpp`, `TargetHolders.*`. 17 retire, swap and first-draw sites call into the book. |
| Deletes | The rule "the target goes back to the pool only after the next slot write replaces it"; `ConcealStackSlots` as a special case in `FinishResize` (it becomes the swap effect); `SlotWriter`'s strong `written` reference; the `slot_only` breakdown (replaced by `lease_violations`). |
| Subsumes | 46b3969 and stage 0's out-of-view conceal. |
| Native tests | `displayleases_tests`: one lease per slot; swap ends the old lease first; retire ends all; granting to a retired geometry fails; a hidden swap yields a conceal effect. `targetholders_tests`: violation counting. |
| Risks | Game material lifetime: a shared vanilla material can reference a presenter after the binding restores; the retired list must keep the target until `SweepRetiredMaterialTextures` sees the field change. Presenter renderer data: a target recycled to the pool keeps its renderer data on the presenter, so a material that still points at the presenter shows another actor's pixels for a frame; ending a lease therefore writes the slot before it drops the reference. Ordering: the write must happen on the render path's thread, inside the tick. |
| Checkpoint | Suite 16 of 16. `soak-fight` and `soak-fight-orbit`: `lease_violations` 0 on every heartbeat; `display_lease_targets` ≤ the number of stack slots; no `slot` holder. `unequip-one` and `equip-cycle`: `retired_targets` returns to 0 within one second of the unequip. |

### Stage 5: game-object ownership under `LiveActor`

| Field | Content |
|---|---|
| Work | 5a: reading a raw game pointer makes an identity. `TextureRef(RE::NiSourceTexture *)` is removed. A game texture held for drawing is a game texture lease granted by `LiveActor` from the bound material (`MaterialInputs::From`). A presenter pointer read from a material resolves to an `Identity<RenderTarget>` and never retains the target. `MaterialBinding` keeps the original and the copy as `Owned<BSShaderMaterial>`; retire puts the original back only when the property still holds the copy (D2). 5b: 3D replacement. `LiveActor` records the identity of the actor's 3D root at install. The tick compares it with the current root every tick, if the measured cost stays under 0.05 ms for 12 actors, and otherwise at the 1 s recheck (D7); a mismatch retires the geometries through the `ClaimLostGeometries` path and marks the actor stale. `SkinPaletteLease::StillOwned` and `ShellBinding::StillOwned` also check that the source geometry still reaches that root, with a depth limit of 64 parents. Lights retire on the same event. 5c: the observed model table keeps `NiPointer`s only for installed models, which `LiveActor` owns; other models are identities with a token. The unloaded-actor check covers `tracked_` as well as `live_`. 5d: `MeshCache` keys by `Identity<BSGeometry>` with a policy of 256 entries and 30 s idle (D4), and drops `MeshEntry::geometry`; mesh reads take the geometry from the `LiveActor` lease at the call. |
| Files and call sites | `TextureRef.h`/`.cpp` (2 constructors), conversion sites: `Binding.cpp` 1, `Compositor.cpp` 1, `TexturePreviews.cpp` 2, `CompositorSource.cpp` 4. `ActorObservation.h`/`.cpp` (`ObservedModel`, `ObservedGeometry`, `ModelFor`, `KeepModels`), `ManagerApply.cpp` (4 model sites), `ManagerTick.cpp` (`Tick`, `ClaimLostGeometries`), `SkinPalette.cpp`, `Shell.cpp`, `Light.cpp`, `MeshCache.*`, `CompositorDemand.cpp` (`MeshHashIdentityOf`), 7 `MeshOf`/`CachedMesh` callers. 34 `NiPointer` members in `engine/` and `render/` are reviewed; each gets a stated owner. |
| Deletes | The `material_input` census holder; the raw-pointer path through `TextureLeases::Retain`; `MeshEntry::geometry`; `NiPointer`s in `TrackedActor::models` for models that are not installed. |
| Native tests | `identities_tests`: a game texture identity from name and extent. `observation_tests` over the stub headers in `tests/engine/lifetime`: models not installed hold no pointer. A pure root-reach check with a depth limit, tested with a cycle and with depth 65. |
| Risks | Engine pointers: an identity must never be dereferenced; every lookup through it goes back to the owner. A game texture lease must outlive every `RenderInstance` draw that samples it; `RenderInstance` is owned only by `LiveActor` geometries and the in-flight resize, so the retire order is: end draws, drop instances, end leases. The root check runs every tick for every live actor; it must cost less than 0.05 ms for 12 actors (`Section::kTick`). |
| Checkpoint | Suite 16 of 16. `unload` and `lifecycle`: a new trace event `game_holders` reports models, geometries, shells and lights per actor, and all four reach 0 after the unload. `material_input_targets` 0 in every soak. `soak-churn`: no `claimed` line without an equip change. |

### Stage 6: cache-key identities and hygiene

| Field | Content |
|---|---|
| Work | 6a: `IdentityFactsOf` takes `Identity<RecipeGraph>` and `Identity<BSGeometry>` instead of three addresses; `NodePositionInput` and the mesh fallback use them. 6b: variant graphs are cached by `Identity<RecipeGraph>`, so a variant install hits the plan cache. 6c: `StepOutputCache` moves into a retained store; the keep predicate is "another holder exists". 6d: preview tickets get an id and a generation; the callback looks the ticket up by id; `ClearPreviews` ends every ticket, and a late callback finds nothing. `PreviewKey` uses `Identity<NiSourceTexture>`. 6e: `MaterialBinding` stores the original material and, per D2, restores it on retire when the property still holds the private copy. 6f: `sampleWarned_` becomes a `LogOnce` keyed by texture name. 6g: shell emissive storage and copied bone weights: find the destructor behaviour (`search-deps --in commonlib`, decompile notes), record it in `REFERENCE.md`, and free in `ShellBinding::Detach` if the engine does not. |
| Files and call sites | `CompositorDemand.cpp` (3 address casts), `IdentityFacts.*` (1), `RenderPlanKey.cpp` (1), `ManagerApply.cpp` (`InstanceGraph`), `StepOutputCache.*`, `TexturePreviews.*`, `ConsumptionLeases.h`, `MenuWidgets.cpp` (1 callback), `Binding.*` (`Install`, destructor), `TextureLab*.cpp`, `Shell.cpp`, `SkinData.cpp`. |
| Deletes | The last raw addresses in content keys; `StepOutputRetention` and `kStepOutputIdleMs` as free constants (they become the store's policy); the raw `Ticket *` hand-off. |
| Native tests | `identityfacts_tests`: equal identities from a reused address with a different install token are unequal. `consumptionleases_tests`: clear, then a callback by id is a no-op. `renderplancache_tests`: two variant installs share one plan. |
| Risks | Memory safety in 6d: the callback must never hold a pointer into the ticket store. 6e: restoring the original material while another mod swapped the material must restore nothing (`OwnedState` already decides this for slot groups). 6b: a graph identity must change when the recipe text changes, or a stale plan is reused; `--verify-render-plans` checks it. |
| Checkpoint | Suite 16 of 16 with `--verify-render-plans`: no difference line. No regression case installs a variant today, so the stage adds one (a test recipe with a variant on two NPCs): the second install writes no `plan_job` event. `load-paint` and `studio-reload`: `preview_targets` and `unreached_targets` are 0 after the menu closes. |

### Stage 7: the census invariant as a gate

| Field | Content |
|---|---|
| Work | `planners/TargetHolders.h` gets `InvariantViolations(tallies, leases)`. A regression case fails when a heartbeat in its measured window reports a violation. `trace-report.py` prints the violations per holder. |
| Files | `TargetHolders.*`, `TargetCensus.cpp`, `regression/Steps.cpp`, `tools/trace-report.py`, `tools/regression_budgets.py`. |
| Deletes | Nothing. |
| Native tests | `targetholders_tests`: each holder outside the owner set counts as a violation. `run_tests`: a violation fails the case. |
| Risks | A transient violation during a swap frame would fail a case. The check reads only heartbeats, which run between ticks. |
| Checkpoint | Suite 16 of 16 and `soak-hour` with zero violations. |

## What stays shared on purpose

| Sharing | Why it stays |
|---|---|
| A game material references a presenter texture | The game owns the material and may share it among NPCs that wear the same armor. The plugin cannot end that reference, so the retired list owns the target until the material's field changes. |
| A step output shared by content key | Equal content on equal inputs is one target. The cache owns it, and instances that read it hold it too; the keep predicate keeps the entry while they do. The census classifies it as `cache`. |
| Pool targets returned by the `shared_ptr` deleter | The deleter is the only return path that works from any holder. The pool is the owner after the return. |
| Presenter textures | `RenderTargetPool::presenters_` loads each presenter once per slot and keeps it for the process. Their addresses are never reused, so the `TextureLeases` registry is not an address-reuse risk; it is bounded by the presenter count. |
| `std::shared_ptr<const RecipeGraph>` and plans | Immutable values. Sharing them cannot change what another holder sees. |

## Documentation impact

No stage edits these files before its checkpoint passes. Each stage rewrites
the sections below from the code as it stands.

| Stage | `REFERENCE.md` | `docs/components/` |
|---|---|---|
| 0 | planners: the resize swap bullets (already updated) | `engine.md`: Slot writes row (already updated) |
| 1 | planners: retention helpers; Render target budget (hold) | `planners.md`: Retention and identity helpers, Resource lifetime; `render.md`: Targets, presenters and previews |
| 2 | The lab's shaders (means); Compositor (image cache); planners: resize start failure | `render.md`: Measurements and readback; `engine.md`: Texture-memory controls |
| 3 | Install pacing and the hidden first draw; planners: resize bullets, Response 1, Install level, the `targetResolution` and `appliedResolution` bullet | `engine.md`: Actor and worn-piece state, Per-actor render assembly, How an actor and a recipe edit flow; `planners.md`: Sides and resolution levels |
| 4 | Bindings; Generated texture references; planners: "only after the next slot write" bullet; Cleanup ownership contracts | `render.md`: Bindings and PBR writes; `engine.md`: Slot writes; `diagnostics.md`: the census |
| 5 | Generated texture references (raw-pointer constructor, stale material-analysis sentence); Bindings (shell, palette, lights); Engine events, hooks and the manager (observation) | `engine.md`: Actor and worn-piece state; `render.md`: Meshes and skin |
| 6 | Plan builds on a worker thread (identity facts); Compositor (step output cache); Menu mechanics (preview tickets); Bindings (private material restore, emissive and skin allocations) | `planners.md`: Pure engine decisions and texture identity, Plan reuse across actors; `menu.md`: previews |
| 7 | planners: the census | `diagnostics.md`, `regression.md` |

`REQUIREMENTS.md` (Safety) gains the census invariant at stage 7.

## Scope

The plan is about 12 sessions. Stages 3 and 4 carry most of the risk,
because they change when targets and slots are released while the game
renders. Each stage can stop at its checkpoint without leaving the code in a
mixed state, because each one replaces a whole mechanism. Stage 3 is the
largest single change: 91 field references in 7 files.

## Decisions

The user decided D1 to D8 on 2026-10-09.

| Id | Decision | Effect on the stages |
|---|---|---|
| D1 | Generic names everywhere: `Owned<T>`, `Lease<T>`, `Identity<T>`, used directly at call sites. | The stages use these names. A draw lease and a display lease have one type, so the compiler does not keep a draw lease out of a slot. The rule is held by which owner can grant which lease: only `LiveActor` grants a lease that a slot stores. |
| D2 | On retire, put the original material back only when the property still holds the binding's copy. | `MaterialBinding` keeps the original and the copy as `Owned<BSShaderMaterial>`. A material that the game or another mod installed after the binding stays in place. |
| D3 | The step-down hold and the hidden-release hold become INI settings with today's values as defaults (`StepDownHoldSeconds=2`, `HiddenReleaseSeconds=5`). The pool idle (5 s), the step idle (500 ms) and the cache idle (30 s) stay constants. | Stage 1 adds the two settings and their `SettingDesc` rows. `Hold` takes the delay as an argument. |
| D4 | Count and idle bounds: images 64 entries, 30 s idle, a failed load kept 10 s; meshes 256 entries, 30 s idle. | Stage 2 and stage 5 use these `RetentionPolicy` values. |
| D5 | The channel-mean cache keys by lowercased name, width, height and channel. A texture without a name is measured and not cached. `MeanLuminance` has no caller and goes. | Stage 2. |
| D6 | The step-down hold and `stepUpBlockedFrom` belong to `Live` and reset when a new `Live` starts. `renderedFrame` stays on `LiveActor` as `std::optional<std::uint64_t>` in place of the 0 sentinel. | Stage 3. |
| D7 | Check every tick whether an actor's 3D root changed, if the measured cost stays under 0.05 ms for 12 actors; otherwise move the check to the 1 s recheck. | Stage 5 measures the cost and records it. |
| D8 | A census violation fails the running regression case, with the holder and the actor in the reason. | Stage 7. |
