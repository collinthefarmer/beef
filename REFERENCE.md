# BetterEnchantmentEffects: what the code cannot say

The sources carry no comments. This file holds the facts a reader would
otherwise have needed one for: engine and Community Shaders (CS) behaviour
the bindings rely on, the decompile lines a port follows, binary layouts and
constant-buffer packings, and the reasons behind constants. It is organised
by module. The recipe format is `schema/recipe.schema.json`; `README.md` gives
the reading order and lists every canon document. Third-party copies
(`src/extern/`, `src/cs/BSLightingShaderMaterialPBR.h`) keep their own comments.

Four sources cited below are not in the repository and never were: `NOTES.md`
(numbered `NOTES n` assumptions with their basis), `ARCHITECTURE.md` (thread and
ownership rules, deleted by `47cb742` when `REQUIREMENTS.md` became canonical),
and the local `reference/` and `decompiled/` trees the ports were read from. A
citation into them names provenance, not a file a reader here can open.

## Foundation (`Core.h`, `Identity.h`, `PCH.h`, `Settings.*`, `SettingsFile.*`)

- `diagnostics/Trace` records bounded, owning text records without retaining engine
  objects. Command context is thread-local and explicitly carried through
  `SessionQueue` callbacks; a captured old load session remains identifiable after
  cancellation. The recorder serializes and flushes transition events under its
  own mutex, with a 512-event ring. The file is written in 32 MiB segments
  (`kTraceSegmentBytes`): when one fills, the recorder opens the next
  (`<name>-trace-<run>-<n>.jsonl`), deletes the one before the previous, and
  starts the new segment with a `rotated` event that repeats the startup
  identity, so the last 32 MiB of a run are always on disk and at most 64
  MiB are; `tools/trace-report.py` reads a segment's siblings in sequence
  order. This diagnostic
  mutex is not a renderer synchronization mechanism. Logging instrumentation does
  not establish engine resource ownership or GPU completion.
- `cmake/BuildIdentity.cmake` runs on every Windows build, before plugin
  compilation, and writes `BuildIdentity.h` and `build-identity.json` only
  when their content changes. The build ID is the first 12 characters of the
  Git revision and the configuration, with a source marker between them:
  none when the build inputs are clean, `dirty-<hash>` when they have changes,
  and `archive` for a `git archive` extraction. The build inputs are `src`,
  `cmake`, `CMakeLists.txt`, `CMakePresets.json`, `COPYING.md`, `flake.nix`
  and `flake.lock`, listed in `cmake/BuildIdentity.cmake`; edits elsewhere,
  such as docs, leave the build ID and the DLL unchanged. The dirty hash is the
  first 12 hex characters of the SHA-256 of `git diff HEAD --binary` over the
  build inputs and the names of their untracked, non-ignored files; it
  identifies the change set, not file content outside the diff. Effective recipe/settings fingerprints use FNV-1a 64 and
  are diagnostic comparisons, not security identities. Capture procedure and
  current limits: `docs/checkpoints/render-state-diagnostic-checkpoint-2026-09-12.md`.

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

## The recipe model (`recipe/Recipe.h`, `recipe/Words.h`, `recipe/Efsh.h`, `recipe/Merge.h`, `recipe/Signals.h`, `recipe/Importer.h`, `recipe/Visit.h`)

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
  `BaselineAlpha` stays public because the importer fills the
  `importFillBase` and `importEdgeBase` template rows from it.
- `Importer.h` imports from a vanilla effect shader by patching a template
  recipe: `ImportEffectShader(record, template)` copies the template, then
  replaces the id, metadata and keys, retargets every `EfshSignal` to the
  imported shader, overwrites the value of every `ConstantSignal` whose name
  is in the `kImportFacts` table (`ImportSignalNames()` publishes the
  vocabulary), and rewrites every `ImageSource` whose path is the
  `$fillTexture` token to the shader's fill texture with its tiling clamped
  to at least `0.01` (a zero tile would collapse the image). The two shipped
  templates, `templates/fill.json` and `templates/bare.json`
  (`ImportTemplateId` picks by fill-texture presence), declare their
  reserved `import*` rows with placeholder values and name their EFSH rows
  `ImportedEffectShader`, so a template file is itself a load-clean recipe
  the validator and the schema accept. CMake stages them beside
  `presets.json`'s folder under `Data/SKSE/Plugins/<plugin>/templates/`,
  outside the recipe root, which loads every `.json` below it as a recipe.
  The version string `BetterEnchantmentEffects 0.1.0` stamped into
  `metadata.imported` lives in `Importer.cpp`; the generated description
  names the template. `ParseEffectShaderRecord` reads the
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
  `LightContribution` over two `enum class` index types, `SlotContributor` and
  `LightContributor`. One `std::size_t` field in the frozen code meant an index
  into a geometry's placed recipes under `PlanGeometry` and into the actor's
  recipe instances under `PlanLights`; the two index spaces are now two
  types the compiler keeps apart.
- `RowTypes` (in `Signals.h`, bundling the `Recipe` and its compiled
  `RecipeGraph`) carries the per-row type resolution and reference checks —
  `TexelTypeOf`, `SignalTypeOf`, `NamesTrigger`, and the per-row `Check*`
  functions — as free functions. Validation and the studio's edit-time check
  both call them, so the three diverging copies the frozen tree carried
  (`Validator`, `CheckSourceKind`, `EditCheck`) become one. A curve on a
  non-scalar signal is an error `RecipeGraph::Compile` reports as a
  diagnostic, closing the frozen code's empty `if (n.curve && n.type !=
  kScalar) {}`.
- `Visit.h` is the published recipe traversal: reach for a walker here before
  hand-rolling a loop over `outputs`/`stack`/params. Each walker takes
  `Recipe &` and a callback and hands back the typed node with its
  `PropertyLocation` (owner + property), so `SurfaceOutput`-vs-`LightOutput`
  variant skipping and the `Get<SurfaceOutput>` descent are handled once, not at
  each call site (a light output has no layer stack). The location vocabulary
  (`ResourceKind`, `ResourceRef`, the `*Owner` records, `PropertyLocation`) is
  shared; `docs/conventions.md`'s glossary carries the "extend this, do not grow
  a private walker" policy. Which walker:
  - `ForEachMaterialLayer(recipe, Fn(Layer &, LayerOwner))` — every material
    layer; the light outputs are skipped for you.
  - `ForEachImageRef(recipe, Fn(Ref &, PropertyLocation))` — each layer's
    `source` and `mask` reference (mutable, so rename and force-clear go through
    it).
  - `ForEachCurveRef(recipe, Fn(CurveRef &, PropertyLocation))` — named curves on
    signals and layers.
  - `ForEachText(recipe, Fn(std::string &, bool isMask, PropertyLocation))` —
    every editable expression string: signal expressions, inline signal/layer
    curve text, curve text, mask text; the bool marks a mask expression.
  - `ForEachSignalRef(recipe, Fn(Ref &, PropertyLocation))` — every param that is
    a signal reference, across signal/source/output/shell params.
  - `ForEachParam(recipe, Visitor &)` — the master numeric/vector/ref walk the
    ref walkers are built on; use it directly for a visitor that reads scalars
    and vectors (literal folding does).
  - `ForEachOverrideName(recipe, Fn(const std::string &, PropertyLocation))` —
    variant override names.
  A visitor for `ForEachParam` implements `Owner`/`Property`/`Reference`/
  `Scalar`/`Vector` (plus the `Optional*` forms); `LocatedVisitor` supplies the
  first two and `RefVisitor<Fn>` adapts a `Fn(Ref &, PropertyLocation)` into the
  protocol. The `Visit*Params` dispatchers (`VisitSignalParams`,
  `VisitSourceParams`, `VisitSurfaceParams`, `VisitLightParams`,
  `VisitOutputParams`, `VisitShellParams`) walk one node kind when a whole-recipe
  pass is too much. `studio/Edits.cpp` drives the whole family — rename, its
  `CountReferences`, literal folding, and the forced mask removal
  (`ForEachMaterialLayer`) — so a new traversal has a pattern to copy.

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
  step smoothstep lerp if length distance dot cross normalize`. There is
  no `^`.
- The vector family (`length`, `distance`, `dot`, `cross`, `normalize`)
  rejects scalar operands at check time: the shader represents a scalar
  as a splatted float3, so a GPU `length` or `dot` of one would disagree
  with the CPU, while a vec2 is `float3(x, y, 0)` and agrees. `length`,
  `distance` and `dot` are the only functions that change a value's
  type (vector in, scalar out); `cross` takes and yields vec3s;
  `normalize` keeps its operand's size and maps a zero vector to zero
  rather than NaN. Every other function preserves its operand's type.
- Comparisons and logic yield 0 or 1. Arithmetic is component-wise on
  vectors and a scalar broadcasts; a vec2 and a vec3 never mix. Every
  operation is defined for scalar-scalar, scalar-vector and same-size
  vectors; anything else, and division by zero, is 0. A type mismatch at
  runtime yields 0 and never throws.
- A keyword is a whole word: `or` inside `orbit` is not one.
- Checking and evaluation keep separate stacks on purpose (`Expression.cpp`).
  `TypeStack` grows, because a check reports the mismatch it finds and the
  depth it may reach is bounded only by `kMaxExpressionDepth`. `ValueStack` is
  a fixed 64 values: a pop from an empty stack reads 0 and a push past the top
  is dropped, so `Evaluate` allocates nothing, throws nothing, and is `noexcept`
  throughout. The bounds `Program::Check` enforces are what keep a dropped push
  from reaching a valid program.
- The limits in `Expression.h` (nesting depth, op count, stack size) are
  hard: input past them is an error, never a deep stack. `kMaxExpressionOps`
  (256) is also the interpreter shader's array size, written there as the bare
  literal `float4 code[256]` (`render/ShaderSource.cpp`), so the two must agree
  or a long mask overruns the constant buffer. The value is carried unchanged
  from the frozen `src/_old/Expression.h`; why 256 rather than another power of
  two is not recorded anywhere.
