# wave 3 seam: the reconciled engine↔render interface

Barrier output. The two shape agents each proposed a seam from their own side;
this file is the reconciliation the orchestrator froze before dispatching fills.
Where a decision changed a header, the header and the two ownership maps already
match this file. Fill agents follow this; if a fill needs to diverge, stop and
raise it, do not re-decide unilaterally.

## Layering (one-way includes)

```
menu (wave 4)  →  engine  →  render  →  { planners, recipe, mesh, studio }
                    │           │
                    │           └─ render also → engine/MeshReader.h (fetch only)
                    └─ engine → studio, planners, recipe, mesh
```

`engine/MeshReader.h` includes NO render header (it is fetch-only). The single
render→engine edge is `render/Compositor.h` including `engine/MeshReader.h` for
`ReadMesh` / `MeshData` / `MeshIdentity` / `IdentityOf`. That is not a cycle:
`MeshReader` never points back at render.

## Decision 1 — `PlaceLights` name collision → render's is `PlaceLightNodes`

`planners/ActorPlanning.h` owns the decision
`ActorLightPlan PlaceLights(const ActorState &, std::span<const Recipe>)`.
Render's geometry-space node placer is renamed `PlaceLightNodes`
(`render/Binding.h`, owned by `render/Light.cpp`). `Manager::PlaceLightsOf`
calls the planner `PlaceLights` to decide, then render `PlaceLightNodes` to
place, then `LightBinding::Create`.

## Decision 2 — `MeshEntry` / `MeshCache` are RENDER types

The frozen tree put them in `MeshReader.h`, but they carry
`TextureLab::RenderTarget` bakes and the Compositor drives the baking and sweep.
`buildup-plan.md` lists `MeshReader` as "fetch only". Resolution:

- `MeshEntry` and `MeshCache` are defined in `render/Compositor.h`.
- `MeshCache::Get`/`Cached`/`Sweep`/`Clear` and `Compositor::MeshOf`/`CachedMesh`/
  `MeshSweepDue`/`SweepMeshes`/`ClearMeshes` are owned by `render/CompositorBake.cpp`.
- `engine/MeshReader` stays fetch-only: `ReadMesh` returns
  `std::shared_ptr<const MeshData>` (decode delegated to
  `mesh/DecodePartition`), plus `IdentityOf`, `CompareWithGpu`,
  `NodeBindPosition`, `ToRootSpace`. It does not define or touch `MeshEntry`.
- `CompositorBake.cpp`'s `MeshCache::Get` calls `engine/MeshReader::ReadMesh` to
  fetch the mesh, then fills `facts` (`mesh/MeshFacts::FactsOf`), `analysis`
  (`mesh/Islands::AnalyseMesh`) and the `bakes` (via `TextureLab`).

## Decision 3 — one execution seam: per-link render, per-binding install

The frozen per-link path is the behaviour oracle for an untested DLL, so it is
the seam. The wave-2 planners supply the DECISION; wave 3 executes it, it does
not re-decide.

- Stacks: `Manager` calls `planners::PlanStacks` to get the `GeometryStackPlan`
  (order, chain links, per-slot animated classification), then for each link in
  plan order calls `Compositor::Prepare` once at apply (holds the returned
  `RenderedStack` in `LivePlacement`/`PlacedOutput`) and `Compositor::Render`
  per tick, threading each link's output as the next link's `StackBase` in the
  order the plan gives. Manager does not recompute `IsAnimated` or the chain.
- Bindings: `Manager` calls `planners::PlanBinding` to get the `BindingDiff`
  (material/shell needed, which slots, restore lists, shell owner), then drives
  the per-binding primitives accordingly: `MaterialBinding::Install`,
  `ShellBinding::Create`, `LightBinding::Create` at apply; the `SlotTarget`
  writers, `ShellBinding::Pose`/`SetVisible`, `LightBinding::Update` per tick;
  and `SlotWriter::Restore` for the diff's restore lists.

Dropped as redundant with this path (removed from `render/`): the one-shot
`Compositor::RenderGeometry` and `SlotTexturesOf` with their `StackInput` /
`SlotTexture` records, and `ApplyBinding` with its `GeometryBinding` /
`BindingInputs` records. Do not reintroduce them.

## Decision 4 — shell suffix is `Identity::ShellNodeSuffix()`

`Identity.h` is the only place the plugin name is spelled. Render's local
`ShellSuffix()` is removed; `render/Shell.cpp` and the engine apply traversal
both call `Identity::ShellNodeSuffix()` (the real name; not `ShellSuffix`).

