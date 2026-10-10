Status: proposed (2026-10-10) on branch `pure-tick`, on hold. The user is reviewing the code and will assess this plan in bulk with the `Manager` split plan; no decision is made. Line numbers were read at 0a7e0a0. Fold in the user's preference for data-driven structure and visible grouping (blank lines between phases, grouped header members) when this is implemented.

# Application ledger: cleaning up `ApplicationService` and `SessionQueue`

**The short version.** Since R4, the path that marks an actor stale goes through six hops to do one simple job: remember an actor id until the next tick. `ApplicationService` mixes three things:
- a pure ledger (`Begin`, `Report`, `PendingFor`, the prune functions, `Snapshot`);
- 10 one-line pass-throughs to `SessionQueue`;
- glue left over from before R4: `RunActor`, `apply_`, `SyncNow`, `Rejections`.

`SessionQueue` holds state that several threads touch and state only the main thread touches, all under one mutex.

**What I propose.**
- Turn the ledger into a plain record with free functions, tested natively.
- Shrink `SessionQueue` to the per-load gate, task posting, stale marks and equip deadlines.
- Delete `ApplicationService`.
- Move the reinstall and equipped sets onto `Manager` as plain sets that only the main thread uses.

Step 1 keeps behaviour identical. Step 2 changes some behaviour, and every change is listed in section 3.

All `.cpp` line numbers below are real file lines.

---

## 1. Context

### What runs today when an actor is marked stale

| Hop | Where | Thread |
|---|---|---|
| An event sink, the regression, a rebuild or `FinishLoad` calls `MarkStale` / `MarkReinstall` | `Events.cpp:63,86,114`; `RegressionRequests.cpp:187`; `ManagerApplication.cpp:192`; `Manager.cpp:85-97` | Any thread for events; the main thread for the rest |
| `SessionQueue::SubmitSync` coalesces requests (`pending`, `rerun`) and posts an SKSE task | `SessionQueue.cpp:132-190` | The caller's thread |
| The task calls `ApplicationService::RunActor`: `DrainRejections`, `BeginActor` (a new actor-scope revision), `PendingFor` | `ApplicationService.cpp:171-183` | Main |
| `apply_` is `Manager::NoteStale`, which writes `stale_[id] = tokens` | `Manager.cpp:29-33`, `ManagerFacts.cpp:119-122` | Main |
| `GatherFacts` drains `stale_` into `TickFacts::stale` and `GatheredFacts::tokens` | `ManagerFacts.cpp:143-147` | Main, once per tick |
| The core's `SettleApplications` runs `RunSettleApplications`, which calls `PrepareApplications(tokens)` | `ManagerCommands.cpp:326-339`, `ManagerApplication.cpp:200-217` | Main |

### `ApplicationService` members, their callers, and whether they are still needed