- A curve is a `Program` over `x` and `mean` that reads no rows, scalar in
  and scalar out; a null curve pointer evaluates to its argument. The
  curve's `mean` is the source's own mean luminance, so `x - mean` centres
  on the map's average.
- Inside a mask a name reads an image (source or mask) first and a signal
  after; a signal rename leaves a mask alone when an image shares the name.
  In `RenameInExpression`, a signal reference is `@name` followed by anything
  but a name character or `(`; a curve reference is `@name(`.

## The lab's shaders (`render/TextureLab.h`, `TextureLabLifecycle.cpp`,
`TextureLabPass.cpp`, `TextureLabReadback.cpp`, `ShaderSource.cpp`)

One pixel shader over a full-screen triangle serves every mode of
`TextureLab::Mode`; the interpreter, bake, ripple and classify passes are
separate shaders so a fault in one costs only its outputs. The shader's op
numbers are `Program::Op`'s enum values and the shader's switch is written
to them; its arrays are sized to `kMaxExpressionOps`, `kProgramRefs`,
`kProgramCurves`, `kRippleFirings` and `kMaxMaterialClusters`, and every
count is
checked against the array before a pass runs.

A bake clears its target to alpha 0 and `BakePS` writes alpha 1, so
alpha marks coverage; after rasterization two `DilatePS` passes (ping-
ponged through the scratch target) flood each uncovered texel with the
average of its covered neighbours, growing a two-texel gutter, because
bilinear sampling and mip generation otherwise pull the empty
background into UV island borders — on a position bake that reads as a
false position sweeping toward the frame origin at every seam. Mip
levels deeper than the gutter still darken at borders; masks sample
mip 0 unless a recipe asks otherwise. Adjacent islands with different
values still blend where their borders touch, which for the id maps
(`componentId`, `chartId`, and the `materialClusters` map — all three
carry id / 255 texels through the same linear sampler) fabricates ids
between the two — those want nearest sampling, recorded as a backlog
item, not a dilation fix.

The C++ side of every `cbuffer` below is declared once in
`render/ShaderConstants.h` (`LayerConstants` for `Params`, plus
`ProgramConstants`, `RippleConstants` and `ClassifyConstants`); the lab and
the pass files include it rather than each declaring their own copy, so a
field added here has one place to be added there. `render/D3DResult.h` holds
the two D3D helpers every render file needs, `Failed` on an `HRESULT` and
`DataOf` on an `NiSourceTexture`.

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
`texFlags[8]` x mirror u, y mirror v, z transpose, w nearest; `misc` x
time, y op count, z vector result. The shared sampler is linear; w
nearest snaps the uv to the texel centre before the read, so a texel is
read whole. The id maps (componentId, chartId, materialClusters) set it:
their texels are identifiers scaled by 1/255, and a linear blend across
an island border fabricates ids that exist on neither side. Every stack value is a float3 with a scalar
broadcast, so component-wise arithmetic matches the CPU's rule;
comparisons and logic read `.x`. `x` evaluates to 0 (it is not per texel)
and `mean` to 0.5. A pop from an empty stack reads 0. After the pops a
binary op has `b` as the first operand and `a` the second; a ternary op
has `d`, `b`, `a` in order.

Ripple pass: `rippleFirings[8]` xyz origin in bind-pose units, w age in
seconds; `rippleShape` x speed, y width, z decay, w 1 = disc; `rippleMisc`
x firing count, y frame; `rippleDir` xyz a normalised worldspace direction,
w 1 = directional. The source is the position bake, rgb = position /
(2 frame) + 0.5. Each firing is a front at distance age x speed from its
origin: radial (w 0) measures `length(pos - origin)`, directional (w 1)
measures `dot(pos - origin, dir)` so the front is a plane sweeping along
`dir`. A ring is a band of the given width, a disc everything inside (and,
when directional, only ahead of the origin plane); fronts fade by
exp(-decay x age) and combine by max. The direction is resolved once per
tick and shared by that source's live firings; the recipe side normalises
it and a zero vector falls back to radial. A pooled target keeps its last
content, so a pass with no firings paints it black.

Classify pass: `centroidRmaos[8]` per cluster in analysis order (roughness,
metallic, occlusion, reflectance); `centroidLuma[8]` x luma, y id;
`centroidDiffuse[8]` RGB diffuse color; `clusterWeights` the four RMAOS
weights; `clusterMisc` x luma weight, y cluster count, z color weight / 3. The RMAOS and diffuse maps at the mesh UV go to the nearest
centroid by the distance `NearestCluster` (`mesh/MaterialClusters.cpp`) uses, the sum over
eight axes of weight x (texel - centroid)^2, the first of equals winning;
the CPU function is the reference and the shader must agree with it on a
texel. The id is written as id / 255 grey. A weight that is not finite or
not positive counts as zero on both sides.

Lab mechanics:

- Presenters are `slot_00.dds` .. `slot_99.dds`, then `slot_100.dds` up to
  `slot_<kPresenterCount-1>.dds` (minimum two digits in `Identity.h`, so any
  count fits the naming). The count is single-sourced in `CMakeLists.txt` as
  `BEEF_PRESENTER_COUNT`: the pool reads it as a compile definition
  (`RenderTargetPool.h`, `kPresenterCount`, default 512 when the definition is
  absent) and `cmake/Generated.cmake` copies the checked-in
  `cmake/presenter-slot.dds` to exactly that many files at configure time. It is 1024 as of
  2026-09-21, raised because cross-actor sharing cut a crowd's target demand far
  below the old 512 wall, so the wall could rise without the pool ever
  approaching it for realistic content; distance eviction is the real VRAM
  control beneath it. Each file is a separate engine resource name, opaque-black
  1x1 RGBA8, and lets an `NiSourceTexture` present a generated target like a
  material texture (NOTES 22).
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
- `RenderTarget::presenterSlot_` (`TextureLab.h`) is declared before the
  presenter and the D3D resources. Members destroy in reverse declaration order,
  so the pool's slot is returned only after the presenter and the resources are
  torn down, and never to a target still holding them.
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
- `SweepRetiredMaterialTextures` (`Binding.h`) is called on the engine thread
  whether or not any actor is applied, because a retired texture's material can
  outlive the actor that installed it. It tells a material no engine consumer
  still owns by sampling the engine's atomic intrusive refcount — `IncRef` then
  `DecRef` — and comparing the reading against the number of journal records
  that retain it; equal means the journal is the only owner left. The retired
  list itself is a never-destroyed function-local (`ImmortalList` in
  `Binding.cpp`), because an engine material can outlive the plugin's statics
  and running `~NiPointer` after the engine is gone is undefined. It is
  populated while preparing a write, never for the first time in retirement.
- Retirement moves journal entries into that list with `splice`, which takes no
  allocation, so no destructor can lose a lease to an allocation failure. The
  nodes were allocated at the first write.
- `SlotWriter::Restore` writes only while `MaterialAttached()`: a property whose
  material another system replaced is not ours to restore through.
  `~MaterialBinding` restores the fields the journal owns and leaves the private
  material attached, because replacing the whole material would discard external
  writes to fields the journal never touched.
- The private material copy (`original->Create()` then `CopyMembers`) copies the
  material's `NiPointer<NiSourceTexture>` fields as refcount bumps on the same
  texture objects, not texture clones. Two actors wearing the same armor
  therefore expose identical `rmaos/diffuse/normal/displacement` source-texture
  pointers even after each gets a private material; the private copy isolates
  only the slot the plugin writes. This is what lets a static composited result
  be keyed by its source-texture identities and shared read-only across actors
  (texture-budget plan, stage 3).
- `CopySkinData` (`render/SkinData.h`) copies every owned bone-weight buffer and
  retains the source's shared skin partition; the copy owns its weights and
  shares nothing else.
- `SkinPaletteLease` owns the repaired links and retains the scene storage they
  point into, so it must be destroyed before the clone is released.
- `LightBinding::Create` resolves `ShadowSceneNode::RemoveLight` through the
  Address Library and stores the pointer before it attaches anything, because
  the destructor must not run address-library initialisation: that path can
  allocate and can take an error branch, and neither is safe while unwinding.
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
  `rimLightPower` (NOTES 22, 48). Glint's fallbacks in `Words.h` are
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
  the same reach for the non-ISL path. Those defaults are ISL's own, read from
  CS `InverseSquareLighting.cpp` when the frozen light path was written:
  `kIslDefaultCutoff` 0.05 and, for a shadow-casting light, `kIslShadowCutoff`
  0.022 (`render/Light.cpp`). A recipe's own `cutoff` below 1 overrides them,
  clamped to 0.01..1. One light per recipe, third person only, at the skinned
  centre of the bones carrying the most vertices (NOTES 34).
- Geometry names ending in `Identity::ShellNodeSuffix()` are shells the plugin
  attached; the apply traversal skips them, and shells are collected before
  applying because attaching one adds a sibling a live walk would visit.
- `ShellPose` (Plan G, 2026-09-14): `PosedTransform` (`mesh/ShellPose.h`,
  engine-free, unit-tested in `tests/mesh/shellpose_tests.cpp`) composes all
  six pose fields onto a bone's rest skin-to-bone transform, per bone, in
  this order: inflate (as before), then scale about `scalePoint`, then spin
  about `spinAxis` through `scalePoint`, then translate by `offset`. No code
  in this plugin writes a shell clone's own `local`/`world` transform for
  posing, only `skinToBone` (as inflate already did); `skinToBone` is
  therefore the only hook available for the other five fields too, and
  `offset` is composed into it alongside them rather than applied
  separately. `scalePoint` is decided to be bone/skin space, the same space
  `inflate` already operates in: the schema is silent on the space of
  `scale`, `scalePoint` and `spinAxis`, and bone space is the only space a
  `skinToBone` composition can express. `offset`'s schema text says "world
  space", but composed into `skinToBone` it is applied per bone, in that
  bone's own bind-pose orientation, not truly world space; it reads as
  world space only to the extent a shell's dominant bone's bind pose is
  itself close to axis-aligned with the world, which is typical near a
  skeleton's rest pose but not guaranteed. The in-game checkpoint's
  `offset` step is the check on whether this approximation holds; if it
  does not, `offset` needs a render-side hook this plan did not add.
  `spin`'s unit is turns (`schema/recipe.schema.json`), converted to
  radians inside `PosedTransform`; trace and log text say "turns", never
  "radians".
  `PosedTransform` normalises `spinAxis` and falls back to +Z when it is
  zero, so the renderer never divides by zero regardless of what
  `CheckShell`'s `spinAxis must not be zero` validation caught first.

