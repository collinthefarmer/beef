# render: `.cpp` ownership map

The function→`.cpp` maps below are authoritative and reflect the barrier
decisions. The "Seam with engine/" and "Gap notes" sections at the end are the
shape agent's original proposal and are SUPERSEDED by `docs/wip/wave3-seam.md`
(which dropped `RenderGeometry`/`SlotTexturesOf`/`StackInput`/`SlotTexture` and
`ApplyBinding`/`GeometryBinding`/`BindingInputs`, renamed `PlaceLights` →
`PlaceLightNodes`, moved `MeshEntry`/`MeshCache` into `render/Compositor.h`, and
replaced `ShellSuffix` with `Identity::ShellNodeSuffix()`). Read wave3-seam.md
for the seam; read below for which `.cpp` owns which function.

Every function declared in `src/render/*.h` has exactly one owning `.cpp`. No
function is unowned. Fill agents implement one `.cpp` each, disjoint. The two
large frozen modules (`RuntimeTextures.cpp` 1525 lines, `Compositor.cpp` 1135
lines) are split by concern up front so no `.cpp` is a catch-all. Every `.cpp`
names the frozen counterpart it diffs against (in `src/_old/`, read never
imported).

## `src/render/PBRMaterial.cpp` (diffs `src/_old/PBRMaterial.h`)

The CS `BSLightingShaderMaterialPBR` mirror. The layout, `GlintParameters`, the
`kPbr*` flag constants and the offsetof asserts live in the header; the one
free function moves out of the header for a home:

- `bool IsPBRProperty(const RE::BSLightingShaderProperty *)`

## RuntimeTextures — the lab (all diff `src/_old/RuntimeTextures.cpp`)

Split four ways by concern (device/lifecycle, pass execution, readback,
preview) plus the shader-source translation unit.

### `src/render/RuntimeTexturesLab.cpp` (device, targets, lifecycle)

- `TextureLab *TextureLab::GetSingleton()`
- `bool TextureLab::Init()`
- `bool TextureLab::Available() const`
- `bool TextureLab::InterpreterAvailable() const`
- `bool TextureLab::RippleAvailable() const`
- `bool TextureLab::ClassifyAvailable() const`
- `bool TextureLab::BakingAvailable() const`
- `std::shared_ptr<RenderTarget> TextureLab::Acquire(TextureSize)`
- `RenderTarget *TextureLab::Scratch(TextureSize)`
- `void TextureLab::Clear()`
- `bool TextureLab::CompileShaders()` (private)
- `bool TextureLab::CreateTarget(RenderTarget &, TextureSize)` (private)
- `RE::NiPointer<RE::NiSourceTexture> TextureLab::LoadPresenter()` (private)
- `void TextureLab::Recycle(RenderTarget *)` (private)
- `TextureLab::Lookup::~Lookup()`
- `TextureLab::RenderTarget::~RenderTarget()`
- `RE::NiSourceTexture *TextureLab::RenderTarget::Texture() const`

### `src/render/RuntimeTexturesPass.cpp` (per-pass D3D execution)

Holds `struct SavedState` and the save/restore + renderer-lock discipline
(REFERENCE "Lab mechanics"). The per-texel switches stay FLAT here.

- `bool TextureLab::Render(RenderTarget &, RE::NiSourceTexture *, const LayerParams &)`
- `bool TextureLab::RenderProgram(RenderTarget &, const ProgramPass &)`
- `bool TextureLab::BakeMesh(RenderTarget &, const BakeBuffers &)`
- `bool TextureLab::RenderRipple(RenderTarget &, const RipplePass &)`
- `bool TextureLab::RenderClusters(RenderTarget &, RE::NiSourceTexture *, RE::NiSourceTexture *, const MaterialAnalysis &)`
- `std::optional<MaterialSample> TextureLab::SampleMaterial(RE::NiSourceTexture *, RE::NiSourceTexture *)`

