# engine/

The game-facing layer. It hooks CommonLibSSE/SKSE events and a per-frame
vfunc, tracks which actors wear which **pieces**, matches **recipes** to
those pieces, ticks their **signals**, and drives the render layer to paint
the result. It also owns the `RecipeStore`/`RecipeEditor` pipeline the menu
edits recipes through. Most of it is engine-facing and not native-tested.
Seven engine-free units are compiled into the native suite
(`ctest --preset native`): `ApplicationService.cpp`, `SessionQueue.cpp`,
`TextFile.cpp`, `PluginEvents.cpp`, `RecipeFiles.cpp`, `InstanceTime.cpp`,
`MenuDependency.cpp`.

The `engine_editorintegration` native target also compiles the actual
`RecipeEditor.cpp` and `RecipeStore.cpp` with isolated platform test doubles.
It executes their queued callbacks and real filesystem operations, including
history, dirty state, load cancellation and import-promotion routing. It does
not simulate actor rebuilds or Skyrim form discovery; see
[the harness boundary](../build.md#native-authoring-integration).

## What it owns

- Event and hook wiring: `RegisterEventSinks` and `InstallHooks` register
  the sinks and the per-frame hook that feed the manager.
  `WatchAnimationEvents` attaches and detaches the per-actor animation
  sink.
- The manager: `Manager` is the singleton. It queues refreshes and
  retirements per actor, ticks every applied actor each frame, and
  publishes the **snapshot** the menu reads.
  Its stack-warning history has fixed key/text budgets and resets with
  `Clear`; logging suppression never removes the underlying diagnostics.
- Actor and worn-piece tracking: for each actor with rendering enabled, the
  manager collects the equipped pieces, matches recipes against them, and
  keeps the result — signals, **bindings**, **placements** — in a
  `LiveActor`.
- The CRUD pipeline: `RecipeStore` holds the loaded `Recipe` list and does
  the file I/O. `RecipeEditor` is the gesture/edit/undo layer the studio
  **intents** call through; it accepts document changes and queues actor
  rebuilds. Application records separately report preparation and rendering.

## Data

### Event and hook wiring, the manager

`RegisterEventSinks` and `InstallHooks` run once at plugin load. The sinks
and the hook they install forward game events into `Manager`, the singleton
that owns every applied actor. `Manager` hands per-actor queueing to
`ApplicationService` and publishes the **snapshot** the menu reads.

| Member | Description | Declared in |
|---|---|---|
| `RegisterEventSinks` | A free function. Registers the equip, load, hit, node-update and animation sinks that call `Manager::QueueRefresh` and its siblings. | `Events.h` |
| `ParsePluginEvent` | A free function, engine-free. Validates another plugin's `PluginEventMessage` (version, actor, bounded id, type tag, finite components) and converts it to a typed `EventRecord` on the plugin channel; `main.cpp`'s listener queues the result per actor or as a broadcast. | `PluginEvents.h` |
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
| `LivePiece` | One collected worn armor clone: the armor form id and name, optional addon `FormKey`, enchantment form id, and its `LiveGeometry`s. The addon comes from the biped entry owning that clone and is copied to each planner geometry for surface and light selectors. |
| `LiveGeometry` | One bound **geometry** of a piece: the engine geometry and shader-property pointers, its `GeometryInputs`, its `MaterialBinding` and `ShellBinding`, the plans that placed it, and its `PlacementId`s. Its `lost` flag marks it for `DropLostGeometries`. |
| `LivePieceId` | A typed index (`enum class`, `std::size_t`). Other code uses it to reference one piece inside its `LiveActor`. |
| `LiveInstance` | One **recipe** matched onto the actor: the `Recipe` pointer, the enchantment, and its `SignalGraph`, `SignalState` and `ActorEnvironment`. It also holds the optional `LightBinding`, light eligibility/replacement/preparation diagnostics, and the timing (`startMS`, `lastTime`) the tick advances. |
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
| `CarriedTimes`, `CarriedTimeKey` | Bounded continuation store keyed by actor, recipe name, and enchantment form ID. Holds only phase and retirement time; consumed entries, expired entries, and old sessions release their state. | `InstanceTime.h` |
| `RecipeEditor` | Drives gestures, **edits**, undo/redo, save/reload, paint sessions and view commands. An edit result acknowledges the document change; separate application records track queued actor rebuilds through preparation and rendering. | `RecipeEditor.h` |
| `Studio::FileOperationResult` | The outcome of one save or revert: the request id, the recipe, the `FileAction`, the `FileOperationState`, the written path, and the error `Diagnostic` if it failed. | `studio/FileOperation.h` |
| `Studio::RecipeEditResult` | The outcome of one edit request: the request id, the recipe, and the error `Diagnostic` if it failed. | `studio/EditResult.h` |

## How an actor and a recipe edit flow

`RecipeStore` keeps mutable documents separately from the published
`LoadedRecipes()` vector. Republication rebuilds that whole vector, including
when saving. `LiveInstance::recipe` and prepared stack layer pointers borrow
from it. `ChangeAndRebuildActors` therefore retires all applied actors before
running its synchronous mutation callback, then queues refreshes. The report
recipe ID scopes diagnostics, not the set of actors retired. A recipe pointer,
span, or planner recipe index must not survive republication; deferred editor
requests capture owned IDs and edits and look documents up when executed.

`ActorPlan` owns instance identity and deduplication. The live instance vector
has one slot per planned instance and retains empty slots if preparation
fails, so placements always use the same indices in both tables.

Save-load teardown invalidates the `SessionQueue` generation and retires
actor effects before canceling gestures or removing the transient paint
recipe. Posted callbacks borrow the singleton manager/editor; pending result
objects retain their journal and report cancellation if queued work is
discarded. This relies on game-thread serialization; the generation check
does not interrupt a callback already running.

`RecipeOperations.h` holds the engine-free operation journal and the pending
file/edit/gesture task guards used by `RecipeEditor`. Its snapshot readers are
also the editor's readers: pending edits are hidden, file operations expose
their pending state, and completed or canceled results cannot be overwritten
by late completion. Each file/edit list retains at most 64 results and only
the latest request per recipe. Gesture publication also requires an active
matching request, so cancellation cannot be undone by a late publication.
Native tests combine these production guards with `SessionQueue` to exercise
load, rejected submission, dropped callbacks, and queue destruction.

An accepted edit is not a saved file or a rendered result. File-operation
results report persistence, while application records report queued,
prepared, rendered, unmatched, failed, or canceled actor work. Even a rendered
record is adapter evidence, not proof of the pixels displayed in game.

Actor retirement reports unfinished tokens retained by that live actor as
unmatched before destroying its effects. This includes distance eviction before
the first render. Reporting captured tokens preserves newer rebuild attempts:
the application service checks both revision and actor attempt. Explicit unload
also finishes currently pending work when no live actor exists; load cancellation
remains canceled. Completed render/failure results are not rewritten by retirement.

Ownership loss retires a geometry's bindings before releasing its inputs and
prepared placement stacks. Placement indices and output diagnostics remain,
while sibling geometry resources and shared instance lights are preserved.
External texture leases remain valid until their consumers release them. Cache
maintenance runs from `OnFrame` outside the nonempty actor tick guard, so the
last actor's retirement cannot stop mesh/material expiration or weak-key pruning.

Application history retains the newest 256 terminal recipe revisions alongside
all records with unfinished actors. The existing independent terminal actor
history cap is also 256. Recipe pruning checks individual actor phases: an
aggregate failure can still contain queued/prepared work. Supersession carries
pending and failed actors into the replacement, together with the manager's
current loaded/applied actors; successful or unmatched historical wearers do
not accumulate across edits. Retry targets in pruned terminal records are no
longer retained, but current manager candidates are still discovered normally.
Cancellation releases actor lists and old problem text, preserving the token
and canceled phase for publication. Resume removes canceled history; held
snapshots remain independent values. These are history limits, not a cap on the
active workload or the size of one current request's actor list.

Recipe save calls engine-free `WriteRecipeFile` with the selected destination
and explicit import-promotion flag. Existing files under `recipes/user/` retain
their destination, including nested paths. Every source outside that folder saves
to `recipes/user/<id>.json`, preserving shipped and imported source files. Only
an imported-folder source has its import metadata cleared. The store adopts the
user destination only after a successful write; later revert/delete use that
adopted destination. An existing user destination is replaced by the requested
save using the failure-safe writer.

Save prepares a copy, removes temporary paint
masks/references, and returns the exact saved document only after writing
succeeds. The store then publishes that document, destination, and saved
baseline together. A refusal leaves the working recipe and import metadata
unchanged. Save normalization is visible to the editor's existing history and
revision handling, so undo can make the document dirty again without rewriting
the saved file.

File-decoding diagnostics survive live edits and refused saves. Successful save
replaces the file bytes and clears those decoding diagnostics; semantic errors
are recomputed from the document and remain until repaired. Undo/redo restores
semantic errors with document contents but does not resurrect old decoding errors
after a successful save. Once any active gesture has been finished, invalid
batches and no-op edits leave history and revisions unchanged. Operation errors
are published separately from document diagnostics.

Revert calls engine-free `ReadRecipeFile` before changing store state. Missing,
empty or undecodable files return a diagnostic; decodable recipes retain both
semantic and original input diagnostics for normal store publication. The
editor delegates revision lookup/advance/reset to `studio/DocumentRevisions`;
reload and game-load cancellation advance the shared epoch, so old indexed
edits, gestures and paint assignments cannot target a reused recipe ID.

Recipe rename/delete calls the engine-free `RecipeFiles` operations before
changing or unpublishing a document. Inspection, move, and removal failures
return a recipe diagnostic naming the path; rename refuses an existing
destination. Missing files allow operations on unsaved documents. Files
outside the user folder stay on disk. The editor advances history, view,
and revision only after the store accepts the operation.
After an accepted rename of a user-owned file, the saved baseline adopts the
new ID along with the working document. A clean rename stays clean, and undo
can still reach the contents of the moved file. Renaming a shipped document
retains its source file and leaves the renamed user document unsaved.

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
| Event sinks and hooks | `Events.h`/`.cpp` (equip, load, hit, node-update, animation sinks), `PluginEvents.h`/`.cpp` (the inter-plugin message contract and its parser), `Hooks.h`/`.cpp` (the `PlayerCharacter::Update` vfunc hook) |
| The manager | `Manager.h`, `Manager.cpp` (construction, load/clear), `ManagerApplication.cpp` (`ChangeAndRebuildActors`, application bookkeeping), `ManagerApply.cpp` (`Refresh`/`Retire`, `CollectPieces`, `MatchRecipes`, `PlaceInstances`, `PrepareChainStacks`/`SlotStackSize`), `ManagerEvents.cpp` (`Fire`/`FireAt`/`QueueEvent`/`QueueBroadcast`), `ManagerInspection.cpp` (the `RequestMesh` debug probe), `ManagerSnapshot.cpp` (`GetStatus`, `BuildSnapshot`, `PublishSnapshot`, `Watch`), `ManagerTick.cpp` (`OnFrame`, `SweepEviction`, `Tick`, `RenderPieces`, `RenderGeometry`, `UpdateLights`) |
| Application and session bookkeeping | `ApplicationService.h`/`.cpp` (per-recipe `ApplicationToken` tracking, rejection handling), `SessionQueue.h`/`.cpp` (per-actor task serialisation onto the SKSE task interface) |
| Actor and worn-piece state | `LiveActor.h`/`.cpp` (`LiveActor`, `LivePiece`, `LiveGeometry`, `RetireGeometry`, `ResolvePlacement`), `Environment.h`/`.cpp` (`ActorEnvironment`, the `SignalEnvironment` a `LiveInstance` ticks against) |
| Recipe CRUD | `RecipeStore.h`/`.cpp` (load, save, mutate, `RefreshRecipeDerivedState`), `RecipeFiles.h`/`.cpp` (save preparation/write and checked rename/delete filesystem operations), `RecipeEditor.h`/`.cpp` (gestures, edits, undo/redo, paint sessions, view commands) |
| Engine form and game-object lookups | `EngineForms.h`/`.cpp` (`FormKeyFor`/`LookupForm`, effect-shader records), `GameObjectService.h`/`.cpp` (game-object and anim-event catalogs), `InputCatalog.h`/`.cpp` (actor-value samples for the studio input pickers), `Tweaks.h`/`.cpp` (`EditorIdOf`, xEdit-tweaks availability) |
| Small utilities | `Clock.h`/`.cpp` (`NowMS`), `InstanceTime.h`/`.cpp` (bounded carry-over and checked clock offsets), `TextFile.h`/`.cpp` (bounded file read and write) |

## See also

- `REFERENCE.md` → *Engine events, hooks and the manager*, *Bindings*,
  *Recipe CRUD travels one pipeline* — the engine-layout facts and CS
  decompile notes these files cannot state.
- `docs/conventions.md` — the `Reporter`/`Diagnostic` and JSON-boundary
  contracts `RecipeStore` and `RecipeEditor` reuse from `recipe/`.

`RecipeStore` retains file-decoding diagnostics separately from model checks.
Editing and undo/redo refresh model diagnostics without erasing file problems.
A successful explicit save clears the old decoding diagnostics and revalidates;
a failed save preserves them, and reload replaces them with the new file result.
Edit refusals travel through the existing edit/gesture result and diagnostic UI.

`MenuDependency` checks loaded-module presence and the editor export inventory
before menu callbacks are registered. It reports missing exports by name and
does not claim ABI/version compatibility. Its native tests cover missing
modules and individual missing exports; a tool test tracks wrapper coverage.

`LoadFile` uses `AppendDefinition`: a successfully decoded later same-ID file
replaces the earlier record and moves to the later traversal position, even
when recipe-level validation holds it back. Unreadable files are skipped.
Overrides and held-back definitions are logged separately; shared matching
keys no longer produce ownership warnings. Generated imports still check
authored key coverage before insertion.

The selected-piece snapshot reports placement priority. Normal matching
outcomes use the actual actor form ID, with preview isolation/pinning labeled
as overrides. Surface replacement problems retain target rows; actor-wide
light replacement names the winning recipe in the light summary.