## Engine events, hooks and the manager

- The plugin-event contract (`engine/PluginEvents.h`, parsed by
  `ParsePluginEvent` in `engine/PluginEvents.cpp`, engine-free and fed
  garbage by `tests/engine/pluginevents_tests.cpp`): another SKSE plugin
  fires a trigger with a `plugin` origin by dispatching an SKSE message
  of type `0x42454546` ("BEEF" in ASCII) to `BetterEnchantmentEffects`,
  whose data is one `PluginEventMessage`: `version` (1), `actor` (the
  wearer's form id, 0 to reach every tracked actor), `id` (a
  null-terminated event id, at most 64 bytes, matched by the origin's
  glob), `type` (1 scalar, 2 vec2, 3 vec3 — a closed tag), `value`
  (three floats, of which `type` reads one, two or three; all read
  components finite). One message carries one typed value; a sender
  decomposes a rich occurrence into suffixed ids
  (`precision.hit`, `precision.hit.position`), fired in the same tick
  with the same lifetime, correlated by arrival only — beef ships no
  mapping. The id is read with a bounded `strnlen` and every field is
  checked before use, because the payload crosses a DLL boundary and
  nothing about it can be trusted; a bad message logs a warning and
  drops. Delivery is queued through the manager's task queue since SKSE
  messages can arrive on any thread. `EventRecord.plugin` separates the
  channels: an `event` origin never fires on a plugin message and a
  `plugin` origin never fires on an engine event. The receiving trigger
  row declares the type it expects (`"payload"`) and the space its
  location resolves in (`"anchor"`: `world`, or a node); a firing of
  another type is dropped and counted (`SignalState::Mismatched`). A
  sender shipping positions tells its users to anchor the receiving
  trigger in world space. The engine's
  own bus follows the same one-typed-value model: `equip` is a scalar
  and `equip.position` a vec3, and the menu's test-fire emits
  `<id>.position` beside `<id>` when its node resolves.
  `hit.received.position` carries the attacker's world position and
  `hit.dealt.position` the struck actor's, because `TESHitEvent` has no
  impact point; the other actor's centre is the best located fact the
  event offers, and it is close enough to aim a world-anchored ripple.

- `target` reads the wearer's current combat target, not a scan for the
  nearest hostile, because the engine maintains the former for free and
  a scan would run per tick per wearer. With no combat target it reads
  zero, which is a real world position; `hasTarget` carries the
  presence bit, so a recipe gates on `@hasTarget` instead of testing
  the position against a sentinel. `distance(@target, @position)`
  reproduces the removed `hostileDistance` selector, and
  `toRoot(@target)` gives the direction to the hostile in root space.
  Who counts as the target differs by wearer: an NPC's combat AI
  maintains `currentCombatTarget` while fighting, but for the player
  the engine sets it from the attack/crosshair focus, so a
  player-worn recipe reads a target when an enemy is targeted, not
  merely when one is hostile nearby (observed in play 2026-09-22 —
  the same behaviour the removed selector had).

- `actorState` splits its return by selector: the flags (`inCombat`,
  `sneaking`, `weaponDrawn`, `swimming`, `sprinting`, `mounted`,
  `hasTarget`) and the scalar `movementSpeed` go through `ActorState`,
  while `position` and `target` go through a separate `ActorVector`
  returning a vec3, because the interface is typed per method.
  `VectorValued` (`Recipe.h`) names the vec3 selectors once; the signal
  evaluator and type inference both read it. The pattern mirrors
  `efsh`, whose field also decides scalar/vec2/vec3.

- `toRoot` converts a world-space position into the wearer's root space
  through `WorldToRoot` (`Environment.cpp`), which applies
  `root->world.Invert()` — the same transform `ToRootSpace` uses render
  side. It is a point transform (affine, with translation), so a
  direction is built by subtracting two converted points, where the
  translation cancels. `Signals` stays engine-free: `WorldToRoot` and
  `ActorVector` are interface methods the null environment answers with
  identity and zero, and only `ActorEnvironment` reads the actor. A
  world-space direction conversion (for a facing vector) would need a
  rotation-only method and is deferred with the `facing` selector.

- A studio gesture (a slider drag) applies its edit and rebuilds the
  world in one atomic step (`ChangeAndRebuildActors`), every frame.
  Live instances point at the shared `Recipe`, so a mutation the render
  path can observe before its rebuild null-derefs in the binding's
  writes (crash 2026-09-22, a throttle that deferred only the rebuild;
  reverted same day). A rebuild throttle must defer the mutation too,
  or give live instances their own recipe snapshot. The cost that
  motivated the attempt stands (measured 2026-09-21: a drag rebuilt
  ~20 actors ~11 times a second, up to 18 ms each, ~320 texture
  acquires per rebuild); the redesign is backlog item 49.

- `LogStackDiagnostics` (`engine/ManagerApply.cpp`) logs each distinct
  (recipe, output, geometry, where, message) once per game session while
  the warning-history budget permits,
  marked "(repeats suppressed)", because a recipe that errors on a
  crowd re-applies on every refresh and wrote 250 identical lines in
  one session. The menu's in-place diagnostics are unaffected; only
  the log line is deduplicated. `Manager` owns `WarningHistory` and clears
  it in `Clear`, so a new save/game session can report the same warning again.
  The history retains at most `kMaxWarningKeys` (1024) and
  `kMaxWarningKeyBytes` (128 KiB of text). Both bounds matter because recipe
  names, geometry names, and diagnostic messages vary in size. Container
  overhead is separately bounded by the key count. A first over-budget
  observation emits one notice per session; later over-budget observations
  are omitted without retaining their keys. Shorter new keys may still fit.
  Repeated keys do not consume capacity or trigger the budget notice.

- `RetireActorEffects` (`engine/LiveActor.cpp`) clears in a fixed order:
  application tokens, then lights, then each geometry through
  `RetireGeometry`, and only then the placement, instance and piece tables.
  A texture producer must not release until the shells that read it have
  detached and the material journals have retired, so the tables that own
  the producers go last.
  `Manager::Retire` first reports the live actor's captured application tokens
  through `ApplicationService::Retire`. Distance eviction can happen between
  preparation and first rendering; destroying the tokens without reporting them
  would leave a permanent prepared result. Retirement must not report every
  current pending token here: a store mutation or refresh has already invalidated
  the old actor attempt and may have queued its replacement. Captured revision
  and attempt checks keep retirement from terminating that replacement.
  Partial `RetireGeometry(actor, geometry)` follows the same binding-before-
  producer rule, resets the geometry's inputs and its placement stacks, and
  keeps diagnostic rows and plan indices. Shared instance lights are independent
  of the lost surface and remain until their instance retires.

- Cache maintenance is driven by `Manager::OnFrame` every five seconds even
  without applied actors. Material records protect active texture pairs, expire
  unused entries after 30 seconds, and retain at most 64 unused pairs per pass.
  These provisional retention values mirror the mesh grace period and preserve
  short rebuild reuse; they are not measured alpha workload limits. Shared target
  caches prune expired keys both on adoption and during maintenance.
- The render-target idle pool retains at most 16 targets and 64 MiB, using the
  same `Metrics::MippedRgbaBytes` accounting as allocation diagnostics. This
  bounds reusable idle allocation while permitting short rebuild reuse. Targets
  held by active consumers, previews, scratch buffers, or material journals are
  outside this allowance. Destruction occurs only after the final target lease
  returns; normal D3D resource ownership and presenter restoration still apply.

- `kMaxTerminalApplicationRecipes` retains 256 recipe/whole-catalog results by
  newest application revision, matching the independent 256 terminal actor
  history allowance. A record is eligible only when none of its actors remain
  queued/prepared; the aggregate failed phase does not establish completion.
  Pending requests are never evicted to satisfy a history allowance. A retry
  inherits pending/failed targets from retained records plus current manager
  candidates, rather than every historical successful wearer. Supersession and
  load release canceled actor vectors, and resume erases cancellation summaries.
  The allowance bounds completed recipe identities, not active workload bytes.

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
- `Manager::CollectPieceGeometries` returning false rejects the whole piece,
  including the geometries it already collected before the layout failure, so a
  piece is never applied half-inspected. Clone identity is filtered earlier, by
  `CollectPieces`.
- `LoadedRecipe` (`RecipeStore.cpp`) carries a `NOLINTNEXTLINE(bugprone-exception-escape)`,
  the only suppression in the tree. MSVC's map move can allocate, so the
  aggregate's implicit move is not `noexcept`; clang-tidy reads it as `noexcept`
  and then reports that it can throw. The `static_assert` below the struct is the
  real check: the aggregate must keep whatever throwing contract
  `Studio::ReferenceCounts` has rather than terminate if moving the reference
  counts fails.
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
- `WornKeysOf` collects every distinct valid `baseEffect` from the effective
  worn-item enchantment's `effects` array. Null effect entries and null base
  effects are skipped. This expands `magicEffect` matching and the editor's
  available keys beyond the costliest effect. The adapter also collects each
  effect's shader, preserving `ShaderFor(EffectSetting*)` precedence (enchant
  visuals, then enchant shader). It does not inspect the wearer's active spells.
- `EnchantmentValueFor` resolves the winning magic-effect or effect-shader key
  against the enchantment's effect entries. Highest matching cost wins, with
  first-entry ties; absent selected effects return zero. Non-effect selectors
  keep the generic costliest-effect fallback. Planner instances and carried
  clocks include the selected effect key to prevent sharing unrelated signals.