## The frozen call sites (engine → render), read off `src/_old/Manager.cpp`

Manager holds render's objects; render owns their headers
(`render/Compositor.h`, `render/Binding.h`, `render/PBRMaterial.h`,
`render/RuntimeTextures.h`).

Per tick, once, before any render: `Compositor::BeginTick(nowMS)`,
`TextureLab::RenderPreviews()`.

Per contribution at apply:
`Compositor::Prepare(recipe, output, GeometryInputs, size, maxSize)` where
`size = TextureSize(settings.runtimeTextureSize)`,
`maxSize = TextureSize(settings.glossMapSize)` (the new `TextureSize` clamps in
its constructor; there is no `TextureSize::Clamp`). Then
`MaterialBinding::Install(geometry, property, settings.uniqueMaterial)` and, for
a shell, `ShellBinding::Create(geometry, property, recipe->shell)`.

Per tick per link: `Compositor::Render(stack, signals, lastTime, filter, base)`,
base threaded from the previous link. `signals` is the placed recipe's live
`SignalState`, `lastTime` the instance clock. Then the `SlotTarget` writers,
`ShellBinding::Pose(inflate, alpha, rimPower, emissive)` / `SetVisible`,
`LightBinding::Update(color, intensity, size, cutoff, visible)`.

Mesh / material / inspection: `Compositor::MeshOf`, `AnalyseMaterial`,
`SweepMeshes`, `ClearMeshes`, `ClearMaterials`, `InspectSource`, `InspectMask`;
`TextureLab::Clear`, `InvalidatePreviews`, `Available`. Lights placed with
`PlaceLightNodes(light->bones, geometries, root, offset)` then
`LightBinding::Create(placements, light->shadow)`.

## Types render owns and engine consumes

`Compositor` (singleton), `GeometryInputs`, `MaterialInputs`
(`static From(const PBRMaterialLayout &)`), `RenderedStack`
(`Texture`/`Animated`/`Size`/`Layers`/`Diagnostics`), `StackBase`, `LayerFilter`,
`MeshEntry`, `MeshCache`, `Compositor::MaterialRecord`, `PreparedSource`,
`PreparedMask`; `MaterialBinding`, `ShellBinding`, `LightBinding`, `SlotTarget`,
`SlotState { Slot slot; std::string original; std::string written; }`,
`SlotWriter`, `LightPlacement`, `PBRMaterialLayout`, `GlintParameters`,
`TextureLab`.

`Manager` reads render's `PBRMaterialLayout` in `LayoutSanityCheck` and
`MaterialInputs::From`; the offsetof-pinned field names are `rmaosTexture`,
`emissiveTexture`, `displacementTexture`, `featuresTexture0`, `featuresTexture1`
(pinned against `src/cs/BSLightingShaderMaterialPBR.h`). SnapshotBuild converts
each `SlotState` to a `Studio::SlotRow`.

## Types engine owns and render consumes

`engine/MeshReader.h`: `ReadMesh`, `MeshData` (via `mesh/Mesh.h`),
`MeshIdentity`, `IdentityOf`. Nothing else of engine's crosses into render.

## REFERENCE.md additions to fold in at merge

From the two shape agents (verbatim intent):
- Compositor: the frozen anonymous `MergeOf` chain is now
  `planners/StackPlan`'s `GeometryStackPlan`; the frozen
  `!animated && !base.animated && renderedOnce_` guard reads the plan's
  `StackLink::animated` instead of recomputing `IsAnimated`.
- Bindings: install/restore is driven by `planners/BindingDiff`'s `BindingDiff`;
  the writer holds the saved `RE::` originals and null-checks each pointer.
- MeshReader is fetch-only; the decoded-mesh + bake cache
  (`MeshEntry`/`MeshCache`) is a render/Compositor concern.
- `engine/Environment::EffectShader` returns `std::optional<Efsh::EffectParams>`
  (the `Efsh` module replaced `Timing`); av current = max − damage, damage is the
  damage modifier negated, max is permanent plus the temporary modifier.
- `engine/RecipeStore` drops dead `RecipeDirectory()`; `LoadedPresets()` returns
  `const Studio::MaskPresets &` and `ReferencesOf()` returns
  `const Studio::ReferenceCounts *`.
- `engine/Manager` runtime state is `LiveActor`/`LivePiece`/`LiveInstance`/
  `LiveGeometry`/`LivePlacement`/`PlacedOutput` (renamed to avoid the planners'
  `ActorState`/`Instance`/`Placement`); `LiveActor.structure` is the planner
  `ActorState`. Dead `Manager::EmissivePathEnabled()` getter dropped.
