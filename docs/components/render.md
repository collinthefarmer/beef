# render/

The GPU layer. It executes an actor's render **plan** (`RenderPlan`,
`planners/RenderPlan.h`) on the GPU and writes the results into the actor's
worn materials. Each **output** of a **recipe** becomes a **stack** of
**layers**. The directory draws each stack into a texture and writes that
texture into a TruePBR material **slot** of Community Shaders'
`BSLightingShaderMaterialPBR` or of a cloned **shell**. It also places the
recipe's point **lights**. Its `ALLOWS` row in `tools/gate.py` is `ADAPTER`:
it may include the engine-free directories (`Core.h`, `recipe`, `mesh`,
`planners`, `diagnostics`, `studio`), itself, `PCH.h`, `Identity.h`,
`Settings.h` and `SettingsFile.h`. `engine`, `menu`, `main.cpp` and
`SettingsFile.cpp` include it. It is engine-facing. It owns Direct3D 11
resources and the Community Shaders material layout directly, and no native
test covers it.

## What it owns

- `TextureLab` (`TextureLab.h`) runs every GPU pass. It borrows the engine's
  D3D11 device and context and compiles its shaders in `Init`. It draws
  layers, **programs**, stacks, **bakes**, **ripples**, material clusters,
  reductions and previews.
- `RenderInstance` (`RenderInstance.h`) executes one actor's `RenderPlan`.
  `BuildActorRender` (`engine/ManagerApply.cpp`) creates one instance per
  actor, and every geometry of the actor shares it through
  `GeometryInputs::render`.
- `Compositor` (`Compositor.h`) is the process-wide entry point for stack
  rendering. It also owns the image cache (`LoadImage`), the mesh cache
  (`MeshOf`), the material analysis cache (`AnalyseMaterial`) and the studio's
  source and mask inspection (`InspectSource`, `InspectMask`).
- `GeneratedShaders` (`GeneratedShaders.h`) compiles and caches the pixel
  shaders generated per program and per **stack shape**.
- The **binding** classes (`Binding.h`) write rendered textures and scalars
  into a worn material or a shell clone, and restore the original state.
  `PbrMaterial::Bind` (`PBRMaterial.h`) is the checked cast from an engine
  property to the pinned PBR layout.
- `RenderTargetPool` (`RenderTargetPool.h`) owns the render **target**s and
  the **presenter** textures. `TextureRef` (`TextureRef.h`) is the handle
  through which a generated target stands in for an engine texture.
- `MeshReader.h`, `MeshCache.h`, `SkinPalette.h` and `SkinData.h` read GPU
  mesh buffers and repair a shell clone's skin.

## Data

### Plan execution

A `RenderInstance` holds the plan, the typed values the engine imports into
it, and the retained result of each step. `RenderExecution<RenderValue,
RenderScratch>` (`planners/RenderExecution.h`) decides which steps run again
and which step results are reused. `ExecuteStep` (`RenderInstance.cpp`)
dispatches each `RenderStepKind` to one function per kind, for example `ExecuteBakeMeshStep`.

| Type | Description | Declared in |
|---|---|---|
| `RenderInstance` | Owns the `RenderPlan`, the recipe graphs, the `GeometryInputs` of each geometry, and the execution. `UpdateInput` imports one value. `Update` refreshes the signal-driven inputs of one recipe instance. `Render` draws one stack output. `BeginFrame` starts one render tick. | `RenderInstance.h` |
| `RenderValue` | The variant of every value a step reads or writes: a numeric `Value`, a `TextureView`, a mesh entry, `MaterialInputs`, a `RenderTransform`, `RenderFirings`, bake buffers, a material sample or analysis, a **lookup**, a `LayerFilter` or a `StackResult`. | `RenderInstance.h` |
| `RenderScratch` | Per-step scratch: a weak reuse hint for the step's target, the reduction and material readback state, and the cached `LayerFieldPack`. | `RenderInstance.h` |
| `TextureView` | A texture with its sampling (`TextureLab::LayerInput`), its normalization factor and, when a step produced it, its owning target. | `RenderInstance.h` |
| `StackResult` | A stack's texture and content version. | `RenderInstance.h` |
| `StackOutcome` | Either a `StackResult` or `StackPending`. | `RenderInstance.h` |
| `LayerFieldPack` | The visible **layer field** indices of a stack and their `PackedLayerFields`. The scratch keeps it until the set of visible fields changes. | `RenderInstance.h` |
| `GeometryInputs` | One geometry's shared render instance, application context, `MaterialInputs`, geometry and root node. | `Compositor.h` |
| `MaterialInputs` | The diffuse, normal, RMAOS and displacement textures of a material, and whether its displacement map is flat. `MaterialInputs::From` reads them from a `PbrMaterial`. | `Compositor.h` |
| `RenderOutput` | The engine's handle on one placed stack: a weak reference to the instance, the stack's step output, the latest texture and version, the size, the animation flag and the diagnostics. | `Compositor.h` |
| `StackRender` | The result of `Compositor::Render`: `kRendered`, `kPending` or `kFailed`. | `Compositor.h` |
| `LayerFilter`, `StackBase` | The hidden layer indices of a stack, and the texture of the preceding stack with its content version. | `Compositor.h` |
| `StackTextureRequest` | One stack the engine asks to render: recipe, graph, output, placement, geometry inputs and size. | `Compositor.h` |
| `PreparedSource`, `PreparedMask` | The studio's inspection records of one source or mask on one geometry. | `Compositor.h` |