- Recipe keyword entries are conjunctive requirements. Other key kinds remain
  alternative selectors and determine the matched key's priority and fallback
  classification; all-keyword recipes use their first keyword at priority 20.
  Required keywords must resolve and match before a candidate can suppress a
  fallback or enter sampling. Duplicate requirements do not require duplicate
  keyword records on the armor.
- CommonLib's `Actor::AddAnimationGraphEventSink` indexes `graphs.front()`
  without checking emptiness and both actor helpers scan the sink array without
  taking the event-source lock. This adapter instead checks graph pointers and
  calls the source's `AddEventSink`/`RemoveEventSink`, which lock and deduplicate
  registrations. `AnimationSubscriptions` retains the first nonnull graph
  until it removes its sink from that exact source. An empty graph list keeps
  the participating actor tracked without attaching; once-per-second maintenance
  retries graph discovery. Graph enumeration remains on the game thread, following
  the existing adapter convention. The source lock protects registration and
  callbacks, not the manager graph array; actual graph replacement timing still
  requires in-game acceptance.
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
  observation belongs to the actor rather than its current recipe bindings.
  `RetireEffects` releases all recipe references before store publication while
  retaining observation across the queued rebuild. `RunRefresh` reconciles the
  final applied state; final retirement, failed rebuilds, unload and clear also
  end observation and forget discovery. The maintenance pass cleans abandoned
  rebuild gaps, including actors temporarily lacking a graph.
- The animation sink copies tag and payload with a shared registration token.
  Detach invalidates that token before removing the sink. Both the source lookup
  mutex and the source's own lock can be used from animation callbacks; the owner
  never holds its lookup mutex across `AddEventSink` or `RemoveEventSink`.
  Subscription reconciliation and destruction belong to the game thread.
  `ManagerAnimation` uses the existing session queue and validates actor identity,
  registration and current graph before updating discovery and firing signals.
  Session generation rejects queued work from old loads; registration validity
  also rejects callbacks captured before detach but posted after resume.
  Events delivered while effect state is absent are discarded, not replayed.
  Discovery survives an ordinary rebuild and graph replacement; signal state
  continues to rebuild normally.
- `RetireAll` retains the existing two-stage queue ordering: it first collects
  applied and observed actor IDs, then queues their retirements behind refreshes
  posted by earlier edits. Retiring immediately in the collection task would
  let those already queued refreshes recreate effects after retirement.
- The destructor's `NOLINTNEXTLINE(bugprone-exception-escape)` is limited to
  `AnimationSubscriptions`. The diagnostic follows CommonLib's
  `RemoveEventSink` pending-removal allocation into Address Library's fatal
  missing-relocation reporting, where formatting the fatal report can throw.
  Swallowing a failed detach would leave an engine source pointing at a destroyed
  sink. Teardown therefore keeps the normal nonthrowing destructor contract
  and does not catch and continue after that unrecoverable dependency failure.
  Ordinary detach is serialized by the source lock; callbacks only enqueue work.
- The vanilla hit event carries no position,
  so `hit.received` and `hit.dealt` have none and a ripple starts at the
  piece's centre.
- A recipe's clock survives a retire followed by a re-apply within
  `kCarryWindowMS` (an edit, an isolate, re-apply all), keyed by actor and
  recipe id, enchantment form id, and selected effect key. The enchantment id comes from the
  collected piece before engine lookup, so lookup failure does not merge it
  with an unenchanted instance. A piece put back on later starts fresh.
  `CarriedTimes` retains only owned keys and finite nonnegative seconds;
  consuming an entry removes it. The two-second window uses unsigned
  millisecond subtraction across clock wrap. The one-second manager heartbeat
  expires unused entries even when no actors are applied, and save-load
  clearing discards all entries.
- `kMaxCarriedInstanceTimes` is 4096: a fixed ceiling for short-lived clock
  continuity records, not a limit on live actors, recipes, or GPU resources.
  At capacity, existing entries can update but new entries are omitted until
  consumption or the expiry sweep makes room. The affected instance starts
  fresh. This bounds retirement bursts without evicting another instance's
  pending continuation or repeatedly scanning a full store on insertion.
- Carry-over and leaving freeze use `ClockOffsetMS` to convert seconds at
  the combined animation/view/recipe speed into a clock offset. It rejects
  non-finite or negative seconds, non-finite/nonpositive speed, and offsets
  beyond `uint32_t` milliseconds before casting; the calculation uses double
  precision. Invalid carry-over starts fresh. Leaving freeze resumes from
  the scrub when representable, otherwise from a fresh clock origin.
- Signal state integrates forward (pulse phase, smoothing, trigger ages),
  so a scrub backwards or a jump rebuilds it from the start and advances it
  to the scrubbed moment in one step. While frozen, the rebuild triggers on
  `a_time + 0.001f < lastTime` (`engine/ManagerTick.cpp`): 0.001 is one
  millisecond in the seconds time base the same function builds from
  `(nowMS - startMS) * 0.001f`, so a scrub that moves the clock by less than
  the clock's own resolution does not rebuild. The epsilon is carried unchanged
  from the frozen `src/_old/Manager.cpp`; why one millisecond rather than a
  larger guard is not recorded.
- Editor IDs: the engine keeps them for a few form types (keywords, magic
  effects); po3's Tweaks export answers for every type (NOTES 41), reached
  through `engine/Tweaks.h` (`EditorIdOf`), which resolves po3's
  `GetFormEditorID` once via `GetProcAddress` on the `po3_Tweaks` module and
  falls back to the form's own id. A form key is written `0x92DED~Skyrim.esm`
  (po3's convention), file compared case-insensitively.
- The Game Object Service (`engine/GameObjectService.h`) owns the reverse
  editor-id map and the discovery catalogs. At load `RebuildGameObjectCatalogs`
  walks every keyword, enchantment, effect shader, magic effect, armor and
  light into a candidate list per kind, and the same pass fills the
  editor-id → form-key map the store resolves against (`ResolveEditorId`);
  armor addons are indexed for resolution only, since they are resolvable but
  not a pickable kind. Catalogs are immutable and cached behind `shared_ptr`,
  keyed by kind (form kinds are session-stable, actor values rebuilt lazily);
  the snapshot carries the pointer, not the strings. A candidate's committed
  value is its editor id, else its form key; its display is the editor id,
  else the full name (via `TESFullName`), else the form key. Without po3's
  Tweaks, editor ids for shaders, enchantments, magic effects and armor are
  mostly empty, so those catalogs read sparser and fall back to full names.
- Anim-event discovery is by observation, not enumeration: `NoteAnimEvent`
  records each `anim.<tag>` the animation sink sees per actor, so the
  `kAnimEvent` catalog lists only events that have fired since watching began
  (`AnimEventCatalogOf`, keyed by actor, rebuilt when a new tag appears).
- `BuildActorValueSamples` emits one sample per `kActorValue` candidate in the
  catalog's order (or none, when no wearer is loaded), so a reader index-aligns
  `actorValueSamples[i]` with the catalog's candidate `i` rather than looking up
  by name. The built-in trigger events `hit.received`/`hit.dealt` are defined
  once in `studio/GameObjects.h` and referenced by the engine emitter, the
  wizard, and the event picker.
- Actor value names resolve through the engine's table once per name; the
  environment holds handles and form ids, never engine pointers across
  ticks, and answers zero for anything it cannot reach. `av` readings:
  current = max - damage; `damage` is the damage modifier negated; `max` is
  permanent plus the temporary modifier.

## Meshes, bakes and analysis (`mesh/Mesh.h`, `mesh/TextureSize.h`, `mesh/Islands.h`, `mesh/MaterialClusters.h`, `mesh/MeshFacts.h`)

- A biped slot has two representations and they are not the same type.
  `BipedSlot` (`recipe/Recipe.h`) is the parsed recipe value: the boundary
  (`Reader::BipedSlotFrom`) admits a name from `kBipedSlots` or a number in
  `kFirstBipedSlot`..`kLastBipedSlot` (30..61), so anything downstream can
  trust it. `MeshPartition::slot` and `SlotCoverage::slot` are the raw
  16-bit field the NIF's dismember skin instance carries, including the
  `MeshPartition::kNoSlot` (0xFFFF) sentinel and values outside 30..61 that
  other tools write; they stay `std::uint32_t`/`std::uint16_t` and convert
  with `std::to_underlying` at the comparison.

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
  origin, z up, so a standing body spans about 0.5 to 1 in z). The
  identity `2 * kPositionFrame == kDistanceFrame` (256) is load-bearing:
  it makes `distance()` of raw position-bake texels equal a `distance`
  source's texel exactly, which is why the source's point-form could be
  deleted; a change to either frame breaks that equivalence and every
  recipe that hard-codes the decode.
  `localPosition` maps into the geometry's own model bound
  (`modelBound.radius`, `MeshReader.cpp`), which skinned armor usually
  stores as zero, so `ReadMesh` falls back to a bound measured from the
  vertices (`MeasureBound`, box centre and enclosing radius) and the
  bake works on every mesh; `normal` is the bind-pose surface normal
  with each axis mapped -1..1 to 0..1 (recover the vector as `v * 2 -
  [1, 1, 1]`; `dot` of it against a direction is the centre-free way to
  shade the side of a body facing something, where a positional split
  needs a centre no recipe can measure; the old `worldUp` selector was
  its z channel and was removed as derivable — the encoding is affine,
  so `dot(@bake, [0, 0, 1])` reads it without a decode); `uv` carries
  the texel's coordinates as `[u, v, 0]` (a vec2; `dot(@uv, [1, 0])`
  extracts u — it is a bake because masks have no uv variable, so a
  texture is the only channel per-texel uv can arrive by); `distance`
  maps 0..kDistanceFrame to 0..1 from a named skeleton node (its
  point-form was removed as derivable, see the frame identity above);
  `partition` is 1 on the biped slot's triangles; `boneWeight`
  is the summed weight of the named bones (sorted, so two orders of one set
  share a key); `componentId` and `chartId` carry island id / 255 in x with
  `kNoIsland` vertices at 1 and need the analysis, not the mesh alone.