### `src/render/RuntimeTexturesReadback.cpp` (staging, readback, means, lookups)

- `std::vector<std::uint8_t> TextureLab::ReadBuffer(REX::W32::ID3D11Buffer *, std::uint32_t)`
- `std::optional<float> TextureLab::ReadBackMean(RenderTarget &)` (private)
- `std::vector<std::uint8_t> TextureLab::ReadBackPixels(RenderTarget &)` (private)
- `std::shared_ptr<Lookup> TextureLab::CreateLookup(std::span<const float, 256>)`
- `std::optional<Extent> TextureLab::ExtentOf(RE::NiSourceTexture *)`
- `float TextureLab::MeanLuminance(RE::NiSourceTexture *)`
- `float TextureLab::MeanChannel(RE::NiSourceTexture *, ShaderChannel)`

### `src/render/RuntimeTexturesPreview.cpp` (preview request/generation/graveyard)

- `std::shared_ptr<RenderTarget> TextureLab::Preview(RE::NiSourceTexture *, ShaderChannel, bool)`
- `void TextureLab::RenderPreviews()`
- `void TextureLab::ClearPreviews()`
- `void TextureLab::InvalidatePreviews()`

### `src/render/ShaderSource.cpp` (the HLSL strings)

No public functions. Owns `kShaderSource` and the interpreter/bake/ripple/
classify shader source strings, with `kProgramStack` (= 32) interpolated into
the source (REFERENCE "Looked dead, keep": the `float3 st[32]` / `if (sp < 32)`
bound is the constant made visible). Compiled by `CompileShaders()` in
`RuntimeTexturesLab.cpp`.

## Compositor — stack execution (all diff `src/_old/Compositor.cpp`)

Split three ways: stack execution + plan driver, source/mask preparation +
cache, and mesh bake + material analysis.

### `src/render/Compositor.cpp` (singleton, stack build/render, plan driver)

Owns the ping-pong between stack target and lab scratch with the parity rule
(REFERENCE Compositor: last layer must land in the stack's own target; first
write chosen by parity of shown-layer count), the static-vs-animated caching
guard driven by the plan's `animated` flag, and `RenderedStack`/`LayerFilter`
accessors.

- `Compositor *Compositor::GetSingleton()`
- `void Compositor::BeginTick(std::uint32_t)`
- `std::unique_ptr<RenderedStack> Compositor::Prepare(const Recipe &, const SurfaceOutput &, const GeometryInputs &, TextureSize, TextureSize)`
- `void Compositor::Render(RenderedStack &, const SignalState &, float, const LayerFilter &, const StackBase &)`
- `RE::NiPointer<RE::NiSourceTexture> Compositor::LoadImage(std::string_view)`
- `std::shared_ptr<TextureLab::RenderTarget> Compositor::NeutralHeight()` (private)
- `bool LayerFilter::Hides(std::size_t) const`
- `RE::NiSourceTexture *RenderedStack::Texture() const`
- `bool RenderedStack::Animated() const`
- `TextureSize RenderedStack::Size() const`
- `std::span<const PreparedLayer> RenderedStack::Layers() const`
- `std::span<const Diagnostic> RenderedStack::Diagnostics() const`

### `src/render/CompositorSource.cpp` (source/mask/ripple prep + cache)

Owns the mask cache with cycle-stop (a mask is entered before its dependencies
recurse) and the interpreter pass-limit refusal (REFERENCE Compositor). The
`RenderedMask` / `RenderedRipple` accessors live here.