`RenderInstance::BeginFrame` runs once per frame, whichever geometry calls it
first. It releases each texture or bake-buffer step that no consumer read for
`kReleaseAfterIdleTicks` (30) ticks, and it never releases a stack result
(`Releasable`). It then collects the finished readbacks. A step whose input
has no value yet while its readback is in flight makes the stack
`StackPending`, not failed (`AwaitingFirstReadback`).

### Stack composition and layer fields

A stack draws its visible layers over a base texture. A layer reads its
source and mask either from a texture or from a **layer field**. A layer
field is a program that the stack evaluates inside its own pass.
`InlineFields` (`planners/FieldInlining.h`) moves a producer program into
its consumer when the plan is built, so the consumer computes the field
inside the pass that reads it (**inline**).

| Type | Description | Declared in |
|---|---|---|
| `TextureLab::LayerPass` | One layer's draw inputs: previous texture, source, sampling, color, opacity, blend, channel bits, mask, the legacy per-layer curve lookup, and the source and mask segment indices when a layer field supplies them. | `TextureLab.h` |
| `TextureLab::BoundLayerFields` | The packed code of a stack's layer fields (`ProgramPack`) with their current `ProgramBindings`. | `TextureLab.h` |
| `TextureLab::LayersWithRenderedFields` | The layer passes after each layer field has been drawn to its own target, with the targets that hold them. | `TextureLab.h` |
| `StackShape` | The record a generated stack shader is keyed by: base presence, one `LayerShape` per layer, and the fields' code without numbers. `CheckStackShape` accepts at most `kMaxStackLayers` (8) layers. | `planners/StackShader.h` |
| `StackConstants` | The one-pass stack shader's constant buffer: offset and scale, flags, layer, color, mask and field rows for 8 layers. | `ShaderConstants.h` |
| `LayerConstants` | The constant buffer of one `Render` call: offset and scale, flags, and the layer's color, mask and curve parameters. | `ShaderConstants.h` |

`TextureLab::RenderStack` first tries `DrawStackPass`, which draws the whole
stack in one pass. `CanRenderStack` refuses that pass when the shape fails
`CheckStackShape`, when a layer has a legacy curve, or when a pipeline is
missing. `RenderStack` then calls `RenderFieldsToTargets` and
`RenderLayersOneByOne`. `RenderLayersOneByOne` alternates between the stack's
target and the lab's scratch target. The layer count's parity makes the last
layer land in the stack's target.

### Programs and the interpreter

A **program** is a compiled field expression (`FieldProgram`,
`planners/FieldProgram.h`). The **interpreter** is the bytecode loop in the
shader (`RunProgram`, `PSProgram` in `ShaderSource.cpp`). A generated shader
replaces the interpreter for one program text when its compile has finished.

| Type | Description | Declared in |
|---|---|---|
| `TextureLab::ProgramBindings` | The current values of a program's inputs: up to `kProgramInputs` values, `kProgramTextures` textures and `kProgramLookups` lookups, each with its count. | `TextureLab.h` |
| `TextureLab::ProgramTexture` | One bound program texture with its sampling and normalization factor. | `TextureLab.h` |
| `TextureLab::Lookup` | A 256-entry sampled function table on the GPU. `CreateLookup` makes one. It cannot be copied or moved. | `TextureLab.h` |
| `ProgramConstants` | The interpreter's constant buffer: 256 instruction rows, 16 inputs and input values, and 8 texture parameter rows. Its static asserts pin the `ProgramOpcode` values that the shader mirrors. | `ShaderConstants.h` |
| `ProgramPack`, `ProgramSegment`, `ProgramCode` | Several programs packed into one instruction block, one program's range in that block, and a view of one program's instructions and inputs. `SegmentCode` returns the code of one segment. | `planners/FieldProgram.h` |
| `GeneratedShaders` | Two caches of compiled pixel shaders, one keyed by program text (`ProgramFor`) and one keyed by `StackShape` (`StackFor`). Each holds at most `kMaxPerCache` (256) entries. Both return null until the compile finishes. | `GeneratedShaders.h` |
| `GeneratedShader` | One cache entry: the pending compile and the last use. | `GeneratedShaders.h` |

