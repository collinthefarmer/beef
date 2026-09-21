# engine/

The game-facing layer. It hooks CommonLibSSE/SKSE events and a per-frame
vfunc, tracks which actors wear which **pieces**, matches **recipes** to
those pieces, ticks their **signals**, and drives the render layer to paint
the result. It also owns the `RecipeStore`/`RecipeEditor` pipeline the menu
edits recipes through. Most of it is engine-facing and not native-tested.
Three engine-free units are compiled into the native suite
(`tests/run-native.sh`): `ApplicationService.cpp`, `SessionQueue.cpp`,
`TextFile.cpp`.

## What it owns

- Event and hook wiring: `RegisterEventSinks` and `InstallHooks` register
  the sinks and the per-frame hook that feed the manager.
  `WatchAnimationEvents` attaches and detaches the per-actor animation
  sink.
- The manager: `Manager` is the singleton. It queues refreshes and
  retirements per actor, ticks every applied actor each frame, and
  publishes the **snapshot** the menu reads.
- Actor and worn-piece tracking: for each actor with rendering enabled, the
  manager collects the equipped pieces, matches recipes against them, and
  keeps the result — signals, **bindings**, **placements** — in a
  `LiveActor`.
- The CRUD pipeline: `RecipeStore` holds the loaded `Recipe` list and does
  the file I/O. `RecipeEditor` is the gesture/edit/undo layer the studio
  **intents** call through; it rebuilds every actor an **edit** could
  affect before it reports the edit done.

## Data

### Event and hook wiring, the manager

`RegisterEventSinks` and `InstallHooks` run once at plugin load. The sinks
and the hook they install forward game events into `Manager`, the singleton
that owns every applied actor. `Manager` hands per-actor queueing to
`ApplicationService` and publishes the **snapshot** the menu reads.

| Member | Description | Declared in |
|---|---|---|
| `RegisterEventSinks` | A free function. Registers the equip, load, hit, node-update and animation sinks that call `Manager::QueueRefresh` and its siblings. | `Events.h` |
| `InstallHooks` | A free function. Installs the `PlayerCharacter::Update` vfunc hook that calls `Manager::OnFrame` every frame. | `Hooks.h` |
| `Manager` | The singleton. It queues refreshes and retirements per actor, holds one `LiveActor` per applied actor, and ticks them each frame. | `Manager.h` |
| `Manager::Status` | The counts the debug overlay shows: actors, **pieces**, **recipes**, geometries, shells, lights, and the tick cost in ms. | `Manager.h` |
| `Manager::Snapshot` | A `Studio::Snapshot` plus a vector of `TextureRef`s. | `Manager.h` |
| `ApplicationService` | Tracks one `ApplicationRecord` per in-flight, recipe-affecting change. It keys its `ApplicationRecord` stores by recipe id and by actor id. | `ApplicationService.h` |
| `ApplicationRecord` | One tracked change: its `ApplicationToken`, its phase, and the per-actor phases and problems. | `studio/ApplicationRecord.h` |
| `ApplicationToken` | The lookup handle `Find` takes. It names one change by recipe id, revision, actor id and attempt. | `studio/ApplicationRecord.h` |
| `SessionQueue` | Serialises per-actor refresh tasks onto the SKSE task interface. It also holds the equip-finalize timers and the load gate (`BeginLoad`/`Resume`). | `SessionQueue.h` |

### Texture-memory controls

Two controls cap texture memory for large crowds. Distance eviction keeps
the applied working set near the player. Per-slot sizing shrinks each
stack's render target below the material's native size. The render
`Compositor` also shares one target across actors, but that is render/'s
concern.

| Member | Description | Declared in |
|---|---|---|
| `Manager::SweepEviction` | Runs once a second from `OnFrame`. It retires each applied non-player actor past `Settings::evictDistance` into `evictedForDistance_`, and `QueueRefresh`es an evicted actor that has closed back inside the hysteresis band. | `Manager.h` |
| `Manager::evictedForDistance_` | The set of actor ids held out for distance. `Manager::Refresh` skips a far actor into it and drops one when it applies. `Manager::Clear` empties it on save-load teardown. | `Manager.h` |
| `EvictionFor` / `EvictionAction` | The pure decision. It returns `kEvict` past the distance, `kRestore` once the actor is back inside 80% of it (the hysteresis band), `kNone` otherwise. | `planners/Eviction.h` |
| `Settings::evictDistance` | The eviction radius in game units. 0 disables eviction; `kMaxEvictDistance` (20000) caps it. | `Settings.h` |
| `SlotStackSize` | Scales a stack's runtime target down per slot: `a_base` pixels divided by `ResolutionDivisor` of the slot's `Resolution`. `PrepareChainStacks` calls it before `Compositor::Prepare`. | `ManagerApply.cpp` |
| `DefaultSlotResolution` / `ResolutionDivisor` | The per-slot default `Resolution` and its divisor (`kFull` 1, `kHalf` 2, `kQuarter` 4). | `recipe/Recipe.h` |
| `SurfaceOutput::resolution` | A recipe output's optional `Resolution`. When set it overrides `DefaultSlotResolution` for that output's slot. | `recipe/Recipe.h` |