- `MaterialInputs MaterialInputs::From(const PBRMaterialLayout &)`
- `std::optional<PreparedSource> Compositor::InspectSource(const Recipe &, std::string_view, const GeometryInputs &) const`
- `std::optional<PreparedMask> Compositor::InspectMask(const Recipe &, std::string_view, const GeometryInputs &) const`
- `std::optional<PreparedSource> Compositor::PrepareSource(...)` (private)
- `std::optional<PreparedMask> Compositor::PrepareMask(...)` (private)
- `std::shared_ptr<RenderedMask> Compositor::PrepareRenderedMask(...)` (private)
- `void Compositor::RenderMask(RenderedMask &, const SignalState &, float)` (private)
- `std::expected<std::shared_ptr<RenderedRipple>, std::string> Compositor::PrepareRipple(...)` (private)
- `void Compositor::RenderRipple(RenderedRipple &, const SignalState &, float)` (private)
- `std::shared_ptr<TextureLab::Lookup> Compositor::BakeCurve(...)` (private)
- `RE::NiSourceTexture *RenderedRipple::Texture() const`
- `RE::NiSourceTexture *RenderedMask::Texture() const`
- `bool RenderedMask::Animated() const`
- `bool RenderedMask::Vector() const`
- `const std::string &RenderedMask::Problem() const`

### `src/render/CompositorBake.cpp` (mesh bakes, derived/cluster maps, material analysis)

Owns the cluster map rendered under a source's settings (replaced when another
source asks for other settings), the flat-displacement neutral-height base, and
the per-material analysis cache. Per the barrier decision (`docs/wip/wave3-seam.md`),
`MeshEntry`/`MeshCache` are RENDER types, defined in `render/Compositor.h`; this
`.cpp` owns both the `MeshCache` methods and the `Compositor` mesh wrappers.
`engine/MeshReader` is fetch-only and this cluster calls its `ReadMesh` to build
each entry.

- `std::expected<std::shared_ptr<MeshEntry>, std::string> MeshCache::Get(RE::BSGeometry *, std::uint32_t, bool)`
- `std::shared_ptr<const MeshEntry> MeshCache::Cached(RE::BSGeometry *) const`
- `void MeshCache::Sweep(std::uint32_t, std::uint32_t, std::span<RE::BSGeometry *const>, bool)`
- `void MeshCache::Clear()`
- `std::expected<std::shared_ptr<MeshEntry>, std::string> Compositor::MeshOf(RE::BSGeometry *)`
- `std::shared_ptr<const MeshEntry> Compositor::CachedMesh(RE::BSGeometry *) const`
- `bool Compositor::MeshSweepDue(std::uint32_t) const`
- `void Compositor::SweepMeshes(std::uint32_t, std::span<RE::BSGeometry *const>)`
- `void Compositor::ClearMeshes()`
- `std::expected<std::shared_ptr<TextureLab::RenderTarget>, std::string> Compositor::BakeInto(...)` (private)
- `std::expected<std::shared_ptr<TextureLab::RenderTarget>, std::string> Compositor::PrepareBake(...)` (private)
- `std::expected<std::shared_ptr<TextureLab::RenderTarget>, std::string> Compositor::PrepareDistance(...)` (private)
- `const MaterialRecord &Compositor::AnalyseMaterial(const MaterialInputs &)`
- `const MaterialRecord *Compositor::CachedMaterial(const MaterialInputs &) const`
- `void Compositor::ClearMaterials()`

## Binding — the writers (all diff `src/_old/Binding.cpp`)

Split three ways: material writer + diff applier, shell, light.

### `src/render/Binding.cpp` (SlotWriter, MaterialBinding, diff applier)

Owns the private material copy (pooled-by-content fork), the feature flag-bit
on/off discipline, and the `BindingDiff` applier.