`TextureLab::RenderProgram` checks the binding counts against the program
before it draws. `TextureLab::SetGeneratedShaders` turns the generated
shaders off, and every draw then uses the interpreter or `PSStack`.

### Other passes

`TextureLab::Render` draws one full-screen pass in a `Mode`. The bake,
ripple, cluster, dilate and reduction passes each have their own pixel
pipeline (`GpuResources` in `TextureLab.h`). A pipeline that fails to compile
disables only its own pass (`ProgramPassAvailable`, `RippleAvailable`,
`ClustersAvailable`, `BakingAvailable`).

| Type | Description | Declared in |
|---|---|---|
| `TextureLab::Mode` | The shader path of one `Render` call: `kGlow` (0), `kChannel` (4), `kLayer` (6), `kCopy` (7). | `TextureLab.h` |
| `TextureLab::MapReading` | How the shader decodes an input map: `kNone`, `kRmaos` or `kNormalSlope`. `InputMap` carries it with the texture. | `TextureLab.h` |
| `TextureLab::Scroll`, `TextureLab::LayerInput` | UV offset, tiling, mirroring, transpose and source mip; and a channel with mesh-space and nearest-sampling flags plus a `Scroll`. | `TextureLab.h` |
| `TextureLab::RipplePass`, `RippleFiring` | A ripple draw: the positions texture, up to `kRippleFirings` (8) firings with origin and age, and the wave's speed, width, decay and direction. | `TextureLab.h` |
| `RippleConstants` | The ripple pass's constant buffer. | `ShaderConstants.h` |
| `ClusterConstants` | The cluster pass's constant buffer: RMAOS, luma and diffuse centroids for `kMaxMaterialClusters` (8) clusters, plus weights. | `ShaderConstants.h` |

`TextureLab::BakeMesh` rasterizes a mesh's triangles into UV space. It then
calls `DrawDilation`, which draws the dilate shader twice through a scratch
target so that covered texels spread into the gutter around each UV island.

### Measurements and readback

A reduction measures a field on the GPU and returns the result a few frames
later. The engine-free `planners/GpuReduction.h` holds the pass extents, the
`ReadbackRing` and the decoding. No readback in a render tick waits for the
GPU.

| Type | Description | Declared in |
|---|---|---|
| `TextureLab::ReductionReadback` | A `ReadbackRing` with `kReadbackSlots` (3) staging textures. `SubmitReduction` fills one slot, and `CollectReduction` maps the newest finished slot without waiting. | `TextureLab.h` |
| `ReductionConstants` | The `PSReduce` constant buffer. Its static asserts pin the `ReductionKind` values: mean, sum, minimum, maximum. | `ShaderConstants.h` |
| `TextureLab::MaterialReadback` | The RMAOS and diffuse staging copies of one material sample, and a pending flag. `SubmitMaterialSample` and `CollectMaterialSample` use it. | `TextureLab.h` |
| `TextureLab::Extent` | A texture's width and height. `ExtentOf` reads it. | `TextureLab.h` |
| `TextureLab::TimedSpan` | A scope that records one GPU timestamp pair when timing is on. `BeginTimedTick`, `CollectTimings` and `DrainTimings` manage the ticks. | `TextureLab.h` |

Each `PSReduce` pass reduces a 4x4 block to one texel (`kReductionBlock`)
until one texel remains. `RenderInstance::CollectReadbacks` imports each
finished result into its readback input once per frame. Each readback emits a
`metrics` trace event with `action=readback` and an `op` of `buffer`,
`reduction`, `mean` or `pixels` (`ReadbackMeter`, `TextureLabReadback.cpp`).
The event records `us`, `lock_wait_us`, `lock_held_us`, `map_us`,
`map_attempted`, `map_succeeded`, `success` and `bytes`. The map time is part
of the lock-held time.

### Targets, presenters and previews