| Member (`.h` line, `.cpp` line) | Callers | Kind | Still needed after R4? |
|---|---|---|---|
| `Post` (h32, cpp89) | `Manager::PostTask` (`Manager.cpp:45-47`), used by the editor, regression, animation and inspection code; menu or animation thread | Pass-through | Yes, but it belongs on the session |
| `MarkStale` (h33, cpp93) | `Manager.cpp:51`, `RegressionRequests.cpp:187` | Pass-through | Yes |
| `SyncNow` (h34, cpp97-101) | `RunObserve` (`ManagerCommands.cpp:314`) | Calls `RunActor` directly | **Leftover.** It only feeds `stale_` |
| `MarkReinstall` / `ReinstallDue` / `ClearReinstall` (h35-37) | `ManagerApplication.cpp:192`; `ManagerFacts.cpp:219`; `ManagerCommands.cpp:331`, `ManagerApply.cpp:1274` | Pass-through | Yes; main thread only |
| `Equip` / `FinalizeDue` / `TakeEquipped` (h38-40) | `Manager.cpp:82`; `ManagerTick.cpp:633` (every frame); `ManagerApply.cpp:1332`, `ManagerCommands.cpp:334` | Pass-through (`FinalizeDue` also drains rejections, cpp119-123) | Yes |
| `BeginLoad` / `Resume` / `Loading` (h41-43) | `Manager.cpp:106`, `Manager.cpp:95`; `ManagerTick.cpp:539`, `RegressionRequests.cpp:168` | Pass-through plus ledger cancel / drop | Yes; this is the session gate |
| `Begin` (h44, cpp209-245) | `ChangeAndRebuildActors` (`ManagerApplication.cpp:188`) | Ledger | Yes |
| `ActorsFor` / `FailedActorsFor` (cpp247, 256) | `ManagerApplication.cpp:189`, `:158` | Ledger | Yes |
| `PendingFor` (cpp269-286) | `ManagerApplication.cpp:233,237`, `ManagerAnimation.cpp:67`, internal | Ledger | Yes |
| `Report` (cpp288-325) | `ManagerApplication.cpp:194,205,211,227` | Ledger plus a trace event | Yes |
| `Retire` (cpp327-333) | `ManagerApply.cpp:1242`, `ManagerApplication.cpp:237` | Ledger | Yes |
| `Cancel` (cpp378-391) | `BeginLoad` and tests only | Ledger | Yes, but it should not be public |
| `Snapshot` (cpp393-403) | `ManagerSnapshot.cpp:489`, `Manager.cpp:141`, `ManagerRegression.cpp:142`, `RegressionRequests.cpp:182,229,283` | Ledger | Yes; studio progress and regression read it |
| Private: `BeginActor`, `InvalidateActorScopes`, `RunActor`, `DrainRejections`, `apply_`, `rejections_`, default constructor (cpp75) | — | Glue | `BeginActor` and `InvalidateActorScopes` are needed. **`RunActor`, `apply_`, `DrainRejections`, `Rejections` and the default constructor are leftovers** |

### State and the thread that touches it

| State | Owner | Thread | Still needed? |
|---|---|---|---|
| `nextRevision_`, `records_` (by recipe), `actors_` (by actor) | ApplicationService, h69-71 | Main only, with no lock | Yes. This is the ledger |
| `rejections_` (its own mutex) | ApplicationService, cpp12-15 | Written under the queue lock on any thread (`SessionQueue.cpp:180-186`); drained on main | **Leftover** |
| `apply_` | ApplicationService, h72 | Main | **Leftover** |
| `loading`, `generation`, `submit` | `SessionQueue::State` | Any thread, under the lock | Yes |
| `pending`, `rerun` (coalescing) | State | Any thread | **Leftover.** Observation happens at gather time, so a rerun only creates a second revision (see 2.1) |
| `finalizeDue` (deadline and trace context) | State | Written by the equip sink, read every frame | Yes |
| `equipped`, `reinstall` | State | Main only, yet kept under the lock | Yes, but they do not need the lock |
| `stale_` | `Manager.h:366` | Main | It becomes the session's stale set |

---

## 2. Findings, biggest improvement first

