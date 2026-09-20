# engine: `.cpp` ownership map

Status: history. Names and paths here predate the critique remediation of
2026-09-14 (Plan C's file moves and Plan D's renames); `docs/README.md` indexes
the current set.

The function→`.cpp` maps below are authoritative. The "Seam with render/" and
"Silent-gap / conflict risks" sections at the end are the shape agent's original
proposal and are SUPERSEDED by `docs/wip/wave3-seam.md` (all four flagged risks
resolved there). `Manager::PlaceLightsOf` calls render's `PlaceLightNodes`;
binding is driven by `PlanBinding`'s `BindingDiff` over the per-binding
primitives (no `ApplyBinding`); shell nodes use `Identity::ShellNodeSuffix()`.

Every function declared in `src/engine/*.h` has exactly one owning `.cpp`.
No function is unowned. Fill agents implement one `.cpp` each, disjoint. The
frozen counterpart each `.cpp` diffs against is named per file. These modules
are engine-facing and not native-testable; correctness rides the wave-5
in-game checkpoint.

## `src/engine/EngineForms.cpp` (owns `EngineForms.h`; diffs `_old/EngineForms.cpp`)

- `FormKey FormKeyFor(const RE::TESForm &)`
- `std::string EditorIdOf(const RE::TESForm &)`
- `bool TweaksEditorIdsAvailable()`
- `RE::TESForm *LookupForm(const FormKey &)`
- `EffectShaderRecord RecordFrom(const RE::TESEffectShader &)`
- `RE::TESEffectShader *ShaderFor(const RE::EffectSetting *)`
- `RE::TESEffectShader *ShaderFor(const RE::MagicItem *)`

`template <class Form> Form *LookupForm(const FormKey &)` is header-inline over
the non-template `LookupForm`; it has no `.cpp` body.

## `src/engine/Environment.cpp` (owns `Environment.h`; diffs `_old/Environment.cpp`)

- `ActorEnvironment::ActorEnvironment(RE::Actor *, RE::MagicItem *)`
- `float ActorEnvironment::ActorValue(std::string_view, Measure) const`
- `float ActorEnvironment::ActorState(ActorStateKind) const`
- `float ActorEnvironment::Enchantment(EnchantmentField) const`
- `std::optional<Efsh::EffectParams> ActorEnvironment::EffectShader(const FormRef &) const`
- `RE::NiPointer<RE::Actor> ActorEnvironment::Actor() const noexcept` (private)

`ActorEnvironment` is the live `SignalEnvironment` (`recipe/Signals.h`): it holds
an `RE::ActorHandle` and a form id, never `RE::` pointers across ticks, and
answers zero for anything it cannot reach.

## `src/engine/Events.cpp` (owns `Events.h`; diffs `_old/Events.cpp`)

- `void RegisterEventSinks()`
- `void WatchAnimationEvents(RE::Actor *)`
- `void UnwatchAnimationEvents(RE::Actor *)`

## `src/engine/Hooks.cpp` (owns `Hooks.h`; diffs `_old/Hooks.cpp`)

- `void InstallHooks()`

## `src/engine/RecipeStore.cpp` (owns `RecipeStore.h`; diffs `_old/RecipeStore.cpp`)

- `RecipeStoreStatus LoadRecipes()`
- `RecipeStoreStatus GetRecipeStoreStatus() noexcept`
- `std::span<const Recipe> LoadedRecipes() noexcept`
- `const Studio::MaskPresets &LoadedPresets() noexcept`
- `std::optional<RecipeOrigin> OriginOf(const Recipe &) noexcept`
- `std::shared_ptr<const SignalGraph> GraphFor(const Recipe &)`
- `const Studio::ReferenceCounts *ReferencesOf(std::string_view) noexcept`
- `Recipe *MutableRecipe(std::string_view) noexcept`
- `std::span<const Diagnostic> RefreshRecipeDerivedState(std::string_view)`
- `bool IsDirty(std::string_view) noexcept`
- `std::expected<std::filesystem::path, std::string> SaveRecipe(std::string_view)`
- `bool RevertRecipe(std::string_view)`
- `bool NewRecipe(std::string_view, RecipeKey, std::string_view)`
- `bool RenameRecipe(std::string_view, std::string_view)`
- `bool AddTransientRecipe(Recipe)`
- `bool DropTransientRecipe(std::string_view)`
- `bool IsTransient(std::string_view) noexcept`

