# render/

The GPU layer. It takes a resolved **plan** (`recipe/Merge.h`'s `SlotPlan`,
`GeometryPlan`, `LightPlan`) and the mesh facts `mesh/` and `planners/`
derived, and turns them into pixels and material writes: it bakes each
**output**'s **layer** stack on the GPU, then writes the result into a worn
actor's TruePBR material **slots** (Community Shaders'
`BSLightingShaderMaterialPBR`) and **shell** geometry. Per `ALLOWS` in `tools/gate.py`
it sits beside `mesh`, `planners`, `studio`, and `diagnostics`, above
`recipe`. It is engine-facing — it owns Direct3D 11 resources and Community
Shaders' material layout directly — and is not native-tested.

## What it owns

- The **TextureLab**: one pixel shader over the modes in `TextureLab::Mode`
  that every **bake**, interpreter, **ripple**, cluster, and dilate pass
  runs through, plus the render-target pool and the readbacks a pass
  needs.
- The **RenderInstance**: it owns one geometry's immutable render plan, imported
  values, cached step results, and retained recipe graphs. The **Compositor**
  supplies imported images, mesh access, inspection, and output handles.
- The **Binding**: it owns the checked cast from an engine material to the
  pinned PBR layout (`PbrMaterial::Bind`) and writes textures, scalars, and
  colors into a worn actor's material and shell.
- Skin data and palette repair for shell geometry; mesh identity and
  GPU-side comparison for the mesh cache; the **presenter** textures and
  the `TextureRef` registry that let a generated render **target** stand in
  for an engine texture name.

## Data

### TextureLab lifecycle and shaders

`TextureLab` (`TextureLab.h`) is the one pixel-shader runner: every **bake**,
interpreter, **ripple**, and cluster pass fills one constant-buffer struct
and draws a full-screen pass into a pooled **target** (the dilate pass
carries no constants: after a bake rasterizes, two dilate draws flood a
two-texel gutter from covered texels — alpha marks coverage — so
bilinear sampling and mips stop pulling background into island borders). The lab borrows the
engine's D3D11 device and context and compiles its own shaders in `Init`.
It owns one `RenderTargetPool` for **target**s and one `TexturePreviews` for
the studio's live previews.

| Type | Description | Declared in |
|---|---|---|
| `Mode` | Selects the shader path for one `Render` call: `kGlow` (0), `kChannel` (4), `kLayer` (6), `kCopy` (7). The enumerator's numeric value reaches the shader in `LayerConstants.flags[3]`. | `TextureLab.h` |
| `MapReading` | Tells the shader how to decode an input map: `kNone`, `kRmaos`, or `kNormalSlope`. It rides beside the texture in `InputMap`. | `TextureLab.h` |
| `LayerConstants` | The constant buffer of the four `Mode` passes: offset/scale, flags, and the **layer**'s color, mask, and curve parameters. | `ShaderConstants.h` |
| `ProgramConstants` | The interpreter's constant buffer: 256 `Program::Node`s, 16 refs and their values, and 8 texture parameter sets. Its static asserts pin the `Program::Op` values the shader mirrors. | `ShaderConstants.h` |
| `RippleConstants` | The **ripple** pass's constant buffer: 8 firings plus the wave's shape parameters. | `ShaderConstants.h` |
| `ClusterConstants` | The cluster pass's constant buffer: RMAOS and luma centroids for `kMaxMaterialClusters` (8) clusters, plus weights. | `ShaderConstants.h` |
| `RenderTargetPool` | Owns the pooled `RenderTarget`s and the configured **presenter** slots. `Acquire` leases a shared **target**, `Scratch` reuses one per size and format, and a released **target** returns to the pool through `Recycle`. | `RenderTargetPool.h` |
| `TexturePreviews` | Tracks the studio's live preview requests, keyed by source texture, channel, and context. Each generation it re-renders the dynamic entries and expires the unused ones. | `TexturePreviews.h` |

### Plan execution and ownership

`ManagerApply` collects texture demands and lowers the requested stacks into a
`RenderPlan`. Each geometry owns one `RenderInstance`. `ManagerTick` updates its
imported values and retains the existing cross-stack fallback selection: a failed
contribution is skipped, and the next contribution receives the last successful
base. A failed visible layer prevents publication of a partial stack. An
all-hidden stack succeeds with an empty `StackResult`.