- Every source bakes into the geometry's UV space, so a bake
  distinguishes exactly what the UVs distinguish — no more. Any texel
  sharing collapses it: humanoid armor mirrors left onto right on one
  island to halve texture use; symmetric or thin parts reuse front
  texels for the back; straps and trim overlap the islands beneath
  them; a double-sided sheet's two faces are the same texels with
  opposite normals. Wherever surfaces share texels, rasterisation is
  last-written-wins and every bake (position, normal, boneWeight — the
  mesh reader resolves per vertex correctly, then collapses at
  rasterisation) reads one surface's value for all of them. Which axes
  survive is per-armor, not a rule (observed 2026-09-22: front/back
  also collapses on some pieces, not only left/right). Two verbose
  lines measure it per geometry (`render/MeshCache.cpp`): `side split
  ... overlap N%` splits by L/R bone names, `facing split ... overlap
  N%` by bind-pose y against the mesh centre; 100% means that axis
  cannot be masked apart on that piece, and `inspect-bake-position`
  (debug recipe) shows the same fact as colour in game. Region masking
  survives because a `partition` bake keys on a biped slot, and slots
  are region-level (forearms, calves), never side-specific. Lights are
  the only per-half path, being placed in bone space, but a recipe
  carries one light output with one intensity signal, so two
  independently-gated halves are still not expressible.
- A bake is cached under `BakeKey{definition, pixels}` (`mesh/Mesh.h`),
  never under the source's name, so a rename cannot serve the old
  picture and two names with one definition share a target. A node key
  names the node, not its position, so the snapshot can find the bake
  without looking the node up.
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

## Compositor (`render/Compositor.cpp`, `CompositorSource.cpp`, `CompositorBake.cpp`)

- Render scheduling is a validated `RenderPlan` with one `RenderInstance` per
  geometry. Produced targets are retained by typed step results; `RenderScratch`
  has a weak reuse hint, never a second owning allocation table. Texture leases
  retain storage for publication and previews after execution ownership ends.
  Shared plan dependencies replace recursive mask/ripple preparation and the
  compositor's cross-actor target caches.
- Many PBR sets ship a displacement map that is a real texture and
  entirely black; a map is flat when its mean sits at either end (NOTES
  46) — at or below 0.02, or at or above 0.98 (`render/CompositorSource.cpp`)
  — measured by a readback once per material. The two margins are carried
  unchanged from the frozen `src/_old/Compositor.cpp`; no source records why
  the band is that wide. A height stack over a flat map starts from the shared
  neutral 0.5 target because CS offsets parallax by (height - 0.5) x scale
  (NOTES 50); the `relief` channel reads displacement when it is real and
  occlusion otherwise.
- An RGB image field is normalized by `0.5 / max(mean(luminance), 0.05)`.
  The projection uses the compatibility weights 0.299, 0.587 and 0.114. The graph
  now carries this measurement and expression explicitly. The original 0.05
  floor caps gain at ten; the historical reason for that threshold is unknown.
- The mean measures the image at the mesh's unscrolled coordinates. Tile,
  mirror and transpose apply; scroll does not. A time-driven scroll would
  otherwise draw the measured field and reduce it again on every tick. The
  import templates scroll every fill layer. The result equals the scrolled
  mean when scroll is zero. For a tiling scrolled image it is the mean at
  scroll zero.
- A curve's `mean` over a scrolled image source measures the same source at
  unscrolled coordinates, for the same reason. The template `glossField` layer
  uses such a curve. A source with its own result transform keeps measuring
  its shown value.
- Stack composition alternates between its retained target and the lab's shared
  scratch. Initial parity ensures the final layer lands in the retained target;
  a preceding stack remains separate from both destinations.
- Reductions measure all requested texel centers, including uncovered texels.
  The GPU reduces the float field in `PSReduce` passes. Each pass reduces a 4x4
  block to one texel until one texel remains (`planners/GpuReduction`). The
  passes accumulate sum and mean in float32. The CPU divides the sum by the
  texel count for the mean. A sum of float32 values can lose a small term next
  to a large one. The first pass writes 1 to alpha for a texel with a
  non-finite used component, and later passes keep the maximum. A flagged or
  non-finite result fails instead of using a fallback. Float scratch prevents
  bake dilation from introducing an extra RGBA8 quantization into the
  measurement path.
- A reduction lowers to a `SubmitReductionStep` and a readback input
  (`ReadbackBinding`). The submission copies the one-texel result to one of
  three staging textures and returns a constant, so its version never
  advances. `RenderInstance::CollectReadbacks` runs once per frame and maps
  each pending copy with `D3D11_MAP_FLAG_DO_NOT_WAIT`. A completed copy imports
  its value into the readback input, and a consumer recomputes only when that
  value changes. A result is one to three frames late. No readback waits,
  because a blocking map waits for all GPU work queued in the frame; a test run
  measured 942 ms for one such wait at apply.
- Material sampling follows the same shape: `SubmitMaterialSampleStep` copies
  the RMAOS and diffuse maps to two staging textures, and a readback input of
  type material sample receives the decoded sample. The studio's
  `TextureLab::SampleMaterial` uses the same copies and waits for them.
- Before a readback input receives its first value, a stack that reads it
  fails. `RenderInstance::Render` reports such a stack as `StackPending` while
  its submission is in flight. A pending output is neither rendered nor
  failed, so its application stays prepared and the slot keeps its base.
- `RenderInstance::BeginFrame` runs once per render tick and releases a
  texture or bake-buffer step whose value no executing consumer and no
  inspection read for 30 ticks (`kReleaseAfterIdleTicks`). A render tick runs
  at most at `animationFPS` (60 by default) and at most once per game frame.
  Stack results are never released, because they are the published textures.
  Thirty ticks is half a second at 60 ticks per second: long enough that a
  chain animated by a signal keeps its static inputs, short enough that a
  static chain returns its targets soon after equip. The three
  demo recipes produce 45 texture steps per geometry at 2048, about 960 MiB
  with mips, and each geometry of a piece holds its own copy.
- One render plan covers every geometry of an actor. `PlaceInstances`
  collects the stack requests of all geometries, keyed by the actor-wide
  `GeometryId`, and builds one `RenderInstance` that every geometry shares.
  Steps whose value identity does not involve a mesh (images, material fields,
  signals, programs over them) get equal keys and are built once for the
  actor. Plan inputs are deduplicated by value identity; mesh and firings
  inputs stay per geometry (`GeometryBound`), because a mesh is the geometry
  and firing origins are resolved against the geometry's skin. The binding
  resolver takes the geometry, so mesh identities come from the right
  geometry.
- A shared input is imported through the first geometry that requested it.
  When one geometry retires, its inputs stay in the shared instance until the
  actor is reapplied; detaching them could leave another geometry's shared
  inputs without a source. `BeginFrame` takes the frame number and does its
  work once per frame, whichever geometry reaches it first.
- A value shared across geometries is built once, under the first geometry
  that reaches it. The lowering keeps the value bindings of each built
  subtree and replays them for every later geometry that reaches the same key,
  so studio inspection finds shared intermediates on every geometry. At most
  2^18 value bindings are recorded (`kMaxValueBindings`).
- GPU timing (`GpuTiming` setting, off by default) brackets each render tick
  with a `D3D11_QUERY_TIMESTAMP_DISJOINT` query and records timestamp pairs
  around the tick (`RenderTick`), each step execution (`<kind> <side>
  <format>`) and each `GenerateMips` (`GenerateMips <side>`). Spans nest: a
  step's time includes its mip generation, and `RenderTick` includes every
  step. The engine-free `diagnostics/GpuTiming` ring holds four ticks; a tick
  with no free slot goes untimed, a disjoint tick is discarded, and at most 256
  spans are recorded per tick. `CollectTimings` polls with
  `D3D11_ASYNC_GETDATA_DONOTFLUSH` once per frame and never waits, because a
  wait would add the GPU backlog to the frame being measured.
- Program fusion inlines a producer program into its consumer's texture input
  when the producer is program-like (`EvaluateProgram`, `MapField`,
  `ComposeVector`), shares the consumer's size and RGBA8 format, has exactly
  one live consumer (counted from the stack outputs; preview demands do not
  count), and animates (depends on a changing graph input or a changing
  readback). The inlined code is followed by `kSplat` for a scalar producer,
  because a scalar field is stored as `xxx`, and by `kQuantize`, because the
  stored field was RGBA8. A texel-centre read returns the stored texel
  exactly, so the fused program computes the same value.
- Only stack results generate mips. The lowering sets `MipPolicy::kNone` on
  every value it builds, because every intermediate is read at its own size,
  where sampling uses mip 0. A stack draws its layers into targets without
  mips and generates mips once on its final result, which the game samples.
  An inspected intermediate gets mips when inspected, because a studio
  thumbnail minifies it. The first timing run measured mip generation at
  about a third of the plugin's GPU time, from about 144 calls per tick.
- `PublishEffects` off keeps the render plan running but writes every slot as
  not shown and hides the shell, so the draws are measured without the cost
  of presenting their results.
- The executor refreshes a consumer's inputs by version before it decides to
  execute, and materializes their values only when it executes. A released
  step whose observed inputs are unchanged therefore stays released while its
  cached consumers are reused. When a value is needed again, the step
  re-executes and keeps its change version: steps are pure, so the same
  observed inputs produce the same value.
- Imported inputs remain stable during one synchronous execution request. A
  per-request refresh set bounds shared DAG traversal by its edges, avoiding
  repeated descent through already-validated dependencies.