- `SlotWriter::SlotWriter(PBRMaterialLayout *, RE::BSLightingShaderProperty *)`
- `std::string SlotWriter::Problem(Slot) const`
- `void SlotWriter::WriteTexture(Slot, RE::NiSourceTexture *)`
- `void SlotWriter::WriteEmissive(const Vec3 &, float)`
- `void SlotWriter::WriteFuzz(const Vec3 &, float)`
- `void SlotWriter::WriteHeightScale(float)`
- `void SlotWriter::WriteGlint(float, float, float, float, bool)`
- `void SlotWriter::WriteCoat(float, float)`
- `void SlotWriter::WriteSubsurface(const Vec3 &, float)`
- `bool SlotWriter::StillOwned() const`
- `void SlotWriter::Restore()`
- `std::vector<SlotState> SlotWriter::Slots() const`
- `void SlotWriter::SetFeature(std::uint32_t, bool)` / `EnableFuzz` / `EnableCoat` / `EnableSubsurface` (private)
- `std::unique_ptr<MaterialBinding> MaterialBinding::Install(...)`
- `MaterialBinding::~MaterialBinding()`
- all `MaterialBinding` accessors and `SlotTarget` overrides (`Material`, `Property`, `Private`, `Problem`, `Write*`, `Slots`, `StillOwned`)

### `src/render/Shell.cpp` (shell clone, NiSkinData/NiAlphaProperty, inflation)

- `std::unique_ptr<ShellBinding> ShellBinding::Create(...)`
- `ShellBinding::~ShellBinding()`
- `void ShellBinding::Detach()` (private)
- all `ShellBinding` accessors and `SlotTarget` overrides (`Geometry`, `Property`, `PbrMaterial`, `Describe`, `Problem`, `Write*`, `Slots`, `StillOwned`)
- `void ShellBinding::Pose(const Vec3 &, float, float, float)`
- `void ShellBinding::SetVisible(bool)`

### `src/render/Light.cpp` (light placement + creation via address-library IDs)

- `std::vector<LightPlacement> PlaceLightNodes(const Bones &, std::span<RE::BSGeometry *const>, RE::NiAVObject *, const Vec3 &)`
- `std::unique_ptr<LightBinding> LightBinding::Create(const std::vector<LightPlacement> &, bool)`
- `LightBinding::~LightBinding()`
- `void LightBinding::Update(const Vec3 &, float, float, float, bool)`
- `std::string LightBinding::Describe() const`

## Header → owning `.cpp`(s)

- `PBRMaterial.h` → `PBRMaterial.cpp`.
- `RuntimeTextures.h` → `RuntimeTexturesLab.cpp`, `RuntimeTexturesPass.cpp`,
  `RuntimeTexturesReadback.cpp`, `RuntimeTexturesPreview.cpp`, `ShaderSource.cpp`.
- `Compositor.h` → `Compositor.cpp`, `CompositorSource.cpp`, `CompositorBake.cpp`.
- `Binding.h` → `Binding.cpp`, `Shell.cpp`, `Light.cpp`.

## No native tests

Per `docs/buildup-plan.md` wave 3: render is not native-testable (it pulls in
`RE/`, D3D and CS types). No test skeletons here; correctness rides the wave-5
in-game checkpoint. Done for this cluster is the zero-warning DLL build plus a
clean tidy baseline.

## Deletions honoured (`docs/wip/deletions.md`)

- `TextureLab::Mode` keeps only `kGlow`/`kChannel`/`kLayer`/`kCopy`; the dead
  `kSheen`/`kHeight`/`kRoughness`/`kMaskedGlow` and their `HeightParams`/
  `RoughnessParams`/`MaskParams` structs, `LayerParams` members and ~20 HLSL
  lines are not recreated. Enum values keep their frozen numbering (0/4/6/7) so
  the surviving HLSL `switch` op numbers stay valid.
- `MapReading` keeps only `kNone`/`kRmaos`/`kNormalSlope` (frozen numbering
  0/3/4); the relief-only `kDisplacementR`/`kOcclusionB`/`kDiffuseLuma` and
  their `Relief` branches are gone with the dead modes.
- `kProgramStack = 32` is kept: it is a real bound interpolated into the HLSL
  (`float3 st[32]`, `if (sp < 32)`), owned by `ShaderSource.cpp`.
- `PBRMaterialLayout`'s six offsetof-pinned unread fields are all kept; the
  static_asserts in the header pin them.
