Status: proposed (2026-10-10) on branch `pure-tick`, on hold. The user is reviewing the code and will assess this plan in bulk with `docs/plans/application-ledger-2026-10-10.md`; no decision is made. Line numbers were read at 0a7e0a0. It answers the walkthrough findings that `Manager` is a god object with a name that says nothing, that `Clear` is a hand-written sequence of resets, and that `OnFrame`/`RunFrame` bury the tick under domain work.

# Effects runtime: splitting `Manager` into named parts

Everything here was read on branch `pure-tick` with a clean tree, and nothing was edited. Line numbers point at today's code. "Today" means the current `Manager`.

**Summary.** The hypothesis mostly holds, with five changes:
- The tick core splits in two. One part holds the core's world (`TickCoreState`). The other holds what the next tick is told (`TickInbox`).
- Reporting where the engine and the core disagree becomes its own small record (`ResourceDisagreements`).
- What the engine has learned by observing actors becomes its own record (`ObservedActors`): `models_`, the material layout, the emissive gate and the armor-without-geometry log.
- The studio link splits in two: the cross-thread mailbox (`PublishedSnapshots`, a class) and the record the editor calls back through (`EditorHost`, plain data).
- The root holds one record of parts (`RuntimeParts`). It runs two declared tables: one for load resets and one for frame steps.

## 1. Inventory

### 1a. Data members (`src/engine/Manager.h`)

| Member (line) | Type | Written by | Read by |
|---|---|---|---|
| `renderFrame_` 193 | `uint64_t` | RunFrame `ManagerTick.cpp:562` | GatherFacts `ManagerFacts.cpp:136`, RenderGeometry `ManagerTick:688`, RunDrawUnpublished `:805` |
| `tickNowMS_` 194 | `uint32_t` | RunTick `ManagerTick:589` | RenderGeometry `:688` |
| `applications_` 343 | `ApplicationService` (treated as opaque) | many | `Manager.cpp:29,46,52,82,95,106,141`; Tick `:539,633`; Facts `:219`; Commands `:314,331,334`; Apply `:1242,1274,1332`; Application `:158-237`; Animation `:67`; Snapshot `:489`; Regression `:142`; RegressionRequests `:168,182,187,229,283` |
| `animations_` 344 | `AnimationSubscriptions` | ctor `Manager.cpp:34`, Clear `:107` | Animation `:14,38,54,58,66`; Apply `:1267` |
| `live_` 346 | `unordered_map<FormID,LiveActor>` | Install `Apply:1331`, Retire `:1257`, Clear, ClaimsOf (retires geometries) | Commands (15 sites), Tick `:576,741,795`, Facts `:227,260`, Heartbeat `:274-327`, Snapshot `:418-512`, Events `:63,85`, Animation, Application, Inspection `:45`, RegressionRequests `:232,275,289` |
| `editor_` 347 | `RecipeEditor{*this}` | — | Tick `:560,614,638,671,686,716`; Facts `:214`; Apply `:1176,1233`; Application `:190`; Snapshot; Regression; RegressionRequests `:195-218`; `Manager.cpp:43,124-144` |
| `loggedNonPBRArmor_` 348 | `unordered_set<FormID>` | `Apply:1085` | Clear `:127` |
| `warningHistory_` 349 | `WarningHistory` | LogOnce in Apply, Commands, Tick, Facts, Heartbeat, Events | Clear `:128` |
| `carriedTimes_` 351 | `CarriedTimes` | Remember `Apply:1247`, Take `:1169`, Expire `Tick:565` | Clear |
| `renderPlans_`, `planJobs_`, `planQueue_` 352-354 | `RenderPlanCache`, `PlanJobBook`, `PlanQueue` (worker thread) | `Apply:1209,1350,1364-1395`, `Application:180-181` | Clear `:121-123` |
| `menuIdleHold_` 356 | `optional<IdleCameraHold>` | `Manager.cpp:61-72` | — |
| `regressionPaintSession_` 357 | `uint64_t` | `ManagerRegression:207` | — |
| `lastTickMS_` 358 | `uint32_t` | `Tick:572`, Clear | `Tick:569` |
| `lastMetricsMS_` 359 | `uint32_t` | `Tick:603`, `Heartbeat:267` | `Tick:545,602`, `Heartbeat:266` |
| `holdingStep_` 360 | `uint32_t` | `Heartbeat:296` | `:295` |
| `videoMemory_` 361 | `VideoMemoryProbe` | `Heartbeat:316` | — |
| `lastSweepMS_` 362 | `uint32_t` | `Tick:564` | `:563` |
| `world_` 363 | `Orchestration::World` | only `Tick:612` and Clear | Facts `:132,149,209,247,258`; Heartbeat; RegressionRequests `:235,276` |
| `models_` 364 | `unordered_map<FormID,ModelTable>` | ObserveActor `Facts:202`, KeepModels `Commands:321`, erase `Apply:1275` | Install `Apply:1289` |
| `results_` 365 | `vector<CommandResult>` | `Tick:628` | `Facts:138` |
| `stale_` 366 | `map<FormID,vector<ApplicationToken>>` | NoteStale `Facts:121` (the `applications_` callback) | `Facts:143`, erase `Apply:1276` |
| `forgets_` 367 | `set<FormID>` | `Manager.cpp:78`, `Animation:20`, `Application:193`, `RegressionRequests:179` | `Facts:246`, `Apply:1277`, `RegressionRequests:275` |
| `heartbeatDue_` 368 | `bool` | `Tick:546,599`, `Heartbeat:268`, Clear | `Tick:598` |
| `pendingSample_` 369 | `optional<BudgetSample>` | `Tick:604`, `Heartbeat:269` | `Facts:142` |
| `tickClock_` 370 | `TickClock` | `Facts:127` | `:135`. **Not reset on load.** |
| `disagreements_` 371 | `map<FormID,vector<string>>` | `Facts:276` | `:269` |
| `disagreementCount_` 372 | `uint64_t` | `Facts:281` | `Heartbeat:279`. **Not reset on load.** |
| `emissivePathEnabled_` 373 | `bool` | `Manager.cpp:157`, `Apply:1079` | `Facts:200`, `Snapshot:414`, `RegressionRequests:168` |
| `materialLayout_` 374 | `MaterialLayout` | ObserveActor `Facts:202` | `Apply:1070`, `Snapshot:415` |
| `frozenLastTick_` 375 | `bool` | `Tick:621`, Clear | `Tick:619` |
| `textureLabStarted_` 376 | `bool` | `Tick:531` | `:530` |
| `snapshotLock_` 378 | `mutable std::mutex` | — | `Snapshot:428-472`, `Manager.cpp:137` |
| `latest_`, `latestBudget_`, `watch_`, `watchedDocument_`, `watchedMS_`, `snapshotVersion_`, `kWatchWindowMS` 379-385 | snapshot mailbox | `Snapshot:426-474`, Clear `:136-148` | Snapshot (`version` is bumped outside the lock at `:470`) |