| Type | Description |
|---|---|
| `MaterialInputs` | Retained diffuse, normal, RMAOS and displacement textures, with the material's flat-displacement classification. |
| `GeometryInputs` | The material, geometry and root handles, application context, and geometry-owned render instance. |
| `RenderInstance` | Owns the plan, retained graphs, typed imported values and `RenderExecution<RenderValue, RenderScratch>`. |
| `RenderValue` | Typed numeric values, texture views, meshes, materials, firings, transforms, bake buffers, material samples/analysis, lookups, visibility or stack results. |
| `TextureView` | A retained texture plus its resolved sampling and optional produced target. |
| `RenderOutput` | A placed stack's output reference, weak instance reference, latest retained texture, size, animation classification and diagnostics. It does not schedule dependencies. |
| `PreparedSource`, `PreparedMask` | Inspection records over current instance results. They own no scheduling state. |
| `LayerFilter`, `StackBase` | Visibility and the engine-selected preceding stack texture. |

A step records the reference and change version of each input it successfully
read. Unchanged observations reuse the output. Numeric recomputation that gives
the same value keeps its version; a successful GPU write advances its version.
Measurement failure makes the current result unavailable and blocks its
consumers. Stack visibility is resolved before its selected data dependencies.
Imported resource owners can report replacement or in-place mutation through
`RenderInstance::UpdateInput`; geometry/material rebinding creates a new instance.

The plan explicitly includes source sampling, mesh buffers and bakes, normal
slope, material sampling and clustering, ripples, interpreter draws, reductions,
lookup construction, component mapping and stack composition. Uniform masks are
materialized when a texture consumer needs them. A failed lowering branch becomes
a typed unavailable producer, so hiding it can still render the remaining stack.

Produced storage is retained by step outputs and consumer `TextureRef`s.
`RenderScratch` holds only a weak reuse hint for the produced target. A stack
alternates between that target and the lab's shared scratch; parity makes the
last write land in its own target. Bake dilation uses scratch of the same format.
The bounded idle target pool distinguishes size and format and accounts for the
larger float allocations. Geometry retirement drops execution ownership while
published or preview handles can retain their storage.

Sharing of produced results is scoped to one plan and its bound identities.
Mesh imports and inspection material analyses keep their existing retained
caches. Plan step observations replace the old cross-actor compositor target
caches and recursive mask/ripple scheduling flags.

### Measurements

`TextureLab::ReduceField` reads the base level of an RGBA32-float measurement
field in row-major order. The pure `FieldReduction` implements component-wise
mean, sum, minimum and maximum; sum and mean accumulate in double precision and
round once to the output float. Every texel counts, including uncovered texels.
Non-finite samples, invalid domains and failed readbacks are errors. Published
textures retain their existing RGBA8 format.

Layer curves use explicit measured arguments. RGB image normalization is also
lowered into graph expressions and a reduction, retaining its 0.5 target and
0.05 denominator floor. The existing material flat-displacement classifier is a
separate import-time policy and still uses the legacy small-image measurement.
Full-resolution synchronous reductions and the new GPU path require the in-game
acceptance cases recorded in the render-plan checkpoint.

### Binding and PBR writes

A **binding** writes a rendered result into a worn actor's engine material
and restores the original when it dies. `SlotTarget` (`Binding.h`) is the
abstract write surface, so the tick loop writes one way whether the target
is the actor's worn material or a **shell** clone. `PbrMaterial::Bind`
(`PBRMaterial.h`) is the one checked cast from an engine property to the
pinned Community Shaders layout, so no write touches an unchecked material.

| Type | Description | Declared in |
|---|---|---|
| `SlotTarget` | The abstract write surface: `WriteTexture`, `WriteEmissive`, `WriteFuzz`, `WriteHeightScale`, `WriteGlint`, `WriteCoat`, `WriteSubsurface`, with `Problem` and `Slots` for reporting. | `Binding.h` |
| `SlotWriter` | The checked-material bookkeeping behind both concrete targets: it captures a **slot** group's original state on first write, tracks the written texture, and `Restore` puts the material back. | `Binding.h` |
| `MaterialBinding` | The `SlotTarget` over the actor's worn material. `Install` attaches it to a geometry's shader property, on a private material copy when asked. | `Binding.h` |
| `ShellBinding` | The `SlotTarget` over a cloned **shell** geometry. It also owns the clone's `SkinPaletteLease` and alpha property, and `Pose` moves the clone each tick. | `Binding.h` |
| `SlotState` | One **slot**'s report row: the slot, the original texture name, and the written one. `Slots()` returns these. | `Binding.h` |
| `LightPlacement` | One bone to hang a **light** on: the bone node, its name, an offset, and its share of the intensity. `PlaceLightNodes` builds the list. | `Binding.h` |
| `LightBinding` | The one-**light**-per-recipe path: `Create` places a `NiPointLight` per `LightPlacement`, and `Update` drives color, intensity, size, cutoff, and visibility. | `Binding.h` |
| `PbrMaterial` | The checked handle on a PBR property: `Bind` verifies the property carries the PBR layout, and `Attached`/`TextureSlotsValid` re-check it at use. | `PBRMaterial.h` |
| `PBRMaterialLayout` | The pinned mirror of Community Shaders' `BSLightingShaderMaterialPBR`; static asserts fix its member offsets and its 0x148 size. | `PBRMaterial.h` |
| `GlintParameters` | The glint block `WriteGlint` writes: screen-space scale, microfacet density and roughness, and density randomization. | `PBRMaterial.h` |