| # | Finding | Where |
|---|---|---|
| 1 | **The stale path is a leftover from before R4.** It takes six hops, plus the coalesce/rerun logic, `RunActor`, `apply_`, `SyncNow`, `NoteStale` and `stale_`, to record an actor id. A rerun posts a second task and mints a second actor revision, yet the gather sees the same world either way. Before R4 the sync ran inside the task, which is why the rerun mattered then. | `SessionQueue.cpp:141-147,158-174`; `ApplicationService.cpp:97-101,171-183`; `ManagerFacts.cpp:119-122` |
| 2 | **`ApplicationService` is mostly a facade.** 10 of its 22 public members only forward to the queue. Generic task posting (`Manager::PostTask`) and the load gate (`Loading`) go through a type named after applications. | h32-43, cpp89-144 |
| 3 | **`Rejections` is a side channel.** It has a second mutex, and its callback runs while the queue lock is held (`SessionQueue.cpp:180-186`). It is drained in four places (cpp120, 122, 172, 212). It reports the same failure twice: `ChangeAndRebuildActors` already reports `actor sync could not be queued` (`ManagerApplication.cpp:192-195`), and the drain then creates a failed actor-scope record (cpp185-198). | |
| 4 | **`Begin` does six things.** It drains rejections, supersedes overlapping recipe records, gathers retry actors, cancels actor records and requeues their scopes, creates the new record, and prunes. One rule is never named: an empty recipe id matches every recipe, in both directions (cpp214). | cpp209-245 |
| 5 | **Bookkeeping is duplicated.** `PruneActors` and `PruneRecipes` are near copies (cpp335-376); so are `AppendActors`/`AppendRetryActors` (cpp30-50) and `ActorsFor`/`FailedActorsFor`. `Find` picks a map by whether `actorID` is set (cpp200-207). | |
| 6 | **The locking is spread out and mixed.** One mutex guards both cross-thread state and main-thread-only sets. `ReinstallDue` and `TakeEquipped` check `loading` again under the lock. The ledger has no lock, and no type says that it is main-thread only. | `SessionQueue.cpp:21-45,122-130,229-232` |
| 7 | **`Report` checks too much inline.** Its guard tests seven conditions in one expression, and it also emits a trace and prunes. | cpp288-325 |
| 8 | **The names are unclear.** `Begin`, `BeginActor` and `BeginLoad` are three unrelated things. `Retire` actually reports *unmatched*. `Pending(phase)`, the `pending` set and `PendingFor` mean different things. `attempt` is the revision of the actor-scope record. `Equip`/`FinalizeDue` are also called `QueueEquipFinalize`/`FireDueFinalizes`. | |
| 9 | **The header breaks the house rules.** The constants come before the type, the data types are not first, and h23 uses `std::function` without including `<functional>`. Two paths exist only for tests: the default constructor and the `actor adapter is unavailable` fallback (cpp178-181). | |
| 10 | **The tests still describe the pre-R4 flow.** `applicator_tests.cpp:58-70` takes the equip flag inside the apply callback. `CoalescedRerunAndStaleActorResult` (:144) and `RejectedInternalScheduling` (:273) pin down the leftover mechanisms. | |
| 11 | **An executor makes a decision (boundary rule).** The install executor decides to fire the equip event from state the engine holds (`ManagerApply.cpp:1332`). This is out of scope here; see Q4. | |

---

## 3. Proposed design

### Data types (types first in each header)

**`src/engine/ApplicationLedger.h`** is engine-free, builds into `BeefEngineServices` and is tested natively:

```cpp
struct ApplicationLedger {
  std::uint64_t nextRevision = 1;
  std::map<std::string, ApplicationRecord, std::less<>> byRecipe;
  std::map<std::uint32_t, ApplicationRecord> byActor;
};
```

The legal states stay as they are today:
- An actor's phase is either *unfinished* (`kQueued`, `kPrepared`, `kOutOfView`) or *final* (the other four).
- A record's phase is the aggregate of its actors' phases.
- A token is current only when both its revision and its actor's attempt match.
- Each map keeps every unfinished record, plus up to 256 final ones.

**`src/engine/SessionQueue.h`** is the only part that touches threads, and it stays a class because it holds the mutex and the SKSE submit:

```cpp
struct StaleMarks { std::vector<std::uint32_t> actors; std::vector<std::uint32_t> equipped; };
class SessionQueue {
 public:
  explicit SessionQueue(Submit a_submit);
  void BeginLoad(); void Resume(); [[nodiscard]] bool Loading() const;
  void Post(Task a_task);
  bool MarkStale(std::uint32_t a_actorID);
  void Equip(std::uint32_t a_actorID, std::uint32_t a_nowMS);
  [[nodiscard]] StaleMarks TakeStale(std::uint32_t a_nowMS);
};
```

`State` keeps only `lock`, `submit`, `generation`, `loading`, `stale`, and the `equipDue` deadlines with their trace context. The due check becomes a small named pure helper, `Due(deadline, now)`, so the wraparound compare at `SessionQueue.cpp:212` is in one place.