### 1b. Methods

The **Ext. callers** column lists callers outside `Manager`. "—" means internal only.

| Method | Pub/Priv | Def file:line | State touched | Ext. callers |
|---|---|---|---|---|
| GetSingleton | pub | `Manager.cpp:38` | — | everyone |
| ctor | priv | `Manager.cpp:28` | binds the `applications_` callback (NoteStale) and the `animations_` callback (QueueAnimationEvent) | — |
| Editor | pub | `:43` | editor_ | menu: `Menu.cpp:60-205`, `StudioPage:137`, `Tuning:153-223`, `RecipesPage:139`, `RecipeActions:190,211` |
| PostTask | priv | `:45` | applications_ | RecipeEditor (friend), about 30 sites |
| EditorClockMS | priv | `:49` | — | RecipeEditor (friend) |
| MarkStale ×2 | pub | `:51,55` | applications_ | `Events.cpp:63,86,114` |
| Hold/ReleaseIdleCameraForMenu | pub | `:61,67` | menuIdleHold_ | `Menu.cpp:341,347` (SKSE task) |
| QueueForget | pub | `:74` | forgets_ (in a task) | `Events.cpp:79` |
| QueueEquipFinalize | pub | `:81` | applications_ | `Events.cpp:65` |
| MarkLoadedActorsStale | pub | `:85` | — | — |
| BeginLoad / FinishLoad | pub | `:90/:92` | Clear; applications_ | `main.cpp:140,160,169` |
| Clear | pub | `:99` | nearly everything | — |
| SetEmissivePathEnabled | pub | `:156` | emissivePathEnabled_ | `main.cpp:102,113` |
| RetireAll | pub | `ManagerAnimation.cpp:11` | animations_, live_, forgets_ (in a task) | `RecipesPage:147` |
| QueueAnimationEvent | priv | `ManagerAnimation:24` | live_, animations_ (in a task) | the `animations_` callback |
| ReconcileAnimationEvents | priv | `:49` | live_, animations_, anim-event globals | — |
| SweepAnimationEvents | priv | `:62` | live_, animations_, applications_ | — |
| ApplicationActors / LoadedActorIDs / ActorsApplying | priv | `ManagerApplication:126/135/157` | live_, applications_ | — |
| ChangeAndRebuildActors | priv | `:171` | renderPlans_, planJobs_, applications_, live_, forgets_, editor_ | RecipeEditor, about 20 sites |
| Prepare/Finish/AbandonApplications | priv | `:200/219/236` | applications_, live_ | — |
| Execute | priv | `ManagerCommands:87` | dispatches | — |
| RunStartResize, RunSwapResize, RunAbandonResize, RunReleaseIntermediates, RunSetAppliedLevel, RunBuildAwaitedRender, RunFailAwaitedRender, RunInstall, RunRetire, RunForget, RunObserve, RunKeepModels, RunSettleApplications | priv | `:150,176,197,209,223,235,259,272,296,308,313,319,326` | live_, warningHistory_, disagreements (through ReportDisagreement), models_, applications_ | — |
| Fire / QueueEvent / QueueBroadcast / FireAt | pub | `ManagerEvents:62/74/83/91` | live_ (in a task), warningHistory_ | `Events.cpp:25,33`, `main.cpp:85,87`, `Menu.cpp:168`; Fire has no outside caller |
| ReapplyAll | pub | `ManagerApply:1064` | posts ChangeAndRebuild | `SetupPage:52,195,200`, `RecipesPage:143` |
| NoteMaterialLayout / NoteArmorWithoutGeometry | priv | `:1069/1082` | materialLayout_, emissivePath; loggedNonPBRArmor_ | — |
| LiveActorFor, FireEquip, BuildInstances, InstanceFor, CarryInstanceTime, PlaceInstances, FailAwaitedRender, ShownOutputs | priv | `:1094,1116,1123,1132,1166,1184,1223,1231` | live_, carriedTimes_, editor_ view, plan stores, warnings | — |
| RetireActorResources / ForgetActorResources | priv | `:1237/1266` | live_, applications_, carriedTimes_, animations_, models_, stale_, forgets_ | — |
| InstallLiveActor / BuildAwaitedRender / DrainPlanJobs | priv | `:1281/1339/1364` | live_, models_, plan stores, applications_ | — |
| NoteStale | priv | `ManagerFacts:119` | stale_ | the `applications_` callback |
| GatherFacts, SightingOf, StaleActorOf, ClaimsOf, ForgetRequests | priv | `:125,162,186,225,244` | tickClock_, world_, results_, stale_, forgets_, pendingSample_, renderFrame_, models_, materialLayout_, emissivePath, editor_ view, applications_; ClaimsOf retires geometries and ForgetRequests forgets actors (side effects during gather) | — |
| CheckAgreement / ReportDisagreement / EmitCoreRecords | priv | `:256/279/293` | disagreements_, disagreementCount_, warningHistory_ | — |
| FlushMetricsHeartbeat | pub | `ManagerHeartbeat:262` | lastMetricsMS_, heartbeatDue_, pendingSample_ | `RegressionRun.cpp:80` |
| EmitHeartbeat / RenderedTargets / ReportRenderMemory / SampleBudget | priv | `:273/287/291/314` | live_, world_, disagreementCount_, holdingStep_, videoMemory_, warnings, latestBudget_ | — |
| RequestMesh | pub | `ManagerInspection:43` | live_ (in a task) | `Menu.cpp:165` |
| RegressionActivity / RegressionRecipe | pub | `ManagerRegression:130/160` | editor_, applications_ | `RegressionWorld:1087,1089` |
| StartRegressionEdit / Gesture / Duplicate / Save / Delete / Paint | pub | `:173/178/187/192/196/200` | editor_, regressionPaintSession_ | `RegressionWorld:825,839,841,962-981` |
| GetStatus | pub | `ManagerSnapshot:412` | live_, emissive, layout | — (only PublishStatus) |
| Watch / LatestSnapshot / LatestBudgetView | pub | `:426/442/447` | snapshot mailbox (locked) | `SetupPage:137,294,296`, `StudioPage:433,436`, `RecipesPage:217,220`, called from the render thread |
| InspectedActor / PublishBudgetView / PublishSnapshot / BuildSnapshot / PublishStatus / PublishPieces / PublishDocument / DocumentRowFor | priv | `:434/452/458/476/488/504/537/548` | mailbox, live_, editor_, applications_ | — |
| OnFrame | pub | `ManagerTick:525` | textureLabStarted_, lastMetricsMS_, heartbeatDue_ | `Hooks.cpp:23` |
| RunFrame / RunTick / FireDueFinalizes | priv | `:553/586/633` | frame timers, world_, results_, pendingSample_, frozenLastTick_ | — |
| RenderPieces, TickInstance, RenderGeometry, UpdateLights, RunTickActor, RunDrawUnpublished | priv | `:635,666,678,714,738,787` | live_, editor_ view, renderFrame_, tickNowMS_, warnings | — |
| QueueRegression / SoloInEditor / EndSoloInEditor / ShowRecipeSetInEditor | pub | `RegressionRequests.cpp:163/192/200/213` | applications_, forgets_, emissive, editor_ (in a task) | the same file `:116,139,147,223` |
| RegressionActors / ObserveRegression | pub | `:227/270` | live_, world_, applications_, forgets_ (under `requestLock`) | `RegressionWorld:1068`; ObserveRegression is internal |

