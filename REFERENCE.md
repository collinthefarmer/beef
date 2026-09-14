# BetterEnchantmentEffects: what the code cannot say

The sources carry no comments. This file holds the facts a reader would
otherwise have needed one for: engine and Community Shaders (CS) behaviour
the bindings rely on, the decompile lines a port follows, binary layouts and
constant-buffer packings, and the reasons behind constants. It is organised
by module. Numbered `NOTES n` references point at `NOTES.md`, which records
each assumption with its basis and what happens if it is wrong; thread and
ownership rules are in `ARCHITECTURE.md`; the recipe format is
`schema/recipe.schema.json`. Third-party copies (`src/extern/`,
`src/cs/BSLightingShaderMaterialPBR.h`) keep their own comments.

## Foundation (`Core.h`, `Identity.h`, `PCH.h`, `Settings.*`, `SettingsFile.*`)

- `diagnostics/Trace` records bounded, owning text records without retaining engine
  objects. Command context is thread-local and explicitly carried through
  `SessionQueue` callbacks; a captured old load session remains identifiable after
  cancellation. The recorder serializes and flushes transition events under its
  own mutex, with a 512-event ring and 32-MiB per-run file limit. This diagnostic
  mutex is not a renderer synchronization mechanism. Logging instrumentation does
  not establish engine resource ownership or GPU completion.
- `tools/build-identity.py` runs before plugin compilation, writes a header only
  when identity changes, and stages the corresponding JSON manifest. The source
  SHA256 includes active project and vendored source, shader and build inputs;
  it excludes `_old`, tests, documents, generated build output, and external
  toolchain binaries. Effective recipe/settings fingerprints use FNV-1a 64 and
  are diagnostic comparisons, not security identities. Capture procedure and
  current limits: `docs/wip/render-state-diagnostic-checkpoint-2026-09-12.md`.

The plugin name is spelled once, in `Identity.h`. `BEEF_PLUGIN_NAME` is
defined by CMake from the project name (`target_compile_definitions … BEEF_PLUGIN_NAME="${PROJECT_NAME}"`);
the `#ifndef` fallback in `Identity.h` only keeps native builds, which do not
pass the define, compiling. `kNodePrefix` ("BEEF") names the shell and light
nodes the plugin adds to the skeleton; `kTextureFolder` is the plugin name and
roots the presenter texture paths under `textures\`.

`FormKeyOf` (`SettingsFile.cpp`) builds a per-record key `<plugin stem>~<local
form id hex>`. The local id is `id & 0xFFF` for an ESL/light master and
`id & 0xFFFFFF` otherwise, because a light master occupies only the low twelve
bits of the form id and the rest is the FE/xxx load-order prefix. The key is
lowercased so a later wave can match records case-insensitively.

`Settings` (`Settings.h`) is the engine-free preference record. It carries
only the preferences with a live consumer — the scope switches (`playerOnly`,
`enableShaders`, `thirdPerson`, `firstPerson`, `uniqueMaterial`),
`verboseLogging`, the animation clock (`animationFPS`, `animationSpeed`), and
`textureScale`. The INI is driven by one `SettingDesc` table
(`SettingTable()`), so a preference is spelled once and read by parse, write
and diff alike. The animation-speed clamp constants (`kMinAnimationSpeed`
0.05, `kMaxAnimationSpeed` 4.0) bound `animationSpeed`.

`textureScale` is the frozen absolute `runtimeTextureSize` reborn as a
`TextureScale` enum — a resolution *relative* to the armor's own maps
(`kQuarter`, `kHalf`, `kFull`), not a pixel count. It serialises to the INI as
a word (`TextureScale=Full`); an unrecognised word parses to the default
`kFull` (the pure `Settings::Parse` is total and never throws), and the
engine-facing `LoadSettingsFromDisk` logs the unrecognised token because the
pure core has no logger. Turning a scale into pixels and clamping the result to
64..4096 is wave-3 render/ work; the setting here has no consumer yet.

`SettingsFile` is the engine-facing half: the on-disk path
(`Identity::IniPath()`), load/save, and the process's single copy behind
`GetSettings`/`SetSettings`.

`g_logRing` (declared in `PCH.h`, defined in `main.cpp`) is a 300-line
in-memory spdlog ring the menu reads to show recent log lines; it is attached
to the logger alongside the file sink at plugin load.

## The recipe model (`recipe/Recipe.h`, `recipe/Words.h`, `recipe/Efsh.h`, `recipe/Merge.h`, `recipe/Signals.h`, `recipe/Importer.h`)

- `Words.h` is the frozen `Vocabulary.h`: one `inline constexpr` spec table
  per enum, in enum order, with a `static_assert` on row count. It is the
  single spelling the parser, writer, validation and bindings read, so a
  vocabulary word is never hand-copied. `kBipedSlots` (slots 30..40, with
  names `head`..`tail`) was a private table in the frozen `Recipe.cpp`; it
  lives in `Words.h` now so `mesh/` reads the slot names from one place.
- `kMaxRecipeRows` caps every recipe collection at read, the cap the frozen
  `NamedRows` lacked while `ParsePresets` had it. `kMaxRecipeDepth` bounds
  the recipe's recursive walks (the animation query and texel-type
  resolution through masks); the frozen code bounded only termination with a
  visited set, never depth.
- `Efsh.h` is the vanilla effect-shader animation model, the half of the
  frozen `Timing` module that serves the `efsh` signal; `Efsh::Evaluate` is
  called once per tick from the signal graph. Colours are `Core.h`'s `Vec3`,
  not a private `Rgb`. `EffectParams` drops the frozen `edgeFalloff` (never
  read). There are no intensity clamp constants: `Evaluate`'s speed and
  intensity are `1.0f` at the one call site. `SegmentAmount`, `LerpColor`
  and `EvaluateAlpha` are `Evaluate`-internal, not public. The colour-seeding
  logic (`ResolveEmissiveColor`, `NormalizeHue`, `Chroma`, `ColorPolicy`) is
  now private to `Importer.cpp`, since only the importer reads it;
  `BaselineAlpha` stays public because the importer seeds the `rest` and
  `edgeRest` curves from it.
- `Importer.h` imports from a vanilla effect shader with fixed defaults: the
  frozen `ImportDefaults` struct is gone (both call sites passed nothing),
  and `EffectShaderRecord::flags` with `kGreyscaleToColor`/`kGreyscaleToAlpha`
  are dropped (declarations with no reader). The shipped default values that
  were `ImportDefaults` fields are now `inline constexpr` in `Importer.cpp`;
  the version string `BetterEnchantmentEffects 0.1.0` stamped into
  `metadata.imported` lives there too. `ParseEffectShaderRecord` reads the
  vanilla EFSH JSON: colour keys and `edgeColor` are 0..255 bytes divided by
  255 into `Vec3`, missing or non-numeric fields fall back (alpha ratios and
  colour scale to 1, texture scale to 1, times and speeds to 0), and the frozen
  `edgeFalloff` is not read (its `EffectParams` field is gone). The
  colour-seeding helpers moved from `Timing` into `Importer.cpp` carry the two
  vanilla thresholds the code cannot name: a fill whose brightest channel is at
  or below `0.02` is treated as white (an unlit fill glows), and the edge tint
  is used only when its chroma exceeds `0.05` (a grey edge does not tint).
  `Importer.cpp` parses the EFSH dump with its own guarded `FloatAt`/`TextAt`/
  `ColorFrom` accessors over `nlohmann::json` and returns a single-error
  `std::expected`, deliberately separate from the recipe `Reader`/`LoadResult`
  vocabulary: the dump is a different, flat input and the recipe `Reader` is
  private to `RecipeRead.cpp`. This is the one place recipe/ uses a second JSON
  style, and it is intentional, not drift.
- `Merge.h` splits the frozen `Contribution` into `SlotContribution` and
  `LightContribution` over two `enum class` index types, `SlotSource` and
  `LightSource`. One `std::size_t` field in the frozen code meant an index
  into a geometry's placed recipes under `PlanGeometry` and into the actor's
  recipe instances under `PlanLights`; the two index spaces are now two
  types the compiler keeps apart.
- `RowTypes` (in `Signals.h`, bundling the `Recipe` and its compiled
  `SignalGraph`) carries the per-row type resolution and reference checks —
  `TexelTypeOf`, `SignalTypeOf`, `NamesTrigger`, and the per-row `Check*`
  functions — as free functions. Validation and the studio's edit-time check
  both call them, so the three diverging copies the frozen tree carried
  (`Validator`, `CheckSourceKind`, `EditCheck`) become one. A curve on a
  non-scalar signal is an error `SignalGraph::Compile` reports as a
  diagnostic, closing the frozen code's empty `if (n.curve && n.type !=
  kScalar) {}`.

## The recipe language (`Expression.h`, `Expression.cpp`)

One grammar serves signals (evaluated per tick on the CPU), masks (per
texel, by the interpreter shader) and curves (a function of `x`). A
`Program` is the compiled form: a bounded postfix op list.

```
expr   := or
or     := and ("or" and)*
and    := cmp ("and" cmp)*
cmp    := add (("<" | ">" | "<=" | ">=" | "==" | "!=") add)?
add    := mul (("+" | "-") mul)*
mul    := unary (("*" | "/") unary)*
unary  := ("-" | "not") unary | atom
atom   := number | "[" expr "," expr ("," expr)? "]" | "(" expr ")"
        | "@" name                      a signal, source or mask
        | "@" name "(" expr ")"         a declared curve applied to a value
        | function "(" expr ("," expr)* ")"
        | "x" | "mean" | "time" | "pi"