## `src/engine/MeshReader.cpp` (owns `MeshReader.h`; diffs `_old/MeshReader.cpp`)

- `std::expected<std::shared_ptr<const MeshData>, std::string> ReadMesh(RE::BSGeometry *)`
- `std::optional<MeshIdentity> IdentityOf(RE::BSGeometry *)`
- `std::optional<GpuComparison> CompareWithGpu(RE::BSGeometry *)`
- `std::optional<Vec3> NodeBindPosition(RE::BSGeometry *, RE::NiAVObject *, std::string_view)`
- `Vec3 ToRootSpace(RE::NiAVObject *, const Vec3 &)`

FETCH ONLY. `ReadMesh` reads the engine's CPU buffers (or the GPU readback
through the lab), assembles a `RawPartition` per skin partition, and delegates
byte→`MeshPartition` to `mesh/DecodePartition(const RawPartition &)`; it holds
no decode logic. `MeshEntry`/`MeshCache` (the decoded-mesh cache with facts,
analysis and bakes) are NOT here — they move to `render/` (see Seam below).

## Manager — the linchpin, split across five `.cpp`

`Manager.h` is where the other engine modules and the wave-2 planners meet.
Its runtime state (`LiveActor`, `LivePiece`, `LiveInstance`, `LiveGeometry`,
`LivePlacement`, `PlacedOutput`) is defined in `Manager.h`; the five `.cpp`
below own the functions. All five diff against `_old/Manager.cpp`. Wave 1's
failure was an unowned recipe core; every Manager method has an explicit home
here so no linchpin surfaces as an undefined symbol at merge.

### `src/engine/Manager.cpp` (core: singleton, queues, task posting)

- `Manager *Manager::GetSingleton()`
- `void Manager::PostTask(std::function<void()>)`
- `void Manager::QueueRefresh(RE::FormID)`
- `void Manager::QueueRefresh(RE::Actor *)`
- `void Manager::QueueRetire(RE::FormID)`
- `void Manager::QueueEquipFinalize(RE::FormID)`
- `void Manager::QueueLoadedActorRefreshes()`
- `void Manager::Clear()`
- `void Manager::BeginLoad()`
- `void Manager::FinishLoad()`
- `void Manager::SetEmissivePathEnabled(bool)`

### `src/engine/ManagerApply.cpp` (responsibility: ActorApply)

Wires `MatchActor` / `PlanGeometryPlacement` / `PlaceLights` (planner) / `RecipesOfInactiveInstances`,
`PlanStacks`, and `PlanBinding` to real handles and the render bindings.

- `void Manager::ReapplyAll()`
- `void Manager::RetireAll()`
- `void Manager::RunRefresh(RE::FormID, std::uint64_t)`
- `void Manager::Refresh(RE::Actor *)`
- `void Manager::Retire(RE::FormID)`
- `void Manager::RetireEveryActor()`
- `void Manager::WithRecipeRetired(std::string_view, const std::function<void()> &)`
- `void Manager::WithListMoved(const std::function<void()> &)`
- `std::vector<LivePiece> Manager::CollectPieces(RE::Actor *, bool)`
- `void Manager::MatchRecipes(RE::Actor *, LiveActor &)` — calls `MatchActor`
- `std::optional<std::size_t> Manager::InstanceFor(RE::Actor *, LiveActor &, RecipeId, RE::MagicItem *)`
- `void Manager::PlaceInstances(RE::Actor *, LiveActor &)` — calls `PlanGeometryPlacement`, `PlanStacks`, `PlanBinding`
- `void Manager::PlaceOnGeometry(RE::Actor *, LiveActor &, PieceId, std::size_t)` — calls `Compositor::Prepare`, `MaterialBinding::Install`, `ShellBinding::Create`
- `void Manager::PlaceLightsOf(RE::Actor *, LiveActor &)` — calls planner `PlaceLights`, render `PlaceLightNodes`, `LightBinding::Create`
- `bool Manager::LayoutSanityCheck(RE::BSLightingShaderProperty *)`