`Manager.h` also holds types that move to their owners' headers: `RenderNotBuilt`, `HeartbeatReading`, `RenderFlags`, `RenderOutcome`, `PlacedRender`, `CommandResults`, `GatheredFacts`, `TickContext`, `RegressionWearer`, `WearerFacts`, the nested `CatalogMatches`, `Status` and `Snapshot`.

### 1c. The friend and the native test stubs

- **`RecipeEditor` (friend).** It holds `Manager &runtime_` (`RecipeEditor.h:105`) and uses exactly three private members: `EditorClockMS` (`RecipeEditor.cpp:61,72,78,92,100,141,236`), `PostTask` (about 20 sites) and `ChangeAndRebuildActors` (about 20 sites).
- **`tests/engine/animation/engine/Manager.h`.** It fakes `live_`, `applications_`, `animations_`, `forgets_`, `PostTask` and `Fire`, so that the real `ManagerAnimation.cpp` compiles natively (`cmake/Native.cmake:38`).
- **`tests/engine/platform/engine/Manager.h`.** It fakes `PostTask`, `EditorClockMS` and `ChangeAndRebuildActors` for the real `RecipeEditor.cpp` (`Native.cmake:43`).
- **`tests/engine/hooks/engine/Manager.h`.** It fakes `GetSingleton` and `OnFrame` for `Hooks.cpp`.

## 2. The proposed owners

The rule for every part: a record with free functions, and a class only where a lock or a worker must change together with the state. Each part gets a `ResetForLoad(Part&)`, and that function says what the part keeps across a load.