### Actor and worn-piece state

All eight records are declared in `LiveActor.h`. `Manager::Refresh` fills
one `LiveActor` per applied actor from the equipped **pieces** and the
matched **recipes**. Each frame the tick advances each **instance**'s
**signals** in place and renders each bound **geometry** through its
**bindings**.

| Record | Description |
|---|---|
| `LiveActor` | One applied actor: its handle, its `ActorPlan`, its **pieces**, **instances** and **placements**, and the `ApplicationToken`s of its in-flight applications. |
| `LivePiece` | One worn armor **piece**: the armor form id and name, the enchantment form id, and its `LiveGeometry`s. |
| `LiveGeometry` | One bound **geometry** of a piece: the engine geometry and shader-property pointers, its `GeometryInputs`, its `MaterialBinding` and `ShellBinding`, the plans that placed it, and its `PlacementId`s. Its `lost` flag marks it for `DropLostGeometries`. |
| `LivePieceId` | A typed index (`enum class`, `std::size_t`). Other code uses it to reference one piece inside its `LiveActor`. |
| `LiveInstance` | One **recipe** matched onto the actor: the `Recipe` pointer, the enchantment, the priority, and its `SignalGraph`, `SignalState` and `ActorEnvironment`. It also holds the optional `LightBinding` and the timing (`startMS`, `lastTime`) the tick advances. |
| `LivePlacement` | The **outputs** bound onto one geometry: a `GeometryId` and one `PlacedOutput` per placed output. |
| `PlacedOutput` | One placed **output**: its index in the recipe, its `RenderedStack`, and its `active`, `rendered` and `renderFailed` flags. Its `problem` string carries the failure text. |
| `ResolvedPlacement` | The placement-index and instance-index pair `ResolvePlacement` returns for a `PlacementId`. |

### CRUD and results

The free functions in `RecipeStore.h` own the loaded `Recipe` list and its
file I/O. `RecipeEditor` is the layer the studio **intents** call through
for every **edit**. Both report through small result records the menu
reads back.

| Member | Description | Declared in |
|---|---|---|
| `RecipeStoreStatus` | The load counts: loaded, with errors, held back, unresolved, imported. `LoadRecipes` and `GetRecipeStoreStatus` return it. | `RecipeStore.h` |
| `RecipeOrigin` | One recipe's source: its file path and its load `Diagnostic`s. `OriginOf` returns it. | `RecipeStore.h` |
| `RecipeEditor` | The class that drives gestures, **edits**, undo/redo, save/reload, paint sessions and view commands. It rebuilds every actor an edit could affect before it reports the edit done. | `RecipeEditor.h` |
| `Studio::FileOperationResult` | The outcome of one save or revert: the request id, the recipe, the `FileAction`, the `FileOperationState`, the written path, and the error `Diagnostic` if it failed. | `studio/FileOperation.h` |
| `Studio::RecipeEditResult` | The outcome of one edit request: the request id, the recipe, and the error `Diagnostic` if it failed. | `studio/EditResult.h` |

## How an actor and a recipe edit flow