- A changed field may have an unchanged reduction result. Its lookup then remains
  cached while mapping consumes the new field. Revisions describe observed input
  changes rather than frame counts. Visibility selects dependencies before they
  are evaluated; all-hidden success is distinct from an unavailable result.

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
- The two `format` failures are deliberately different (`RecipeRead.cpp`).
  A missing `format` is reported ("'format' is required; this loader reads
  format 1") and the parse continues, so an author who forgot the field still
  sees every other problem in the file at once. A `format` above
  `kRecipeFormat` is reported and the parse also continues: keys this loader
  does not know surface as `unknown key` errors, which are recipe-level too.
  Both are recipe-level errors, so either holds the recipe out of the
  applied set while leaving it in the menu, where the author can set
  `format` to 1 and see what else the loader refuses. Stopping the parse
  instead (the behaviour until 2026-09-14) left no recipe for the menu, and
  the store reported the file as unreadable, which the in-game checkpoint
  for Plan A showed.
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
- The slot tables in `Words.h` are the one spelling of what CS reads
  from each map (`BSLightingShaderMaterialPBR.h`): height reads red alone,
  emissive and normal are rgb, glint has no texture, the diffuse's alpha is
  a shell's visibility, and the feature maps pack a colour with a weight.

## Menu mechanics (`menu/MenuWidgets.cpp`, `studio/Intent.h`, `studio/MenuState.h`)

- `menu/MenuWidgets.cpp` is the menu's sole direct render dependency: no other
  file under `menu/` includes `render/`. It calls `TextureLab::Preview`
  through `render/TextureLab.h` to obtain the read-only preview for a
  `Studio::TextureHandle`, and DX11's ImGui backend invokes a queued draw
  callback only after it submits the *previous* frame's image, so
  `FinishPreviewDraw`'s callback only marks the ticket `Consumed` (an atomic
  flag, `planners/ConsumptionLeases.h`); the retained `RenderTarget` it holds
  is not released there. `TexturePreviews::CollectDraws` (called from
  `RenderPreviews`, once per tick like every preview request) erases and so
  releases every ticket already marked consumed — the actual lease release is
  that later collection pass, not this callback.
- `Studio::FieldKey` (`studio/MenuState.h`, a `std::uint32_t`) and the
  framework's `ImGuiID` are the same type, enforced by `MenuWidgets.cpp`'s
  static assertion; `MenuState::textBuffers`, `numberBuffers` and `comboMode`
  are keyed by it, and `kNoField` (zero) is "no field", matching how ImGui
  itself reads a zero ID.
- The menu frame (`menu/Frame.h`) borrows the snapshot, selected rows, names,
  a `Studio::MenuState*` and the outgoing `Studio::Intents*` collection.
  Widget scale comes from `Frame::scale`; geometry bone coverage comes from
  `GeometryRow::bones`. `ModeBar` (`MenuWidgets.cpp`) draws one tab per
  `Studio::kModes` entry — compose and paint are the only two modes; the
  frozen tree's design mode is gone.
- A field's key is `ImGui::GetID` of its literal name (`KeyOf` in
  `MenuWidgets.cpp`) inside whatever ID scope is active when it draws, so the
  same literal (`"text"`, a field's own name) names a different
  `Studio::FieldKey` on every row without building a per-frame string. Callers
  push that scope by nesting `ImGui::PushID`: a recipe's id
  (`StudioPage.cpp`), an output or layer index (`StackPanel.cpp`,
  `Workspace.cpp`), then a row's field name (`FormDraw.cpp`'s
  `DrawRowField`/`DrawFieldTable`) — each level is a plain `PushID` call at
  its own draw site, not a mechanism `MenuWidgets.cpp` provides.
- `-FLT_MIN` is ImGui's exact "everything left" (`kFillWidth` in
  `MenuWidgets.cpp`); `-1` leaves a pixel, and a stretch table measuring such
  content shrinks a pixel per frame (NOTES 51).
- A field's active state (`TrackActive`) is read from the input item itself
  right after drawing it: an item drawn after it would report its own state,
  and the field would otherwise read inactive every other frame. A refused
  text (its `TextCheck` still fails) keeps the typed text and keyboard focus
  instead of committing; the refusal reason (`ProblemLabel`) draws to the
  foreground draw list, in a box placed below or above the field depending on
  which side has room, so it takes no layout space of its own.
- A drag payload (`DragHandle`/`DropTarget`) is the bytes ImGui copied for a
  `std::size_t` row index; only a payload of that exact size is accepted as a
  row move. An auto-resizing popup starts narrow, so `DetailModal`'s minimum
  width constraint keeps a definition on one or two lines rather than one
  character per line. A `Split` table's column weights set the divider
  position only the first time the table appears; after that ImGui keeps
  whatever width the user last dragged, and `Split` reports the ratio back
  only when it has moved by more than a small deadband.
- The framework's `ImTextureID` is a D3D11 shader resource view pointer
  (NOTES 28); `PreviewOf` builds one from `TextureLab::Preview`'s result with
  `reinterpret_cast`.
- `studio/Intent.h`'s `Intent` variant and `studio/Edits.h`'s `RecipeEdit`
  variant are each dispatched by an exhaustive set of lambdas or `operator()`
  overloads in `studio/MenuState.cpp` (`ReduceVisitor` for `Intent`,
  `ReduceEdit` for `RecipeEdit`, plus the separate `Match` calls in
  `AcceptIntent`, `ChangesPaint`, `PaintSources` and `Reduce`'s own
  size-limit check) — one arm per alternative, no catch-all case.
  `kIntentCount`'s `static_assert` in `Intent.h` only guards the variant's own
  size; it is these per-alternative arms, not that assertion, that fail to
  compile when a new `Intent` or `RecipeEdit` alternative is added without
  updating every dispatch site.

### Recipe CRUD travels one pipeline (`studio/Intent.h`, `engine/RecipeEditor.*`, `engine/RecipeStore.*`)

- Every recipe change is an `Intent`; `menu/` never writes a `Recipe`. Path:
  post `Intent` → applier in `menu/Menu.cpp` → `RecipeEditor` method (task queue,
  revision-stamped, file ops journaled in `FileOperations`) → `RecipeStore` (the
  only disk layer). Reads use the `RecipeSnapshot`/`Rows` projection. A new
  operation adds one arm at each stage.
- Row-level CRUD (signals, sources, masks, curves, outputs, layers):
  - `EditRecipe{recipeID, edits, expectedRevision?}` — a `RecipeEdit`
    (`studio/Edits.h`) batch through `Studio::Apply`; interactive creation via
    the `studio/Create.h` seam; names unique across the four kinds via
    `NameInUse` (apply) / `ReservedNames` (studio).
  - `Undo{recipeID}` / `Redo{recipeID}` — step the recipe's edit history.
- Recipe-file lifecycle (each: intent → `RecipeEditor` method → `RecipeStore` fn):
  - `CreateRecipe{recipeID, key, geometry}` — new empty recipe, dirty until saved.
  - `DuplicateRecipe{from, to}` — copy `from` as `to`, provenance cleared, dirty
    until saved.
  - `RenameRecipe{from, to}` — change id; moves the user file; retargets history,
    view pin/isolation, paint.
  - `DeleteRecipe{recipeID}` — delete the user file, unpublish, `View::ForgetRecipe`.
- Guards: ids are file stems (`IsStem`) and unused; the paint recipe refuses
  rename/duplicate/delete; imported/shipped files are unpublished, not deleted.
- `SaveRecipe` / `RevertRecipe` are `RecipeEditor` methods called directly from
  `menu/RecipeActions.cpp`, not intents — persistence, not CRUD.

## studio (`studio/Snapshot.h`, `View.h`, `Intent.h`, `MenuState.h`, `Forms.h`, `Edits.h`, `Mask.h`, `Presets.h`, `TermTemplates.h`, `PaintSession.h`, `Board.h`, `Panels.h`, `SelectorEdit.h`, `Selection.h`, `Names.h`, `Rows.h`, `FieldCheck.h`, `History.h`, `Widgets.h`, `Fields.h`)

- `Snapshot::status` (engine scalars plus loaded-file and recipe-error counts) is filled
  once on the game thread where the snapshot is built, so the render thread
  reads it from the immutable snapshot and never calls a live getter. The cost
  is a one-tick lag in the status line; the frozen tree paid a data race for
  freshness instead.
- `Snapshot::tickMS` is the configured tick interval. Loaded-recipe summaries
  own their keys, row counts, diagnostics and file paths; menu rendering never
  borrows the recipe store's mutable containers.
- `TextureHandle` (`Snapshot.h`) is an opaque `enum class` over
  `std::uintptr_t`; its value is the address of an `RE::NiSourceTexture` the
  engine retained in `Manager::Snapshot::textures` when it built the
  snapshot, and `TextureHandle{}` is "no texture". Exactly two places convert:
  `TextureHandleOf` in `engine/ManagerSnapshot.cpp` (pointer to handle, while
  retaining the texture) and `TextureOf` in `menu/MenuWidgets.cpp` (handle to
  pointer, for `TextureLab::Preview`). A handle is valid only while the
  snapshot that published it is alive; a pinned preview must copy the
  snapshot's `shared_ptr`, never the handle alone.
- `ApplicationRecord.h` holds the application phase, token, actor and record
  types the snapshot shows; the engine's `ApplicationService` includes it, so
  studio never includes `engine/`. The namespace stays
  `BetterEnchantmentEffects` because the engine and its tests name the types
  unqualified.
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
  accept a value the loader rejects. `Apply` compiles a `RecipeGraph` for edits
  that need typed-row checks: an interactive edit-time path, not per-tick. No
  public shell row-check exists, so shell scalar/vector reference type-checks
  compose the public `SignalTypeOf` query directly; route them through a
  `CheckShell`-equivalent if one is later published.
- The mask-editor presets file (`presets.json`, path from `Identity.h`) is one
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

### Field ranges, cascade removal, channels (2026-09-17)