| Part (recommended / alternative) | State it holds | Public functions | Needs as explicit parameters |
|---|---|---|---|
| **`TickCoreState`** / `CoreWorld` (record) | `Orchestration::World world; TickClock clock;` | `void ResetForLoad(TickCoreState&)`. It resets the world and **keeps the clock**, which must stay monotonic. | — |
| **`TickInbox`** / `NextTickFacts` (record) | `CommandResults results; map<FormID,vector<ApplicationToken>> stale; set<FormID> forgets; optional<BudgetSample> memory;` | `void NoteStale(TickInbox&, RE::FormID, const std::vector<ApplicationToken>&)`; `void QueueForgets(TickInbox&, std::span<const RE::FormID>)`; `void DropActor(TickInbox&, RE::FormID)`; `CommandResults TakeResults(TickInbox&)`; `std::map<RE::FormID,std::vector<ApplicationToken>> TakeStale(TickInbox&)`; `std::set<RE::FormID> TakeForgets(TickInbox&)`; `std::optional<BudgetSample> TakeMemory(TickInbox&)`; `void ResetForLoad(TickInbox&)` | — |
| **`ResourceDisagreements`** / `AgreementLog` (record) | `map<FormID,vector<string>> lastFound; uint64_t reported;` | `void ReportDisagreement(ResourceDisagreements&, WarningHistory&, RE::FormID, Trace::Field, std::string)`; `void CheckAgreement(ResourceDisagreements&, WarningHistory&, const Orchestration::World&, const Orchestration::TickFacts&, const ActorResources&)`; `void ResetForLoad(ResourceDisagreements&)`, which **keeps `reported`** | WarningHistory, ActorResources |
| **`ActorResources`** / `InstalledActors` (record) | `unordered_map<FormID,LiveActor> live; CarriedTimes carriedTimes;` | `std::size_t RetireActorResources(ActorResources&, ApplicationService&, RE::FormID)`; `void ForgetActorResources(ActorResources&, ObservedActors&, TickInbox&, ApplicationService&, AnimationSubscriptions&, RE::FormID)`; `std::vector<Orchestration::ClaimedGeometry> TakeClaims(ActorResources&)`; `void ResetForLoad(ActorResources&)`, which retires every effect and then clears | ApplicationService, AnimationSubscriptions, ObservedActors, TickInbox |
| **`ObservedActors`** / `ActorObservations` (record) | `unordered_map<FormID,ModelTable> models; MaterialLayout layout; bool emissivePath; unordered_set<FormID> armorWithoutGeometryLogged;` | `bool EffectsAllowed(const ObservedActors&, const Settings&)`; `void NoteMaterialLayout(ObservedActors&, MaterialLayout)`; `void NoteArmorWithoutGeometry(ObservedActors&, const Actors::ActorFacts&)`; `void ResetForLoad(ObservedActors&)`, which clears the models and the log and **keeps the layout and `emissivePath`** | — |
| **`RenderPlanStore`** / `PlanBuilds` (record; replaces the anonymous `RenderStores` at `ManagerApply.cpp:640`) | `RenderPlanCache cache; PlanJobBook jobs; PlanQueue queue{BuildPlanFrom, kPlanJobCapacity};` | `std::vector<Orchestration::PlanJobDone> DrainPlanJobs(RenderPlanStore&, WarningHistory&)`; `void ForgetPlans(RenderPlanStore&)`; `void ResetForLoad(RenderPlanStore&)`, which also calls `DropQueued` | WarningHistory |
| **`PublishedSnapshots`** / `StudioMailbox` (**class**: mutex plus state) | lock, `latest`, `latestBudget`, the watched piece and document, `watchedMS`, `version` | `void Watch(const std::optional<Studio::PieceRef>&, std::string_view, std::uint32_t a_nowMS)`; `std::optional<WatchRequest> WatchedAt(std::uint32_t) const`; `std::optional<RE::FormID> InspectedActor(std::uint32_t) const`; `void Publish(SnapshotWithTextures)`, which bumps the version under the lock; `void PublishBudgetView(Studio::BudgetView)`; `void ResetForLoad(SnapshotWithTextures a_empty)`; `std::shared_ptr<const SnapshotWithTextures> Latest() const`; `std::shared_ptr<const Studio::BudgetView> LatestBudgetView() const` | — |
| **`EditorHost`** / `EditorReach` (record of functions; replaces the friend) | `std::function<void(SessionQueue::Task)> post; std::function<std::uint32_t()> clockMS; std::function<void(std::string, const std::function<void()>&)> changeAndRebuild;` | the constructor `RecipeEditor(EditorHost)` | — |
| **`MemoryHeartbeat`** / `HeartbeatCadence` (record) | `uint32_t lastMS; bool due; uint32_t holdingStep; VideoMemoryProbe videoMemory;` | `bool HeartbeatDue(const MemoryHeartbeat&, std::uint32_t)`; `HeartbeatReading TakeHeartbeat(MemoryHeartbeat&, TickInbox&, WarningHistory&, std::uint32_t a_nowMS, const Settings&, std::uint64_t a_plannedBytes)`, which also merges the duplicate at `Tick:598-605` with `Heartbeat:262-270`; `void EmitHeartbeat(MemoryHeartbeat&, const HeartbeatReading&, const ActorResources&, const Orchestration::World&, std::uint64_t a_disagreements, PublishedSnapshots&)`; `void ResetForLoad(MemoryHeartbeat&)`, which clears only `due` | TickInbox, ActorResources, World, PublishedSnapshots |
| **`FrameCadence`** / `FrameTimers` (record) | `uint64_t frame; uint32_t lastTickMS, lastSweepMS; bool frozenLastTick, textureLabStarted;` | `DueCadences DueAt(const FrameCadence&, std::uint32_t a_nowMS, std::uint32_t a_tickIntervalMS)`; `void ResetForLoad(FrameCadence&)`, which resets `lastTickMS` and `frozenLastTick` and keeps the rest. `tickNowMS_` goes away: `TickContext` gains `std::uint64_t frame` and already has `nowMS`. | — |
| `AnimationSubscriptions` (exists, keeps its name) | as today | the free functions in `RecipeEvents.h`: `void Fire(ActorResources&, RE::FormID, const EventRecord&)`; `void FireEquip(ActorResources&, RE::FormID)`; `void FireTrigger(ActorResources&, WarningHistory&, const Studio::FireTrigger&)`; `void DeliverAnimationEvent(ActorResources&, AnimationSubscriptions&, const AnimationEvent&)`; `void ReconcileAnimationEvents(AnimationSubscriptions&, const ActorResources&, RE::FormID)`; `void SweepAnimationEvents(AnimationSubscriptions&, const ActorResources&, const ApplicationService&)`; `std::vector<RE::FormID> ActorsToRetire(const AnimationSubscriptions&, const ActorResources&)`; `void ReportMeshProblemsOf(const ActorResources&, RE::FormID, std::string_view)` | ActorResources, ApplicationService |
| `ApplicationService` (opaque) | unchanged | the glue in `RecipeRebuild.h`: `void RebuildAfterRecipeChange(RenderPlanStore&, ApplicationService&, const ActorResources&, TickInbox&, const RecipeEditor&, std::string, const std::function<void()>&)`, which reads the view **after** the action; `std::vector<RE::FormID> LoadedActorIDs()`; `std::vector<RE::FormID> ApplicationActors(const ActorResources&)`; `std::vector<RE::FormID> ActorsApplying(const ActorResources&, const ApplicationService&, std::string_view)`; `void PrepareApplications(ApplicationService&, ActorResources&, RE::FormID, const std::vector<ApplicationToken>&)`; `void FinishApplications(ApplicationService&, RE::FormID, LiveActor&, bool)`; `void AbandonApplications(ApplicationService&, RE::FormID)` | — |

**Behaviour that becomes free functions over parts:**