- `blend: "lerp"` is not a distinct path (byte-identical to `replace`); no
  separate blend handling.
- `ShellPose`'s five dead fields (`offset`/`scale`/`scalePoint`/`spin`/
  `spinAxis`) do not appear; `ShellBinding::Pose` takes only inflate + alpha +
  rimPower + emissive, as frozen.

## Seam with engine/

What `render/` expects the engine `Manager` to hand it, and what it returns.
Read off the frozen `Manager.cpp` ↔ `Compositor.cpp`/`Binding.cpp` calls
(`src/_old/Manager.cpp`). The orchestrator reconciles this against the engine
shape agent.

Types render OWNS and the engine consumes:

- `Compositor` (singleton), `GeometryInputs`, `StackInput`, `SlotTexture`,
  `RenderedStack`, `StackBase`, `LayerFilter`, `MaterialInputs`,
  `PreparedSource`, `PreparedMask`, `Compositor::MaterialRecord`.
- `MaterialBinding`, `ShellBinding`, `LightBinding`, `SlotTarget`, `SlotState`,
  `LightPlacement`, `GeometryBinding`, `BindingInputs`, `PBRMaterialLayout`,
  `GlintParameters`, `TextureLab`.

Calls the engine makes into render (per-actor apply and per-tick), with the
frozen `Manager.cpp` line they come from:

- `Compositor::GetSingleton()->BeginTick(nowMS)` (1159); once per tick before
  any render.
- `Compositor::Prepare(recipe, output, GeometryInputs, size, maxSize)` (716) —
  build one contribution's `RenderedStack`; engine holds the returned
  `unique_ptr` (frozen `output->stack`). `size`/`maxSize` are
  `TextureSize(settings.runtimeTextureSize)` / `TextureSize(settings.glossMapSize)`
  (frozen `TextureSize::Clamp`; the new `TextureSize` clamps in its constructor).
- `Compositor::Render(stack, signals, lastTime, filter, base)` (1254) — the
  frozen per-link render, base threaded from the previous link. `signals` is
  the placed recipe's live `SignalState`; `lastTime` the instance clock.