```

- Functions: `abs min max clamp saturate floor ceil frac sqrt pow sin cos
  step smoothstep lerp if`. There is no `^`.
- Comparisons and logic yield 0 or 1. Arithmetic is component-wise on
  vectors and a scalar broadcasts; a vec2 and a vec3 never mix. Every
  operation is defined for scalar-scalar, scalar-vector and same-size
  vectors; anything else, and division by zero, is 0. A type mismatch at
  runtime yields 0 and never throws.
- A keyword is a whole word: `or` inside `orbit` is not one.
- The limits in `Expression.h` (nesting depth, op count, stack size) are
  hard: input past them is an error, never a deep stack.
- A curve is a `Program` over `x` and `mean` that reads no rows, scalar in
  and scalar out; a null curve pointer evaluates to its argument. The
  curve's `mean` is the source's own mean luminance, so `x - mean` centres
  on the map's average.
- Inside a mask a name reads an image (source or mask) first and a signal
  after; a signal rename leaves a mask alone when an image shares the name.
  In `RenameReferences`, a signal reference is `@name` followed by anything
  but a name character or `(`; a curve reference is `@name(`.

## The lab's shaders (`RuntimeTextures.cpp`, `kShaderSource`)

One pixel shader over a full-screen triangle serves every mode of
`ShaderMode`; the interpreter, bake, ripple and classify passes are
separate shaders so a fault in one costs only its outputs. The shader's op
numbers are `Program::Op`'s enum values and the shader's switch is written
to them; its arrays are sized to `kMaxExpressionOps`, `kProgramRefs`,
`kProgramCurves`, `kRippleFirings` and `kMaxClusters`, and every count is
checked against the array before a pass runs.

`cbuffer Params` (b0):

| field | packing |
|---|---|
| `offsetScale` | xy uv offset, zw tile scale |
| `flags` | x mirrorU, y mirrorV, z transpose, w mode |
| `extra` | x source mip; y armor input (0 none, 1 displacement.r, 2 ao.b, 3 rmaos, 4 normal slope, 5 diffuse luminance); zw per mode: height = armor weight, noise weight; roughness = strength, contrast; masked glow = channel, threshold; channel = channel (as `Pick` reads it), slope-instead flag |
| `extra2` | height: x relief mean, y relief contrast, z noise mean; masked glow: x softness, y invert, z strength |
| `layer` | x source channel (as `Pick` reads it), y mesh space, z blend mode, w opacity |
| `layerColor` | rgb colour, w normalise factor |
| `layerMask` | x mask channel (-1 none), y channel bits, z has previous, w has source |
| `layerCurve` | x has curve; the curve texture is t3, 256 x 1 over 0..1 |

Mode formulas:

- Relief of an armor input at the raw UV, 0..1 with higher raised: input 1
  reads displacement.r, 2 reads RMAOS occlusion (b), 4 the normal map's
  slope, 5 diffuse luminance `dot(rgb, (0.299, 0.587, 0.114))`.
- Height = 0.5 + (relief - reliefMean) x armorWeight x reliefContrast +
  (noise - noiseMean) x noiseWeight. Centred so neither input pins the
  field at the clamp; the relief is stretched around its own mean because
  occlusion maps sit near white.
- Roughness: the armor's RMAOS with roughness (r) pulled toward smooth where
  the noise is bright; metallic, occlusion and reflectance pass through.
- Copy (mode 7): every channel of the source at the given mip.
- The `normal` blend is reoriented normal mapping: the value's normal
  rotated so its up follows the normal below; both maps are 0..1
  tangent-space encodings (NOTES 58). `lerp` is replace under the opacity
  mix, so it shares replace's arithmetic.
- A stack without a base starts transparent black: alpha is the fuzz
  weight, the coat strength or the subsurface thickness.

Interpreter constants (`cbuffer` of the interpreter pass): `code[256]` x
op, y number, z index; `refs[16]` x 1 = texture read (y slot) or 0 =
value; `refValues[16]` the value broadcast; `texParams[8]` x channel, y
mesh space, z normalise, w mip; `texTransform[8]` xy uv offset, zw tile;
`texFlags[8]` x mirror u, y mirror v, z transpose; `misc` x time, y op
count, z vector result. Every stack value is a float3 with a scalar
broadcast, so component-wise arithmetic matches the CPU's rule;
comparisons and logic read `.x`. `x` evaluates to 0 (it is not per texel)
and `mean` to 0.5. A pop from an empty stack reads 0. After the pops a
binary op has `b` as the first operand and `a` the second; a ternary op
has `d`, `b`, `a` in order.

Ripple pass: `rippleFirings[8]` xyz origin in bind-pose units, w age in
seconds; `rippleShape` x speed, y width, z decay, w 1 = disc; `rippleMisc`
x firing count, y frame. The source is the position bake, rgb = position /
(2 frame) + 0.5. Each firing is a front at distance age x speed from its
origin; a ring is a band of the given width, a disc everything inside;
fronts fade by exp(-decay x age) and combine by max. A pooled target keeps
its last content, so a pass with no firings paints it black.

Classify pass: `centroidRmaos[8]` per cluster in analysis order (roughness,
metallic, occlusion, reflectance); `centroidLuma[8]` x luma, y id;
`classifyWeights` the four RMAOS weights; `classifyMisc` x luma weight, y
cluster count. The RMAOS and diffuse maps at the mesh UV go to the nearest
centroid by the distance `NearestCluster` (Analysis.cpp) uses, the sum over
five axes of weight x (texel - centroid)^2, the first of equals winning;
the CPU function is the reference and the shader must agree with it on a
texel. The id is written as id / 255 grey. A weight that is not finite or
not positive counts as zero on both sides.

Lab mechanics:

- Presenters are `slot_00.dds` .. `slot_99.dds`, then `slot_100.dds` ..
  `slot_511.dds` (`kPresenterCount`, minimum two digits in `Identity.h`).
  `tools/presenter-textures.py` stages 512 opaque-black, 1x1 RGBA8 DDS files
  through an always-run CMake target. Each is a separate engine resource name.
  The files let an `NiSourceTexture` present a generated target like a material
  texture (NOTES 22). The generator is part of the build fingerprint.
- Loading must find the requested resource through the engine resource system,
  return the requested texture name (case/slash normalization and optional
  `textures\` prefix), and return an unclaimed presenter object. Retained
  presenter references reserve identities for the pool lifetime; allocation is
  still bounded to 512 names. A non-null fallback is not successful loading.
  Checks precede renderer replacement. Acquisition rejects changed renderer
  ownership, and `Texture()` returns null if the presenter no longer points to
  its target's renderer metadata. This does not provide target leases to existing
  material snapshots or preview consumers; that remains a separate change.
- Run 1789247531051909 demonstrated six simultaneous target identities sharing
  one presenter, with 24/32 acquisitions in the eight-Solo comparison pointing
  at another target's renderer. The installed mod had no texture directory and
  the previous CMake build staged no presenter assets. The new trace records
  requested and loaded presenter names and rejection reasons. The report counts
  acquisitions aliasing live presenters and acquisitions with wrong renderer
  pointers; correct captures must report zero for both.
- A target restores its presenter's original renderer metadata only while
  the presenter still points to that target's replacement. Teardown must not
  overwrite metadata installed by another owner. The lab's availability flag
  is atomic because initialization writes it on the game thread and preview
  requests read it on the render thread.
- Every pass saves and restores everything it touches on the immediate
  context, because the engine's state cache does not know the pass ran,
  and unbinds its target and the armor inputs before the engine binds them.
  `Get*` calls on the context AddRef what they return.
- Pass state is scoped: all eight render-target slots and all viewports are
  retained, including a zero-viewport state. Binding one target clears the
  other target slots and can clear conflicting SRVs in any shader stage
  ([D3D11 target binding](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargets)).
  All six stages' SRVs are therefore saved. The five graphics shaders retain
  their class instances; geometry, hull and domain shaders and predication are
  disabled for our draws, then restored. Output-merger UAV slot zero is saved
  when no render target is bound, because our one-target pass can displace it;
  its append/consume counter is preserved when rebinding. Other UAV slots are
  untouched, and restoration uses the last populated RTV slot rather than
  declaring all eight occupied
  ([D3D11 output-merger rules](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargetsandunorderedaccessviews)).
- Every pass and readback runs on the game thread while the render thread
  renders with the same context, so each takes the engine's renderer lock
  (re-entrant) for its duration; without it the driver crashes on a worker
  thread with nothing of ours on the stack (NOTES 53, 57).
  `TextureLab::RenderPass` owns that lock before capturing state. Its destructor
  restores the context, then releases captured COM references, then unlocks.
  It requires nonnull renderer/context references and cannot be copied or moved.
- A readback goes through a staging copy of the 1x1 mip and waits on the
  GPU; the log line before it names the step should the wait never end.
  Bytes are read as RGBA8, so any other format is refused before the map.
  `SampleMaterial` picks the mip whose side is still at least
  `kSampleSide`, so a sample point reads a mip average, never a sparse pick.
- Staging resources have COM ownership, and each successful map has a scoped
  unmap even if the returned data is unusable or a CPU allocation throws.
  Readback also disables and restores predication: D3D's predicate applies to
  `CopySubresourceRegion` as well as draw calls
  ([D3D11 predication](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-setpredication)).
  Mean readback checks the source format, mip count, data pointer and row
  pitch. Texture-extent queries take the renderer lock while reading resident
  metadata. Mesh-bake uploads reject sizes exceeding the D3D11 byte range
  before narrowing to the buffer descriptor's 32-bit width.
- Previews: the render thread records a request under `previewLock_` and
  gets the last finished target; the game thread renders what was asked
  for once per tick. A new generation drops entries nobody asked for in
  the last one; a retired target lingers in the graveyard for a moment so a
  draw list the render thread already built can still present it. After an
  apply or retire, pooled targets change hands, so every preview asked for
  again is re-rendered.
- The engine's placeholder textures are 1x1 and its renderer record says
  0x0 for streamed maps, so a texture's real size comes from the D3D
  resource (NOTES 43). `GetTexture` accepts a record's raw path and the
  `textures\` prefixed form (NOTES 7).

## Bindings (`Binding.cpp`, `Binding.h`, `PBRMaterial.h`)

- Slot writers retain both the property and its installed material. Retaining
  a property alone does not keep a replaced material alive. Writes check the
  installed material identity; material-binding liveness also checks the
  geometry's property identity. Shells retain their vanilla material and
  check the clone's property and parent before posing or writing emissive.
  `PbrMaterial::Bind` is the checked construction boundary; only it casts the
  engine material to the pinned PBR layout. `SlotWriter` and `MaterialInputs`
  consume that owning record. Raw layout getters on material and shell bindings
  have been removed; attachment checks remain necessary after construction.
- Temporary materials returned by `Create` and `CreateMaterial` are held in
  `BSTSmartPointer` through installation. The pinned CommonLib uses intrusive
  material references; CS's `Create` returns a regular-heap material with the
  matching virtual destructor (`reference/community-shaders/BSLightingShaderMaterialPBR.cpp`).
  A failed private-material installation drops the binding.
- Saved texture writes retain a `NiPointer` as well as the original texture.
  A replaced material slot can release its texture before the studio reads
  the saved texture's name; pointer identity alone does not keep it alive.
- `PBRMaterial.h` mirrors CS's `BSLightingShaderMaterialPBR` (pinned copy
  in `src/cs/`, provenance in `src/cs/SOURCE.txt`). CS adds no virtual
  functions, so the vtable layout is the base class's and only the data
  members extend it; `GlintParameters` comes from CS `src/TruePBR.h` at the
  same commit. A CS PBR material is told from a vanilla one (0xA0 bytes, no
  PBR fields) only by its vtable; CS's own test is `kVertexLighting` on the
  property plus a `kDefault` or `kMultiTexLandLODBlend` feature
  (`reference/community-shaders/TruePBR-GetRenderPasses-excerpt.cpp`).
  Reading PBR fields off a vanilla material runs past its allocation
  (NOTES 3, 4).
- Rejecting only `VTABLE_BSLightingShaderMaterial` does not identify PBR:
  vanilla `BSLightingShaderMaterialLandscape` also reports
  `kMultiTexLandLODBlend` and has a different vtable and field layout.
  `IsPBRProperty` additionally requires the vtable's allocation base, queried
  with `VirtualQuery`, to be the loaded `CommunityShaders.dll` module before
  reading extended fields. This rejects vanilla and other plugins' material
  implementations. It does not establish ABI compatibility across CS versions;
  the pinned layout remains an integration requirement.
- Slot to material field: emissive `emissiveTexture`; fuzz
  `featuresTexture1` (colour in rgb, weight in a); coat and subsurface
  share `featuresTexture0` (coat colour + strength, or subsurface colour +
  thickness); glint has no map, parameters only. CS evaluates fuzz only on
  materials without a coat or hair model, in the order coat, hair,
  subsurface, then either fuzz or glint (`TruePBR.cpp SetupMaterial`); a
  hair material takes none of them (NOTES 24, 48).
- CS keeps the subsurface colour in `specularColor` and its opacity in
  `subSurfaceLightRolloff`, and the PBR displacement scale in
  `rimLightPower` (NOTES 22, 48). Glint's fallbacks in `Vocabulary.h` are
  the values CS starts a material at.
- Materials are pooled by content (NOTES 5): a shared one would glow on
  every wearer, so a bound property gets a private copy. A feature's flag
  bits go on with its first write and off when its output is hidden,
  unless the material had them; "off" means back to what the material had
  for those bits.
- Shells: `kVertexLighting` stays set on the clone's property so CS still
  draws it as PBR (NOTES 4). The vanilla shell material is white diffuse,
  the original's normal map, rim lighting from the material's rim power
  (NOTES 33). Inflation edits the shell's private `NiSkinData`; Skyrim
  bones point along local X, Y and Z run across the bone (NOTES 35).
- Shell diagnostics capture source/clone palettes during creation, both after
  attachment/update, and the clone before/after its first pose plus after calls
  2 and 30. Pose-call count is not a render-frame counter; the skin frame ID
  and matrix-cache addresses are logged to distinguish actual cache progress.
  Local/world geometry transforms accompany these new snapshots. Bone records
  carry their stage role and cover up to 256 entries; `bone_count`,
  `sampled_bones` and `palette_limited` expose coverage. World-transform entries
  and node addresses are logged without dereferencing them. No source geometry
  or external transform owner is retained by the added diagnostics.
- `render/SkinPalette` restores only missing clone world-transform entries whose
  source bone node is null and whose transform address belongs to a recognized
  flattened tree. Source and clone must have distinct arrays, matching roots,
  bone counts and node identities. Validation finishes before any pointer write.
  Source geometry/skin, root and flattened owners are retained until teardown;
  the clone keeps its own skin data and matrix cache. Array/count/root changes
  invalidate the lease through `StillOwned`; the existing tick retirement path
  then drops it. Teardown clears only still-owned repaired entries before
  releasing the retained owners. No original skeleton field is changed.
- Flattened-tree layout comes from
  [PLANCK's BSFlattenedBoneTree declaration](https://github.com/adamhynek/activeragdoll/blob/master/include/RE/misc.h):
  0x80-byte entries with world transform at 0x34; VR count/array offsets are
  0x150/0x158. SE/AE offsets 0x128/0x130 are inferred by subtracting the
  NiNode base-size difference documented in the pinned CommonLib NiNode header.
  Access requires the engine's BSFlattenedBoneTree NiRTTI. Address membership
  must match an exact world-transform entry, not merely fall in an allocation.
  This layout inference still requires the live checkpoint; unknown ownership
  rejects the shell. Searches are bounded to 4096 nodes/bones and do not
  dereference external transform addresses. Owner array stability is checked
  before writes on the existing engine execution path; this change does not
  establish new synchronization against concurrent scene mutation.
- The engine exposes no constructor for `NiAlphaProperty`; the object is
  laid out by hand as vtable, zero refcount, empty `NiObjectNET`, flags
  (NOTES 32). The private `NiSkinData` copy is the same: vtable at word 0,
  refcount at word 2 (NOTES 35).
- A private skin copy keeps its bone count zero until its bone array is ready.
  Copied bone-weight pointers are cleared before individually allocating and
  copying the weights with the engine allocator. An owning `NiPointer`
  releases the partial skin if allocation fails; no weight allocation is
  shared with the source skin.
- Lights: Address Library IDs (SE, AE) for `NiPointLight`'s constructor and
  the shadow scene node's light registration come from powerof3's
  CommonLibSSE fork; the pinned CommonLibSSE-NG does not wrap them (NOTES
  29). `ShadowSceneNode::LIGHT_CREATE_PARAMS` uses powerof3's field names;
  the pinned header has the same 0x30-byte layout under placeholder names.
  CS Light Limit Fix keeps per-light flag bits in the `NiLight`'s
  `ambient.red`; inverse-square lighting (ISL) reads flags in
  `ambient.red`, cutoff in `ambient.green`, source size in `radius.z` (CS
  `InverseSquareLighting/Common.h`; NOTES 42). ISL's radius is
  sqrt(3920 (8 fade - cutoff size^2) / (2 cutoff)) with fade = intensity /
  4 and the cutoff at ISL's default unless overridden; the plugin writes
  the same reach for the non-ISL path. One light per recipe, third person
  only, at the skinned centre of the bones carrying the most vertices
  (NOTES 34).
- Geometry names ending in `Identity::ShellSuffix()` are shells the plugin
  attached; the apply traversal skips them, and shells are collected before
  applying because attaching one adds a sibling a live walk would visit.

## Engine events, hooks and the manager

- Recipes with errors (decided 2026-09-13): a row error (`where` starts
  with `signal`, `curve`, `source`, `mask`, `output` or `variant`,
  `RowLevel` in `Recipe.cpp`) keeps that row inert and the recipe applied.
  A recipe-level error (`file`, `recipe`, `clock`, `key ...`, format,
  metadata) keeps the recipe in the menu's loaded set (`g_loaded`, so it
  can be fixed in place) but out of the applied set (`LoadedRecipes()`,
  rebuilt by `RebuildApplied` on every publish). `RecipeStoreStatus::heldBack`
  counts them and the `recipes: ... held back` log line reports the count.
  Fixing the error in the menu applies the recipe on the next republish;
  introducing one withdraws it. The menu reads the same fact from a row's
  `problems` through `HasRecipeErrors`.
- `kMaxTextFileBytes` (`engine/TextFile.h`) caps a recipe or presets file at
  4 MiB. A recipe is a few kilobytes; the cap stops a stray binary dropped
  into the recipes folder from being read into memory whole. `ReadText`
  names the failure ("does not exist", "cannot be opened", "larger than
  the ... cap") so the recipe store's log line states the real cause instead
  of the parser's "not a JSON object"; an empty file reads as empty text and
  the store reports it as "empty".
- `kPostLoadGame` reports whether the load succeeded as the value of `data`,
  not a pointer to a boolean: SKSE dispatches `(void*)result` with length one
  ([SKSE load hook](https://github.com/ianpatt/skse64/blob/master/skse64/Hooks_SaveLoad.cpp)).
  A failed load leaves engine effects paused; only a successful load or new
  game resumes discovery and ticks.
- `SessionQueue` owns loading, generation, refresh coalescing, and equip timers
  under one mutex. Refresh reservations and their later callbacks must belong to
  the same active generation. Loading clears all bookkeeping atomically; failed
  submission releases its reservation only if that generation is still current.
  A stale callback cannot consume a new session's pending actor or carry its
  follow-up into that session. Scheduled callbacks reference queue state weakly.
  The submit adapter returns true only when SKSE accepts the task for later
  execution. Load transitions, callback execution, and queue destruction remain
  game-thread operations; producers may submit from other threads. The queue
  does not hold its mutex across engine calls or synchronize actor state itself.

- `kDataLoaded` loads recipes, registers the menu, and installs the event sinks
  and player-update hook when Community Shaders is present. The emissive path
  defaults off. Hook installation uses `call_once`: installing the same thunk
  twice would otherwise replace the saved original with the thunk itself.
- `kPreLoadGame` pauses runtime work and clears the prior session;
  `kPostLoadGame` resumes it. `kNewGame` clears then resumes. Tasks queued before
  clearing fail their generation check; ticks and new tasks are suppressed
  while loading. The player hook calls the prior update function first.
- CommonLib's `Actor::AddAnimationGraphEventSink` indexes `graphs.front()`
  without checking emptiness and both actor helpers scan the sink array without
  taking the event-source lock. This adapter instead checks graph pointers and
  calls the source's `AddEventSink`/`RemoveEventSink`, which lock and deduplicate
  registrations. An empty graph list leaves the actor unwatched safely.
- `ActorHandle::get()` returns an owning `NiPointer<Actor>`. The signal
  environment keeps that owner through each actor query; returning its raw
  pointer from a helper would release the reference before the query.
Decompile provenance (`decompiled/WornEnchantmentFX/plugin.c` unless noted):

| what | lines |
|---|---|
| `PlayerCharacter::Update` hook: vfunc 0xAD in `RE/A/Actor.h` (SE/AE; VR differs and is not built) | NOTES 12 |
| the log ring buffer the Log page reads | `all.c` 32840-32866 |
| equip, load and node-update sinks | 1404-1489, 1495-1508, 1383-1396; registration 2257-2334 |
| the engine swaps the worn mesh after `TESEquipEvent`; the original re-attaches 100 ms later | 1461-1482 |
| a player-applied enchantment on the worn extra list beats the base record | 2013-2039 |
| one pending refresh per actor; a duplicate request while pending runs it once more afterwards | 1513-1608 |
| the effect shader of an enchantment: the costliest effect's enchant visuals, then its enchant shader, then the first effect with either (as Visible Armor Enchantments) | 2013-2039 |
| `Timing.h`: `WornShaderData::Update`, `EvaluateAlpha`, `LerpColor`, `SegmentAmount` | 4199-4573 |
| `Update` | 4199-4264 |
| `EvaluateAlpha` | 4414-4573 |
| `SegmentAmount`: position inside a segment, 1 when degenerate | 4385-4408 |
| key times forced monotonic so a mis-authored record cannot make a negative segment | 4424-4437 |
| `fmod` that lands in [0, period) for negative inputs | 4442-4445 |
| the 1e-4 guard on every divide and zero-length segment | `__real_38d1b717` |
| edge colour and edge alpha | 4544-4552, 4553-4560 |

- The refresh after the delay sees the new piece's 3D, so it is the apply
  that fires `equip`; the immediate refresh on the equip event does not.
  `equip` carries the centre of the first bound geometry as its position.
- SKSE drains its task queue until empty, so a task may never re-post
  itself to wait for a later frame (NOTES 12); due times fire from
  `OnFrame`.
- Animation graph events arrive on the animation thread and reach the bus
  as `anim.<tag>` with the event's payload string as the trigger's arg;
  watching is per actor and renewed on every apply, since a reloaded 3D
  brings a new graph (NOTES 44). The vanilla hit event carries no position,
  so `hit.received` and `hit.dealt` have none and a ripple starts at the
  piece's centre.
- A recipe's clock survives a retire followed by a re-apply within
  `kCarryWindowMS` (an edit, an isolate, re-apply all), keyed by actor and
  recipe id, so a change never snaps the animation to zero; a piece put
  back on later starts fresh. Leaving freeze resumes every clock from the
  scrub, not from where the real clock ran on to.
- Signal state integrates forward (pulse phase, smoothing, trigger ages),
  so a scrub backwards or a jump rebuilds it from the start and advances it
  to the scrubbed moment in one step.
- Editor IDs: the engine keeps them for a few form types (keywords, magic
  effects); po3's Tweaks export answers for every type (NOTES 41). At load
  the store asks for every effect shader, enchantment, magic effect,
  keyword, armor, addon and light and builds the reverse map. A form key is
  written `0x92DED~Skyrim.esm` (po3's convention), file compared
  case-insensitively.
- Actor value names resolve through the engine's table once per name; the
  environment holds handles and form ids, never engine pointers across
  ticks, and answers zero for anything it cannot reach. `av` readings:
  current = max - damage; `damage` is the damage modifier negated; `max` is
  permanent plus the temporary modifier.

## Meshes, bakes and analysis (`mesh/Mesh.h`, `mesh/TextureSize.h`, `mesh/Islands.h`, `mesh/MaterialClusters.h`, `mesh/MeshFacts.h`)

- Renderer byte counts must fit D3D11's 32-bit byte range before a mesh
  copy or readback. Index-count multiplication uses `size_t` to avoid
  wrapping first. GPU readback also checks the source buffer's `GetDesc`
  byte width before submitting a copy. CPU buffer allocation lengths are
  not exposed by CommonLib's `BSGraphics::TriShape`; those copies still
  depend on the engine's vertex and triangle counts matching its storage.
- The frozen `Analysis` module is split in two that share no type, function
  or test: `Islands.h` holds the connected-component and UV-chart
  segmentation (`MeshAnalysis`, `AnalyseMesh`, `BuildIslandBake`),
  `MaterialClusters.h` holds the k-means++ material segmentation
  (`MaterialSample`, `ClusterMaterial`, `NearestCluster`, `DescribeTexel`).
  `MaterialClusters.h` does not include `Mesh.h`; it works on a
  `MaterialSample`, not a `MeshData`.
- Decode is a `mesh/` boundary, not the engine's: the engine reader fetches
  raw bytes only, and `DecodePartition(const RawPartition&)` turns raw
  vertex and index bytes into the trusted `MeshData`. `RawPartition` carries
  sized spans plus the vertex and triangle counts, so the decode bounds both
  counts against the actual buffer size and never reads past a buffer;
  malformed bytes yield no partition, never undefined behaviour. `TextureSize`
  is the other boundary type: its one constructor clamps to 64..4096, so a
  bake size downstream is always in range.
- The engine packs one vertex as: position 4 floats (xyz and a tangent
  component), uv 2 halfs, normal 4 bytes (xyz biased, 1 tangent), skinning
  4 half weights then 4 byte bone indices. Offsets come from the vertex
  descriptor; an attribute the mesh lacks has no offset. The vertex and
  triangle counts are the engine's own uint16 fields, so a copy holds at
  most 65535 of each; the CPU copy's length is the engine's invariant and
  cannot be checked from here. A skinned `BSTriShape` keeps buffers per
  skin partition (NOTES 47); the reader takes the CPU copy when the engine
  kept one, else reads the GPU back through the lab.
- `skinToBone` maps skin space into the bone; its inverse puts the bone's
  origin in skin space. A node's bind-pose position comes from the skin
  data when the node is one of the geometry's bones, else from the node's
  current position relative to the actor's root, which is bind-pose skin
  space for a standing actor.
- Bake frames: `position` maps -kPositionFrame..kPositionFrame per axis to
  0..1 in one frame shared by every geometry (the skeleton root is the
  origin, z up, so a standing body spans about 0.5 to 1 in z);
  `localPosition` maps into the geometry's own model bound; `distance` maps
  0..kDistanceFrame to 0..1; `worldUp` is how far the bind-pose normal
  points up; `partition` is 1 on the biped slot's triangles; `boneWeight`
  is the summed weight of the named bones (sorted, so two orders of one set
  share a key); `componentId` and `chartId` carry island id / 255 in x with
  `kNoIsland` vertices at 1 and need the analysis, not the mesh alone.
- A bake is cached under `<definition>@<size>`, never under the source's
  name, so a rename cannot serve the old picture and two names with one
  definition share a target; names never contain `@`. A node key names the
  node, not its position, so the snapshot can find the bake without looking
  the node up.
- Welding: `kMaxWeldCell` is 2^40 so the float-to-integer cast of a cell
  index is defined for any finite coordinate; only triangle corners weld,
  so a vertex no triangle reaches stays alone whatever it coincides with;
  a UV seam's duplicated vertices do not split a component. Union-find
  uses iterative path halving, so no call recurses. Islands rank by
  triangle count with ties by first vertex, so the same mesh always labels
  the same way; past `kMaxIslands` the rest are `kNoIsland` so a bake never
  overflows a byte. A component and a chart are twins when they hold
  exactly the same vertices, and the offers list the two as one.
- Material clustering uses a linear congruential generator so the same
  seed gives the same picks on every platform; k-means++ seeding stops
  early when every texel already sits on a centroid, since a duplicate
  centroid would only make an empty cluster; an empty cluster keeps its
  centroid. `DescribeTexel`'s words read low to high with a value at a cut
  going to the word above, except metallic, which reads "greater than" so a
  value at the cut stays non-metal. The description's word order is
  "{finish} {tone} {metal}".
- A geometry the engine builds from an armor addon has no authored name; it
  is named ` (<addon id>)[<index>]/ (<armor id>) [<weight>%]` (eight hex
  digits in each pair of parentheses), which the menu reads as
  `<armor> geometry <index> (addon <id>)`; the raw name stays the key
  (NOTES 49).
- Coverage of a bone is the summed weight it carries over every vertex as a
  share of all vertices: a bone moving half the mesh fully reads 0.5.

## Compositor (`Compositor.cpp`)

- Many PBR sets ship a displacement map that is a real texture and
  entirely black; a map is flat when its mean sits at either end (NOTES
  46), measured by a readback once per material. A height stack over a flat
  map starts from the shared neutral 0.5 target because CS offsets parallax
  by (height - 0.5) x scale (NOTES 50); the `relief` channel reads
  displacement when it is real and occlusion otherwise.
- A colour field is normalised by 0.5 / its mean luminance; mask data is
  not. A mask that is exactly one material channel reads the map directly;
  any other expression renders through the interpreter.
- A stack on a slot that edits an existing map renders at that map's own
  resolution, clamped between the requested size and the maximum; other
  slots start from black at the requested size. Writes alternate between
  the stack's target and the lab's scratch, and the last layer must land in
  the stack's own target: the first write is chosen by the parity of the
  shown layer count for that reason. Chaining depends on it. A stack given
  another stack's texture as its base reads that stack's own target while
  ping-ponging through the shared scratch, so the two never collide.
- A rendered mask is entered in the cache before its dependencies recurse,
  so a cycle finds an unfinished mask and stops. Depth is bounded at
  preparation; the interpreter refuses a mask that reads more names,
  images or curves than the pass holds.
- The cluster map is rendered under a source's settings and replaced when
  another source asks for other settings, so a source that reads it must
  hold the returned target for as long as it samples it.

## Recipe format details beyond the schema (`Recipe.h`, `RecipeRead.cpp`, `RecipeWrite.cpp`)

- Key priority is in `KeyKind` enum order, ten per step, so a magic-effect
  key outranks an enchantment key, which outranks an effect-shader key,
  down to material and default. A `default` key is written as the bare
  word, every other kind as `{"<kind>": <form or glob>}`. A key belongs to
  the last file loaded with it, which is said once in the log because a
  recipe sharing a key with a later file never resolves by it.
- Colours are 0..1 in the model. Light Placer's rule applies at both doors,
  the file and the studio's text form: a colour whose components are all
  numbers with one above 1 is read as 0..255. A constant signal is not a
  colour and is read as written. Validation refuses a stored colour
  component outside 0..1. A single number in a colour or vector field
  stands for all three components. Numbers serialise as the shortest decimal that reads
  back as the same float.
- Duplicate keys inside one JSON object are an error, found while parsing;
  every key no reader asked for is reported. `//` and `/* */` comments are
  accepted in recipe files. Nesting past `kMaxRecipeDepth` levels is rejected
  before the JSON parser recurses, from a brace/bracket scan of the raw text.
- `SerializeRecipe` writes the canonical form: `nlohmann::ordered_json` keeps
  object key order equal to the model's field order, and every field at its
  default is omitted. Parse-then-serialise reproduces a file byte-for-byte
  only when that file is already canonical (a shipped or importer-generated
  recipe, and the `tests/fixtures/recipes/*.json`). `schema/example-magicka.json`
  is a hand-authored illustration with explicit defaults, alignment and blank
  lines, so it round-trips to an equal recipe but not to identical bytes.
- Text forms: a parameter is `@name` for a reference, a number for a
  constant, `r, g, b` for a colour. A row name is letters, digits and
  underscores, not starting with a digit; a recipe id is a file stem
  (letters, digits, `-`, `_`, `.`, not starting with `.`).
- `meta` is free-form JSON object text kept verbatim; `imported` is
  `<plugin> <version>` on a generated recipe not yet edited, and a save
  drops it and moves the recipe to `user/<id>.json`.
- The slot tables in `Vocabulary.h` are the one spelling of what CS reads
  from each map (`BSLightingShaderMaterialPBR.h`): height reads red alone,
  emissive and normal are rgb, glint has no texture, the diffuse's alpha is
  a shell's visibility, and the feature maps pack a colour with a weight.

## Paint's term templates (`Paint.cpp`, `Region.cpp`, `TermKind.h`)

- A region stack builds to one expression: `and` is the product, `or` is
  `max`, `not` is the product with the complement, and the first term is
  `set`. `Build` writes a fixed shape (`or` as `max(chain, (T))`; `set`,
  `and` and `not` as the last group `(X)` and what precedes it), one term
  alone bare. Nothing reads the shape back: editing a kept mask loads its
  whole text as one raw `set` term. The built text never exceeds
  `kMaxExpressionLength`: a term that would push it past stops the build.
- Template spellings, one exact text per template: numbers carry at most
  four decimals (what `ParamText` writes); a
  threshold's operand is the source quantised to P levels when posterize >
  1; each edge is `smoothstep(c - s, c + s, X)` with the settings written
  as themselves, never summed; the threshold is the low edge times the
  complement of the high edge, each omitted where trivial (low 0, high 1),
  `step(0, X)` when both are, and invert wraps it; a component or cluster
  is `abs(@name * 255 - ID) < 0.5`.
- A term's source is named by an existing twin's name (same definition
  under another name), else a new row named as wanted and made unique among
  the taken names, so a term never adds a row the recipe already has.
- The scratch mask lives under a reserved name; Keep renames it, Discard
  removes it, a save drops it, and a layer still masked by it is unmasked.

## Menu mechanics (`MenuWidgets.cpp`, `MenuState.h`)

- `menu/MenuWidgets.cpp` is the menu's sole direct render dependency. It calls
  `TextureLab::Preview` through `render/RuntimeTextures.h` to obtain the
  read-only preview for a `Studio::TextureHandle`.
- `Studio::FieldKey` and the framework's `ImGuiID` are the same type, enforced
  by the renderer's static assertion; text and number buffers share that key.
- The menu frame borrows the snapshot, selected rows, names, state and intent
  collection. Widget scale comes from `Frame::scale`; geometry bone coverage
  comes from `GeometryRow::bones`. The mode bar exposes compose and paint.
- A field is keyed by the ImGuiID of its literal key in the ID scope it is
  drawn in; the page pushes a scope per recipe, output, layer and row, so
  one literal names a different field on every row and no string is built
  per frame. Zero is no field, as ImGui reads it (NOTES 54).
- `-FLT_MIN` is ImGui's exact "everything left"; `-1` leaves a pixel, and a
  stretch table measuring such content shrinks a pixel per frame (NOTES
  51).
- A field's active state is tracked from the input item itself: an item
  drawn after it would report its own state and the field would read
  inactive every other frame. A refused text keeps the text and the focus.
  The refusal reason is drawn on the foreground draw list so it takes no
  layout space.
- A drag payload is bytes ImGui copied; only a whole index is a row. An
  auto-resizing window starts narrow, so a floor on the width keeps a
  definition on one or two lines. A table's column weights set the split
  only when it first appears; after that ImGui keeps the dragged widths.
- The framework's `ImTextureID` is a D3D11 shader resource view pointer
  (NOTES 28).

## studio (`studio/Snapshot.h`, `View.h`, `Intent.h`, `Forms.h`, `Edits.h`, `Mask.h`, `Presets.h`, `TermTemplates.h`, `PaintSession.h`, `Board.h`, `Panels.h`, `SelectorEdit.h`, `Selection.h`, `Names.h`, `Rows.h`, `FieldCheck.h`, `History.h`, `Page.h`, `Widgets.h`, `Fields.h`)

- `Snapshot::status` (engine scalars plus loaded-file and recipe-error counts) is filled
  once on the game thread where the snapshot is built, so the render thread
  reads it from the immutable snapshot and never calls a live getter. The cost
  is a one-tick lag in the status line; the frozen tree paid a data race for
  freshness instead.
- `Snapshot::tickMS` is the configured tick interval. Loaded-recipe summaries
  own their keys, row counts, diagnostics and file paths; menu rendering never
  borrows the recipe store's mutable containers.
- Full recipe projections carry every output definition independently of
  geometry placement. The output-settings editor can therefore repair a
  selector that excludes every geometry. Selector editing uses typed values,
  never a parse of the human-readable selector description.
- `Studio::TextureHandle` is `RE::NiSourceTexture*` behind a forward
  declaration, so the pure module compiles and unit-tests natively with only
  the name in scope. The engine writes the pointer during snapshot build; no
  studio code dereferences it, which is why the native tests link without
  CommonLibSSE.
- `BuildMask` (`Mask.h`) composes a term stack into one expression, the shape
  the frozen `BuildRegion` used: `and` is the product, `or` is `max`, `not` is
  the product with the complement, the first term is `set`, and the built text
  never exceeds `kMaxExpressionLength` — a term that would push it past stops
  the build. Editing a kept mask loads its whole text as one raw `set` term;
  nothing reads the shape back.
- Term template spellings are one exact text per template (numbers carry at
  most four decimals, matching `ParamText`): a threshold's operand is the
  source quantised to P levels when posterize > 1; each edge is
  `smoothstep(c - s, c + s, X)`; the threshold is the low edge times the
  complement of the high edge, each omitted where trivial, `step(0, X)` when
  both are, invert wrapping it; a component, chart or cluster is
  `abs(@name * 255 - ID) < 0.5`. A term's source reuses an existing twin's row
  (same definition under another name) or adds a new uniquely-named row, so a
  term never duplicates a row the recipe already has.
- The mask editor names parts, charts, materials, bones and partitions from
  `mesh/` results (`MeshFacts`, `MeshAnalysis`, `MaterialAnalysis`,
  `PlainIslandSourceName`), not a studio-owned name table. The frozen tree
  carried `RegionsFile::partitionNames`/`boneNames` and `PlainBoneName`/
  `PlainPartitionName`; those are dropped so studio and mesh cannot disagree on
  what a partition is called.
- Names that changed from the frozen tree, and why: `region` is gone
  everywhere (`BuildRegion`→`BuildMask`, `RegionStack`→`MaskStack`,
  `RegionsFile`→`MaskPresets`, `WhatPresetTerm`→`PresetTerm`,
  `ProposedRegionName`→`ProposedMaskName`, the `LoadRegion`/`ClearRegion`/
  `UndoRegion`/`RedoRegion` intents→`LoadMask`/`ClearMask`/`UndoMask`/
  `RedoMask`, `MenuState::region`→`mask`, `regionHistory`→`maskHistory`); the
  format calls the thing a mask and `island` stays the mesh word.
- Dropped from the frozen studio surface as dead or design-only:
  `Mode::kDesign` (so `kModeCount == 2`), `Layout::rowThumbnail`/`designPanel`/
  `widgetScale`/`compositeSize`/`implemented`, `Studio::Unresolvable`, and the
  thirteen `where` presets with their `PresetKind` split.
- Studio edits the full recipe format. Beyond the frozen `RecipeEdit` set,
  these arms cover fields `_old` left uneditable: `SetPriority`,
  `SetClockSpeed`, `SetOutputReplace` and `SetOutputSelector` (material
  outputs), and `SetLightReplace`/`SetLightSelector` (lights).
- A `Selector` has no free-text spelling in the editor; `SelectorEdit.h` is its
  structured editor. `SelectorViewOf` projects a `Selector` to a `SelectorView`
  (`matchAll` = the `anyOf` is empty, one `SelectorClauseRow` per clause with
  `isForm` set exactly when the kind is `kAddon`, `value` the form-ref text for
  an addon clause or the glob otherwise). `SelectorWithClause`/`WithoutClause`/
  `WithKind`/`WithOperand` return a new `Selector`; each is bounds-checked and
  returns the input unchanged on an out-of-range index. A kind change to or from
  `kAddon` resets the operand to the matching variant alternative (an empty
  `FormRef` or an empty glob), because the two carry different operand types. A
  `kAddon` operand parses through `FormRef::From`; a blank string is refused
  (the clause is left unchanged, matching the loader's "must be an editor ID or
  \"0x<id>~<plugin>\"" rule), and an unresolved-but-non-blank ref is kept for
  the field check to report against loaded plugins rather than rejected here.
  The page picks `SetOutputSelector` or `SetLightSelector` for the amended
  `Selector`; `OutputHeaderForm` surfaces the `SelectorView` beside the material
  output's `replace` toggle, and `RecipeHeaderForm` exposes `priority`
  (empty clears to the key-derived default) and `clockSpeed`.
- The UI-primitive layer (`studio/Page.h`, `Widgets.h`, `Fields.h`,
  `Fields.cpp`) is pure spec data; the engine renderer resolves what needs an
  ImGui call. `Width` carries the intent only: `kPx` a pixel count, `kFill` a
  fraction of the available width, `kFit` the label text the renderer measures
  against the current font. Measuring `kFit`, turning a `RuleSpec` into drawn
  buttons, and reporting which button was pressed (`RuleClick.index`, the
  data replacement for `_old`'s `RuleLine` `std::function`) all belong to the
  renderer; the pure side supplies only the button records and their action
  tags. `ThumbnailSpec` likewise carries a texture handle, channel and a size
  the renderer draws.
- `Page::scale` and `ScaleOf` carry the UI scale the frozen tree threaded as
  `Layout::widgetScale` (dropped from `Layout` as a per-mode design field). It
  is renderer-supplied — a font/DPI factor no pure data can compute — so it
  lives on the per-frame `Page` and the field/width specs consume it there.
- Toggle and choice fields cross the `FieldBinding` boundary as text: a toggle
  is the literal `"on"`/`"off"`, a choice is the exact word from its table
  (`BlendName`, `ShellMaterialName`, `DefaultSignalKind`'s kind word). This is
  why `BindLightShadow`, `BindShellDepthBias` and `ToggleField` compare against
  `"on"` — the renderer maps a checkbox state to that token, and the pure bind
  turns the token back into a boolean edit.
- `BindSignalMember`/`BindSourceMember` copy the whole signal/source record
  into the closure by value, set one member through a member pointer, and emit
  a `SetSignal`/`SetSource` carrying the amended record. The record is captured
  by value (init-capture), not by `const&`, so the closure owns its state and
  no dangling reference outlives the form build.
- `Names::GeometryLabel` parses the engine-built geometry name and renders it as
  `"<armorName|armor <armorId>> geometry <index> (addon <addonId>)"`. The engine
  grammar it recognises is
  `" (" + <8-hex addon FormID> + ")[" + <decimal index> + "]/ (" + <8-hex armor FormID> + ") [" + <percent> + "]"`;
  any string not matching this exact shape is returned verbatim.
- Edit-time validation (`Edits.cpp`) reuses the `recipe/Signals.h` `RowTypes`
  checks (`CheckLayer`/`CheckSource`/`CheckMask`/`CheckCurve`/`CheckOutput`); an
  edit is refused only when it introduces a new diagnostic, so the editor cannot
  accept a value the loader rejects. `Apply` compiles a `SignalGraph` for edits
  that need typed-row checks: an interactive edit-time path, not per-tick. No
  public shell row-check exists, so shell scalar/vector reference type-checks
  compose the public `SignalTypeOf` query directly; route them through a
  `CheckShell`-equivalent if one is later published.
- The mask-editor presets file (`regions.json`, path from `Identity.h`) is one
  top-level `presets` array; the old `where`/`what` split and the
  `names.partitions`/`names.bones` maps are gone. Each entry: `name` (required),
  optional `partition`, `bones`, `expression`, `sources`, and must carry at least
  one of expression/partition/bones. Caps are parse errors, never UB:
  `kMaxPresets` 256 entries, `kMaxPresetBones` 64 per preset, `kMaxPresetSources`
  16 per preset. A file with no `presets` array is zero presets, not an error.
- `BuildMask` emits into the recipe expression language: `and`→`a * b`,
  `or`→`max(a, b)`, `not`→`a * (1 - b)`; a single whole-parenthesised term is
  unwrapped; growth stops at `kMaxExpressionLength`.
- Mask-editor term and analysis names come from `mesh/`: partition label =
  `SlotCoverage.name` (fallback `BipedSlotName`), bone label = raw
  `BoneCoverage.name`/`MeshIsland.dominantBone`, material label =
  `MaterialCluster.description`. The old friendly-name table is dropped, so bone
  labels are raw skeleton names.

## Build and tools

- Project targets bypass the compiler launcher; third-party targets still
  use ccache when available. A cached rebuild produced Ninja objects with
  zero header dependencies, allowing incompatible `TextureLab` layouts in
  one DLL. The binary evidence and dependency reproduction are recorded in
  `docs/wip/crash-2026-09-11.md`. Do not restore caching for project targets
  without verifying that cache hits preserve their header dependencies.
- `CMakeLists.txt`: nothing is installed, which also keeps CommonLibSSE-NG
  from exporting a target set that would need spdlog exported too; spdlog is
  fetched with `OVERRIDE_FIND_PACKAGE` because CommonLibSSE-NG calls
  `find_package(spdlog CONFIG REQUIRED)`; CommonLibSSE-NG is pinned to the
  last commit on main as of 2026-09-03 since no release tag covers Skyrim
  1.6.1170; the rapidcsv cache variable is seeded to short-circuit its
  `find_path`; the xwin SDK layout is case-sensitive on Linux and ships
  lowercase import libraries while CommonLibSSE-NG links mixed-case names;
  clang defers template parsing the way MSVC does because CommonLibSSE-NG
  uses dependent-base members without `this->`; a PDB is emitted in every
  config because SKSE crash logs need one; the test sources also build as
  Windows console executables; `dist/<mod>/SKSE/Plugins/` is staged and
  recipes are never staged.
- `cmake/clang-cl-xwin.toolchain.cmake`: cross-compiles an x64 MSVC-ABI
  binary with clang-cl and lld-link against the CRT and SDK from `xwin
  splat`; `XWIN_DIR` is the splat root (holding `crt/` and `sdk/`), default
  `~/.xwin/splat`. xwin splats only the release CRT, so every config links
  `/MD`.
- `setup-xwin.sh` downloads the CRT and SDK (about 630 MB) into `XWIN_DIR`.
  `build.sh [Release|Debug] [extra cmake --build args]` configures and
  builds; use `-j 4` on WSL. `install.sh` copies the staged mod folder into
  the MO2 mods directory in one transfer (rsync, or tar when rsync is
  missing); the INI is copied only when the mod has none, since it holds
  the user's settings; a DLL locked by the running game fails on that one
  file while the rest transfer, and the exit status is kept.
- `tests/run-native.sh [--update]` builds and runs every engine-free suite
  with `NATIVE_CXX`, else `CXX`, else clang++ on PATH, into `TEST_OUT_DIR`
  (default `build/native-tests-<compiler>`); the dev shell exports
  `NATIVE_CXX` because its `CXX` is g++, which cannot build this code under
  `BEEF_SANITIZE`, and the run stops rather than use it there. Each source
  compiles once and a suite links the objects it names; `--update` rewrites
  the importer's expected recipes; the checked-in recipes are validated
  against the schema when `check-jsonschema` is on PATH.
- `tools/compile-db.sh` writes `build/clangd/compile_commands.json` from the
  Release configure, reduced to this repo's `src/` and `tests/` so clangd
  indexes our code and not CommonLibSSE's; rerun after adding a source.
- `tools/rename.py` drives the `clangd` on `PATH`, which the dev shell
  makes the unwrapped one: a wrapped clangd adds the host's glibc and
  libstdc++ include paths, which shadow the Windows SDK's. clangd starts
  indexing when a file it covers is first opened; a file edited since the
  last run is re-indexed after the progress token ends, so a symbol it
  declares can be missing for a while; edits are applied from the end so
  earlier offsets stay valid.
- `tools/efsh_dump.py` reads EFSH `DATA` by file offset: the 400-byte file
  layout differs from CommonLibSSE-NG's `EffectShaderData` after 0xF4 (the
  file stores the addon models and ambient sound form IDs in 4 bytes each,
  the runtime in 8); older records ship a 308-byte `DATA` without flags or
  texture scale, which default to 0 and 1.
- `tools/make_flipbook.py` names its folders after CMake's `project()`,
  bakes a frame exactly as the lab's shader tiles, mirrors, transposes and
  scrolls, and records the mean luminance of frame 0 so the plugin can
  divide texture brightness out.
- `src/Identity.h` is the only place the plugin name is spelled; CMake's
  `project()` feeds it. Recipes load from `Data/<plugin>/<anything>/*.json`
  recursively; the importer writes under `imported/`, the menu saves under
  `user/`, which loads last. The shipped region presets sit beside the DLL
  rather than under the recipe root, which loads every `.json` below it as
  a recipe.

## planners (`planners/ActorState.h`, `planners/StackPlan.h`, `planners/BindingPlan.h`, `planners/ActorPlanning.h`, `planners/TextureIdentity.h`)

- `TextureIdentity.h` holds the two engine-free halves of
  `render/SourceSampling`: `ImageCacheKey` lowercases a texture path so
  the compositor's image cache treats two spellings of one file as one
  entry, and `IsPlaceholderExtent` treats a map of
  `kPlaceholderTextureExtent` (4) texels or fewer on either axis as a
  placeholder. The threshold is carried from the frozen tree
  (`src/_old/Compositor.cpp`); it separates the engine's tiny fallback
  textures from real maps, and no source records a finer reason.

The pure decision halves of the wave-3 engine modules (`engine/Manager`,
`render/Compositor`, `render/Binding`). Each is data-in / data-out and native-
tested; the wave-3 shell is a thin adapter that owns the real `RE::` handles and
calls these with value records. The split's discipline: no planner takes or
stores an `RE::` pointer.

- **Opaque handle spaces.** `PieceId`, `InstanceId`, `PlacementId`, `RecipeId`,
  `OutputIndex` are `enum class : std::size_t` typed index spaces (as
  `Merge.h`'s `SlotSource`/`LightSource` already are). They carry no
  enumerators, so the one-spec-table-per-enum rule does not apply to them — a
  handle is an index, not a closed set. `RecipeId` indexes the recipe store the
  shell passes as `std::span<const Recipe>`; the pure `ActorState` never stores
  a `const Recipe*`, because a long-lived table cannot own a pointer's validity.
  `Merge`'s `PlacedRecipe` holds `const Recipe*` only as a transient function
  argument, never in a table — `GeometryPlacement`/`ActorLightPlan` follow that
  same transient-only rule.

- **Three-table `ActorState`** (DECIDED 2026-09-09; `design-actor-state-tables`).
  A `Piece` is one engine geometry, not an armor: it carries its own
  `GeometryIdentity` plus the armor's `WornPiece` match keys, denormalised onto
  each geometry so selector matching is per-geometry. `Instance` grain is per
  recipe per actor per enchantment form (`FindInstance` dedups by
  `RecipeId` + enchantment `FormKey`; unenchanted matches share one instance per
  recipe). `Placement` joins an instance to a piece with the `RecipeKey` it
  matched by and per-surface-output selection. The engine runtime that the
  frozen `Manager` nested under these — `MaterialBinding`/`ShellBinding`/
  `LightBinding`, `SignalState`/`ActorEnvironment`, `startMS`/`lastTime` — is
  shell-owned state keyed by the same handles, not fields of the pure tables.
  `Piece::lost` is the one shell-maintained flag the pure `AnyLivePiece` reads
  (a geometry whose material or shell another system replaced); light-liveness
  stays a shell check because a light is a `RE::` binding.

- **Row projections belong to `studio/`, not here.** The frozen `BuildSnapshot`
  inlined its row builds. The recipe-definition → row projection is a view-model
  concern owned by `studio/Rows`/`Panels`/`Board`. The planners expose only the
  `ActorState` tables and structural queries. The studio `Snapshot` is assembled
  in wave-3 glue, which reads `ActorState` for structure and overlays the live
  and engine-only fields (evaluated `SignalState` values, the `StackPlan`'s
  animated flag, resident `RE::NiSourceTexture*`, rendered stack size, `EditorID`
  lookups, reference counts) over the studio rows. The planners never build the
  `Snapshot` type.

- **`StackPlan` classification.** A slot's chain and its replace cut come from
  `Merge`'s `SlotPlan`; `PlanStacks` adds static-vs-animated. `selfAnimated` is
  `IsAnimated(recipe, Output{SurfaceOutput})` (`Recipe.cpp`) — true iff a layer
  reads a scrolling/tiling image, a ripple, a non-constant signal, or a mask
  over one. `animated` is the chained-base result: link 0 renders over the
  static base map (an armor's own texture and the neutral-height base do not
  animate), so `animated[0] = selfAnimated[0]` and
  `animated[i] = selfAnimated[i] || animated[i-1]`. This is the frozen
  `Compositor::Render` guard `!animated_ && !base.animated && renderedOnce_`
  turned into a plan: a static link over an animated base has `animated == true`
  and re-renders each tick. `ChainIndexOf` is the frozen anonymous `MergeOf`,
  retyped onto `SlotContribution`.

- **`BindingPlan`.** `PlanBinding` decides which surface bindings a geometry
  needs and selects the highest-priority shell contribution as `shellOwner`.
  It does not describe incremental restoration: application rebuilds actors;
  material ownership and restoration belong to `render/Binding`.

## Generated texture references

`TextureRef` distinguishes static engine textures from registered generated
textures. A generated reference retains its RenderTarget and acquisition
generation. The target cannot enter the pool until every lease is released;
static textures retain their ordinary NiPointer. The registry stores weak targets
and tombstones, so it does not extend target lifetime or reinterpret an expired
generated handle as static. Presenter objects remain retained for pointer identity; presenter slots are
leased to live targets and become reusable after target teardown. Registering a still-live presenter for another target
or generation is rejected. This is CPU lifetime ownership, not GPU completion.

Material journals, prepared sources/masks/material inputs, compositor base and
material-analysis records, snapshots and preview entries/work retain TextureRef.
SlotTarget writes accept a TextureRef; raw pointers remain at engine/renderer
calls and Studio row transport, backed by their retaining snapshot. References
validate generation and renderer backing on access. Empty means restore/default;
a rejected reference is invalid and cannot request a material restore.

The current producer may update its pixels while consumers retain the same
resource. Snapshot pixel immutability, per-field restore ownership, atomic
application replacement, resource budgets and GPU retirement fences remain
separate contracts. The existing render/update synchronization policy is unchanged.

## Cleanup ownership contracts (2026-09-13)

See [the cleanup checkpoint](docs/wip/cleanup-checkpoint-2026-09-13.md) for the
material group journal, retained external textures, explicit actor retirement,
presenter slot leases, skin palette ownership, and preview submission contract.
`SourceSampling` centralizes compositor sampling policy. `ChangeAndRebuildActors`
retires candidates before mutation and rebuilds them afterward; its recipe
argument scopes reporting, not actor selection. Generated compositor outputs and
snapshot retention now use `TextureRef` directly.
