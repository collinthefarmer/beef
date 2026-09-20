# render/

The GPU layer. It takes a resolved **plan** (`recipe/Merge.h`'s `SlotPlan`,
`GeometryPlan`, `LightPlan`) and the mesh facts `mesh/` and `planners/`
derived, and turns them into pixels and material writes: it bakes each
**output**'s **layer** stack on the GPU, then writes the result into a worn
actor's TruePBR material **slots** (Community Shaders'
`BSLightingShaderMaterialPBR`) and **shell** geometry. Per `tools/layers.sh`
it sits beside `mesh`, `planners`, `studio`, and `diagnostics`, above
`recipe`. It is engine-facing — it owns Direct3D 11 resources and Community
Shaders' material layout directly — and is not native-tested.

## What it owns

- The **TextureLab**: one pixel shader over the modes in `TextureLab::Mode`
  that every **bake**, interpreter, **ripple**, and classify pass runs
  through, plus the render-target pool and the readbacks a pass needs.
- The **Compositor**: it turns a `SurfaceOutput`'s layer stack into a
  `RenderedStack` by preparing each layer's **source** and **mask** and
  running the lab over them in order.
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
interpreter, **ripple**, and classify pass fills one constant-buffer struct
and draws a full-screen pass into a pooled **target**. The lab borrows the
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
| `ClassifyConstants` | The classify pass's constant buffer: RMAOS and luma centroids for `kMaxClusters` (8) clusters, plus weights. | `ShaderConstants.h` |
| `RenderTargetPool` | Owns the pooled `RenderTarget`s and the 512 **presenter** slots. `Acquire` leases a shared **target**, `Scratch` reuses one per size, and a released **target** returns to the pool through `Recycle`. | `RenderTargetPool.h` |
| `TexturePreviews` | Tracks the studio's live preview requests, keyed by source texture, channel, and context. Each generation it re-renders the dynamic entries and expires the unused ones. | `TexturePreviews.h` |

### Compositor, sources, bake

All these types are declared in `Compositor.h`. `Compositor::Prepare`
resolves a `SurfaceOutput`'s **layer** **stack** into prepared records once,
and `Compositor::Render` replays them through the lab each tick, so per-tick
work never resolves a name again. The prepared records hold `TextureRef`s
and shared lab **target**s, which keeps a **stack** alive across ticks and
re-renders only what animates.

| Type | Description |
|---|---|
| `MaterialInputs` | The diffuse/normal/RMAOS/displacement `TextureRef` set a **bake** reads; `From` lifts it off a `PbrMaterial`. |
| `GeometryInputs` | Everything `Prepare` needs from one geometry: its `MaterialInputs`, the geometry and root pointers, and the shared **mask**, **ripple**, and derived-map caches. |
| `DerivedMaps` | The normal-slope and cluster **target**s derived from one geometry's material, each with a problem string and a tried flag so a failure is not retried. |
| `PreparedSource` | One resolved **source**: its `TextureRef`, sampling, optional scroll and tile parameters, a normalize factor, and the `RenderedMask` or `RenderedRipple` it stands on. |
| `PreparedMask` | One resolved **mask**: its `TextureRef`, the channel to read, and the `RenderedMask` behind a computed mask. |
| `RenderedMask` | A cached, recursively prepared expression **mask**: its `Program`, ref bindings, prepared textures, dependency masks, and curve lookups, rendered into its own **target** at most once per tick. |
| `RenderedRipple` | A cached **ripple**: its positions **bake**, its firing state, and its **target**, re-rendered each tick from the signal state. |
| `PreparedLayer` | One **layer** of a **stack**: the `Layer` it came from, its index, its optional `PreparedSource` and `PreparedMask`, and its curve lookup. |
| `LayerFilter` | The **layer** indices `Render` hides; `Hides` answers for one index. |
| `StackBase` | The optional texture a **stack** renders on top of, with its animated flag. |
| `RenderedStack` | The prepared **stack**: its `PreparedLayer`s, its base and result **target**s, its size and animated flag, and the `Diagnostic`s preparation produced. `Texture()` is what the **binding** writes. |
| `Compositor` | The singleton that prepares and renders **stack**s. Beside them it owns the loaded-image cache, the `MeshCache`, and the per-material analysis records. |

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
| `MeshEntry` | One cached geometry: its `MeshIdentity`, the read mesh data, its derived facts and analysis, its named **bake** **target**s, and a last-used time for sweeping. | `MeshCache.h` |
| `MeshCache` | The per-geometry store: `Get` reads or reuses an entry, and `Sweep` drops aged entries not in the caller's keep list. | `MeshCache.h` |
| `SkinPaletteLease` | Ownership of a **shell** clone's repaired bone-transform links. `Preserve` takes them from the source geometry, and `StillOwned` reports whether the clone still carries them. | `SkinPalette.h` |

### Generated textures

Everything above this layer passes textures as `TextureRef` (`TextureRef.h`),
never as a raw pointer, because a generated **target** and a static engine
texture have different lifetimes. `RegisterTextureTarget` enters a **target**
into the registry that lets it stand in for an engine texture name.

| Type | Description |
|---|---|
| `TextureRef` | The handle every consumer holds: a generated render **target** or a static engine texture, told apart without either side reinterpreting the other. `get()` yields the **presenter** or the engine texture, and `Generation()` and `Valid()` tell a live **target** from a stale one. |

## How a stack flows

```
SlotPlan (recipe/Merge.h)             built by planners/ActorPlanning.cpp
  │                                   (MatchActor) over recipe/Merge.cpp;
  │                                   engine/ManagerApply.cpp carries it
  ▼
Compositor::Prepare(recipe, surface, geometryInputs, size, maxSize)
  │   CompositorSource.cpp: resolve each Layer's source + mask,
  │   normalise colour, flatten displacement, cap mask nesting (kMaxMaskDepth)
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