### `src/engine/ManagerTick.cpp` (responsibility: ActorTick)

- `void Manager::OnFrame()` — drives `Tick`, `FireDueFinalizes`, `PublishSnapshot`
- `void Manager::Tick(std::uint32_t)` — `Compositor::BeginTick`, mesh sweep
- `void Manager::TickInstance(LiveInstance &, float, float)` — `SignalState::Tick`
- `void Manager::DropLostGeometries(LiveActor &)` — `MaterialBinding/ShellBinding::StillOwned`
- `void Manager::RenderGeometry(LiveActor &, LivePiece &, LiveGeometry &)` — `Compositor::Render`, `SlotTarget` writes
- `void Manager::UpdateLights(LiveActor &)` — `LightBinding::Update`
- `void Manager::FireDueFinalizes()`
- `bool Manager::Alive(const LiveActor &) noexcept` (static)

### `src/engine/ManagerRecipes.cpp` (responsibility: RecipeCommands)

Studio-driven mutations over the `RecipeStore` plus event injection.

- `void Manager::EditRecipe(std::string, Studio::EditBatch)`
- `void Manager::UndoRecipe(std::string)`
- `void Manager::RedoRecipe(std::string)`
- `void Manager::SaveRecipe(std::string)`
- `void Manager::RevertRecipe(std::string)`
- `void Manager::ReloadRecipes()`
- `void Manager::NewRecipe(std::string, RecipeKey, std::string)`
- `void Manager::RenameRecipe(std::string, std::string)`
- `void Manager::BeginPaint(std::string, RecipeKey, Surface)`
- `void Manager::SetPaintSurface(Surface)`
- `void Manager::KeepPaint(std::string, std::string)`
- `void Manager::EndPaint()`
- `void Manager::ApplyEdits(const std::string &, const Studio::EditBatch &)`
- `void Manager::RestoreRecipe(const std::string &, bool)`
- `void Manager::Fire(RE::FormID, const EventRecord &)`
- `void Manager::QueueEvent(RE::FormID, EventRecord)`
- `void Manager::FireAt(RE::FormID, std::string, std::string, Vec3, float, float)`
- `void Manager::RequestMesh(RE::FormID, std::string)`

### `src/engine/ManagerSnapshot.cpp` (responsibility: SnapshotBuild)

Reads `LiveActor.structure` (the planner `ActorState`) for structure and
overlays live `SignalState` values and render textures onto studio's rows.
Studio owns the recipe/state→view projections (`Studio::SignalRowOf`,
`OutputRowOf`, …); this glue calls them, it does not duplicate them.

- `void Manager::Isolate(std::string, int, int)`
- `void Manager::PinRecipe(Studio::PieceRef, std::string)`
- `void Manager::UpdateView(std::function<void(Studio::View &)>)`
- `Manager::Status Manager::GetStatus() const`
- `void Manager::Watch(const std::optional<Studio::PieceRef> &)`
- `std::shared_ptr<const Manager::Snapshot> Manager::LatestSnapshot() const`
- `Manager::Snapshot Manager::BuildSnapshot(const std::optional<Studio::PieceRef> &) const`
- `void Manager::PublishSnapshot(std::uint32_t)`

## Header → owning `.cpp`(s)

- `EngineForms.h` → `EngineForms.cpp`
- `Environment.h` → `Environment.cpp`
- `Events.h` → `Events.cpp`
- `Hooks.h` → `Hooks.cpp`
- `RecipeStore.h` → `RecipeStore.cpp`
- `MeshReader.h` → `MeshReader.cpp`
- `Manager.h` → `Manager.cpp` + `ManagerApply.cpp` + `ManagerTick.cpp` +
  `ManagerRecipes.cpp` + `ManagerSnapshot.cpp`

## Seam with render/

Manager holds and drives the render cluster's types; render owns their
headers (`render/Compositor.h`, `render/Binding.h`, `render/PBRMaterial.h`,
`render/RuntimeTextures.h`). This is read off the frozen `Manager.cpp` ↔
`Compositor.cpp` / `Binding.cpp` calls; the orchestrator reconciles it against
the render shape agent.