- **Tuning-slider ranges are declared, and where the numbers came from.**
  `FormField.range` is a hard limit (`FieldCheck` rejects out-of-range typed
  input, "Allowed range"); `workingRange` is only the slider span (typed input
  unrestricted). `ResolveTuningRange` (`menu/Tuning.cpp`) prefers
  `workingRange`, then `range`, then a stored custom range, then the eased
  `ValueRelativeRange`. Declaring either hides the "Range" adjust button. The
  constants (`studio/Forms.cpp`), derived from where each value is consumed:
  - light `intensity` `{0,16}` — `fade = intensity·share/4` (`render/Light.cpp`),
    so `fade=1` at intensity 4 (one vanilla light); above ~16 only radius grows.
  - light `size` `{0,8}` slider — the engine hard-clamps `[0.01,50]`
    (`Light.cpp`); usable values sit near √2.
  - light `cutoff` `{0.01,1}` — ISL attenuation cutoff; `≥1` is the "use the
    engine default" sentinel, the override clamped to `[0.01,1]`.
  - shell `emissive` `{0,10}` (1 = full, >1 bloom), `rimPower` `{0,8}` (Fresnel
    exponent), pose `scale` `{0,3}` — no downstream clamp; pragmatic soft tops.
  - `alphaTest` is a hard `range {0,1}` (the one field the engine clamps,
    `RecipeRead.cpp`); `mip` is `{0,12}` integral (`SampleLevel` LOD index,
    log2(4096)).
- **Cascade resource removal.** `Remove{Signal,Curve,Mask,Source}{cascade}`
  runs `PlanCascade` (a BFS over `RelationshipsOf`): a resource whose body
  references a to-be-deleted name is deleted too, transitively; a layer that
  references one is deleted; a shell/output param that references one is reset
  to its default (`ResetRefVisitor` — a `ParamRefVisitor` sharing the
  optional/vector/component traversal with `RefVisitor` and `LiteralVisitor`,
  each differing only in an `OnRef(ref, replace)` hook). A variant override
  that still references it refuses. The resource inspectors gate this behind a
  `ConfirmModal`.
- **Channels need at least one bit.** An empty `ChannelSet` serialises to `""`,
  which `ChannelSet::Parse` rejects, so `DrawChannels` disables the sole
  remaining channel toggle rather than letting a commit silently revert.

## Build and tools

- Project targets bypass the compiler launcher; third-party targets still
  use ccache when available. A cached rebuild produced Ninja objects with
  zero header dependencies, allowing incompatible `TextureLab` layouts in
  one DLL. The binary evidence and dependency reproduction are recorded in
  `docs/checkpoints/crash-2026-09-11.md`. Do not restore caching for project targets
  without verifying that cache hits preserve their header dependencies.
- `CMakeLists.txt` defines the engine-free static library and validator for
  both platforms. `cmake/Native.cmake` owns host tests and sanitizer flags;
  `cmake/Windows.cmake` owns dependencies and the SKSE DLL. Native setup does
  not require the Windows SDK. The native preset selects `NATIVE_CXX`, the
  Nix host compiler wrapper; unwrapped clang++ cannot locate the host CRT.
- Windows dependency details remain deliberate: spdlog uses
  `OVERRIDE_FIND_PACKAGE` for CommonLib's `find_package`; rapidcsv's include
  cache variable short-circuits `find_path`; the pinned CommonLib revision
  supports the target runtime; import libraries are lowercased for xwin's
  case-sensitive layout; delayed template parsing supports CommonLib code.
  Third-party install rules stay disabled. FetchContent uses its standard
  per-build source storage (unless explicitly overridden), and dependency
  binary directories always belong to the current configuration.
- `cmake/Plugin.cpp.in` reproduces the CommonLib helper's SKSE declaration
  with `configure_file`, preserving its timestamp when metadata is unchanged.
  The upstream helper unconditionally writes its generated source on every
  configure, causing avoidable recompilation and relinking.
- `cmake/clang-cl-xwin.toolchain.cmake` cross-compiles x64 MSVC ABI with
  clang-cl and lld-link against `XWIN_DIR`, which `nix develop` sets to the
  `build/windows-sdk` link to the flake's `windows-sdk` package: an xwin
  splat of SDK 10.0.26100 and CRT 14.44.17.14, pinned by its output hash.
  The package is marked unfree, so building it needs `NIXPKGS_ALLOW_UNFREE=1`
  and `--impure`; that explicit step is where a builder accepts Microsoft's
  license, and the dev shell never fetches it. Two independent builds of it give
  the same hash. If
  Microsoft withdraws either version from the manifest, the build fails
  instead of changing; raise the versions and the hash together. An exported
  `XWIN_DIR` overrides it. Every configuration uses the release CRT (`/MD`).
- `cmake/Generated.cmake` runs the identity script as an always-run target
  whose outputs are byproducts; Ninja restats them, so an unchanged header
  does not recompile the plugin. Presenter copies are refreshed when CMake
  configures.
- Without a root `.git` entry, build identity reads the revision from
  `cmake/source-revision.txt`, which `.gitattributes` marks `export-subst`
  so `git archive` stamps it with the commit hash. An unstamped file stops
  the build; a parent checkout's revision is never used. An archive build
  does not detect later edits to the extracted tree.
- `cmake/Compatibility.cmake` reads the selected profile with
  `string(JSON)`, stops configuration when a field it needs is missing, and
  compares the SHA-256 of `src/cs/BSLightingShaderMaterialPBR.h` and
  `src/extern/SKSEMenuFramework.h` with the profile, because those headers
  fix engine and peer struct layouts. It writes `BuildCompatibility.h` and
  `Plugin.cpp`; other profile fields are not validated.
- `cmake/Stage.cmake` defines `stage`. Building the plugin target alone does
  not stage; the `windows-release` build preset builds `stage`, and
  `tools/gate.sh release` builds `all` so it never touches `dist/`.
  It copies the DLL/PDB, build identity file, INI, templates, presets, presenter DDS
  files, and Windows validator. Recipe files are not staged. Runtime asset
  changes do not require relinking to reach the staged mod. It also writes
  `generated/package.json`, the explicit file list that
  `cmake/Package.cmake` archives and `tools/demo-package.py` reads.
- CMake presets delegate dependency tracking and linking to Ninja
  and execution to CTest. Each test has separate scratch storage. Normal
  and ASan/UBSan builds use separate directories; schema validation is
  required. `BEEF_UPDATE=1` enables intentional fixture updates.
- `python3 tools/compile-db.py` configures Release and writes the first-party clangd
  view only when changed: `src/` entries from the Windows database and, when
  `build/native` is configured, `tests/` entries from the native database.
  `tools/tidy.py` reads the Windows database and runs uncached. Baselines
  deduplicate diagnostics and compare file/check counts, including headers;
  line shifts and resolved findings do not fail checks.
- `docs/build.md` owns commands and recovery. `install.sh` copies
  the staged mod while preserving an existing INI and reporting copy errors.
- `tools/rename.py` drives the `clangd` on `PATH`, which the dev shell
  makes the unwrapped one: a wrapped clangd adds the host's glibc and
  libstdc++ include paths, which shadow the Windows SDK's. clangd starts
  indexing when a file it covers is first opened; a file edited since the
  last run is re-indexed after the progress token ends, so a symbol it
  declares can be missing for a while; edits are applied from the end so
  earlier offsets stay valid.
- `src/Identity.h` is the only place the plugin name is spelled; CMake's
  `project()` feeds it. Recipes load from `Data/<plugin>/<anything>/*.json`
  recursively; the importer writes under `imported/`, the menu saves under
  `user/`, which loads last. The shipped region presets sit beside the DLL
  rather than under the recipe root, which loads every `.json` below it as
  a recipe.

## planners (`planners/ActorPlan.h`, `planners/TargetPool.h`, `planners/StackPlan.h`, `planners/BindingPlan.h`, `planners/ActorPlanning.h`, `planners/TextureIdentity.h`)

- `EvictionFor` restores an evicted actor only inside 80% of the eviction
  radius. This hysteresis prevents repeated retire/reapply near the boundary;
  a non-positive radius disables distance eviction.

- `TargetPool` (`planners/TargetPool.h`) hands out an index as a
  `shared_ptr` lease. An index is free again only after its last owner
  releases the lease, and a lease outlives the pool that issued it, so
  `render/RenderTargetPool`'s presenter slots stay reserved while any
  target still holds one.

- `OwnedState` (`planners/OwnedState.h`) restores a coupled field group only
  while every value still equals what we last wrote. Equality is the whole test,
  so an external writer that happens to write the same value is
  indistinguishable from us and the restore proceeds.

- `ConsumptionLeases` (`planners/ConsumptionLeases.h`) releases a resource when
  the consumer acknowledges submission (`Ticket::Consumed`), never on elapsed
  ticks: a tick delay is not evidence the draw happened. Unacknowledged work
  stays retained, including across a clear.

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