- **`CommandExecutors.h`.** The executors move from `ManagerCommands.cpp`, plus `RunTickActor` and `RunDrawUnpublished`.
  - `CommandResults Execute(ExecutionParts&, const std::vector<Orchestration::Command>&, const TickContext&)`.
  - `ExecutionParts` is a stack-only record of references: `ActorResources&, ObservedActors&, RenderPlanStore&, ApplicationService&, AnimationSubscriptions&, TickInbox&, ResourceDisagreements&, WarningHistory&`.
  - Each `RunX` takes only the parts it needs. For example: `CommandResults RunSetAppliedLevel(ActorResources&, ResourceDisagreements&, WarningHistory&, const Orchestration::SetAppliedLevel&)` and `CommandResults RunKeepModels(ObservedActors&, const Orchestration::KeepModels&, const TickContext&)`.
- **`ActorInstall.h`** (from `ManagerApply.cpp`):
  - `Orchestration::CommandResult InstallLiveActor(ExecutionParts&, RE::Actor&, const Orchestration::Install&, const TickContext&)`
  - `Orchestration::RenderEvent BuildAwaitedRender(RenderPlanStore&, WarningHistory&, RE::FormID, LiveActor&, const InstallBound&, const Settings&)`
  - `void FailAwaitedRender(LiveActor&, const std::string&)`
  - `OutputFilter ShownOutputsOf(const Studio::View&)`
  - `LiveActorFor`, `BuildInstances`, `InstanceFor`, `CarryInstanceTime` and `PlaceInstances`, each taking `const Studio::View&` and `CarriedTimes&` or `RenderPlanStore&` explicitly.
- **`ActorTickRender.h`** (the render half of `ManagerTick.cpp`):
  - `std::vector<bool> RenderPieces(LiveActor&, RE::FormID, const RenderFlags&, const TickContext&, WarningHistory&)`
  - `void RenderGeometry(LiveActor&, LiveGeometry&, bool, bool, const RenderFlags&, const TickContext&, WarningHistory&)`
  - `void UpdateLights(LiveActor&, const std::vector<bool>&, const Studio::View&)`
  - `void TickInstance(LiveInstance&, float, float, const Studio::View&)`
- **`FactGathering.h`** (from `ManagerFacts.cpp`):
  - `GatheredFacts GatherFacts(ExecutionParts&, const TickCoreState&, const Studio::View&, std::optional<RE::FormID> a_inspected, std::uint32_t a_nowMS, std::uint64_t a_frame, const Settings&, std::vector<Orchestration::PlanJobDone>)`
  - `Orchestration::Sighting SightingOf(RE::FormID, const Settings&, std::optional<RE::FormID>)`
  - `Orchestration::StaleActor StaleActorOf(ObservedActors&, const Orchestration::World&, const ApplicationService&, const Studio::View&, RE::FormID, const RecipeCatalog&, const Settings&, std::uint32_t)`
  - `std::vector<Orchestration::ActorId> ForgetRequests(ExecutionParts&, const Orchestration::World&)`
  - `void EmitCoreRecords(...)`
- **`SnapshotBuild.h`** (from `ManagerSnapshot.cpp`):
  - `SnapshotWithTextures BuildSnapshot(const ActorResources&, const ObservedActors&, const RecipeEditor&, const ApplicationService&, const std::optional<Studio::PieceRef>&, std::string_view)`
  - `RuntimeStatus StatusOf(const ActorResources&, const ObservedActors&)`
  - `SnapshotWithTextures EmptySnapshot(const RecipeEditor&, const ApplicationService&)`
  - `Manager::Snapshot` becomes `SnapshotWithTextures` (alternative: `HeldSnapshot`). `Manager::Status` becomes `RuntimeStatus`.
- **`RegressionEdits.h`** (from `ManagerRegression.cpp` and the `Manager::` half of `RegressionRequests.cpp`):
  - `Regression::Activity RegressionActivity(const RecipeEditor&, const ApplicationService&, std::uint64_t, std::uint64_t, std::uint64_t)`
  - `std::uint64_t StartRegressionEdit(RecipeEditor&, const std::string&, float)`, and likewise for Gesture, Duplicate, Save and Delete
  - `void StartRegressionPaint(RecipeEditor&, const std::string&)`. The paint session counter moves beside the file-static `request` state.
  - `std::vector<WearerFacts> RegressionActors(const ActorResources&, const Orchestration::World&, const ApplicationService&, std::span<const RegressionWearer>)`
  - `void ObserveRegression(const TickInbox&, const ActorResources&, const Orchestration::World&, const ApplicationService&)`
  - `void DispatchRegression(std::uint64_t, ApplicationService&, TickInbox&, bool a_effectsAllowed)`

**Menu camera hold.** It moves to `menu/Menu.cpp`: a menu-owned `std::optional<IdleCameraHold>`, touched only inside the `OnGameThread` task that already exists.

## 3. The root

**Name.** I recommend **`EffectsRuntime`**. `RecipeEditor` already calls it `runtime_` / `a_runtime`, and it holds every running part of the effects. The alternative is `PluginRuntime`. I reject `Plugin` as too vague, and `EffectsSession` because the editor histories survive a load.

**Shape:**