What Manager hands render, and what it expects back:

Compositor (singleton, `Compositor::GetSingleton()`):
- `std::unique_ptr<RenderedStack> Prepare(const Recipe &, const SurfaceOutput &, const GeometryInputs &, TextureSize, TextureSize)` — Manager passes the recipe, the `SurfaceOutput`, the geometry's `GeometryInputs`, and the runtime/max sizes; keeps the `RenderedStack` in `PlacedOutput::stack`.
- `void BeginTick(std::uint32_t)` (per frame).
- `void Render(RenderedStack &, const SignalState &, float time, const LayerFilter &, const StackBase &)` — Manager passes the stack, the instance's `SignalState`, the instance time, the per-output `LayerFilter`, and the chaining `StackBase`; reads back `RenderedStack::Texture()`, `Animated()`, `Diagnostics()`.
- `std::expected<std::shared_ptr<MeshEntry>, std::string> MeshOf(RE::BSGeometry *)`, `std::shared_ptr<const MeshEntry> CachedMesh(RE::BSGeometry *)`, `bool MeshSweepDue(std::uint32_t)`, `void SweepMeshes(std::uint32_t, std::span<RE::BSGeometry * const>)`, `void ClearMeshes()` — the decoded-mesh cache (with `Studio::MeshFacts`, `MeshAnalysis`, bakes) lives in render, fed by engine `ReadMesh`/`IdentityOf`.
- `const MaterialRecord &AnalyseMaterial(const MaterialInputs &)`, `const MaterialRecord *CachedMaterial(const MaterialInputs &)`, `void ClearMaterials()`.
- `std::optional<PreparedSource> InspectSource(const Recipe &, std::string_view, const GeometryInputs &)`, `std::optional<PreparedMask> InspectMask(...)` — SnapshotBuild reads texture/channel/animated/problem for the picture rows.

Compositor records Manager depends on: `GeometryInputs`, `MaterialInputs`
(with `static MaterialInputs From(const PBRMaterialLayout &)`), `RenderedStack`
(`Texture`/`Animated`/`Diagnostics`/`Size`/`Layers`), `StackBase`,
`LayerFilter`, `MeshEntry`, `MaterialRecord`, `PreparedSource`, `PreparedMask`.

Binding (render owns):
- `std::unique_ptr<MaterialBinding> MaterialBinding::Install(RE::BSGeometry *, RE::BSLightingShaderProperty *, bool uniqueCopy)` — methods used: `StillOwned()`, `Private()`, `Slots() -> std::vector<SlotState>`, `Problem(Slot)`, and the `SlotTarget` writes (`WriteTexture`, `WriteEmissive`, `WriteFuzz`, `WriteHeightScale`, `WriteGlint`, `WriteCoat`, `WriteSubsurface`).
- `std::unique_ptr<ShellBinding> ShellBinding::Create(RE::BSGeometry *, RE::BSLightingShaderProperty *, const ShellSettings &)` — `Pose(const Vec3 &inflate, float alpha, float rimPower, float emissive)`, `SetVisible(bool)`, `StillOwned()`, `Describe()`, `Slots()`, `Problem(Slot)`.
- `std::unique_ptr<LightBinding> LightBinding::Create(const std::vector<LightPlacement> &, bool shadow)` — `Update(const Vec3 &color, float intensity, float size, float cutoff, bool visible)`, `Describe()`.
- `std::vector<LightPlacement> PlaceLights(const Bones &, std::span<RE::BSGeometry * const>, RE::NiAVObject *root, const Vec3 &offset)` — geometry-space light node placement.
- `SlotTarget` interface and `SlotState { Slot slot; std::string original; std::string written; }` — SnapshotBuild converts `SlotState` to `Studio::SlotRow`.

Binding records/types Manager depends on: `PBRMaterialLayout`
(`render/PBRMaterial.h`; Manager casts `property->material` to it in
`LayoutSanityCheck` and `MaterialInputs::From`), `ShellSettings`
(`recipe/Recipe.h`), `LightPlacement`, `SlotState`, `SlotTarget`.