```
(a) a game event reaches render

RE::TESEquipEvent ──▶ EventSink::ProcessEvent          Events.cpp
  │
  ▼
Manager::QueueRefresh ──▶ ApplicationService::Refresh   Manager.cpp / ApplicationService.cpp
  │        (queues a SessionQueue task onto the SKSE task interface)
  ▼
Manager::RunRefresh ──▶ Manager::Refresh                ManagerApply.cpp
  │   CollectPieces / MatchRecipes / PlaceInstances
  │   (Refresh skips an actor past Settings::evictDistance into
  │    evictedForDistance_, and drops one from the set when it applies)
  ▼
PlaceInstances ──▶ PlaceOnGeometry ──▶ PrepareChainStacks   ManagerApply.cpp
  │   SlotStackSize scales each slot's runtime target down
  │   (ResolutionDivisor of the slot's Resolution) before Compositor::Prepare
  ▼
applied_[actorID] = LiveActor                           (held on Manager)

PlayerCharacter::Update hook (every frame)              Hooks.cpp
  │
  ▼
Manager::OnFrame ──▶ Manager::Tick                      ManagerTick.cpp
  │   TickInstance advances each LiveInstance's signals
  │
  ├─(once a second) Manager::SweepEviction              ManagerTick.cpp
  │      retires an applied non-player actor past Settings::evictDistance
  │      into evictedForDistance_ (evict_far retire trace), and
  │      QueueRefreshes an evicted actor back inside the hysteresis band
  ▼
RenderPieces ──▶ RenderGeometry ──▶ MaterialBinding/ShellBinding   ManagerTick.cpp, render/Binding.cpp

(b) a menu edit reaches an applied recipe

studio Intent ──▶ applier in menu/Menu.cpp ──▶ RecipeEditor::EditRecipe   Menu.cpp, RecipeEditor.cpp
  │   (IntentPerformer calls Manager::Editor().EditRecipe; ApplyEdits runs on the posted task)
  ▼
MutableRecipe(id) ──▶ Studio::PrepareEdits              RecipeStore.cpp, studio/Edits.cpp
  │        (look up the loaded Recipe; build the proposed one)
  ▼
Manager::ChangeAndRebuildActors                          ManagerApplication.cpp
  │   retires every actor the recipe could affect, then runs the injected
  │   action (RecipeEditor::ApplyEdits' lambda moves the prepared Recipe
  │   into place, RecipeEditor.cpp), then re-refreshes those actors
  ▼
RefreshRecipeDerivedState(id)                            RecipeStore.cpp
  │        (invalidates the cached SignalGraph for lazy rebuild,
  │         re-validates, re-resolves forms)
  ▼
re-Manager::Refresh of the retired actors ── rejoins (a): MatchRecipes
         reads the edited Recipe back out of LoadedRecipes()
```

## The files

| Concern | Key files |
|---|---|
| Event sinks and hooks | `Events.h`/`.cpp` (equip, load, hit, node-update, animation sinks), `Hooks.h`/`.cpp` (the `PlayerCharacter::Update` vfunc hook) |
| The manager | `Manager.h`, `Manager.cpp` (construction, load/clear), `ManagerApplication.cpp` (`ChangeAndRebuildActors`, application bookkeeping), `ManagerApply.cpp` (`Refresh`/`Retire`, `CollectPieces`, `MatchRecipes`, `PlaceInstances`, `PrepareChainStacks`/`SlotStackSize`), `ManagerEvents.cpp` (`Fire`/`FireAt`/`QueueEvent`), `ManagerInspection.cpp` (the `RequestMesh` debug probe), `ManagerSnapshot.cpp` (`GetStatus`, `BuildSnapshot`, `PublishSnapshot`, `Watch`), `ManagerTick.cpp` (`OnFrame`, `SweepEviction`, `Tick`, `RenderPieces`, `RenderGeometry`, `UpdateLights`) |
| Application and session bookkeeping | `ApplicationService.h`/`.cpp` (per-recipe `ApplicationToken` tracking, rejection handling), `SessionQueue.h`/`.cpp` (per-actor task serialisation onto the SKSE task interface) |
| Actor and worn-piece state | `LiveActor.h`/`.cpp` (`LiveActor`, `LivePiece`, `LiveGeometry`, `RetireGeometry`, `ResolvePlacement`), `Environment.h`/`.cpp` (`ActorEnvironment`, the `SignalEnvironment` a `LiveInstance` ticks against) |
| Recipe CRUD | `RecipeStore.h`/`.cpp` (load, save, mutate, `RefreshRecipeDerivedState`), `RecipeEditor.h`/`.cpp` (gestures, edits, undo/redo, paint sessions, view commands) |
| Engine form and game-object lookups | `EngineForms.h`/`.cpp` (`FormKeyFor`/`LookupForm`, effect-shader records), `GameObjectService.h`/`.cpp` (game-object and anim-event catalogs), `InputCatalog.h`/`.cpp` (actor-value samples for the studio input pickers), `Tweaks.h`/`.cpp` (`EditorIdOf`, xEdit-tweaks availability) |
| Small utilities | `Clock.h`/`.cpp` (`NowMS`), `TextFile.h`/`.cpp` (bounded file read and write) |

## See also

- `REFERENCE.md` → *Engine events, hooks and the manager*, *Bindings*,
  *Recipe CRUD travels one pipeline* — the engine-layout facts and CS
  decompile notes these files cannot state.
- `docs/conventions.md` — the `Reporter`/`Diagnostic` and JSON-boundary
  contracts `RecipeStore` and `RecipeEditor` reuse from `recipe/`.