```cpp
struct RuntimeParts {
  RuntimeParts();
  RuntimeParts(const RuntimeParts &) = delete;

  ApplicationService applications;
  AnimationSubscriptions animations;

  ActorResources resources;
  ObservedActors observed;
  RenderPlanStore plans;

  RecipeEditor editor;

  TickCoreState core;
  TickInbox inbox;
  ResourceDisagreements disagreements;

  FrameCadence cadence;
  MemoryHeartbeat heartbeat;
  WarningHistory warnings;

  PublishedSnapshots snapshots;
};

class EffectsRuntime {
public:
  [[nodiscard]] static EffectsRuntime *GetSingleton();

  void OnFrame();

  void BeginLoad();
  void FinishLoad();
  void SetEmissivePathEnabled(bool a_enabled);

  void MarkStale(RE::FormID a_actorID);
  void MarkStale(RE::Actor *a_actor);
  void QueueEquipFinalize(RE::FormID a_actorID);
  void QueueForget(RE::FormID a_actorID);
  void ReapplyAll();
  void RetireAll();

  void QueueEvent(RE::FormID a_actorID, EventRecord a_event);
  void QueueBroadcast(EventRecord a_event);
  void FireAt(Studio::FireTrigger a_trigger);
  void RequestMesh(RE::FormID a_actorID, std::string a_geometry);

  [[nodiscard]] RecipeEditor &Editor() noexcept;
  [[nodiscard]] PublishedSnapshots &Snapshots() noexcept;

  void FlushMetricsHeartbeat();
  void QueueRegression(std::uint64_t a_request);
  void SoloInEditor(std::string a_recipe);
  void EndSoloInEditor();
  void ShowRecipeSetInEditor(std::vector<std::string> a_recipes);
  [[nodiscard]] RegressionParts ForRegression() noexcept;

private:
  RuntimeParts parts_;
};
```

**How the root is laid out.**
- **Parts are grouped by blank lines** in this order: cross-thread queues; actor state; studio; tick core; frame and telemetry; mailbox. This order also preserves today's destruction order: the snapshot is destroyed first, the plan queue's worker before `live_`, and `applications_` last.
- **`RuntimeParts()` wires the three callbacks** to `this`:
  - the `applications` callback, which calls `NoteStale(inbox, …)`;
  - the `animations` callback, which posts `DeliverAnimationEvent`;
  - the editor's `EditorHost`, which posts through `applications`, reads `NowMS` and calls `RebuildAfterRecipeChange(parts…)`.
- **Public functions are grouped by blank lines** in the order shown above: frame; load; game-thread entry points (they post); events; studio access; regression.
- **Off-thread callers can reach only two things:** functions that post, and `Snapshots()` and `Editor()`, whose methods post or lock. The parts are private, so the type enforces the threading boundary.

**Data-driven load handling** (replaces the hand-written `Clear` at `Manager.cpp:99-154`).
- It is a table of `struct LoadReset { std::string_view part; void (*reset)(RuntimeParts &); };` held in a `constexpr std::array kLoadResets`, filled with captureless lambdas.
- The rows run in today's order: applications (generation), animations plus `ClearAnimEvents`, resources, core, inbox, disagreements, plans, editor file operations, retired material textures, editor paint, observed, warnings, compositor, texture lab, snapshots (`EmptySnapshot`), cadence, heartbeat.
- `BeginLoad` becomes: the begin trace; a loop over the table; the end trace with the actor count read before the loop.
- `FinishLoad` stays straight-line: trace, `Resume`, mark loaded actors stale. A table of two calls would hide more than it shows (Q10).

**Data-driven frame routing** (replaces the bodies of `OnFrame` and `RunFrame`).
- It is a table of `struct FrameStep { Cadence cadence; std::optional<Metrics::Section> timed; void (*run)(RuntimeParts &, const FrameMoment &); };` where `FrameMoment` holds `nowMS`, the settings copy and `TextureLab&`.
- **Every frame:** regression advance, heartbeat due, lab timings, retired-texture sweep, preview draws, equip finalizes, editor gesture, frame count.
- **Every second:** carried-time expiry, animation sweep.
- **Every tick:** the tick, mesh sweep (`kSweep`), snapshot publish (`kSnapshot`), regression observe, heartbeat emit.
- The texture-lab start and the loading guard stay as guards before the loop.
- **The tick itself stays a straight-line pipeline.** Data flows through it, so a table would hide that flow. Its phases are separated by blank lines:
  1. clock and drain plan jobs;
  2. compositor and lab sweeps;
  3. take the heartbeat;
  4. gather and check agreement;
  5. `Orchestration::Tick` and emit its records;
  6. build the context;
  7. previews, then execute.

**How callers reach it after the split:**

| Caller | After |
|---|---|
| `Hooks.cpp:23` | `EffectsRuntime::GetSingleton()->OnFrame()`. The hooks stub is renamed. |
| `Events.cpp`, `main.cpp` | the same entry points, under the new name |
| `menu/` | `Editor()` unchanged; `Snapshots().Watch(req, doc, NowMS())`, `.Latest()`, `.LatestBudgetView()`; the camera hold becomes menu-local |
| `RegressionRequests`, `RegressionRun` | the entry points as today |
| `RegressionWorld` | `ForRegression()` returns `{RecipeEditor&, const ActorResources&, const Orchestration::World&, const ApplicationService&}` (game thread only) and calls the free functions |
| `RecipeEditor` | `EditorHost` only. It no longer includes or names the root. |

**Dependency graph.** Arrows mean "a function over the left side needs the right side":

```
FrameSteps/RunTick ─> plans ─> gather{inbox, core, resources, observed, applications, animations, snapshots.InspectedActor, editor.view}
                     ─> disagreements{resources} ─> Orchestration::Tick(core.world)
                     ─> executors{resources, observed, plans, applications, animations, inbox, disagreements, warnings}
heartbeat ─> {resources, core.world, disagreements.reported, snapshots, inbox.memory}
snapshot build ─> {resources, observed, editor, applications} ─> snapshots
regression ─> {editor, applications, resources, core.world, inbox}
ForgetActorResources (in ActorResources.cpp) <─ executors and gather
```

**Cycles and how each is broken.**

| Cycle | How it is broken |
|---|---|
| (a) editor ↔ root: the rebuild reads the editor's view | `EditorHost`, a record of functions bound by the root. There is no longer a type cycle and no friend. |
| (b) `applications` → inbox | a callback set in the constructor, as today |
| (c) `animations` → `applications.Post` → task → `animations` | runtime only, not a type cycle |
| (d) gather → `ForgetActorResources` | the function moves to `ActorResources.cpp`, so gather and the executors both depend on it and not on each other |

No cycle remains between part headers.

## 4. Member → owner