## REFERENCE.md additions (draft)

Shape changes vs the frozen headers, for the orchestrator to merge into
REFERENCE.md under the engine headings:

- `engine/MeshReader` no longer owns `MeshEntry`/`MeshCache`. The decoded-mesh
  cache (decoded `MeshData` + `Studio::MeshFacts` + `MeshAnalysis` + the bakes,
  which are `RenderTarget`s) is a render/Compositor concern and moves there.
  `MeshReader` is fetch-only and delegates decode to
  `mesh/DecodePartition(const RawPartition &)`.
- `engine/RecipeStore` drops `RecipeDirectory()` (dead per
  `docs/wip/deletions.md`; the recipe root is `Identity::RecipeRoot()`).
  `LoadedPresets()` returns `const Studio::MaskPresets &` (frozen returned
  `Studio::RegionsFile`; the "region" vocabulary is gone). `ReferencesOf()`
  returns `const Studio::ReferenceCounts *`, now defined in `studio/Edits.h`.
- `engine/Environment`: `ActorEnvironment::EffectShader` returns
  `std::optional<Efsh::EffectParams>` (frozen: `Timing::EffectParams`); the
  `Efsh` module replaced `Timing`. The environment still holds an
  `RE::ActorHandle` + form id, never `RE::` pointers across ticks, and answers
  zero for anything unreachable (av current = max − damage; damage is the
  damage modifier negated; max is permanent plus the temporary modifier).
- `engine/Manager`: the engine runtime state is renamed to avoid colliding
  with the planners' `ActorState`/`Placement`/`Instance` (both live in
  namespace `BetterEnchantmentEffects`): `LiveActor`, `LivePiece`,
  `LiveInstance`, `LiveGeometry`, `LivePlacement`, `PlacedOutput`. `LiveActor`
  embeds the planner `ActorState` as `structure` (the `MatchActor` result) and
  layers the live handles over it. `Manager::EmissivePathEnabled()` (the
  getter) is dropped as dead per `docs/wip/deletions.md`; the setter, field and
  `Status.emissivePath` stay (GetStatus reads the field directly).

## Silent-gap / conflict risks for the barrier

Resolve these before dispatching fills — a promised capability with no
home, or a cross-cluster name collision, is what the barrier exists to catch.

1. `PlaceLights` name collision across clusters. `planners/ActorPlanning.h`
   declares `ActorLightPlan PlaceLights(const ActorState &, std::span<const Recipe>)`;
   `render/Binding.h` (frozen) declares
   `std::vector<LightPlacement> PlaceLights(const Bones &, std::span<RE::BSGeometry * const>, RE::NiAVObject *, const Vec3 &)`.
   Different parameter lists make them legal overloads in the shared namespace,
   but they are unrelated operations and Manager calls both in `PlaceLightsOf`.
   Recommend the render agent rename its geometry-space placer (e.g.
   `PlaceLightNodes`). Orchestrator decision, not an agent's.
2. `MeshEntry` / `MeshCache` / `MeshOf` / `AnalyseMaterial` / `InspectSource` /
   `InspectMask` are consumed by Manager (ActorApply + SnapshotBuild) but owned
   by the render shape agent. If render does not expose them, SnapshotBuild's
   geometry/picture rows and the material-analysis path have no source. Must be
   reconciled against the render shape.
3. `PBRMaterialLayout` (render/PBRMaterial.h) and `LayoutSanityCheck`: the
   `offsetof`-pinned layout mirror is a render header; `LayoutSanityCheck`
   (ManagerApply.cpp) and `MaterialInputs::From` read it. Manager depends on
   render exposing `PBRMaterialLayout` with the same field names the frozen
   `LayoutSanityCheck` reads (`rmaosTexture`, `emissiveTexture`,
   `displacementTexture`, `featuresTexture0`, `featuresTexture1`).
4. `ShellSuffix()` (frozen `render/Binding.h`) is superseded by
   `Identity::ShellNodeSuffix()`; `Identity.h` is the only place the plugin
   name is spelled. Render should drop its own `ShellSuffix` and use Identity.