**On `Manager`, main thread only:** `std::set<RE::FormID> reinstall_` and `equipped_`, next to `forgets_`.

### Free functions over the ledger

| Function | Replaces |
|---|---|
| `BeginRecipeChange(ledger, recipe, candidates) -> ApplicationToken` | `Begin`. It is built from named steps: `Overlaps`, `RetryActorsOf`, `Supersede`, `CancelActorRecords`, `NewRecipeRecord`, `PruneFinished` |
| `BeginActorSync(ledger, actor) -> ApplicationToken` | `BeginActor` plus `InvalidateActorScopes` (renamed `Requeue`) |
| `Report(ledger, token, actor, phase, problem)` | `Report`, with its guard moved into `Accepts(record, token, actor, phase)` |
| `EndUnfinished(ledger, actor, tokens, phase, problem)` | `Retire`, and the loop in `DrainRejections` |
| `PendingFor`, `ActorsOf`, `FailedActorsOf`, `RecordsOf` | Readers with the same behaviour |
| `CancelAll`, `DropCancelled` | `Cancel`, and the erase in `Resume` |
| `DropOldestFinished(map, cap, isFinished)`, in the `.cpp` only | `PruneActors` and `PruneRecipes` |

### How the engine uses them

| Site | Change |
|---|---|
| `PostTask`, `MarkStale`, `QueueEquipFinalize`, `OnFrame`, regression | `session_.Post`, `session_.MarkStale`, `session_.Equip`, `session_.Loading()` |
| `ChangeAndRebuildActors` | A `MarkReinstall(id)` helper: `if (!session_.MarkStale(id)) return false; reinstall_.insert(id);` The failure branch at `:192-195` stays as it is |
| `GatherFacts` | Call `TakeStale(NowMS())` and add the finalized equips to `equipped_`. For each actor, call `BeginActorSync`, then `inputs.tokens[id] = PendingFor(...)`. `StaleActorOf` reads `reinstall_.contains(id)` |
| `RunObserve` | `session_.MarkStale(id)` |
| Settle, Forget and Install executors | Erase from `reinstall_` and `equipped_` |
| `Clear` / `FinishLoad` | `session_.BeginLoad(); CancelAll(...)`, clear both sets / `DropCancelled(...); session_.Resume()` |
| Removed | `ApplicationService.{h,cpp}`, `NoteStale`, `stale_`, `FireDueFinalizes` |

**Boundary rule.** The ledger is reporting state, not a tick decision; R-5 says "the core decides only when applications finish". Its only links to the core already arrive as facts: `StaleActor::reinstall` and the stale actors themselves. The equip debounce reads one actor's event and becomes a pure helper. The rule holds without moving anything into `World`.

### Behaviour changes in step 2 (everything else stays the same)

| Change | What it affects |
|---|---|
| **a.** No SKSE task per actor and no coalesce/rerun, so there is one actor revision per gather. | Studio sees fewer superseded actor records. Regression polls for `revision > previous` (`RegressionRequests.cpp:183-186,283`), so it is unaffected. |
| **b.** The actor-scope record is created at the gather, not when the task runs. | It appears up to one tick later. The phase order (queued, then prepared or out of view, then rendered) is unchanged. |
| **c.** The failure "actor sync could not be queued" can no longer come from SKSE refusing a task. | That only happens when the task interface is missing, and then `PostTask` already fails. The failure when loading stays. |
| **d.** Equip deadlines are checked at the gather, not every frame. | At most one tick later; the same at the default of 60 animation FPS. |
| **e.** The queue trace actions `coalesced_sync`, `execute_sync`, `stale_sync` and `sync_rejected` go away; `mark_stale`, `equip_finalize` and `post` stay. | Only `docs/components/diagnostics.md:49` and old evidence `.jsonl` files mention them. `tools/trace-report.py` does not read them. |

---

## 4. Steps (the build stays green after each)