| Today | Goes to |
|---|---|
| world_, tickClock_ | `TickCoreState` |
| results_, stale_, forgets_, pendingSample_; NoteStale | `TickInbox` |
| disagreements_, disagreementCount_; CheckAgreement, ReportDisagreement | `ResourceDisagreements` |
| live_, carriedTimes_; RetireActorResources, ForgetActorResources, ClaimsOf→TakeClaims | `ActorResources` |
| models_, materialLayout_, emissivePathEnabled_, loggedNonPBRArmor_; NoteMaterialLayout, NoteArmorWithoutGeometry | `ObservedActors` |
| renderPlans_, planJobs_, planQueue_; DrainPlanJobs | `RenderPlanStore` |
| snapshot mailbox members, kWatchWindowMS; Watch, Latest*, PublishBudgetView, InspectedActor | `PublishedSnapshots` |
| BuildSnapshot, PublishStatus, PublishPieces, PublishDocument, DocumentRowFor, GetStatus | `SnapshotBuild` |
| PublishSnapshot | a frame step: `WatchedAt` → `BuildSnapshot` → `Publish` |
| lastMetricsMS_, heartbeatDue_, holdingStep_, videoMemory_; SampleBudget, EmitHeartbeat, ReportRenderMemory, RenderedTargets | `MemoryHeartbeat` |
| FlushMetricsHeartbeat | root entry, which calls `TakeHeartbeat` and `EmitHeartbeat` |
| renderFrame_, lastTickMS_, lastSweepMS_, frozenLastTick_, textureLabStarted_ | `FrameCadence` |
| tickNowMS_ | deleted; replaced by `TickContext::nowMS` |
| animations_; QueueAnimationEvent, Reconcile, Sweep, Fire, FireEquip, FireAt, RequestMesh | `AnimationSubscriptions` plus `RecipeEvents` |
| QueueEvent, QueueBroadcast, RetireAll | root entries (they post) |
| editor_, PostTask, EditorClockMS | `RuntimeParts::editor` plus `EditorHost` |
| ChangeAndRebuildActors, ApplicationActors, LoadedActorIDs, ActorsApplying, Prepare/Finish/AbandonApplications | `RecipeRebuild` (glue around the opaque applications) |
| Execute, all RunX, InstallLiveActor, BuildAwaitedRender, FailAwaitedRender, LiveActorFor, BuildInstances, InstanceFor, CarryInstanceTime, PlaceInstances, ShownOutputs | `CommandExecutors` / `ActorInstall` |
| RenderPieces, RenderGeometry, UpdateLights, TickInstance | `ActorTickRender` |
| GatherFacts, SightingOf, StaleActorOf, ForgetRequests, EmitCoreRecords | `FactGathering` |
| warningHistory_ | `RuntimeParts::warnings`, passed by reference |
| OnFrame, RunFrame, RunTick, FireDueFinalizes | root plus `FrameSteps.cpp` |
| Clear, BeginLoad, FinishLoad, MarkLoadedActorsStale | root plus `kLoadResets` |
| SetEmissivePathEnabled, MarkStale, QueueEquipFinalize, QueueForget, ReapplyAll | root entries |
| menuIdleHold_; Hold/ReleaseIdleCameraForMenu | `menu/Menu.cpp` |
| regressionPaintSession_, Regression* methods, QueueRegression, Solo*, ShowRecipeSet*, RegressionActors, ObserveRegression | `RegressionEdits` plus `RegressionRequests` file state; entries stay on the root |
| CatalogMatches, HeartbeatReading, RenderFlags, RenderOutcome, PlacedRender, GatheredFacts, TickContext, WearerFacts, RegressionWearer | the headers of their owners |

**Hard to place, with a recommendation each:**
- **`ChangeAndRebuildActors`** touches five parts. Make it a free function with explicit parts, bound into `EditorHost`. It must read the view after `a_action` runs, because the action can change the view.
- **`ClaimsOf` and `ForgetRequests`** have side effects during gather (they retire geometries and forget unknown actors at once). Keep the behaviour, give them names that say so (`TakeClaims`), and record moving them under core commands as a follow-up (Q6).
- **`ReportDisagreement`** is shared by gather and the executors, so it gets its own record.
- **`RenderPieces`** reads `InspectedActor(NowMS())` inside the tick. Use `TickContext::inspected` instead; the 1 s window makes the difference negligible (Q7).
- **`ShownOutputs`** captures `this`. The new version captures `const Studio::View&`. That is safe only because it is used synchronously inside the tick; it must never be stored.

## 5. Threading

| Thread | Touches | Lock |
|---|---|---|
| Game thread (`PlayerCharacter::Update`, SKSE tasks) | every part | none needed; tasks are serialised by `SessionQueue` |
| Render / ImGui thread (menu) | `Snapshots()` (Watch, Latest, LatestBudgetView); `Editor()` methods that post, plus `fileOperations_` | `PublishedSnapshots` mutex (moved from `snapshotLock_`); `FileOperationJournal` lock (unchanged); `SessionQueue` lock |
| Animation graph sinks (any thread) | the `AnimationSubscriptions` callback, which posts | `sourceLock_` (unchanged); `SessionQueue` |
| Plan worker | `RenderPlanStore::queue` internals | `JobQueue::shared_->lock` (moves with the record) |
| SKSE messaging / event sinks | root entries that post or mark stale | `SessionQueue` |
| Regression | runs on the game thread through `AdvanceRegressionRun`; `requestLock` is still taken around `RegressionActors` and `ObserveRegression` | `requestLock` (unchanged, file-static) |

One fix rides along: `snapshotVersion_` is bumped outside the lock today (`Snapshot:470`, `Manager.cpp:139`). It moves inside `Publish` and `ResetForLoad`, under the lock.

Rule to keep: no posted task captures `ExecutionParts`, a `LiveActor&`, an iterator or a view reference. Tasks capture a `FormID` and look it up when they run, as today.

## 6. Order of work

Each step leaves the Windows build and the native tests green. Use `tools/rename.py` for renames and `python3 tools/compile-db.py` after adding files.