- `Compositor::RenderGeometry(GeometryStackPlan, span<StackInput>)` +
  `SlotTexturesOf(...)` — the NEW plan-driven entry replacing the frozen
  Manager-side `MergeOf` chain loop. The engine builds one `StackInput` per
  plan `StackLink` (its `Recipe`, `SurfaceOutput`, prepared `RenderedStack`,
  the placed recipe's `SignalState`, time, filter); render walks the plan in
  chain order, threads bases per parity, and returns per-slot textures. This is
  the primary reconciliation item: the engine agent must agree whether it drives
  the chain (calling `Prepare`/`Render` per link, frozen style) or hands the
  whole plan (calling `RenderGeometry`). Both entries are provided.
- `Compositor::InspectSource(recipe, name, GeometryInputs)` (1504) /
  `InspectMask(recipe, name, GeometryInputs)` (1516) — studio inspection.
- `Compositor::MeshOf(geometry)` (1099), `AnalyseMaterial(inputs.material)`
  (1102), `SweepMeshes(nowMS, bound)` (1195), `ClearMeshes()`/`ClearMaterials()`
  (323-324).
- `TextureLab::GetSingleton()->Clear()` (325), `InvalidatePreviews()`
  (458/1124), `RenderPreviews()` (1160), `Available()` (1312).
- `MaterialBinding::Install(geometry, property, settings.uniqueMaterial)` (706),
  `ShellBinding::Create(geometry, property, recipe->shell)` (699). Alternatively
  the engine calls `ApplyBinding(GeometryBinding, BindingInputs, BindingDiff)`
  with a `PlanBinding` result; the engine agent picks which. Then per tick the
  engine calls the `SlotTarget` writers, `ShellBinding::Pose(...)` (1274),
  `SetVisible`.
- `PlaceLights(light->bones, geometries, root, offset)` (762) then
  `LightBinding::Create(placements, light->shadow)` (763); per tick
  `LightBinding::Update(color, intensity, size, cutoff, visible)` (1290).
- `ShellSuffix()` (524) — the engine's apply traversal skips geometry whose
  name ends with it.

What render EXPECTS the engine to hand IN (types render references but does not
own):

- `engine/MeshReader.h` must declare `MeshEntry`, `MeshCache`, `MeshData`-fetch
  `ReadMesh`, `NodeBindPosition`, `ToRootSpace`, `MeshIdentity` — the frozen
  `src/_old/MeshReader.h` is the template. `Compositor.h` includes
  `engine/MeshReader.h` and stores a `MeshCache meshes_` by value; `MeshEntry`
  carries `std::shared_ptr<TextureLab::RenderTarget>` bakes, so
  `engine/MeshReader.h` includes `render/RuntimeTextures.h` (as the frozen
  header did). This is a render→engine header include the orchestrator must
  land together. See the gap note below — MeshCache ownership between the two
  clusters is unresolved.
- `planners/StackPlan.h` (`GeometryStackPlan`, `SlotStackPlan`, `StackLink`),
  `planners/BindingDiff.h` (`BindingDiff`), `recipe/Merge.h`
  (`SlotContribution`, `Surface`, `Slot`), `recipe/Signals.h` (`SignalState`) —
  all wave-2 frozen; render consumes, never re-decides.

## REFERENCE.md additions (draft)

The render facts are already recorded in `REFERENCE.md` ("The lab's shaders",
"Bindings", "Compositor") and did not change shape. Two additions for the new
tree, for the orchestrator to fold in:

- Under Compositor: the chain that the frozen `Manager` drove with its
  anonymous `MergeOf` is now `planners/StackPlan`'s `GeometryStackPlan`;
  `Compositor::RenderGeometry` executes it, and the frozen
  `!animated_ && !base.animated && renderedOnce_` guard now reads the plan's
  `StackLink::animated` instead of recomputing `IsAnimated`.
- Under Bindings: `render/Binding::ApplyBinding` executes
  `planners/BindingDiff`'s `BindingDiff` (which surfaces must exist, which slots
  to restore); the writer holds the saved `RE::` originals and null-checks each
  pointer.

## Gap notes (silent-gap risk for the barrier)

1. **MeshCache/MeshEntry cluster ownership is unresolved.** The frozen
   `MeshReader.h` owns `MeshEntry`/`MeshCache`, but `MeshEntry` carries
   `TextureLab::RenderTarget` bakes and the Compositor drives the baking
   (`BakeInto`, `PrepareBake`, `PrepareDistance`) and the sweep. The
   buildup-plan lists `MeshReader` under engine/ as "fetch only", which argues
   the RenderTarget-bearing cache is render's. This shape places `MeshCache`
   in `engine/MeshReader.h` (faithful to frozen, keeps render's header self-
   contained for its own types) and has render include it. The orchestrator
   must confirm with the engine shape agent whether `MeshCache`/`MeshEntry`
   land in `engine/MeshReader.h` or move into `render/`. Everything render
   declares has a home either way; only the include direction is at stake.

2. **Two rendering entry points by design.** `RenderGeometry` (plan-driven) and
   `Render` (per-link primitive) both render. This over-covers the "takes a
   GeometryStackPlan" requirement while staying faithful to the frozen
   per-stack surface the engine may prefer. The reduce stage should drop
   whichever the engine seam does not use once that is settled; it is not a
   missing capability.

3. **`ApplyBinding` vs engine-driven binding.** The frozen `Manager` created and
   restored bindings itself; this shape also offers `ApplyBinding` over a
   `BindingDiff` per the brief. `BindingInputs.shellSettings`/`shellOriginal`
   are the engine-resolved shell owner (frozen `shellTop`/`shellOwner`). If the
   engine keeps driving creation directly, `ApplyBinding` is redundant, not a
   gap.