| Step | Content | Tests | Risk |
|---|---|---|---|
| **1. Extract the ledger (no behaviour change)** | Add `ApplicationLedger.{h,cpp}`. `ApplicationService` keeps its API and delegates to a `ledger_` member. Split `Begin`, merge the prune functions, add `Accepts`. Add the file to `cmake/Native.cmake:21` and to the fake fixture list in `tests/tools/build_tests.py:25`; run `python3 tools/compile-db.py`. | New `tests/engine/applicationledger_tests.cpp`, ported from the ledger-only cases in `applicationservice_tests.cpp` (`RecipeRetention` through `WholeCatalogOverlap`). The old suites stay green unchanged. `ctest --preset native`, then the Windows build. | Low. This is a mechanical move. |
| **2. Shrink the session and delete the service (behaviour changes a to e)** | Change `SessionQueue` to the new API. `Manager` owns `session_`, `applications_` (the ledger), `reinstall_` and `equipped_`, with the wiring from section 3. Delete `ApplicationService`, `NoteStale`, `stale_` and `FireDueFinalizes`. | Rewrite `sessionqueue_tests.cpp`: marks dedupe, marks dropped during loading, `TakeStale` equip delay, a stale `Post` across a load, queue lifetime. Port the retire-before-render, retire-across-load and recipe-overlap cases from `applicator_tests.cpp` to the ledger, then delete `applicator_tests.cpp` and `applicationservice_tests.cpp`. Update the constructor in `recipeoperations_tests.cpp:14,44,130`, `authoringlifecycle_tests.cpp:142`, and the doubles `tests/engine/platform/engine/Manager.h:15` and `tests/engine/animation/engine/Manager.h:20-43`. Move `animationsubscriptions_tests.cpp:93-209` to the free functions. Native tests, the Windows build, then **in game**: the full regression suite (equip, rebuild, retire, crowd). Look for `application` traces going queued, then prepared, then rendered, `actor_decide` after an equip, and no `resource_disagreement`. | Medium. Marks must still reach the gather when no tick runs while loading; `TakeStale` returns nothing while loading, as today. This lines up with the R6 in-game checkpoint. |
| **3. Docs** | Update `docs/components/engine.md:54-60,93-96,203,267,290-301,396-426`, `REFERENCE.md:21,1022-1026,1391-1400,2510-2513` and `docs/components/diagnostics.md:49`. | — | None. |

If you decline the behaviour changes, a fallback for step 2 is possible: keep `SubmitSync`, have `Manager` supply the sync callback (`BeginActorSync` plus the tokens), and make the rejections a locked vector owned by `Manager`. It works but leaves findings 1, 3 and 6 mostly in place. I do not recommend it.

---

## 5. Open questions

| # | Question | Recommendation |
|---|---|---|
| Q1 | Accept behaviour changes a to e? | Yes. They delete about 250 lines of thread code, and the regression results do not depend on them. |
| Q2 | Where should the ledger live: `engine/` or `planners/`? | `src/engine/ApplicationLedger.*`. It is bookkeeping, not planning; it is already engine-free in `BeefEngineServices`, and keeping it there means less churn. |
| Q3 | Read the tokens when settling (`PendingFor` in `RunSettleApplications`) and drop `GatheredFacts::tokens` (`Manager.h:86`)? | Do it later, as a separate flagged commit. Today a deferred install that settles in a later tick finds no tokens, so its applications stay queued until the next stale mark. The change would prepare them instead; check that case in game first. |
| Q4 | Make the equip flag a core fact (`StaleActor::equipped`, `Install::fireEquip`) to satisfy the executor rule (finding 11)? | Yes, but as a follow-up in the pure-tick plan, because it touches `Facts.h` and `Commands.h`. |
| Q5 | Rename `SessionQueue` (to `Session` or `GameSession`)? | Keep the name. It still posts tasks, and a rename touches 6 test files for little gain. |
| Q6 | A record for the reinstall and equipped sets, or two `Manager` members? | Two members next to `forgets_`. They are plain sets with no invariants to protect together. |
| Q7 | Keep the rule that an empty recipe id overlaps every recipe? | Keep it, named `Overlaps`. `ReapplyAll` (`ManagerApply.cpp:1066`) depends on it. |