| Step | Work | Native tests | In-game check |
|---|---|---|---|
| 1 | Add `EditorHost`; `RecipeEditor(EditorHost)`; drop the friend. The root builds the host. Delete `tests/engine/platform/engine/Manager.h`; the editorintegration fixture builds an `EditorHost` from its queue and clock. | `engine_editorintegration` passes unchanged | studio: edit, undo, save, rename; recipe file written |
| 2 | Add the `PublishedSnapshots` class; `Manager` delegates; the menu calls `Snapshots()`. | new `engine_publishedsnapshots`: watch window expiry at 1000 ms, version rises across a reset, InspectedActor expiry. If `TextureRef` will not compile natively, extract a pure `WatchOpen(watchedMS, now)` and test that. | Studio, Recipes and Setup pages update; the budget view shows |
| 3 | Add the records `TickCoreState`, `TickInbox`, `ResourceDisagreements`, `ObservedActors`, `RenderPlanStore`, `MemoryHeartbeat` and `FrameCadence`, each with `ResetForLoad`. Bundle them into `RuntimeParts`. `Clear` becomes `kLoadResets`. | `engine_tickinbox` (stale overwrite, `DropActor`, `Take*` leave it empty, reset); `engine_framecadence` (cadences, uint32 wrap, reset keeps `frame`); `engine_disagreements` (the same findings are not re-reported, the count survives a reset) | load a save twice: `cleared N actor states`, `load clear_begin/clear_end` traces, no crash on new game |
| 4a | Executors become free functions (`CommandExecutors.cpp`, `ExecutionParts`) | extend the lifetime-stub suites: missing actor gives a disagreement and no crash; `ForgetActorResources` empties the inbox entries | zero `resource_disagreement` traces in normal play |
| 4b | `ActorInstall.cpp` (from `ManagerApply`) and `ActorTickRender.cpp` (from the render half of `ManagerTick`); `TickContext` gains `frame` | `engine_installstages` still passes | equip and unequip enchanted armor; `actor_install` and `installed` traces; first draw, resize and plan-job traces |
| 5 | `RecipeEvents.cpp` (Events, Animation, Inspection). The animation suite switches from the Manager stub to a stub `ActorResources.h` on its include path. | `engine_animationsubscriptions` | footstep or animation-driven recipe fires; plugin event fires; studio fire button |
| 6 | `FactGathering.cpp`; `FrameSteps.cpp` with the frame table and the tick pipeline | `ForgetRequests` split (known vs unknown) against `Orchestration::World` | heartbeat `metrics` once a second; `texture_lab_start` once |
| 7 | `SnapshotBuild.cpp`; `RuntimeStatus`, `SnapshotWithTextures` | — | overlay counts match |
| 8 | `RegressionEdits.cpp`; `ForRegression()`; paint session counter moves to regression state | `engine_regressionrequest` | full unattended regression suite; compare per-case trace action counts with baseline run 20261010T174638 |
| 9 | Menu camera hold moves to menu | — | open and close the menu: the idle camera holds and releases |
| 10 | Rename `Manager` → `EffectsRuntime` (`tools/rename.py`); hooks stub to `EffectsRuntime.h`; `Native.cmake:38,43`; `docs/components/engine.md` ("The manager", the table at 395-420); the `REFERENCE.md` heading; the README reading order | all of `ctest --preset native` | `hooked PlayerCharacter::Update` and one full suite run |

**Risks:**
- **Clock and counters across a load.** The tick clock must stay monotonic, and `reported`, `frame` and `holdingStep` must not reset. Each `ResetForLoad` states what it keeps, and the step-3 tests check it.
- **Member order.** Destruction order matters: the PlanQueue worker joins, `LiveActor` destructors touch engine objects, and the snapshot holds `NiPointer`s. Keep the relative order given in section 3.
- **Callbacks wired in the constructor capture the parts' address.** `RuntimeParts` must not move; copying and moving are deleted.
- **Losing the friend.** Any undiscovered private use breaks the build loudly; I grepped and found only the three uses listed.
- **Small differences in trace and section timing** from the frame table, for example when `AdvanceRegressionRun` runs relative to the `kFrame` timer. Keep the timer scopes the same, so the trace baseline comparison still holds.

## 7. Open questions, each with a recommendation

1. **Root name.** `EffectsRuntime` or `PluginRuntime`? I recommend `EffectsRuntime`.
2. **Singleton accessor.** Keep `GetSingleton()` returning a pointer through steps 1-9, then switch to `Instance()` returning a reference in step 10. A reference removes the dead null checks.
3. **Camera hold.** I recommend moving it to `menu/`.
4. **`loggedNonPBRArmor_`.** Keep it as a set in `ObservedActors`. `Severity` has no info level, so do not stretch `LogOnce`.
5. **A trace line per load-reset row?** Not now; it would break the trace baseline. Maybe later.
6. **Side effects during gather** (`TakeClaims`, the immediate forgets). Keep them for behaviour parity, and add a follow-up to turn them into core commands under the boundary rule.
7. **Inspected actor during render.** Read it from `TickContext`.
8. **Executor parameters.** The dispatcher takes `ExecutionParts`; each executor takes only its parts.
9. **Regression access.** Use the `ForRegression()` record rather than eight wrappers on the root.
10. **`FinishLoad` as a table?** No, keep it straight-line.
11. **Coordinating with the ApplicationService cleanup.**
    - This plan touches every `applications_` call site but changes none of its API.
    - All posting goes through one root function and through `EditorHost::post`. If the other agent moves `Post` out of `ApplicationService`, that is a one-line change here.
    - The apply callback's shape is bound in one place, `RuntimeParts()`.
    - Ask them whether `Prepare/Finish/AbandonApplications`, `ActorsApplying` and `ChangeAndRebuildActors` become theirs. If yes, skip `RecipeRebuild.cpp`.
    - Sequencing: do steps 1-3 now, and freeze or land their API before steps 4 and 8.