A generated target and a static engine texture have different lifetimes. For
that reason, code above the lab passes textures as `TextureRef`, never as a
raw pointer. `RegisterTextureTarget` enters a target into the `TextureLeases`
registry, so that a later `TextureRef` built from the presenter pointer finds
the target again.

| Type | Description | Declared in |
|---|---|---|
| `TextureLab::RenderTarget` | One D3D11 texture with its views, size, `TextureFormat`, `MipPolicy`, generation and presenter. It cannot be copied or moved. | `TextureLab.h` |
| `RenderTargetPool` | Owns the idle targets (at most 16 and 64 MiB), the scratch targets per size and format, and `kPresenterCount` (512) presenter slots. `Acquire` returns a shared target, `Scratch` returns a reused one, and `Recycle` returns a released target to the pool. | `RenderTargetPool.h` |
| `TextureRef` | A generated target or a static engine texture. `get()` returns the presenter or the engine texture. `Valid()` and `Generation()` tell a live target from a stale one. | `TextureRef.h` |
| `TextureRefIdentity` | The engine texture's address as `std::uintptr_t`. `CompositorDemand.cpp` uses it in material identities. It differs from `ImageCacheKey` (`planners/TextureIdentity.h`), which identifies a file by its path. | `TextureRef.h` |
| `TexturePreviews` | The studio's live preview requests, keyed by source texture, channel and context. It re-renders the dynamic entries each generation and expires the unused ones. | `TexturePreviews.h` |
| `TextureLab::PreviewSampling` | The sampling and normalization of one sampled preview. | `TextureLab.h` |

### Bindings and PBR writes

A binding writes a rendered result into an engine object and restores the
original when it ends. `SlotTarget` is the one write interface, so the tick
loop writes the same way to a worn material and to a shell clone.

| Type | Description | Declared in |
|---|---|---|
| `SlotTarget` | The abstract write surface: `WriteTexture`, `WriteEmissive`, `WriteFuzz`, `WriteHeightScale`, `WriteGlint`, `WriteCoat`, `WriteSubsurface`, with `Problem` and `Slots` for reports. | `Binding.h` |
| `SlotWriter` | The bookkeeping behind both concrete targets. It captures a slot group's original state on the first write, and `Restore` puts it back. | `Binding.h` |
| `MaterialBinding` | The `SlotTarget` over a worn material. `Install` attaches it to a geometry's shader property, on a private material copy when asked. | `Binding.h` |
| `ShellBinding` | The `SlotTarget` over a cloned shell geometry. It owns the clone's `SkinPaletteLease` and alpha property. `Pose` applies `ShellPoseValues` to the clone's bone transforms each tick. | `Binding.h` |
| `SlotState` | One slot's report row: the slot, the original texture name and the written one. | `Binding.h` |
| `LightPlacement`, `LightValues` | One bone that holds a light, with an offset and a share of the intensity; and a light's color, intensity, size and cutoff. `PlaceLightNodes` builds the placements. | `Binding.h` |
| `LightBinding` | One `NiPointLight` per placement. `Create` places them, and `Update` sets the values and visibility. | `Binding.h` |
| `PbrMaterial` | The checked handle on a PBR property. `Bind` verifies the layout, and `Attached` and `TextureSlotsValid` check it again at use. | `PBRMaterial.h` |
| `PBRMaterialLayout` | The pinned copy of Community Shaders' `BSLightingShaderMaterialPBR`. Its static asserts fix the member offsets and the 0x148 size. | `PBRMaterial.h` |
| `GlintParameters` | The glint block that `WriteGlint` writes. | `PBRMaterial.h` |

### Meshes and skin

A bake needs a mesh's positions and UVs, which the engine holds in GPU
buffers. `Compositor::MeshOf` reads them once per geometry and caches the
result. `Compositor::SweepMeshes` drops entries after `kMeshMaxAgeMS`.

| Type | Description | Declared in |
|---|---|---|
| `MeshIdentity` | The key of one GPU mesh: skin partition, vertex buffers and vertex count. `IdentityOf` reads it. | `MeshReader.h` |
| `GpuComparison` | The count of differing values between a cached mesh and a fresh GPU read. `CompareWithGpu` produces it. | `MeshReader.h` |
| `MeshEntry` | One cached geometry: identity, mesh data, facts, analysis and last use. | `MeshCache.h` |
| `MeshCache` | The per-geometry store. `Get` reads or reuses an entry, and `Sweep` drops aged entries not in the keep list. | `MeshCache.h` |
| `SkinPaletteLease` | Ownership of a shell clone's repaired bone-transform links. `Preserve` takes them from the source geometry. `StillOwned` reports whether the clone still carries them. | `SkinPalette.h` |