- **Opaque handle spaces.** `GeometryId`, `InstanceId`, `PlacementId`, `RecipeId`,
  `OutputId` are `enum class : std::size_t` typed index spaces (as
  `Merge.h`'s `SlotContributor`/`LightContributor` already are). They carry no
  enumerators, so the one-spec-table-per-enum rule does not apply to them — a
  handle is an index, not a closed set. `RecipeId` indexes the recipe store the
  shell passes as `std::span<const Recipe>`; the pure `ActorPlan` never stores
  a `const Recipe*`, because a long-lived table cannot own a pointer's validity.
  `Merge`'s `PlacedRecipe` holds `const Recipe*` only as a transient function
  argument, never in a table — `GeometryPlacement`/`ActorLightPlan` follow that
  same transient-only rule.

- **Three-table `ActorPlan`** (DECIDED 2026-09-09; `design-actor-state-tables`).
  A `Geometry` is one engine geometry, not an armor: it carries its own
  `GeometryIdentity` plus the armor's `WornPiece` match keys, denormalised onto
  each geometry so selector matching is per-geometry. `Instance` grain is per
  recipe per actor per enchantment form (`FindInstance` dedups by
  `RecipeId` + enchantment `FormKey`; unenchanted matches share one instance per
  recipe). `Placement` joins an instance to a geometry with the `RecipeKey` it
  matched by and per-surface-output selection. The engine runtime that the
  frozen `Manager` nested under these — `MaterialBinding`/`ShellBinding`/
  `LightBinding`, `SignalState`/`ActorEnvironment`, `startMS`/`lastTime` — is
  shell-owned state keyed by the same handles, not fields of the pure tables.
  `Geometry::lost` is the one shell-maintained flag the pure `AnyLiveGeometry`
  reads (a geometry whose material or shell another system replaced);
  light-liveness stays a shell check because a light is a `RE::` binding.

- **Row projections belong to `studio/`, not here.** The frozen `BuildSnapshot`
  inlined its row builds. The recipe-definition → row projection is a view-model
  concern owned by `studio/Rows`/`Panels`/`Board`. The planners expose only the
  `ActorPlan` tables and structural queries. The studio `Snapshot` is assembled
  in wave-3 glue, which reads `ActorPlan` for structure and overlays the live
  and engine-only fields (evaluated `SignalState` values, the `StackPlan`'s
  animated flag, resident `RE::NiSourceTexture*`, rendered stack size, `EditorID`
  lookups, reference counts) over the studio rows. The planners never build the
  `Snapshot` type.

- **`StackPlan` classification.** A slot's chain and its replace cut come from
  `Merge`'s `SlotPlan`; `PlanStacks` adds static-vs-animated. `selfAnimated` is
  `IsAnimated(recipe, Output{SurfaceOutput})` (`recipe/Vocabulary.cpp`) — true
  iff a layer reads a scrolling/tiling image, a ripple, a non-constant signal,
  or a mask over one. `animated` is the chained-base result: link 0 renders over the
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

A `TextureRef` built from a `RenderTarget` retains that producer directly. The
`NiSourceTexture` constructors are the engine boundary's: a texture arriving as a
pointer is looked up in the registry, which is why pointer lookup exists at all
and why it is not the path a generated texture takes.

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

See [the cleanup checkpoint](docs/checkpoints/cleanup-checkpoint-2026-09-13.md) for the
material group journal, retained external textures, explicit actor retirement,
presenter slot leases, skin palette ownership, and preview submission contract.
`SourceSampling` centralizes compositor sampling policy. `ChangeAndRebuildActors`
retires candidates before mutation and rebuilds them afterward; its recipe
argument scopes reporting, not actor selection. Generated compositor outputs and
snapshot retention now use `TextureRef` directly.

## History

Sections describing code that lives only in `src/_old/`. They stay because the
frozen tree stays: it is the behaviour oracle each module is diffed against.

### Paint's term templates (`_old/Paint.cpp`, `_old/Region.cpp`, `_old/TermKind.h`)

The frozen tree's shape. The live rules are under studio above
(`studio/Mask.h`, `studio/TermTemplates.h`, `studio/PaintSession.h`), where
`BuildRegion` is `BuildMask` and the paint-era word "region" is gone.

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

### Menu dependency preflight

The vendored framework header's `IsInstalled` checks a disk path, whereas
its wrappers resolve exports from the loaded module. Many ImGui wrappers
call the resulting pointer without checking it. Registration therefore uses
`MenuDependency` to require a loaded module and the current editor export
inventory before publishing callbacks. The inventory includes every overload
of each used wrapper name; `tests/tools/menu_exports_tests.py` checks it
against first-party qualified calls and the vendored header. This conservative
export check does not certify signatures or ImGui structure layouts.

Without Community Shaders the runtime hooks are not installed, so the editor
may never receive a snapshot. Missing-dependency messages must also be drawn
on that path. An existing snapshot with the effects path disabled can instead
reflect a failed PBR layout check and must not be labeled a missing DLL.

## Armor-addon selector identity (2026-09-23)

CommonLibSSE-NG's `RE/B/BipedAnim.h` declares `BIPOBJECT::addon` as
`TESObjectARMA*` alongside `partClone`. `Manager::CollectPieces` preserves
that entry's optional addon as a plugin/local-ID `FormKey` on `LivePiece`;
`BuildPlannerGeometries` copies it to each geometry collected from the clone.
Use this association rather than guessing from the armor's addon list or
mesh name. A missing addon stays absent and cannot match an addon-only selector.
`LightEligible` uses that same identity for planning and actual light placement.

## Recipe sampling (2026-09-23)

`recipe/Resolve.cpp` pins FNV-1a 32-bit: offset basis 2166136261, prime
16777619, four actor form-ID bytes in little-endian order, unsigned wrapping.
Candidate identities sort by case-sensitive UTF-8 bytes before modulo
selection; composition priority and definition traversal order do not enter
the hash. `tests/planners/resolution_tests.cpp` contains golden vectors.
The approved behavior and compatibility change live in
`docs/recipe-resolution.md`.

## Compatibility profiles

Target profiles in `cmake/compatibility` drive pins, loader declarations, startup
checks, and package identity. CommonLib structure independence covers its Skyrim
structure boundary, not the mirrored CS PBR ABI. Explicit runtime whitelists still
require Address Library for relocations. See the
[profile audit](docs/checkpoints/compatibility-profiles-2026-09-24.md) for the
initial candidate and unverified assumptions.

## Static-analyzer ownership review

Image-source form bindings are owned by `std::function` inside `FormField`, then
by the returned form vector; retaining a binding copies its source record. The
MSVC allocation trace is a reviewed analyzer modeling warning, corroborated by
native lifetime/leak coverage. Relocation warnings still depend on valid runtime
and Address Library inputs. See the
[analyzer checkpoint](docs/checkpoints/static-analyzer-2026-09-24.md) for traces,
dispositions, report-parser corrections, and validation limits.

The hook installer checks the resolved PlayerCharacter vtable address before
CommonLib performs slot arithmetic. `std::call_once` publishes the completed
installation result; an atomic failure flag exposes an immutable message to
the menu without requiring a frame snapshot. A nonzero address is not proof
of mapping or slot validity. See the
[startup safeguard](docs/checkpoints/hook-startup-2026-09-24.md).

## First-party legal notices (2026-09-24)

`COPYING.md` states GPL-3.0-only and the first-party modding/linking permission.
GPL section 7 requires a statement or reference to additional terms in relevant
source files. First-party C++ files and `cmake/Plugin.cpp.in` carry a short
reference; required legal notices are excepted from the no-comments convention.
Vendored files retain their exact upstream bytes. `render/PBRMaterial.h`
separately references the Community Shaders-derived material's upstream terms.
`COPYING.md` participates in build identity so a changed permission produces a
new candidate identity, and is shipped in source, mod, symbols and staging.

## Paint source retention (2026-09-25)

A paint session retains generated definitions so mask undo can reconstruct old
terms. `PaintDependencies` selects only definitions referenced by current term
expressions for preview and Keep requests, including muted and non-solo terms and the current peek.
Historical definitions are not live recipe dependencies. A malformed term keeps
the catalog conservatively until expression validation reports the problem.

When Keep replaces a mask, `KeepEdits` checks the prepared recipe's reference
counts and removes direct source dependencies abandoned by that mask only when
no other recipe row references them. Unrelated unused author-created sources
are preserved. The removals share the mask edit's document undo transaction.
Existing unrelated orphan rows are not swept retroactively.

## Material terms and diffuse color (2026-09-25)

Cluster IDs are local ranks within each geometry's independently sampled
material; a `ClusterTerm` contains settings and an ID, not a geometry selector.
Identical material terms therefore produce identical recipe expressions even
when offered by different geometries. `OffersOfRecipe` consolidates these
operations, retains a common description/coverage when equal, and explicitly
reports varying appearance otherwise. Parts, charts and other geometry offers
retain their existing behavior.

Diffuse color adds three axes to the existing four RMAOS axes and diffuse luma.
`weights.color` defaults to 1 and weights the mean squared RGB distance, so RGB
as a group has the same maximum contribution as one scalar channel. Luma remains
independently adjustable. Color 0 restores the prior distance metric. CPU sample
readback, centroids, GPU constant layout, nearest-cluster shader and cluster-map
cache identity all carry color. Existing recipes that omit color now use it and
may receive different cluster IDs; this is an intentional pre-alpha behavior
change. The five-value source weights field retains its order, with a separate
color field, while term tuning exposes all six weights individually.

## Unified recipe graph and signal execution

`RecipeGraph` owns copied authored rows and compiled programs. Named curves and
anonymous inline curves are function-domain nodes in the same graph as value
expressions. Dependencies and function calls use indices into that graph, so
copying a compiled graph does not retain pointers into the original.

`SignalState` borrows an immutable graph that must remain at the same address
and outlive it. Construction binds signal parameter references and function
program pointers into that graph and allocates expression scratch. A graph
may be copied or moved before constructing its state; existing states must be
rebuilt after graph replacement. The engine instance holds shared ownership
of its graph alongside its state. Renderer signal handles belong to that same
instance graph.

`CheckSourceInputs` performs typed source checks without compiling a candidate.
The graph compiler uses it during analysis. Editor-facing `CheckSource` also
compiles changed candidate rows, so using it inside compilation would recurse
for duplicate authored source names with different definitions.

Function nodes currently implement the scalar curve contract: scalar `x` and
`mean`, a scalar result, no row reads or nested function calls. `time` has no
binding at this execution site and is rejected instead of silently reading
zero. These are validation rules on the common program representation, not a
second compiled expression payload.

Prepared mask and ripple cache keys include the application context (instance
index plus one) within one geometry's cache. Preparation and inspection supply
the same context. Geometry retirement clears these caches before instance
indices are reused. Global static-target sharing additionally requires that
the compiled signals equal the authored signals used in the existing cache
key; variant overrides conservatively disable that sharing until effective
computation identities replace recipe-based keys.

## Console-driven regression fixture

`engine/Regression.cpp` registers the optional quest's Papyrus bridge. Calls
store scalar request data and queue mutations through Manager's session task
queue. Application completion must belong to a newer actor attempt, with a
rendered surface output on demo armor; retirement checks actor-state removal.
The bridge exposes strings and identifiers rather than renderer pointers.
Save/new-game messages invalidate the request and the process/session token.

The runner and packaging contract are in `tests/in-game/README.md`. Caprica
v0.3.0 in Skyrim mode compiles the scripts against narrow API declarations;
only the two fixture scripts enter the package. The quest is manually started,
with no aliases or fragments. QUST and VMAD layouts follow the TES5Edit
`dev-4.1.5` definitions in `Core/wbDefinitionsTES5.pas`. These offline checks do
not establish game execution or rendered acceptance.