### Meshes and skin

A **bake** needs a mesh's positions and UVs, which the engine holds on the
GPU, so the **compositor** reads them back once and caches the result per
geometry. `MeshReader.h` reads and identifies a geometry's buffers,
`MeshCache.h` keys the cache, and `SkinPalette.h` repairs a **shell**
clone's bone links (`SkinData.cpp` implements `CopySkinData`, the copy of a
geometry's `NiSkinData` a clone needs).

| Type | Description | Declared in |
|---|---|---|
| `MeshIdentity` | The key that names one GPU mesh: its skin partition, its vertex buffers, and its vertex count. `IdentityOf` reads it off a geometry. | `MeshReader.h` |
| `GpuComparison` | The readback-driven check that a cached mesh still matches its source: how many compared values differ, out of how many. `CompareWithGpu` produces it. | `MeshReader.h` |
| `MeshEntry` | One cached geometry: its `MeshIdentity`, the read mesh data, its derived facts and analysis, its **bake** **target**s keyed by `BakeKey`, and a last-used time for sweeping. | `MeshCache.h` |
| `MeshCache` | The per-geometry store: `Get` reads or reuses an entry, and `Sweep` drops aged entries not in the caller's keep list. | `MeshCache.h` |
| `SkinPaletteLease` | Ownership of a **shell** clone's repaired bone-transform links. `Preserve` takes them from the source geometry, and `StillOwned` reports whether the clone still carries them. | `SkinPalette.h` |

### Generated textures

Everything above this layer passes textures as `TextureRef` (`TextureRef.h`),
never as a raw pointer, because a generated **target** and a static engine
texture have different lifetimes. `RegisterTextureTarget` enters a **target**
into the registry that lets it stand in for an engine texture name.

| Symbol | Description |
|---|---|
| `TextureRef` | The handle every consumer holds: a generated render **target** or a static engine texture, told apart without either side reinterpreting the other. `get()` yields the **presenter** or the engine texture, and `Generation()` and `Valid()` tell a live **target** from a stale one. |
| `TextureRefIdentity` | Returns the underlying engine texture's pointer as a `std::uintptr_t`. The share-key builders use it as one texture's identity, so two materials that point at the same source texture key the same share. It is not `planners/TextureIdentity.h`'s `ImageCacheKey`, which identifies a texture by its load path. |

## How a stack flows

```
SlotPlan (recipe/Merge.h)             built by planners/ActorPlanning.cpp
  │                                   (MatchActor) over recipe/Merge.cpp;
  │                                   engine/ManagerApply.cpp carries it
  ▼
Compositor::Prepare(recipe, graph, surface, outputIndex, geometryInputs, size, maxSize)
  │   CompositorSource.cpp: resolve each Layer's source + mask,
  │   normalise colour, flatten displacement, cap mask nesting (kMaxMaskDepth)
  ▼
Compositor::AdoptSharedTarget(kind, key, render)         Compositor.cpp
  │   for a stack/mask/cluster/bake whose content is identical across
  │   actors: return the live shared target from the kind's ResourceCache
  │   on a key hit, else run `render` and enter the result
  │   (StackTarget, CompositorSource.cpp, CompositorBake.cpp build the key)
  ▼
RenderedStack                          Compositor.cpp / CompositorBake.cpp
  │  held on PlacedOutput::stack, re-baked once per tick:
  ▼
Compositor::Render(stack, signals, time, filter, base)   engine/ManagerTick.cpp calls this
  │   for each PreparedLayer: TextureLab runs the layer/interpreter/ripple
  │   shader (TextureLabPass.cpp), ping-ponging stack target <-> lab scratch
  ▼
TextureRef (stack->Texture())          TextureLab.h / TextureRef.cpp
  │
  ▼
SlotWrite -> WriteSlot(SlotTarget&, SlotWrite)   engine/ManagerTick.cpp
  │
  ▼
MaterialBinding::WriteTexture/WriteEmissive/...  Binding.cpp
  writes the TruePBR material slot (emissiveTexture, featuresTexture0/1, ...)
  on the actor's private PbrMaterial copy
```