`CopySkinData` (`SkinData.h`) copies a geometry's `NiSkinData` for a shell
clone.

## How a stack flows

```
LiveActor stack requests                      engine/ManagerApply.cpp
  │  BuildActorRender: CollectLayerDemands,
  │  BuildRenderPlan = LowerRenderPlan + InlineFields   planners/RenderPlanLowering.cpp
  ▼
RenderInstance(plan, geometries, graphs)      engine/ManagerApply.cpp
  │  one per actor, shared through GeometryInputs::render
  ▼
RenderInstance::BeginFrame, Update            engine/ManagerTick.cpp calls
  │  release idle steps, CollectReadbacks,    RenderInstance.cpp
  │  import signal values
  ▼
Compositor::Render(RenderOutput, filter, base) Compositor.cpp
  │
  ▼
RenderInstance::Render                        RenderInstance.cpp
  │  imports the base texture and LayerFilter, then Demand
  │  -> RenderExecution::Evaluate -> ExecuteStep per changed step
  ▼
ExecuteCompositeStackStep                     RenderInstance.cpp
  │  VisibleLayers, LayerPassesFor, LayerFieldPackFor, BindLayerFields
  ▼
TextureLab::RenderStack                       TextureLabPass.cpp
  │  DrawStackPass: one pass, shader from GeneratedShaders::StackFor
  │                 (GeneratedShaders.cpp) or PSStack (ShaderSource.cpp)
  │  else RenderFieldsToTargets + RenderLayersOneByOne
  ▼
StackResult -> RenderOutput::Texture()        Compositor.cpp
  │
  ▼
WriteSlot(SlotTarget&, SlotWrite)             engine/ManagerTick.cpp
  │
  ▼
MaterialBinding / ShellBinding ::WriteTexture ...   Binding.cpp / Shell.cpp
  writes the TruePBR slot on the actor's material or shell clone
```

Lights and shells run beside this path. `LightBinding::Create` (`Light.cpp`)
places the lights, and `LightBinding::Update` sets their values each tick.
`ShellBinding::Pose` (`Shell.cpp`) moves the shell clone each tick.

## The files

| Concern | Files |
|---|---|
| Lab setup, passes and readback | `TextureLab.h`, `TextureLabLifecycle.cpp`, `TextureLabPass.cpp`, `TextureLabReadback.cpp`, `ShaderSource.cpp`, `ShaderConstants.h`, `D3DResult.h` |
| Generated shaders | `GeneratedShaders.h`, `GeneratedShaders.cpp` |
| Plan execution | `RenderInstance.h`, `RenderInstance.cpp` |
| Compositor entry points and caches | `Compositor.h`, `Compositor.cpp`, `CompositorSource.cpp`, `CompositorBake.cpp`, `CompositorDemand.cpp`, `SourceSampling.h`, `SourceSampling.cpp` |
| Targets, presenters and previews | `RenderTargetPool.h`, `RenderTargetPool.cpp`, `TextureRef.h`, `TextureRef.cpp`, `TexturePreviews.h`, `TexturePreviews.cpp` |
| Bindings and material layout | `Binding.h`, `Binding.cpp`, `PBRMaterial.h`, `PBRMaterial.cpp`, `Shell.cpp`, `Light.cpp` |
| Meshes and skin | `MeshCache.h`, `MeshCache.cpp`, `MeshReader.h`, `MeshReader.cpp`, `SkinData.h`, `SkinData.cpp`, `SkinPalette.h`, `SkinPalette.cpp` |

`CompositorDemand.cpp` holds `TextureValueBindings`, which gives each graph
value an identity from its geometry, material and recipe instance.
`SourceSampling.cpp` holds `MaterialTexture` and `IsNonPlaceholderTexture`.

## See also

- `REFERENCE.md` → *The lab's shaders*, *Bindings*, *Compositor*,
  *Generated texture references* and *Cleanup ownership contracts*.
- `docs/conventions.md` → *Component ownership* (`TextureLab`,
  `TexturePreviews`, `PbrMaterial` and writes) and *Glossary* (**target**,
  **binding**, **lease**, shader constants, D3D result, mesh cache).
- [Render graph correctness plan](../plans/render-graph-correctness-2026-09-29.md)
  and [render performance plan](../plans/render-performance-2026-09-29.md).