**Lights** and shells run beside this path. `LightBinding::Create`
(`Light.cpp`) places a `NiPointLight` from a `LightOutput`. `Shell.cpp`'s
pose path composes `ShellPose`'s per-bone transform onto the shell clone's
`skinToBone` each tick, and `ShellBinding` writes the shell's emissive, rim,
and alpha.

## The files

| Concern | Key files |
|---|---|
| TextureLab: setup, passes, readback | `TextureLab.h`, `TextureLabLifecycle.cpp`, `TextureLabPass.cpp`, `TextureLabReadback.cpp`, `ShaderSource.cpp`, `ShaderConstants.h`, `D3DResult.h` |
| Render-target and presenter pooling | `RenderTargetPool.h/.cpp`, `TextureRef.h/.cpp`, `TexturePreviews.h/.cpp` |
| Compositor: prepare and bake a stack | `Compositor.h`, `Compositor.cpp`, `CompositorSource.cpp`, `CompositorBake.cpp`, `SourceSampling.h/.cpp` |
| Binding: material and shell slot writes | `Binding.h`, `Binding.cpp`, `PBRMaterial.h`, `PBRMaterial.cpp` |
| Shell and light | `Shell.cpp`, `Light.cpp` |
| Meshes and skin | `MeshCache.h/.cpp`, `MeshReader.h/.cpp`, `SkinData.h/.cpp`, `SkinPalette.h/.cpp` |

## See also

- `REFERENCE.md` → *The lab's shaders*, *Bindings*, *Compositor*, *Generated
  texture references*, *Cleanup ownership contracts* — the D3D and CS
  packing facts, the ownership races, and the cbuffer layouts these files
  cannot state.
- `docs/conventions.md` → *Component ownership* — how `TextureLab` delegates
  to `RenderTargetPool`, and what `TextureLab::RenderPass` guarantees around
  the renderer lock and D3D state.

## Readback measurements

Each `metrics` trace event with `action=readback` retains total CPU wall time
in `us` and identifies `buffer`, `mean`, or `pixels` in `op`. It also records
`lock_wait_us`, `lock_held_us`, and `map_us`; map time is inside lock-held
time, so these values must not be summed. These are CPU timings, not GPU
timestamps. `map_attempted`, `map_succeeded`, and `success` distinguish
validation/allocation failures, failed maps, and completed reads. `bytes`
is the successfully consumed payload, excluding texture row padding (four
bytes for a mean). Old traces without these fields cannot establish phases
or successful completion. Trace emission happens after the renderer unlocks.

Curve lookup preparation requests a source mean only when the parsed curve
uses `mean`. Normalization and flat-displacement measurement still require
their means. GPU mesh reads and material sampling remain synchronous, as do
necessary mean reads; asynchronous readback remains outstanding.

### Compiled recipe inputs

Preparation and inspection receive the instance's immutable `RecipeGraph`,
including variant overrides. Mask programs, types, function handles, and
animation flags come from that graph. Signal bindings retain graph indices;
rendering reads those indices from the matching `SignalState`. The single-map
mask shortcut inspects compiled operations instead of parsing expression text.
Layer curves use compiled function bindings by their output/layer location.
Prepared mask/ripple caches are scoped by application; graphs with overridden
signals cannot share targets keyed by the original recipe. Allocation and pass
scheduling remain unchanged.

### Interpreter execution

The compositor stores a validated `InterpreterProgram` from `planners/` and
prepares its requested textures and function lookups. `TextureLab::InterpreterBindings`
supplies current values and GPU resources separately. TextureLab checks binding
counts and packs interpreter-owned instructions into shader constants; recipe
opcode numbering is not part of that ABI. Component-width metadata preserves
vec2 behavior through arithmetic and reductions. Pass scheduling and resource
ownership remain in the compositor.

## Texture demand acquisition

`CompositorDemand.cpp` binds graph identities to geometry/material/runtime inputs
and collects all eligible stack requests for one geometry before acquisition.
The geometry's prepared texture registry uses `TextureKey`; source preparation
binds existing results. Static shared results capture constant graph bindings,
and dynamic results retain instance identity. Existing source operations own
their internal bake/readback passes. See the
[texture-demand checkpoint](../checkpoints/texture-demand-2026-09-27.md).
