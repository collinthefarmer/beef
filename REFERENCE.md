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
  objects. Operation context is thread-local and explicitly carried through
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
  (`Validator`, `CheckSourceKind`, `EditCheck`) become one.
- Each row kind has one check. Its field rules (ranges, counts, one value
  alone) are written once in `Validation.cpp` (`CheckSignalFields`,
  `CheckSourceFields`, `CheckOutputFields`, `CheckShellFields`,
  `CheckVariantFields`), and the graph-side row check calls them before its
  reference and type rules (`CheckSourceRow`, `CheckOutput`, `CheckShell`,
  `CheckVariants`; signals through `RecipeGraph`'s analysis). Graph
  compilation, studio edits and `Validate` all reach a rule through that one
  check, so a field error also makes its row inert. `CheckRecipeLevelFields`
  holds the rules about the whole recipe; `CheckRecipeFields` (every field
  rule, no graph) stays as the cheap check `PrepareEdits` runs after each edit
  batch. A curve on a
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

### The applied set (`recipe/RecipeCatalog.h`)

- `RecipeCatalog` keeps its recipes and fingerprints in two private vectors,
  not one vector of `CatalogEntry`, because `Recipes()` must return a
  `std::span<const Recipe>`: `Resolve`, `MatchActor`, `PlanGeometryPlacement`,
  `PlanActorLights` and about 150 call sites, most of them tests, take that
  span. `Of` is the only way to build a catalog, so the two vectors always
  have one entry each per recipe.
- `ContentFingerprint` is FNV-1a 64 over `SerializeRecipe`. The store caches
  it per loaded recipe and resets it when the recipe is republished.
- Each publication copies every applied recipe into a new catalog. An actor
  that a scoped rebuild skips keeps its older catalog alive until its next
  rebuild, so the worst case is one full catalog per publication that a live
  actor still holds. The size of a `Recipe` in memory was not measured
  (2026-10-06).
- A **debug recipe** is a file under the top-level `recipes/debug` folder
  (`ClassOfRecipePath`). The folder name ignores case, because Windows
  folder names do. A `debug` folder below another folder, and a top-level
  file named `debug.json`, are regular. `LoadRecipes` classifies each
  shipped file once and keeps the class on `LoadedRecipe::recipeClass`. A
  file under `recipes/user` is regular, but a user file that replaces a
  debug recipe of the same id stays debug, because saving a debug recipe
  in the editor writes it to `recipes/user/<id>.json`.
- `RebuildApplied` publishes two catalogs: one with every applied recipe,
  and one without the debug recipes (`MatchingEntries`). When no debug
  recipe is loaded, both are the same object, so the second costs nothing.
  `LoadedRecipes()` reads the full catalog, so the Recipes and Studio pages
  list and edit debug recipes. The tick's gather (`Manager::GatherFacts`)
  reads `AppliedCatalog(DebugRecipes)` once per tick with the
  `ShowDebugRecipes` setting. Planning and the `Install` executor use that
  catalog, so a hidden debug recipe never matches a piece. The filter sits at the catalog
  and not in `MatchActor`, because a `RecipeId` indexes the catalog an actor
  was planned against, and a smaller catalog keeps every planner unchanged.
- `ShowDebugRecipes` is a `reapply` setting. The Setup page calls
  `ReapplyAll`, which rebuilds every actor through
  `ChangeAndRebuildActors` (`RebuildAll`). The next gather reads the catalog
  for the new value, so actors gain or lose debug recipes without a
  reload. An INI edit takes effect at the next reapply.
- The inspector's per-geometry selections (`Resolve` over `LoadedRecipes()`
  in `ManagerSnapshot.cpp`) list every loaded recipe, so they can show a
  hidden debug recipe as selected for a key it would match.

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
        | "rand" "(" expr? ")"
        | "randrange" "(" expr "," expr ("," expr)? ")"
        | "x" | "mean" | "time" | "pi"
```

- The functions are the rows of `ExpressionFunctions()` (`Expression.h`):
  a name, a usage string and a call form. A `FixedCall` has an op and an
  argument count; `RandomCall` is `rand()`; `RandomRangeCall` is
  `randrange` with an optional seed. The schema's expression description
  lists every row, and `schema_tests` checks it. The studio's expression
  hint appends the usage strings (`Studio::FieldKindHelp`). There is no `^`.
- `rand()` expands at parse time to `identity`, a salt and `hash`. The salt
  counts the `rand()` calls of one expression, starting at 0, so each call
  draws a different value. `randrange(start, stop)` uses the next `rand()`
  as its seed. The lowering binds `identity` to `Program::Hash` of the
  instance identity and the row name; `Program::Hash` builds that program
  without parsing, so it cannot fail.
- `shuffle(index, total, seed)` is the index's position in a fixed order of
  `0 … total-1` that the seed chooses, so `shuffle(i, n, s) < k` picks
  exactly k of n items. The order is the affine map `(a·i + b) mod P` over
  the prime P at or above the total (a in 1…P-1 from `frac(seed·29.41)`, b
  in 0…P-1 from `frac(seed·13.37)`), cycle-walked back into range: a
  position at or above the total is mapped again until it is below, which
  keeps the order a permutation and ends within P steps. The total is
  floored and capped at 251 (`kMaxShuffleTotal`), so the walk stays
  bounded on the GPU. The CPU finds P with `PrimeAtLeast` (gaps below 16).
  The GPU reads P from `kPrimeAtLeast`, a 252-entry table that
  `ShaderSource.cpp` generates from `PrimeAtLeast`, so the two cannot
  differ and no texel searches for a prime. An index outside the
  order, an empty or unreadable total, and a non-finite index all give 256
  (`kShuffleOutside`), which no `< k` with k ≤ 251 selects; a non-finite
  seed orders like seed 0. It takes scalars only. `ShufflePosition`
  (`recipe/ExpressionMath.cpp`) and the HLSL `Shuffle` (`shaders/ProgramOps.hlsl`)
  are the same arithmetic; the CPU reference calls the former.
- `hash(value, salt)` mixes the two floats' bit patterns with Chris
  Wellons' `lowbias32` (xor-shift-multiply by 0x7feb352d and 0x846ca68b):
  `mix(bits(value) ^ mix(bits(salt) + 0x9e3779b9))`, and returns the top
  24 bits over 2^24, so it is exact in a float and below 1. A non-finite
  value and -0 hash as +0. The CPU (`HashUnit`) and HLSL (`Hash`) agree bit
  for bit on equal inputs; a value computed separately on each side can
  differ in its last bit and then hash differently, so seed it from a row
  the CPU evaluates (a signal or an imported uniform), not from per-texel
  arithmetic, when the CPU preview must match.
- `randrange(start, stop[, seed])` floors both bounds and returns
  `start + min(floor(seed · span), span - 1)` with the seed clamped to
  0..1, so a seed of 1 stays below `stop`; an empty or reversed range
  returns `start`. Without a seed it uses `rand()`.
- `rand(salt)` is `hash(identity, salt)`: the salt pins the value, so it
  does not move when other `rand()` calls are added or removed. A salt of n
  equals the n-th free call. `rand()` without a salt is `hash(identity, n)`
  for the n-th free `rand()` (or seedless `randrange`) in the expression, where the lowering binds `identity` to a
  per-row node: `hash(hash(idHash, effectHash), UnitHash(row name))` for a
  signal, with the item hash mixed in (`hash(…, itemHash)`) for a mask,
  which is per piece. So two rows, or two calls in one row, differ; the same
  enchantment, effect, item and row repeat exactly; renaming a row changes
  its values. Unbound (validation, a bare evaluation) the identity is 0. A
  curve has no row and rejects `rand()`; a program with `rand()` is never
  folded as a constant.
- Every `rand()` step is static (2026-10-07). An instance's `EnchantmentSignal`
  fields (`magnitude`, `cost`, `idHash`, `effectHash`) never change: the
  enchantment form and the matched effect are part of the instance's
  identity, and a re-equip builds a new instance. So `TraitsOf` does not mark
  `EnchantmentSignal` as changing, and a step over `rand()` or an
  enchantment field runs once and gets a content key. The input's identity
  is its value (`enchantment:<field>:<bits>`, `EnchantmentIdentity` in
  `planners/ValueIdentity.h`, which the template-cost test also calls), taken from `GeometryInputs::enchantment`,
  which `StackInputsOf` fills per instance from `ActorEnvironment`. An
  identity by value is required: the render plan key (W2) reads static input
  identities with the actor left out, so an identity by instance would let
  two actors with different enchantments share a plan. Before 2026-10-07
  every `rand()` step counted as animated and ran again whenever the actor
  was re-evaluated.
- An `EffectShaderInput` is identified by its record alone (`efsh:<record>`),
  because `ActorEnvironment::EffectShader` reads the same parameters from the
  form for every actor. Before 2026-10-07 the identity carried the instance
  string, so two instances of one recipe got different render plan keys and
  their sharing depended on which plan the cache held first.
- An `efsh` signal without `record` lowers to a `MatchedShaderInput`, and an
  `image` with `effectShader` lowers to a `MatchedShaderTextureInput`
  (2026-10-12). Their values depend on the effect that the recipe instance
  matched, so one recipe on two effects can read two shaders. Their
  identities come from `IdentityFacts::shader` (`MatchedShaderFacts`), which
  holds the resolved shader. A `MatchedShaderInput` is
  `efsh:matched:<form key>`, with an empty form key when the effect has no
  shader. A `MatchedShaderTextureInput` is `ImageCacheKey` of the resolved
  texture path, the same identity as a `TextureInput` with that path. The
  identity never names the instance or the actor. The render plan key
  leaves the actor out, so an identity by instance would not separate two
  shaders, and the 2026-10-06 `item:<hash>` bug shows what a key that misses
  an input does: two items shared static outputs.
- `hue(turns)` is the fully saturated wheel colour
  `saturate(|frac(turns + [0, 2/3, 1/3]) · 6 - 3| - 1)`: 0 red, 1/3 green,
  2/3 blue, wrapping each turn. `hueShift(colour, turns)` rotates the colour
  about the grey axis (1, 1, 1)/√3 by `turns · 2π` (Rodrigues), which keeps
  the channel sum and so the brightness, then saturates. A non-finite number
  of turns counts as 0, and a non-finite result channel is 0 on the CPU
  (`Saturated`) and on the GPU (`isfinite`, since 2026-10-07; before, HLSL
  `saturate` gave 1 for +inf). `HueColor`/`ShiftHue` (`recipe/ExpressionMath.cpp`) and
  the HLSL `Hue`/`HueShift` are the same formulas. `hue` matches bit for bit,
  because it uses only `frac`, `abs` and `saturate`. `hueShift` matches to
  rounding only, because the CPU and HLSL `sin` and `cos` differ in their last
  bits.
- `pow(a, b)` follows `std::pow` and maps a non-finite result to 0:
  `pow(-2, 2)` is 4, `pow(-2, 3)` is -8, `pow(0, 0)` is 1, a negative base
  with a fractional exponent is 0. The GPU interpreter and the CPU reference
  compute the magnitude as `exp2(log2(|a|) * b)`, because HLSL `pow` is
  that expansion and is undefined for a negative base; the recipe
  evaluator uses `std::pow`, which agrees to rounding.
- A lookup (a curve) is a 256-entry R32_FLOAT texture. The shader reads the
  two neighbouring entries with `Load` and interpolates in the shader,
  exactly as the CPU reference does. The sampler's linear filter is not
  used, because D3D11 guarantees only 8 bits of filter-weight precision.
- The vector family (`length`, `distance`, `dot`, `cross`, `normalize`)
  rejects scalar operands at check time: the shader represents a scalar
  as a splatted float3, so a GPU `length` or `dot` of one would disagree
  with the CPU, while a vec2 is `float3(x, y, 0)` and agrees. `length`,
  `distance` and `dot` and `hue` are the only functions that change a value's
  type (vector in, scalar out; `hue` scalar in, vec3 out); `cross` takes and yields vec3s, and `hueShift` takes a vec3 and a scalar;
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
  (256) is also the interpreter's instruction capacity: `programCode` holds two
  instructions per row, so the shader declares `uint4 programCode[128]`
  (`shaders/ProgramOps.hlsl`, sized by the emitted `kProgramRowCount`). The two must agree or a long mask overruns the
  constant buffer; the dxc layout check compares the declared size with
  `ProgramConstants`. The value is carried unchanged
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
`TextureLabPass.cpp`, `TextureLabReadback.cpp`, `planners/ShaderSource.cpp`, `src/shaders/`)

The shader code lives in `src/shaders/*.hlsl`, one shared file and one file
per pass. `cmake/EmbedShaders.cmake` writes every file into a generated
`ShaderFiles.cpp` as a raw string, and `ShaderFiles()` returns them by name.
`ShaderSource.cpp` joins the parts in the order of `kShaderParts` into the one
string `kShaderSource`. The runtime compiles each entry point from that
string, as before. The parts, in order:

| Part | Kind | Holds |
|---|---|---|
| words | generated | `static const uint` lines from `kShaderWords` and the blend modes of `kBlends` (`kBlendMultiply`, ...). |
| palette | generated | `kIdPalette` from `kClusterPalette`. |
| resources | generated | The sampler `linearSampler` and every `Texture2D` from `kShaderTextures`. |
| switches | generated | The texture switches `SampleProgramTexture`, `ProgramTextureSize`, `LoadLookup`, `SampleStackSource`, `SampleStackMask`, from `kTextureSwitches`. SM5 cannot index a texture array by a runtime value, so a switch selects the texture. |
| `Common.hlsl` | file | `VSMain`, the records `TextureRead`, `LayerMix`, `NormalDetail`, and the layer composition: `Pick`, `Blend`, `LayerValue`, `ComposeLayer`, `LayerSourceValue`, `MixLayer`, `MixBits`. |
| `Pass.hlsl` | file | `PSMain` and its modes. |
| `ProgramOps.hlsl` | file | The interpreter's constants and the helpers its opcodes call. |
| opcodes | generated | `RunOpcode`: the opcode switch and the z rule from the `ProgramShader` table. |
| `Program.hlsl` | file | `RunProgram` and `PSProgram`. |
| `Bake.hlsl`, `Noise.hlsl`, `Dilate.hlsl`, `Filter.hlsl`, `Reduce.hlsl`, `Ripple.hlsl`, `Stack.hlsl`, `Material.hlsl`, `Groups.hlsl` | file | One pass each. `Material.hlsl` holds `NormalDetailAt` and `PSDiffuseDetail`, which `Groups.hlsl` also uses. |

A generated part comes before the first file that uses it. A file that uses
a later part fails to compile, and the dxc check reports it.

Layer composition has one implementation. `PSMain`'s layer mode and
`PSStack` both call `LayerSourceValue` and `MixLayer`, which take the
`TextureRead` and `LayerMix` records. A generated stack shader calls
`LayerValue` and `ComposeLayer` directly with the layer's blend, channels and
mask channel as literals, so the compiler can drop the branches the layer
does not take.

Texture registers (`kShaderTextures`; the C++ binding code uses the same slot
constants):

| Register | Name | Pass |
|---|---|---|
| t0 | `passSourceTexture` | `PSMain` |
| t1 | `passMapTexture` (mask in the layer mode, input map in the channel mode) | `PSMain` |
| t2 | `passBelowTexture` | `PSMain` |
| t3 | `passCurveTexture` | `PSMain` |
| t0..t7 | `programTexture0..7` | `PSProgram`, `PSStack`, generated shaders |
| t8..t11 | `programLookup0..3` | `PSProgram`, `PSStack`, generated shaders |
| t12 | `stackBaseTexture` | `PSStack`, generated stack shaders |
| t13..t20 | `stackSourceTexture0..7` | `PSStack`, generated stack shaders |
| t21..t28 | `stackMaskTexture0..7` | `PSStack`, generated stack shaders |
| t0 | `materialDiffuse` | `PSClusters`, `PSDiffuseDetail` |
| t1 | `materialRmaos` | `PSClusters` |
| t2 | `materialNormal` | `PSClusters`, `PSDiffuseDetail` |
| t3 | `groupChartIds` | `PSClusters` |
| t4, t5 | `groupOddsLow`, `groupOddsHigh` | `PSClusters` |
| t0 | `filterInput` | `PSFilter` |
| t1 | `filterCharts` | `PSFilter` |
| t0 | `singleSource` | `DilatePS`, `PSReduce`, `PSRipple` |

Several names share a register because each pass binds its own textures
before it draws. A pass reads only the names of its own row.

One pixel shader over a full-screen triangle serves every mode of
`TextureLab::Mode`; the interpreter, bake, ripple and classify passes are
separate shaders so a fault in one costs only its outputs. The shader's op
numbers are `Program::Op`'s enum values and the shader's switch is written
to them; its arrays are sized to `kMaxExpressionOps`, `kProgramInputs`,
`kProgramLookups`, `kRippleFirings` and `kMaxMaterialClusters`, and every
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
(`part`, `chart`, and the material groups map — all three
carry id / 255 texels through the same linear sampler) fabricates ids
between the two — those want nearest sampling, recorded as a backlog
item, not a dilation fix.

`planners/ShaderConstants.h` declares the C++ side of every `cbuffer`. The
lab, the pass files and the shader test include it. Each C++ struct uses the
same member names, types and order as its `cbuffer`, so a field has one name
on both sides. The shader test compiles each entry point with dxc, reads the
compiler's layout listing and compares every member's name, type and offset
with the C++ struct (see "Shader source check"). `render/D3DResult.h` holds
the two D3D helpers every render file needs, `Failed` on an `HRESULT` and
`DataOf` on an `NiSourceTexture`.

Data rules for the constants:

- A flag is a `bool` in HLSL and a `ShaderBool` (0 or 1, 32 bits) in C++.
- An enum value, count, index or slot is a `uint`. A bit set is a `uint`
  named `options`.
- Every struct inside a `cbuffer` fills whole 16-byte rows. HLSL packs a
  scalar that follows an array of structs into the free tail of the last
  element, and C++ does not, so a struct with a partial last row would move
  every member after it. `GroupScore::padding` fills its last row for this
  reason.
- Every `cbuffer` member name is global in HLSL, so each name carries its
  buffer's prefix (`pass`, `program`, `stack`, `ripple`, `group`,
  `diffuseDetail`, `noise`, `filter`, `reduce`).
- Every value the shaders compare against or size an array by is declared
  once in C++ and emitted as a `static const uint` line: option bits
  (`kReadMirrorU`, `kLayerHasMask`), write channels (`kWriteR`), pass modes
  (`kPassLayer`), map views, group draws, channels (`kChannelLuma`), blend
  modes (`kBlendReorient`), reduction kinds, filter operations, noise styles,
  the instruction byte positions and the array counts (`kProgramRowCount`,
  `kStackLayerCount`, `kGroupCount`). `PassMode` replaced the old mode
  numbers 0, 4, 6 and 7 with 0 to 3; `TextureLab::Mode` is an alias of it.

Constant slots (`ConstantSlot`; `SavedPipeline` saves and restores all nine
slots around a pass):

| Slot | `cbuffer` | C++ struct | Read by |
|---|---|---|---|
| b0 | `PassParams` | `PassConstants` | `PSMain` |
| b1 | `ProgramParams` | `ProgramConstants` | `PSProgram`, `PSStack`, generated shaders |
| b2 | `RippleParams` | `RippleConstants` | `PSRipple` |
| b3 | `GroupParams` | `GroupConstants` | `PSClusters` |
| b4 | `ReduceParams` | `ReductionConstants` | `PSReduce` |
| b5 | `StackParams` | `StackConstants` | `PSStack`, generated stack shaders |
| b6 | `DiffuseDetailParams` | `DiffuseDetailConstants` | `PSDiffuseDetail` |
| b7 | `BakeNoiseParams` | `BakeNoiseConstants` | `BakeNoisePS` |
| b8 | `FilterParams` | `FilterConstants` | `PSFilter` |

Shared records:

| Record | Fields |
|---|---|
| `TextureRead` | `offset`, `tile`: the uv placement. `mip`: the read mip. `normalize`: the factor on the read value. `channel`: the `ShaderChannel` that `Pick` reads. `options`: `kReadMirrorU`, `kReadMirrorV`, `kReadTranspose`, `kReadMeshSpace` (read at the raw uv), `kReadNearest` (snap to the texel centre). |
| `LayerMix` | `color`, `opacity`, `blend` (the shader blend mode), `writeChannels` (r 1, g 2, b 4, a 8), `maskChannel`. `options`: `kLayerHasSource`, `kLayerHasMask`, `kLayerHasBelow`, `kLayerHasCurve`. |
| `NormalDetail` | `fineMip`, `coarseMip`, `footprint` of the normal-detail measure; `present` is false when the normal map is absent. |

`cbuffer PassParams` (b0):

| Field | Holds |
|---|---|
| `passSource` | The source read. In the copy and glow modes only the placement and `mip` count. |
| `passLayer` | The layer mix of the layer mode (6). |
| `passMode` | The `TextureLab::Mode` value. |
| `passBelowMip` | The read mip of the texture below, in the layer mode. |
| `passMapChannel`, `passMapView`, `passMapMip` | The channel mode (4): the channel `Pick` reads, the view (`kMapChannel`, `kMapSlope`, `kMapIds`) and the read mip. Slope wins over ids when `ChannelParams` sets both. |

Mode formulas:

- Relief of an armor input at the raw UV, 0..1 with higher raised: input 1
  reads displacement.r, 2 reads RMAOS occlusion (b), 4 the normal map's
  slope, 5 diffuse luminance `dot(rgb, (0.299, 0.587, 0.114))`.
- Normal slope = `saturate(length(n.xy))` with `n.xy = normal.rg x 2 - 1`:
  the sine of the surface's tilt away from the face, 0 on a flat face and 1
  on a vertical one. The tangent-space z (`sqrt(1 - |n.xy|^2)`) is flatness,
  the inverse, and made every flat face read as an edge.
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
  mix, so it shares replace's arithmetic. A reoriented normal of zero
  length becomes the flat normal (0, 0, 1) instead of a NaN.
- `planners/ProgramShader` holds one row per interpreter opcode: the HLSL
  statement and whether the opcode keeps z for two components (`length`,
  `distance`, `dot`). The generated `RunOpcode` (its switch and its z rule,
  `InterpreterZRule`) and the generated programs are all built from that
  table. `RunProgram` calls `RunOpcode` once per instruction. The table's `kNormalize` statement is the switch's `default`, as in
  the CPU reference.
- A generated program declares one `float3` local per stack slot and runs
  one block per instruction. Each block reads `number` with `NumberAt(k)`
  for a literal `k`, and declares `idx` and `components` as literals, so the
  table statement reads them as the interpreter does. A pop from an empty stack
  reads `float3(0, 0, 0)` and a push past 32 slots is dropped, as in the
  interpreter. Numbers and value inputs stay in the program constants
  (`programCode`, `programInputs`), so programs that differ only in numbers
  share one shader.
- `render/GeneratedShaders` owns the compiled shaders. `ProgramFor` keys
  compiled programs by their generated text, so asking for one formats the
  whole program as HLSL. A program step therefore asks once: its scratch
  keeps the compiled shader (`RenderScratch::programShader`, a counted
  reference that survives the cache evicting the entry), and later draws use
  it without generating text. Until the compile finishes the step asks again
  each draw and draws with the interpreter. Before 2026-10-16 every draw
  generated the text; in run 20261016T041250-1 a mask draw cost 25 us of CPU
  against 11 us for a whole stack.
  The first draw of a text starts a compile on a detached thread and draws
  with the interpreter; later draws use the shader once the compile has
  finished, or the interpreter if it failed. The thread holds its own
  reference to the device and delivers its result through a promise, so a
  pending compile never blocks shutdown and a failure never throws on the
  render thread. The worker creates the shader on the game's device. This
  relies on the device being created without
  `D3D11_CREATE_DEVICE_SINGLETHREADED`, which has not been verified against
  the game.
- Each cache (programs and stack shapes) holds at most 256 entries
  (`GeneratedShaders::kMaxPerCache`). When a cache is full, the least recently used
  finished entry is evicted, and the first eviction writes one log line. A
  cache full of pending compiles starts no new compile until one finishes.
- Layer fields drawn to targets (`RenderFieldsToTargets`, the per-layer
  fallback) use a generated program shader for each segment once it has
  compiled, through the same cache as `RenderProgram`
  (`GeneratedShaders::ProgramFor` keys by the generated text). Until then
  they use the interpreter. The dxc check compiles every segment shader the
  planned recipes produce.
- `planners/StackShader` generates one pixel shader per **stack shape**:
  whether the stack has a base, and per layer whether the source is absent,
  a texture (in mesh space or placed) or a layer field, whether the mask is
  absent, a texture or a layer field, the source channel, blend, channel
  bits and mask channel, plus the fields' code without numbers. The shader
  repeats `PSStack`'s statements with those values as literals, and each
  layer field is a straight-line function built by `ProgramFunction` with
  code indices offset to the packed block. Constants and textures are bound
  exactly as for `PSStack`, so `DrawStackPass` changes only the pixel shader.
  A texture read counts only when its view exists, as in `PSStack`.
- `StackFor` keys compiled stack shaders by shape, compiles on the same
  path and draws with `PSStack` until ready. `DrawStackPass` builds the shape
  once per draw from the layer passes and fills the `PSStack` constants from
  it (`HasSource`, `HasMask`, `SourceSegment`, `MaskSegment`), so the
  generated shader and `PSStack` read the same per-layer facts.
  `CheckStackShape` decides whether a shape fits one pass (at most 8
  layers, every field read inside the packed code); `DrawStackPass` returns
  false when it does not, or when a legacy curve or a pipeline is missing,
  and `RenderStack` then falls back to the per-layer chain.
- The shaders compile with `D3DCOMPILE_OPTIMIZATION_LEVEL3`. The compiler
  may reorder float arithmetic at any level, so an inlined or one-pass draw
  and the separate draws it replaces can
  differ by one 8-bit step at a rounding boundary. An in-game equivalence
  check measured that bound for one-pass stacks, layer fields and generated
  shaders before it was removed (docs/plans/render-performance-2026-09-29.md).
- A stack without a base starts transparent black: alpha is the fuzz
  weight, the coat strength or the subsurface thickness.

Interpreter constants (`cbuffer ProgramParams`, b1):

| Field | Holds |
|---|---|
| `programCode[128]` | Two instructions per row: xy the first, zw the second. Each instruction is a word and the bits of its float number. The word holds the opcode in byte 0, the component count (1 to 3) in byte 1, the pop count in byte 2 and the index in byte 3 (`InstructionWord`). The pop count is stored so the shader does not decode it per instruction. |
| `programInputs[16]` | `value` the broadcast value; `textureSlot` the texture slot the input reads, or `kNoTextureSlot` for a value input. |
| `programTextures[8]` | One `TextureRead` per program texture. |
| `programCount` | The instruction count. |
| `programVectorResult` | True when the result is a vector; false broadcasts x. |

`cbuffer StackParams` (b5): `stackLayers[8]` holds one `StackLayer` per layer:
`source` (a `TextureRead`), `mix` (a `LayerMix`), and `sourceCode` and
`maskCode`, each the first instruction and count of a layer field. A count
of 0 samples the bound texture. `stackLayerCount`, `stackHasBase` and
`stackBaseMip` describe the stack. The shared sampler is linear;
`kReadNearest` snaps the uv to the texel centre before the read, so a texel
is read whole. The id maps (part, chart, material groups) set it:
their texels are identifiers scaled by 1/255, and a linear blend across
an island border fabricates ids that exist on neither side. Every stack value is a float3 with a scalar
broadcast, so component-wise arithmetic matches the CPU's rule;
comparisons and logic read `.x`. `x` evaluates to 0 (it is not per texel)
and `mean` to 0.5. A pop from an empty stack reads 0. After the pops a
binary op has `b` as the first operand and `a` the second; a ternary op
has `d`, `b`, `a` in order.

Ripple pass (`cbuffer RippleParams`, b2): `rippleFirings[8]` holds each
firing's `origin` in bind-pose units and its front `radius`, age x speed.
`rippleStrengths` holds each firing's strength, exp(-decay x age).
`RippleConstantsOf` computes both on the CPU once per draw, so the shader
does no `exp` for the decay. It drops a firing whose strength is below 1/512:
that firing adds less than half of one 8-bit step. `rippleFiringCount`
counts the firings kept. `rippleWidth` and `rippleFrame` shape the fronts. `rippleDisc` draws discs instead of
rings. `rippleDirection` is a normalised worldspace direction, used when
`rippleDirectional` is true. The source is the position bake, rgb = position /
(2 frame) + 0.5. Each firing is a front at distance age x speed from its
origin: radial measures `length(pos - origin)`, directional measures `dot(pos - origin, dir)` so the front is a plane sweeping along
`dir`. A ring is a band of the given width, a disc everything inside (and,
when directional, only ahead of the origin plane); fronts fade by
exp(-decay x age) and combine by max. The direction is resolved once per
tick and shared by that source's live firings; the recipe side normalises
it and a zero vector falls back to radial. A pooled target keeps its last
content, so a pass with no firings paints it black.

Filter pass (`PSFilter`, cbuffer `FilterParams` b8, `FilterConstants`): two
draws, horizontal then vertical. The horizontal draw's target is the lab's
sweep for that size (`FilterSweepFor`): one RGBA16F target per side, kept
while filters run and released by `ExpireIdleTargets` after 1 s without a
filter. Draws run one at a time on the render thread, so one sweep per size
serves every filter. Before 2026-10-16 each filter run acquired and
recycled a pool target, which wrote two trace events per run (128,090
acquisitions in run 20261016T041250-1). The horizontal draw writes the blurred
value, the minimum and the maximum of each texel's row to an RGBA16F target
(`kRgba16Float`). The vertical draw reads that target and writes the
operation's result. `filterWeights` holds the Gaussian weight of each offset
0..16, computed on the CPU by `FilterWeight` (`mesh/FieldFilter.h`, the CPU
reference), so the shader does no `exp` per tap. `filterResamples` is true
only when the input's size differs from the target's. Only then does the
shader scale the texel position with an integer divide. RGBA16F holds an
8-bit input's minimum and maximum exactly after rounding to 8 bits, because
its error (about 0.0005) is below half an 8-bit step (0.002). A blurred value
can still move by that error, so a blur that falls on an 8-bit rounding
boundary can round the other way.

Group pass (`PSClusters`, cbuffer `GroupParams` b3, `GroupConstants`):

| Field | Holds |
|---|---|
| `groups[8]` | One `GroupScore` per group: `mean[2]`, the group's mean in standardised axes; `whitening[9]`, the 36 entries of the inverse Cholesky factor, row by row, lower triangle; `id`; `logScale`, the log weight minus half the log determinant and the normaliser. |
| `groupAxisCentre[2]`, `groupAxisSpread[2]` | The texel standardisation: each of the eight axes minus its centre, divided by its spread. |
| `groupLitCharts[2]` | The lit chart table: one bit per chart id, 256 bits. |
| `groupDetail` | The normal-detail levels. |
| `groupCount` | The group count. |
| `groupDraw`, `groupPicked` | What the pass draws: `kGroupDrawIds`, `kGroupDrawPicked` (1 on the texels of group `groupPicked`) or `kGroupDrawPalette`. |
| `groupOddsWeight` | The weight of the smoothed odds: 1 with smoothing, 0 without. |
| `groupRmaosMip`, `groupDiffuseMip` | The read mips. |
| `groupChartFilter` | True when the lit chart table filters the draw. |

- A texel's score for a group is the group's log density at its own eight
  axes plus w times the log of the group's smoothed odds. The highest score
  wins. The first of equal scores wins.
- The smoothed odds are two RGBA8 textures (`t4`, `t5`) of the 256-texel
  analysis, one channel per group id. `TextureLab::CreateGroupOdds` makes
  them immutable for each draw. A sampled odds value below 1/512 counts as
  1/512.
- `ScoreOf` (`mesh/MaterialScoring.cpp`) is the CPU reference for the density
  part. A test checks it against the exact density.
- The id is written as id / 255 grey. The palette draw uses `kIdPalette`,
  which the shader source declares from `kClusterPalette`.

A cluster source with `pick` draws membership instead of ids: 1 on the
texels of one cluster, 0 elsewhere. `PickCluster` (`mesh/MaterialClusters.cpp`)
chooses it on the CPU from the analysis, so it never names a cluster that
does not exist: clusters are ranked by texel count with empty ones dropped,
each weighs its `share` raised to `areaBias` (0 uniform, 1 by area, up to 8
favouring the largest), and the pick walks the cumulative weights. A pick
outside 0..1 is clamped, a non-finite pick takes the largest cluster, and a
non-finite bias counts as 1. Without a `pick` the lowering ignores
`areaBias`, so validation warns when `areaBias` is set to anything but its
default of 1 and `pick` is absent. The draw passes the picked id in
`groupPicked` with `groupDraw` set to `kGroupDrawPicked`, so a changed pick
redraws one pass and never repeats the grouping. The enchantment signal fields `idHash` and
`effectHash` give a stable 0..1 value for this: `UnitHash` (FNV-1a over the
form key's text, top 24 bits) of the enchantment, and of the magic effect
the instance was matched on. Form keys name the plugin and local id, so the
values survive a load order change.

Sample levels (`planners/MipLevel.h`): every read goes through one
trilinear sampler with an explicit `SampleLevel` LOD, so the hardware never
picks a mip from screen derivatives. Each texture read whose source is larger
than the target, or tiled, has to name the mip itself; a read at mip 0 of a
larger source skips texels, and the target's own mip chain then averages that
aliasing. `SampleMip` sets a read's mip to log2 of the source's larger tiled
side over the target side, clamped to the source's last mip, and never below
the recipe `mip`, so an author can still blur. Mesh-space reads count as
untiled. A `nearest` read keeps the recipe `mip`, because it snaps the uv to
mip-0 texel centres and its texels are ids or positions that an average
would corrupt. The stack base, the cluster draw's RMAOS and diffuse, and the
one-by-one fallback's previous texture use `FootprintMip` with no floor.
Only textures with a chain gain anything: game DDS maps and stack results.
Intermediates have one level, so their mip is always 0. Mean readbacks draw
the source into a 64-texel target at `MipThatFits`, the deepest mip whose
side is still at least 64, and copy that target's 1x1 last mip; a target
whose last mip is not one texel is refused with a log line, because D3D11
does not allow a copy of a larger level into the 1x1 staging texture.

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
- A capture saves every shader-resource slot of five stages and the compute
  stage, about 768 views, and the restore writes and releases them again. Done
  around every pass, the restore path was on the stack in 45 % of the plugin's
  sampled main-thread time (VramProfiler, 2026-10-01). `TextureLab::PassBatch`
  captures once for all passes of one geometry (`Manager::RenderGeometry`) and
  holds the re-entrant renderer lock for that span. A pass inside a batch
  skips its own capture and, when it ends, unbinds its render target and the
  `kStackSrvs` (29) pixel-shader input slots, so an early return cannot leave
  a target bound as an output that the next pass reads as an input; D3D would
  bind that input as null. Holding the lock for a whole tick instead would
  stop the render thread for the tick's full length; one geometry keeps each
  window to tens of passes. Nested batches capture only at the outermost.
  The outermost batch captures when the first pass inside it starts
  (`TextureLab::CaptureBatch`), so a geometry whose steps are all current
  costs no capture. Before that change, 12 mannequins in `soak-frost`, which
  ran no pass once settled, spent about 300 of their 921 µs of render CPU per
  frame on captures (runs 20261004T033046 and 20261004T180916).
- Most of the rest was the executor's walk: each demand walked its output's
  whole upstream graph, 72 times a frame, to find nothing changed. Dirty
  marks (`DownstreamStepsOf`, `RenderExecution::MarkDownstream`) limit a walk
  to the steps an input change can reach. With both changes, `soak-frost`
  fell from 993 to 287 µs per frame, `soak-stack` from 6385 to 4271 µs and
  `soak` from 2974 to 2622 µs (run 20261004T205942). The heartbeat's
  `render_inputs`, `render_slots`, `render_shell` and `render_evaluate`
  sections split the render cost; `render_evaluate` includes step execution.
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
- `SlotWriter::Problem` refuses a slot write in one of two ways:

  | Kind | Severity | Cause | Output state |
  |---|---|---|---|
  | problem (`ProblemMessage`) | error | The material is detached, the slot has no field, the property has no emissive storage, or the recipe wrote two slots that exclude each other (fuzz with glint, coat with subsurface). | `OutputFailed`: the application fails. |
  | unsupported (`UnsupportedMessage`) | warning | The material's own shading model cannot show the slot: a hair model refuses fuzz, glint, coat and subsurface; a coat model (`kPbrTwoLayer`) refuses fuzz and glint; a material that already carries subsurface refuses coat, and one that carries a coat refuses subsurface. | `OutputSkipped`: the output draws nothing and the rest of the application continues. |

  `TargetsOf` (`engine/InstallStages.cpp`) reads the severity into
  `SlotTargetState::refusal`, and `ActivateOutputs` turns an unsupported
  target into `OutputSkipped` with the message as its reason. A skipped
  output is not active, has no problem, and does not count toward the
  application's phase. The studio shows the reason as a warning ("skipped
  on this piece"). The fill template's material fuzz is the common case: on
  a coated material such as stalhrim the halo is skipped and the other
  outputs render.
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
- Inflate pivots on each bone's bounding-sphere centre
  (`NiSkinData::BoneData::bound`, bone space, the vertices that bone
  carries in this geometry), falling back to the joint when the sphere is
  empty or non-finite (`RestSkinToBone::inflateCentre`). Until 2026-10-14 it
  pivoted on the joint, so a piece weighted to a bone but far from its
  joint (a horn at the belt on the pelvis) drifted away by
  inflate × its distance from the joint; body meshes, which wrap their
  bones, hid the difference.
- `ShellPose` (Plan G, 2026-09-14): `PosedTransform` (`mesh/ShellPose.h`,
  engine-free, unit-tested in `tests/mesh/shellpose_tests.cpp`) composes all
  six pose fields onto a bone's rest skin-to-bone transform, per bone, in
  this order: inflate about the bone's vertex centre, then scale about `scalePoint`, then spin
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

## Actor core (`actors/`)

The core's constants and rules come from engine behaviour, so their reasons
sit with the adapter under *Engine events, hooks and the manager*: the
pending model paragraph gives `kModelWaitMs`, `kLoadingCheckMs` and the
third-person-only wait; the eviction paragraph gives the 0.8 restore factor
of `EvictionFor` and `InRange`; the claim paragraph gives `Claim` and
`LiveClaims`. `docs/components/actors.md` maps the
directory.

`KindOf` (`SyncLog.h`) names what a sync did. It reads the state before the
sync and the transition. No state after a record is a forget. No state after
no record changes nothing. The other kinds are a first install, an eviction,
a demanded reinstall, a new claim and another change. The tick core's sync
step (`Decide`, `orchestration/Sync.cpp`) calls it. `RunSync` adds every sync
that changed something to `World::syncLog` (`Actors::Recorded`). The trace
event `sync_deferred` carries the kind as its `kind` field.

## Engine events, hooks and the manager

A **sync** runs one stale actor through the actor core (`src/actors/`, plan
`docs/plans/actor-core-2026-10-03.md`). The work has two halves.

| Half | Where | What it does |
|---|---|---|
| Observe and plan | `Manager::StaleActorOf` (`engine/ManagerFacts.cpp`), during the gather | Observes the actor (`engine/ActorObservation`) against its model table in `Manager::models_`, then plans it (`Actors::PlanActor`) with its live claims. It gives a `StaleActor` fact. |
| Decide | `SyncStale` (`orchestration/Sync.cpp`), in the core's `RunSync` | Advances the actor's state (`Actors::Advance`) and issues the commands. |

`StaleActorOf` traces `actor_decide` with `observe_us`, `plan_us`, `pieces`
and `planned`. `TrackedActor::syncState` (`World.h`) holds the actor's
`ActorState`. An actor without a record in `World::actors` has no state.

| Decision | Commands, in order |
|---|---|
| No state | `Forget`, then `SettleApplications`. The record is erased. |
| A state and no actor-core commands | `KeepModels`, then `SettleApplications`. The state is recorded. |
| Actor-core commands | `KeepModels`; `Retire` when anything was installed or the record has a render; `Install` when the new state installs anything; then `SettleApplications`. |

- The recorded installed plan is the old one with every command applied
  (`AfterCommands`).
- The `Retire` executor (`Manager::RetireActorResources`) removes every
  installed effect and keeps the record and the models. `installed` therefore
  always records what is on the actor.
- When `LiveActorFor` fails, the `Install` executor reports `InstallFailed`.
  `RecordSyncResults` then clears the actor's state, and `RunSync` forgets the
  actor in the same tick.
- The `Forget` executor (`Manager::ForgetActorResources`) stops the actor's
  animation subscriptions and events, abandons its applications, retires its
  resources and erases its models.
- `RunSync` forgets an actor whose sighting has no loaded 3D (`Loaded3D`), an
  actor whose install failed, and an actor the engine asks to forget. A sync
  without a state is the only other forget path.

Observation reads the worn armor per person: one piece per armor, so an armor
listed in several biped slots counts once; a piece the inventory no longer
marks worn is left out, because the unequip sync runs about 2 ms after
`TESEquipEvent` while the biped still lists the item. A model table holds an
`NiPointer` per model, its `ModelToken` and its geometry facts (name and
diffuse path), read once when the model is first seen and before anything is
installed on it, so the facts never come from the plugin's own materials. The
table keeps the models that are observed or installed.

An equip event can arrive before the armor's model is attached to an NPC's
biped. The slot already holds the item while `partClone` is still null, and
no later event announces the attach. Observation records such a piece with no
model token. It is a **pending model** (`PendingModel`, with the time it was
first seen).

- While any pending model of an installed actor is younger than
  `kModelWaitMs` (1 s), `Advance` keeps the actor's effects and issues
  nothing. An armor change therefore rebuilds once, on attach, and not twice.
  An actor with nothing installed never waits.
- Each piece waits on its own. An armor whose model never attaches (no addon
  for the race) stops holding the actor back after its own second. It does
  not disable the wait for later equips.
- Only a third-person piece holds the actor back. The first-person biped can
  list an item that has no first-person model (a helmet), and such a piece
  would delay every equip of the player by the full wait. A first-person
  piece stays pending until it attaches.
- `ModelLoadOf` reports an actor as **loading** while a pending model is
  younger than the wait plus `kLoadingCheckMs` (100 ms). It reports the
  actor as **late** after that. One observation therefore lands after the
  wait ends.
- The core's `ObserveDue` (`orchestration/Sync.cpp`) issues `Observe` for
  each loading actor every `kLoadingCheckMs` and for each late actor every
  `kSlowCheckMs` (1 s). The timers are `World::timers`.
- A sync that leaves the actor waiting sends `SettleApplications` with
  `equip` false, so the equip flag stays. The equip signal fires when the
  late model installs.
- A multi-slot armor lists its item in every slot it covers but attaches one
  model. Observation takes the model from any slot that holds it.
- The in-game `unload` regression case showed the late attach on a spawned NPC
  on 2026-10-01. In the 2026-10-03 fight soak the wait cut full rebuilds from
  33 to 6.

`TextureLab::Init` compiles every built-in shader. It took 1.67 to 1.79 s
on 2026-10-03. `Manager::OnFrame` calls it on the first frame (the main menu)
and traces `texture_lab_start`. Before, the first use started the lab. The
actor core issues no command for a player without enchanted armor, so the
first crowd rebuild paid the start as a frame of 1.6 to 1.8 s.

- Observation reads a geometry's diffuse path through
  `PbrMaterial::DiffusePath`, never through `MaterialInputs::From`.
  `MaterialInputs::From` calls `MeasureFlatDisplacement`, which renders and
  reads back the displacement mean. Observation does not need that blocking
  GPU readback.
- The sync's phases have these trace events:

| Event | Fields | Written by |
|---|---|---|
| `actor_decide` | `observe_us`, `plan_us` | `Manager::StaleActorOf` |
| `actor_install` | live actor, placement, lights, report | `Manager::InstallLiveActor` |
| `actor_place` | surface plan, bindings and output activation; render | `Manager::PlaceInstances` |
| `actor_render` | plan, attach | `BuildActorRender` (`ManagerApply.cpp`) |
| `render_plan_build` | demand collection, lowering; `thread` names the main thread or the plan worker | `EmitPlanBuild` (`ManagerApply.cpp`) |

- A Vigilant sync on 2026-10-05 spent about 1 ms observing and planning,
  10 ms installing seven bindings and 23 ms on a plan miss. The phase events
  split the rest.
- A rebuild's `rebuild_state` lists only its target recipe's fingerprint, or
  every recipe when the rebuild has none. Serializing all 34 loaded recipes on
  every edit cost each drag update for nothing.

Distance eviction is an `Advance` rule.

- The facts carry the distance. The player has none.
- `InRange` derives the hysteresis from what is installed, through
  `EvictionFor`. An actor with something installed stays in range out to
  1.0 × the eviction distance. An actor with nothing installed comes in range
  only inside 0.8 × the eviction distance. Before 2026-10-07 an arriving
  actor got effects inside 1.0 ×.
- An actor out of range has every installed entry retired. Its state keeps
  the plan and the claims. An **evicted** actor therefore has a plan and
  nothing installed.
- The eviction distance is `EffectiveEvictDistance`: the smaller of the
  `EvictDistance` setting and the budget radius (`World::radius`). A budget
  radius applies also when `EvictDistance` is 0 or less. Every actor is in
  range when `EvictDistance` is 0 or less and no budget radius is set, so
  turning eviction off restores an evicted actor.
- Every `kSlowCheckMs` (1 s), the core's `ObserveDue` reads the sighting
  distance (`DistanceToPlayer`) of each actor with a state. `RangeChanged`
  (`orchestration/Sync.h`) holds when `InRange` differs from "anything
  installed". The core then issues `Observe`. `RangeChanged` skips an actor
  with neither a plan nor anything installed.
- `Decide` and `ObserveDue` read the same eviction distance in the same tick,
  so a range check and the sync it causes agree.
- `Advance` returns no state when the plan, the installed plan, the pending
  models and the claims are all empty. The sync then forgets the actor. Its
  next equip event tracks it again.

A rebuild asks for a **reinstall** (`Reinstall::kAlways` in the
`AdvanceRequest`).

- A reinstall changes only the commands of an in-range actor. It retires
  everything installed and installs the whole plan, even when the plan is
  unchanged. A reinstall does not wait for a pending model. The threshold,
  the eviction and the record rules do not change.
- The queue holds the demand (`SessionQueue::MarkReinstall`).
  `Manager::StaleActorOf` reads it (`ReinstallDue`) into
  `StaleActor::reinstall`. The `SettleApplications` executor clears it
  (`ClearReinstall`). The `Forget` executor and a load also clear it. A
  deferred install sends no `SettleApplications`, so the demand stays.
- A plan change alone does not need a reinstall, because `InstanceRef`
  carries the recipe fingerprint. `ReapplyAll` and a view change such as a
  pin leave the plan unchanged and still need a new install.
- Before 2026-10-07 the rebuild retired each actor at once and marked it
  stale. That emptied `installed`, so the sync applied the arrival threshold.
  A recipe edit then removed the effects of every actor between 0.8 × and
  1.0 × `EvictDistance`, and the actors stood without effects until their
  sync. The retire was not needed for memory safety. The rebuild retired
  after publication, each `LiveInstance` keeps its catalog alive
  (`SharedRecipe`), and each `RenderInstance` keeps its graphs alive.

Another system can replace the material or the shell of a geometry that the
plugin bound. The plugin then gives the geometry up and records a **claim**
(`Claim`: the `GeometryRef` and the piece's `ModelToken`).

- Each tick, the gather (`Manager::ClaimsOf`, `engine/ManagerFacts.cpp`)
  checks every bound geometry in `live_`. `TakenByAnotherSystem` reports a
  geometry whose `MaterialBinding` or `ShellBinding` fails `StillOwned()`.
- The gather retires that geometry's binding at once (`RetireGeometry`),
  logs `claimed '…'` and reports a `ClaimedGeometry` fact. The ownership is
  already lost, so the retire decides nothing.
- The core's `RecordSyncResults` adds the claim to the actor's state
  (`WithClaim`). `ObserveDue` issues `Observe` for the actor.
- `PlanActor` takes the claims in `PlanRequest`. Its recipe resolver returns
  nothing for a claimed geometry, so the geometry keeps its position in
  `ActorMatches` (the order of `LivePiecesOf`) and gets no placement.
  `Reconcile` then retires the geometry as any other removal.
- `LiveClaims` keeps a claim only while its piece has the claimed
  `ModelToken`. A claim therefore ends when the piece gets a new model (a
  re-equip or a 3D reload). A load clears every record and so every claim.
- Before 2026-10-07 the tick forgot an actor whose geometries were all lost
  (`!Alive`), and its next equip installed the effects again on the taken
  geometries. That reinstall loop was found by reading the code and was
  never reproduced in game.

`RenderPlanCache` (`planners/RenderPlanCache.h`) reuses an actor's render plan
for every actor whose stack requests give the same `RenderPlanKey`
(`planners/RenderPlanKey.h`): per request the graph pointer, output index,
placement, geometry and instance ids, side reference, `MaterialIdentityOf` and
the mesh hash, plus the identities of the graph's external inputs that do not
change (`StaticExternalSources`), plus the slot chains. A plan stores
`GeometryId` and `PlacementId` indices and `const RecipeGraph *` pointers, so
the key keeps positions and graph pointers; keying by `GeometryRef` would
include the item form and stop two items with one mesh from sharing.
The actor's form ID is left out: `PlanKeyOf` (`engine/ManagerApply.cpp`)
builds the key's bindings with `scope.actor` 0, so an effect-shader or event
input, whose identity carries the instance string, keys alike for every actor.
The actor still enters identity strings (`producedKey`) and the content keys
made from them (`contentKey`), and the runtime reads content keys:
`ExecuteCached` keys the compositor's step outputs, which every actor shares,
by them. A reused plan therefore runs with the storing actor's content keys.
That is safe only when every input that can enter a content key is in the
render plan key. Static external inputs are exactly those inputs, so the key
takes their identities from the same `ValueBindings` the content keys use and
a new input kind needs no edit here. Before 2026-10-06 the key was a
hand-kept list; it missed the item hash input (`item:<hash>`), so an actor in a
different item with the same mesh reused another item's static outputs.
A plan has no key (`uncacheable` in the trace) when a mesh has no hash yet
(the identity would be a pointer nothing holds) or when a static input's
identity fails. Such a plan builds in a job and is handed to its actor
(`uncacheable_job`). A node position's identity is its name and its bind position rounded to
0.01 units (`node:<length>:<name>@x,y,z`, `NodeIdentity`; `@missing` when no
skin carries the node). `IdentityFactsOf` reads each node the graph names with
`NodeBindPosition`, the same call the instance reads its input with, so the
identity and the value agree. Actors whose skeletons give one bind position
share the plan and every output that depends on it; another bind position
gives another plan key and other content keys. A bake's runtime cache key
still carries the exact position bits (`CacheKeyFor`). History: before
2026-10-15 a graph that read a node position made its plan uncacheable (its
identity held geometry and root pointers), so the fill template lowered on
the main thread on every equip; from `8ceffb3` to the bind-position key,
node-position steps had no content key, which cost about 10 MB per mannequin
in run 20261015T222111-1 (922 MB against 802 MB). `SameStructure` compares only
whether a step has a content key, because legitimate keys differ between
actors. An entry holds the `RecipeGraph`s its key names (`GraphsNamedBy`), so
a graph cannot be freed or its address reused while the plan lives. No rebuild
and no load clears the cache since 2026-10-12: an edited recipe's new graph
gives its plans new keys and the old entries age out; it holds 64 entries and
evicts the least recently used. A load keeps it because every key part
survives a load: a texture identity is its address, generation and file name,
so an address reused for another texture only matches with the same name, which
is the same file; a mesh identity is its content hash; and a node position
keys by its name and rounded bind position. On 2026-10-12 the five loads of trace
`...-1791770642202300.jsonl` gave 16 misses, all right after a load. `VerifyRenderPlanCache`, or a run
file's `verifyRenderPlans`, builds every reused plan again and logs, traces
and replaces one that differs. On 2026-10-03 (runs 20261003T175431 and
20261003T180945) verification found no mismatch in 12 stacked mannequins, six
vanilla cuirass types (six plans) or 108 churn re-equips; a reused plan cost
3.7 to 4.6 ms against 17 to 39 ms to build, and an actor's place phase fell
from 50 to 14 ms.

An actor's first render acquires about 70 targets, and twelve at once
produced a 14 GB transient. Until 2026-10-08 a tick admitted one actor's first
render. Since then the **first draw** spreads every first render over ticks
under a time budget (see "Install pacing and the first draw").

Each tick the gather (`Manager::SightingOf`) tests each NPC against the
renderer's own camera. It does so only for a non-player with `CullOutOfView`
on (`CameraViewOf`).

- The test is `NiCamera::BoundInFrustum` with `Main::WorldRootCamera()`
  (`InCameraView`, `engine/ActorObservation.cpp`). Camera mods move that
  camera, so their view is the one tested. No render-path hook is used,
  because Community Shaders and its forks hook the culling and batching code.
- The bound is centred 64 units above the reference position, not on the
  root's `worldBound`. The centre of `worldBound` moves only on the next
  scene-graph update. A mannequin moved behind the player with `SetPosition`
  still tested in view for one frame and was admitted to its first render
  (run 20261004T230733).
- The bound's radius is the root's bound radius (at least 96) × 1.25 + 64
  units. The tick runs before camera mods finish the frame, and a fast turn
  would otherwise show one stale frame.
- An NPC out of view (`Sighting::shown` false) gets a `TickActor` with
  `drawStacks` false. It skips `RenderPieces` and nothing else. Its signals
  keep ticking, its lights keep updating, and its steps stay dirty, so the
  first demand back in view catches up.
- An NPC out of view is not in the `DrawUnpublished` order, so its first draw
  waits. Its applications settle as `kOutOfView`. `Report` and `PendingFor`
  count `kOutOfView` as pending, so drawing the actor later reports
  `kRendered`. Idle checks count it as settled.
- The player and the inspected actor are always in view (`InViewOf`).
- Skipping the draw frees nothing. A stack output written into a material is
  never released while its actor lives (`Releasable` excludes
  `CompositeStackStep`). Idle intermediate outputs are released only from
  `BeginFrame`, which a skipped actor does not reach. Targets are
  `shared_ptr`-owned. The release of an actor out of view is a separate
  decision of the core (response 1 of the render target budget).
- An NPC behind the camera that shows in a reflection shows its last frame.
- Ring soaks keep every actor in view (run 20261004T215805: 0 %). In
  `soak-fight-orbit` the orbiting camera put 55.9 % of actor-frames out of
  view (run 20261004T222929).

The idle release counts only rendered time (render-performance 7b,
2026-10-07). `RenderInstance::BeginFrame` sets the execution clock to its
**render clock** (`AdvancedRenderClock`), which advances by the gap since the
instance's last frame but at most 100 ms (`kMaxRenderAdvanceMS`). Before, the
clock was wall time: an actor that returned to view after more than 500 ms
released every intermediate output on its first frame, and the same frame
restored them. The idle pool keeps at most 64 MiB, so the released targets
were destroyed and the restored ones created again. In `soak-fight-orbit
--quick 60` the orbit window created 10 059 MiB and restored 4998 steps
before (run 20261007T183514-2) and created 5609 MiB and restored 1360 steps
after (run 20261007T184926-1); the worst frame fell from 89 to 52 ms. The
live bytes were the same (about 6.4 GiB). The rest of the churn came from static
chains under hidden layers; static outputs are no longer released on the
timer (see the planners section).

Crowd actors are placed from an XMarker set at their spot (`PlaceAt` in
`engine/RegressionWorld.cpp`). A mannequin placed at the player and then moved
with `SetPosition` walked back to where it was placed: in run
20261004T230733, 11 of 12 ring mannequins had left their spot within seconds,
so every ring soak before 2026-10-05 ran with the crowd standing on the player.
Their performance numbers measured the same actors and effects, but the ring
layout, and so its out-of-view share, did not exist.

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
- The bus vocabulary (`recipe/BusEvents.h`, 2026-10-15): `equip`,
  `hit.received` and `hit.dealt`, each with a `.position` companion
  (`PositionEventId`), `spell.cast.<school>` for the five schools, and
  `anim.<tag>` (`AnimEventId`). A trigger whose event glob matches none of
  them warns at validation (`IsKnownEventGlob`); a plugin's event needs a
  `plugin` trigger. `spell.cast.<school>` comes from `TESSpellCastEvent`
  (`engine/Events.cpp`): the school is the magic skill of the spell's
  costliest effect (`GetCostliestEffectItem`, `GetMagickSkill`), and the
  scalar payload is `CalculateMagickaCost` for the caster. A spell whose
  costliest effect has no school (a shout, a power with no skill) publishes
  nothing. The event names the caster, so it reaches the caster's
  instances only.

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
  `hasTarget`) and the scalars `movementSpeed` and `resolutionLevel` go
  through `ActorState`, while `position` and `target` go through a
  separate `ActorVector` returning a vec3, because the interface is
  typed per method. `VectorValued` (`Recipe.h`) names the vec3
  selectors once; the signal evaluator and type inference both read
  it. The pattern mirrors `efsh`, whose field also decides
  scalar/vec2/vec3.

- `resolutionLevel` reads the environment's applied level
  (`ActorEnvironment::appliedResolution_`) as `LevelIndex`: 0 full, 1 half,
  2 quarter, 3 eighth. The read is one field load per tick.
- The environment keeps its own copy of the level and does not read the
  `LiveActor`. The `LiveActor` owns the environment, and the environment
  holds no pointer back to it.
- `SetAppliedResolution` (`LiveActor.cpp`) is the only writer. It copies the
  level into every instance's environment. Its one caller is the
  `SetAppliedLevel` executor.
- The core sends `SetAppliedLevel` when it records `Installed` or
  `ResizeSwapped` (`PhaseChanges.cpp`). A budget step down reaches the level
  through the same resize and swap.
- A new environment starts at full (`InstanceFor`). An actor without a render
  therefore reads 0. A new render reads its own level from the tick after its
  `Installed` result.

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

- A view change rebuilds only when it changes what is installed
  (`View::ChangesInstalls`): the isolated recipe or output, the recipe set or
  the pin. Mute, layer isolation and the solo piece apply per tick
  (`WriteSlots` renders every slot through `RenderSlotChain` with the view's
  `LayerFilter`, a render input, and `PieceShown`), so they rebuild nothing.
- A recipe keeps its compiled graph, and its variant graphs
  (`VariantGraphFor`, at most 16), while its content equals the recipe they
  were compiled from (`DropStaleGraph`, `RecipeStore.cpp`). A Save or Revert
  that changes nothing therefore reuses the graph and the cached plans, and an
  install of a recipe with a variant no longer compiles a new graph.
- "Reload INI" reapplies only when a setting read at apply time changed
  (`ReapplyDiffers`).
- A studio gesture (a slider drag) applies its edit and rebuilds in one
  step (`ChangeAndRebuildActors`), at most once per
  `kGestureApplyInterval` (100 ms). A drag update that arrives sooner
  stays in the mailbox untouched in state `kHeld`; newer updates
  replace it, `TickGesture` posts it once the interval has passed
  (`ReleaseHeldGesture`), and ending the gesture posts it at once. The
  mailbox (`Studio::GestureMailbox`) owns every hold fact: the delivery
  state, whether the gesture started, the time of the last apply and the
  last touch. Every time comes from `EditorClockMS`, which the caller
  passes in, so the hold and the 2 s idle commit (`kGestureIdleTimeout`)
  read one clock and native tests drive both. While the last apply
  settles (`Manager::IsSettling`: an actor of the recipe's application
  is still `queued` or `prepared`, so it plans or first-draws), the hold
  lasts up to `kGestureSettleLimit` (5 s) instead of one interval. Each
  apply reinstalls every actor of the recipe and starts a plan job; on
  2026-10-12 a drag queued one 2.7 s job per 100 ms step, and the plan
  for the final value attached 37.5 s late. The limit keeps a drag
  responsive when an actor never settles. `Since` reads the
  difference of two clock values as signed: a clock wrap gives a small
  positive interval, and a time read before the last touch gives 0 instead
  of about 49 days, which would trigger the idle commit. The held value never reaches the
  recipe before its rebuild: a throttle that mutated the recipe every
  frame but deferred the rebuild crashed on 2026-09-22 (null deref in
  the binding's writes), when live instances still pointed into the
  store. Each publication is now an immutable `RecipeCatalog`. A
  `LiveActor` holds the catalog it was planned against, its
  `ActorMatches` index that catalog, and each `LiveInstance` holds its
  recipe through `SharedRecipe`, which keeps the catalog alive. A store
  change therefore cannot reach a live binding or move a `RecipeId`.
- A rebuild touches only the actors applying the edited recipe (an
  instance of it, or a failed application of it) when the applied set's
  `MatchFacts` (id, keys, priority, merge mode, in order) are unchanged;
  those facts alone decide `Resolve`, so no other actor's matches can
  move. Any other change, or a rebuild without a recipe (reload, view
  change), asks every loaded actor for a reinstall and clears the render
  plan cache. Measured 2026-10-05 before the scoping: one drag edit
  refreshed 25 actors at about 2 ms each while 3 wore the recipe.

- `LogStackDiagnostics` (`engine/ManagerApply.cpp`) logs each distinct
  (recipe, output, geometry, where, message) once per game session while
  the warning-history budget permits,
  marked "(repeats suppressed)", because a recipe that errors on a
  crowd re-applies on every sync and wrote 250 identical lines in
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
  detached and the material journals have retired. The tables that own the
  producers therefore go last.
- `Manager::RetireActorResources` first reports the live actor's captured
  application tokens through `ApplicationService::Retire`. Distance eviction
  can happen between preparation and first rendering. Destroying the tokens
  without a report would leave a permanent prepared result.
- The retire must not report every current pending token. A store mutation or
  a sync has already invalidated the old actor attempt and can have queued
  its replacement. Captured revision and attempt checks keep the retire from
  ending that replacement.
- Partial `RetireGeometry(actor, geometry)` follows the same
  binding-before-producer rule. It resets the geometry's inputs and its
  placement stacks, and keeps diagnostic rows and plan indices. Shared
  instance lights do not depend on the lost surface and remain until their
  instance retires.

- Cache maintenance is driven by `Manager::OnFrame` every five seconds even
  without applied actors. Material records protect active texture pairs, expire
  unused entries after 30 seconds, and retain at most 64 unused pairs per pass.
  These provisional retention values mirror the mesh grace period and preserve
  short rebuild reuse; they are not measured alpha workload limits. Shared target
  caches prune expired keys both on adoption and during maintenance.
- The render-target idle pool retains at most 16 targets and 64 MiB
  (`ResourcePool`). When a returned target does not fit, the pool lets its
  oldest idle targets go, not the returned one, because a newer shape is the
  likelier to be asked for again. Each tick, `TextureLab::ExpireIdleTargets`
  lets go every target idle for 5 s (`kMaxIdleMs`), counted from the first
  sweep that saw it idle. Before 2026-10-09 the pool refused new targets when
  full and kept old ones until a load, so shapes from earlier resizes could
  hold the pool. The pool returns what it lets go, and the caller destroys it
  after the pool lock is released. Targets held by active consumers,
  previews or material journals are outside this allowance.
- A draw that needs a second target for one draw only (the layer swap of
  `RenderLayersOneByOne`, the dilation gutter of `DilateInto`) leases one
  from the pool and returns it when the draw ends. Before 2026-10-09 these
  were scratch targets that the pool kept per size and format until a load. Destruction occurs only after the final target lease
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
- Observation reads each model's geometries once (`GeometriesOf`,
  `ActorObservation.cpp`). A geometry whose material fails
  `TextureSlotsValid` is left out, and the model is read again on the next
  observation. The first PBR material observed sets `MaterialLayout`; a
  failed layout disables the emissive path for the process.
- The install builds pieces only from the observed handles (`LivePiecesOf`,
  `ManagerApply.cpp`). It follows `Actors::PlannedPieces`, which is also the
  order of the plan's matches, so a geometry's index in the matches is its
  index in the live actor. A count mismatch installs nothing and logs an
  error.
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
- `SessionQueue` owns loading, generation, sync coalescing, and equip timers
  under one mutex. Sync reservations and their later callbacks must belong to
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
- `MatchedShaderOf` (`engine/EnchantmentEffects.cpp`) is the shader of the
  matched effect: the effect `EnchantmentValueFor` selects, then
  `ShaderFor(EffectSetting*)`. Under a `magicEffect` or `effectShader` key it
  is the costliest effect that matches the key. Under any other key it is the
  costliest effect. `ActorEnvironment` resolves it once, at construction,
  because the enchantment and the effect key are part of the instance and a
  re-equip builds a new instance. `StackInputsOf` (`engine/ManagerApply.cpp`)
  copies its `MatchedShaderFacts` into `GeometryInputs::shader`, which the
  identities read. A `RenderInstance` holds one `GeometryInputs` per
  geometry, and several instances share a geometry, so
  `RenderInstance::Update` takes the instance's `MatchedShaderFacts` with its
  signals and loads the texture from them. The fill
  texture is the EFSH `ICON` field (`TESEffectShader::fillTexture`). The
  membrane texture is the `NAM8` field
  (`TESEffectShader::membranePaletteTexture`), the palette that the vanilla
  membrane shader reads.
- An `effectShader` image whose effect has no shader, or whose shader has no
  such texture, gets an empty path. `Compositor::LoadImage` returns no texture
  for an empty path, as for a file that does not load, so the render input
  stays unset and a stack that reads it reports `render input is
  unavailable`. An `efsh` signal without a shader reads zero in every field,
  as a `record` that names no form does.
- `ImportEffectShader` still sets `record` on every `efsh` row of a template,
  so an imported recipe reads the imported shader by name.
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

- The sync after the delay sees the new piece's 3D, so it is the apply
  that fires `equip`; the immediate sync on the equip event does not.
  `equip` carries the centre of the first bound geometry as its position.
- SKSE drains its task queue until empty, so a task may never re-post
  itself to wait for a later frame (NOTES 12); due times fire from
  `OnFrame`.
- Animation graph events arrive on the animation thread and reach the bus
  as `anim.<tag>`, with the event's payload string as the trigger's arg.
  Observation belongs to the actor, not to its current recipe bindings.
- A sync's retire and install keep observation. The `SettleApplications`
  executor reconciles it with the final live state
  (`ReconcileAnimationEvents`). The `Forget` executor (an unload, a failed
  install, `RetireAll`) and a load (`Manager::Clear`) end observation and
  forget discovery. The
  maintenance pass cleans abandoned rebuild gaps, including actors that lack
  a graph for a time.
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
- `RetireAll` posts one task. The task collects the applied and observed
  actor ids and adds them to `forgets_`. The next gather sends a forget
  request for each actor with a record, and `RunSync` forgets those actors
  before it decides any stale actor. A stale mark from an earlier edit
  therefore cannot install effects again after the retire, because `RunSync`
  skips a stale actor that it forgot in the same tick.
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
  `CarriedInstances` retains only owned keys and finite nonnegative seconds;
  consuming an entry removes it. The two-second window uses unsigned
  millisecond subtraction across clock wrap. The one-second manager heartbeat
  expires unused entries even when no actors are applied, and save-load
  clearing discards all entries.
- `kMaxCarriedInstances` is 4096: a fixed ceiling for short-lived clock
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
  catalog's order (or none, when no wearer is loaded). Readers find a sample by
  its actor value's name (`SampleNamed`), so the two lists need not stay
  aligned. The built-in trigger events are listed once, in
  `recipe/BusEvents.h` (`kBusEvents`): the engine emitters, the hit connection,
  the event picker and validation all read that table.
- Actor value names resolve through the engine's table once per name; the
  environment holds handles and form ids, never engine pointers across
  ticks, and answers zero for anything it cannot reach. `av` readings:
  current = max - damage; `damage` is the damage modifier negated; `max` is
  permanent plus the temporary modifier.

### Plan builds on a worker thread (`planners/JobQueue.h`, `engine/PlanJobs.*`, `planners/RenderPlanJob.*`, `planners/IdentityFacts.*`)

- On 2026-10-08, with five Vigilants of Stendarr spawning, each plan cache
  miss cost 36 to 100 ms on the main thread inside the install. Of that,
  `lower_us` was 36 to 84 ms and `demand_us` was 2 to 10 ms. A spawn burst
  gave 8 misses in 2 s.
- On a miss, the install executor submits the demand collection and the
  lowering as one job (`BuildPlanFrom`) to one worker thread (`PlanRouteOf`
  gives `kSubmitJob` or `kShareJob`). The executor still places the
  geometries and reports `PlanAwaited`. The actor runs without a render
  instance until the job ends.
- The job input is a value (`PlanBuildInput`). This table lists what the plan
  build reads and how each input crosses to the worker:

| Input | Crosses | How, and why it is safe |
|---|---|---|
| `StackTextureRequest::graph` | Yes | `shared_ptr<const RecipeGraph>` from the `LiveInstance`. A graph has no `mutable` member and no lazy cache, so reads from two threads do not race. |
| `StackTextureRequest::recipe` and `output` | Yes | `shared_ptr<const SurfaceOutput>` that aliases the instance's `shared_ptr<const Recipe>` snapshot, so the job owns the recipe it reads. The recipe id is copied for warnings. |
| Geometry, root, material textures, mesh (`GeometryInputs`) | No | The main thread turns them into `IdentityFacts` (`IdentityFactsOf`): the instance string, the mesh identity, the material identity, the geometry and root addresses as numbers, the item hash and the enchantment fields. The worker formats identities from these copies. `MeshIdentityOf` reads the compositor's mesh cache and `MaterialIdentityOf` reads engine textures, so both stay on the main thread. |
| Mesh data (`MeshEntry`) | No | Lowering reads a mesh only through its identity string. |
| The binding resolver (`RenderBindingResolver`) and `ValueBindings` | Rebuilt | The worker builds its resolver and bindings from the owned requests (`ResolverOf`, `IdentityBindings`). No closure crosses, because a closure over the engine's requests would hold `GeometryInputs` by reference. |
| Slot chains (`SlotChainRequest`) | Copied | Plain values. |
| `LiveGeometry`, `PlacedOutput`, `WarningHistory`, the plan cache | No | Only the main thread touches them. The worker returns warnings as text, and the drain writes them through `LogOnce`. |

- `render/CompositorDemand.cpp` builds every `ValueBindings` from
  `IdentityFacts`, on both threads. One formatting path keeps the plan key, a
  synchronous plan and a worker plan in agreement. The facts are computed when
  the bindings are made, not when an identity is read. `PlanKeyOf` already
  read the mesh and material identities of every request, so the cost moved
  and did not grow.
- `JobQueue` takes a function pointer, not a closure, so a job cannot capture
  the manager or engine state.
- The queue holds at most 16 unfinished jobs (`kPlanJobCapacity`): queued,
  running, or finished and not yet drained. A full queue refuses a job. The
  install executor then builds the plan on the main thread and stores it, as
  before 2026-10-08. A spawn burst gave 8 misses, and equal plan keys share
  one job. Since 2026-10-12 the core sees the free slots
  (`TickFacts::planSlots`, `FreePlanSlots`) and starts no more installs in a
  tick than there are free slots, so the main-thread build happens only
  when the engine and the core disagree. A 532-step plan takes about 2.7 s.
- The core cancels a plan job that no actor awaits (`CancelPlanJob`): when
  it retires or forgets an actor in `Planning` and no other actor awaits
  the same job, after the `Retire` or `Forget` and before any `Install`, so
  a reinstall with the same key cannot share the cancelled job. A job
  reported done in the same tick is not cancelled: `PlanBuilt` keeps the
  phase `Planning` until `Installed` arrives. `JobQueue::Cancel` removes
  only a queued job; a running job runs to the end, keeps its book entry
  and stores its plan in the cache.
- The worker starts at the first submission, so no thread exists before
  `kDataLoaded`. A thread that cannot start makes every later submission
  build on the main thread.
- The worker catches every exception of a job and returns it as an error
  result. An exception that escapes a thread function ends the process.
  When the worker loop itself fails, the worker stops, the queue refuses new
  jobs, and the next drain fails every stranded job, so no actor waits for a
  job that will not run.
- The worker thread is detached, as in `render/GeneratedShaders`, and owns
  the shared queue state through a `shared_ptr`. The queue's destructor sets
  an atomic stop flag and notifies the worker without taking the lock. At
  process exit Windows ends every other thread before it runs the DLL's
  static destructors, and a thread that ended while it held the lock would
  leave the lock held forever; a destructor that locked or joined could then
  hang the game's exit. A worker that misses the stop notice stays blocked on
  state it owns, which frees nothing it reads.
- A game load (`Manager::Clear`) empties the job book and drops the queued
  jobs. A rebuild keeps the book, so a reinstall shares a job that is still
  open for its key; the core cancels the jobs no actor awaits.
- A job result is **stale** when the book no longer holds its job. A stale
  result is not stored, and `render_plan_job` traces it as `dropped`.
- `PlanJobDoneOf` turns a finished job into the tick core's fact. A stale job
  is `PlanJobBuilt`, so its awaiter rebuilds and asks again (decision Q3 of
  `docs/plans/pure-tick-r4-2026-10-10.md`). A failed job is `PlanJobFailed`
  with the worker's problem or the lowering's problem.
- The core accepts a result only for an actor in `Planning` with that job
  (`AwaitedJob`, `RecordRenderResults`). A reinstall gives the actor a new
  phase, and a retire erases its render, so neither can accept an older
  job's plan. The agreement check compares the phase's job with
  `LiveActor::awaitedPlan`.
- A built or stale job gives `BuildAwaitedRender`. The executor reads the
  surface plan back (`SurfacePlanOf`), runs the install program from
  `ActivateOutputs` and runs `BuildActorRender`. A built plan is in the cache
  by then, so the build is a cache hit when the stacks still give the job's
  key. Stacks with another key get a new job, and the actor awaits again. A
  stale job therefore makes its actors ask again and never leaves them
  waiting.
- A failed job gives `FailAwaitedRender`, and the phase becomes
  `Empty`. The executor records the problem on every stack output
  (`FailedOutputs`), as a synchronous failure does.
- The book entry of an open job holds the graphs its key names. The cache
  entry takes them when the plan is stored, so no graph address in a stored
  key can be freed and reused while the plan lives.
- `RunSync` releases the replaced actor's planned bytes when it issues the
  `Install`. The core gives `BuildAwaitedRender` its bound when the plan
  arrives (`PlanBuilt`, `InstallBoundsOf` with no replaced bytes), so the
  headroom is that tick's. `RenderBuildAt` then chooses the level.
- While an actor waits, its stack outputs have no `RenderOutput` and no
  problem. `WriteSlots` then writes no texture and zero strength. A material
  slot writes back its original texture with the feature off
  (`SlotWriter::WriteTexture`), so a material keeps the vanilla look.
- A shell would still be posed with its recipe opacity. `TickActorFor`
  therefore sets `concealSlots` and `hideShells` in `Planning`, as in
  `Drawing`.
- `ObserveApplication` reports the outputs as waiting (`prepared`), not as
  failed.
- `Planning` has no target level (`TargetLevel`), so the screen level
  step skips the actor. It has no instance to resize.
- `VerifyRenderPlanCache`, a run file's `verifyRenderPlans` and a disabled
  plan cache keep the synchronous build (`PlanRouteOf`). An unkeyed request
  builds in a job.
  Verification compares a reused plan with a fresh build in the same executor
  call.
- The first draw advances only while the actor is in view, because the core
  puts only actors in view in the `DrawUnpublished` order.
- An actor out of view reports its applications during a first draw.
  `TickActorFor` sets `finishApplications` in `Planning` and
  `Drawing` only out of view. `FinishApplications` then reports a
  prepared application as `kOutOfView`. From 7h (2026-10-08) until
  2026-10-09 the report waited for the first draw. An actor installed out of
  view then stayed `prepared` until it came into view, and every crowd soak
  and `cull` timed out with 3 of 12 actors neither rendered nor waiting.
- A plan that arrives starts a first draw as an install does (`KeepRender`,
  then `Installed` creates `Drawing`). The attach itself is a plan cache
  hit of about 0.5 ms, so install pacing does not count it.

### Install pacing and the first draw (`planners/InstallPacing.h`, `planners/DrawPacing.h`, `orchestration/Sync.cpp`, `orchestration/Lifecycle.cpp`, `engine/ManagerTick.cpp`, `engine/ActorResize.*`)

Render-performance 7g and 7h, 2026-10-08. In game, five Vigilants of Stendarr
with the budget radius active gave these costs while the player moved through
them:

| Source | Cost |
|---|---|
| Geometry placement in a sync (`actor_place` `geometry_us`) | 12 to 29 ms per actor |
| Installs close together | 2 to 5 within 30 ms, up to 22 syncs per heartbeat |
| First draw on the tick after an install or a plan attach | 21 to 48 ms; five plans together gave one tick of 110 ms that created 112 MiB |

**Install pacing (7g).** The core's sync step paces installs.

- An **install** is an `Install` command: a sync whose decision has commands
  and installs anything (`Installs`, `orchestration/Sync.cpp`). A first
  install, a reinstall after an equip change and a return into the budget
  radius are installs.
- `FrameSyncs` holds the install time spent, the install count and the
  free plan slots. `RunSync` starts each tick from an empty `FrameSyncs`
  with the tick's `planSlots`. `CanInstallThisTick` refuses an install when
  the installs already started fill the free slots; the deferral trace
  names them (`plan_slots`). An install that hits the plan cache needs no
  slot, so the rule is conservative.
- `CanInstallThisTick` lets an install start when no install ran yet or when
  less than 8 ms (`kInstallBudgetUs`) was spent. The core cannot measure
  install time, so it charges each issued install the full `kInstallBudgetUs`
  (`Synced`). At most one install therefore starts per tick.
- An install that may not start is **deferred** (`Defer`). The core issues
  `KeepModels` for the old installed plan, so the old instance stays valid.
  It records no state, retires nothing and sends no `SettleApplications`.
  The reinstall demand therefore stays, and the applications stay `queued`.
- A deferred actor enters `World::waitingInstalls` (`Deferred`). The entry
  keeps the time of its first deferral. The trace event `sync_deferred`
  (`queue`) gives the actor, the sync kind, the tick's install time and
  count, the wait and the list length.
- Retires, evictions and forgets never wait and take no budget. They free
  targets, and the budget and the radius depend on that memory coming back
  at once.
- `RunSync` refreshes each waiting entry's role, view and screen fraction
  from the sightings (`RefreshedWaiting`). The player counts as in view.
- `RunSync` decides the stale actors in this order (`InstallOrder` and `OrderedBy`): the
  waiting actors first, in `NextInstall` order, then the others.
- `NextInstall` orders the waiting entries: an entry that waited 1 s or more
  (`kInstallWaitLimitMS`) first, the oldest first; then the player; then
  actors in view, the largest screen fraction first; then the others, the
  oldest first. The wait limit keeps an actor from starving.
- Each tick, `ObserveDue` issues `Observe` for the next waiting install, so
  that actor arrives stale in the next tick.
- A sync that runs and does not defer removes the actor's entry, for example
  when the actor left the radius. A forget and a load remove it too.
- A deferred install is not charged. The core charges an install's bytes
  when its `Installed` result arrives (`RecordSyncResults`, `Charged`).
- A deferred sync does not change the actor's state. A deferred first
  install therefore has no state, and the budget radius and
  `FarthestTrackedDistance` do not see it.
- The engine times each `Install` command in `Execute` and reports the tick's
  install time (`CountSyncFrame`). `RunInstall` reports how long the actor
  waited (`CountInstallWait`, from the waiting list before the tick).
  `EmitCoreRecords` counts each `sync_deferred` record (`CountSyncDeferral`).

**First draw (7h).** A render is drawn before it is shown. The core holds the
draw's progress in the phase `Drawing`; the engine holds the slot list.

- Every render that an install or an awaited plan builds starts a **first
  draw**. The engine lists every slot of every stack plan
  (`UnpublishedSlotsOf`) in `LiveActor::firstDraw` (`StartFirstDraw`,
  `HiddenSlot`). The `Installed` result carries the count as `total`, and the
  core creates `Drawing` with an `UnpublishedDraw`.
- During the first draw, `TickActorFor` sets `concealSlots`, `hideShells` and
  `hideLights`. `RenderGeometry` writes the slots as hidden, so the material
  keeps the vanilla look. The shells and lights are hidden.
- The last command of the lifecycle step is `DrawUnpublished`. Its order
  holds each actor in view with loaded 3D whose phase has an unpublished draw
  (a first draw or a running resize), except an actor that swaps in the same
  tick. `UnpublishedDrawOrder` puts the player first, then the actors by
  screen fraction, the largest first.
- The executor (`Manager::RunDrawUnpublished`) shares one `DrawPace` across
  the order. A running resize draws through `DrawResize`; a first draw draws
  through `DrawFirstSlots`. It reports `UnpublishedDrawn` per actor: the drawn
  count and whether the actor drew in this tick.
- The executor draws stacks until it spent the budget: the setting
  `UnpublishedDrawBudgetMs` (default 4, 1 to 16), sent as
  `DrawUnpublished::budgetUs` (`MayDrawStack`). The first stack of a tick
  always draws.
- The budget is time, not a stack count, because one stack costs from a
  cached composite to a full chain of masks and bakes. The 7c-2 swap drew 16
  stacks in 2 frames with 31 ms of plugin tick, about 2 ms per stack. The
  default budget therefore draws about 2 stacks per tick, and an actor of 16
  stacks attaches in about 8 ticks. A single stack that costs more than the
  budget still draws in one tick.
- The time is CPU submit time (`Metrics::Stopwatch`). The GPU cost of a draw
  is not measured synchronously.
- A slot is drawn when `RenderSlotChain` returns no pending stack
  (`SlotChain::pending`). The unpublished draw renders the slot with the real
  filter and base, so the publish tick draws again only the animated steps.
- The core ends a first draw when every slot is drawn (`FirstDrawDone`). It
  also ends it after 60 ticks in which the actor drew
  (`kFirstDrawAttachAfterTicks`), so a stack that stays pending cannot hold
  the vanilla look. The value is the 60 frames of the 7c-2 swap.
- A tick in which the actor drew nothing does not count, because a crowd can
  use the whole budget. An actor out of view does not draw and keeps waiting.
  A first draw has no old instance to free, so waiting out of view holds no
  extra targets. A publish out of view would draw every stack in one tick
  when the actor comes into view.
- The end writes the trace event `first_draw`: the actor, the end (`drawn` or
  `tick_limit`), the frames since the draw began, the drawing ticks, the slot
  count and the drawn count. `EndFirstDraw` gives `Shown` at the draw's level,
  or `Queued` when a level was wanted during the draw
  (`Drawing::next`).
- The **publish** happens in the same tick. The `TickActor` after the end has
  no `concealSlots`, so its executor writes the stacks and drops
  `LiveActor::firstDraw`.
- A reinstall during the first draw retires the render and installs a new
  one, which starts its own first draw. A retire drops it.
- A resize does not start during a first draw. A level wanted by the screen
  waits in `Drawing::next`. The budget does not step the actor down,
  because `MayStepDown` holds only in `Shown`.
- The instance is attached to the geometries during the first draw. The
  engine's `InstancesOf` and `PlannedTargets` count its targets once the
  actor is in view.
- The heartbeat gives `sync_frame_max_us`, `sync_deferred`, `sync_waiting`,
  `install_wait_max_ms`, `hidden_stacks`, `hidden_stacks_tick_max`,
  `hidden_draw_us` and `hidden_draw_tick_max_us`. The `hidden_` names stay,
  because the regression tools read them.

**Restage (2026-10-12).** A forced reinstall keeps the published render and
builds the new one beside it, as a resize does.

- The core restages (`CanRestage`, `orchestration/Sync.cpp`) when the stale
  actor arrives with `Reinstall::kAlways` (a studio edit, Save, Revert, a view
  change or a setting reapply), its phase is `Shown` or `Restaging`, and the
  installed geometries, model tokens included, equal the new ones. An equip
  or a 3D rebuild changes the geometries and still retires and installs.
- The phase `Restaging` holds the published `Shown`, its planned bytes
  (`shownBytes`) and a step: `RestageRequested`, `RestagePlanning{job}` or
  `RestageDrawing{level, draw, chargedBytes}`. `TickActorFor` publishes it like
  `Shown`, but does not finish the applications while the actor is in view,
  so a studio gesture holds until the swap.
- The staged render is charged when its `Installed` arrives and is admitted
  without credit for the published render. The swap (`RestageSwapped`)
  releases `shownBytes`; an abandon (`RestageNotSwapped`) releases the staged
  `chargedBytes`. A restage while restaging keeps the published render,
  releases the staged bytes and cancels the staged plan job before it issues
  the new `Restage`, so the new restage cannot share the cancelled job.
- The swap follows the resize rule (`SwapDue`): drawn, out of view, or 60
  frames after the staged first draw began.
- The engine (`Manager::Restaged`, `ManagerApply.cpp`) builds a staged
  `LiveActor` in `LiveActor::restage` from copies of the published pieces and
  geometries (`StagedActorOf`, `engine/ActorRestage.cpp`), with no bindings.
  The copies keep the published `GeometryInputs`, which hold the material
  textures read before the first binding, so a staged render never reads the
  published render's written targets as its originals.
- The staged instances start from the published instances' signal memory and
  clock (`RememberInstances`, then `CarryInstanceState`).
- Two bindings on one geometry are unsound: with `uniqueMaterial` the second
  copies the first's written material, and the first then skips its restore.
  The staged render therefore uses the published bindings' surfaces
  (`BoundSurfacesOf(published)`), and the swap hands the material and shell
  bindings over (`HandOverBindings`) after it writes every slot back to its
  original (`ConcealStackSlots`), so no material points at a freed target.
  The `TickActor` later in the same tick publishes the new stacks.
- The engine reinstalls instead (`ReinstallFor`: retire, then install, both
  results in one tick) when the staged binding plan differs from the
  published one (`KeptBindingsOf`: material, shell and shell settings per
  geometry), when the matched geometry count differs, or when the staged
  render is not built. The log line is `reinstalled instead of restaged`.
  The core releases `shownBytes` on the `Retired` of a restaging actor.
- The swap places the lights of the new instances; the published instances'
  lights go with them. The trace event is `restage` with `result` staged,
  swapped, not_swapped, abandoned or reinstalled.

### The text log (`engine/LogOnce.h`, `ManagerApply.cpp`, `ManagerTick.cpp`)

- No line reaches the text log per frame or per tick in steady play. A fault
  that repeats every frame or every reinstall goes through `LogOnce`. The
  first occurrence of a line is written with `(repeats suppressed)`. Later
  occurrences of the same text are not written until the next load.
- `LogOnce` keys on the whole line. A line that names the recipe, the
  geometry or the actor is therefore reported once for each of them.
- Faults are never gated by `VerboseLogging`. The stack diagnostics, the
  texture-demand errors and the render-input errors were gated or unbounded
  before 2026-10-08.
- `VerboseLogging` defaults to off since 2026-10-08. A 7-minute session with
  verbose on wrote 46 195 lines; 45 122 were a per-frame mesh-cache line and
  about 870 were the apply block of one actor that resized 45 times. Verbose
  also compares every mesh read with a GPU readback, which costs a stall per
  read. Default users keep every fault and one summary line per apply.
- `DiagnosticLogging` stays on by default. It gates only the JSONL trace,
  which is bounded and rotated, and bug reports depend on it.
- A resize is not a sync and writes no apply summary. The core's trace event
  `resolution_level` records the wanted change, and the engine's
  `resolution_swap` records the swap. With `VerboseLogging` the swap writes
  one line with the new level (`TraceSwap`).
- `warning` marks a fault only. The render-target holding line is `info`.
  The burst line is `info` and needs `VerboseLogging`, because a step to full
  resolution on one large actor creates 257 to 1 253 MiB, which is above the
  256 MiB burst threshold during normal play.
- The GPU comparison of a mesh read (`CompareWithGpu`) ran on every read and
  reported `0 of N bytes differ` every time. It now runs only under
  `VerboseLogging` and logs a warning only when a byte differs.
- The side and facing UV split lines were removed on 2026-10-08. The
  debug recipe `inspect-bake-position` shows the same fact as colour.

## Meshes, bakes and analysis (`mesh/Mesh.h`, `mesh/TextureSize.h`, `mesh/Islands.h`, `mesh/MaterialClusters.h`, `mesh/MaterialRegions.h`, `mesh/GaussianMixture.h`, `mesh/MeshFacts.h`)

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
  `MaterialClusters.h` holds the material sample and the summary the studio
  and the pick read (`MaterialSample`, `MaterialAnalysis`, `PickCluster`,
  `DescribeTexel`). `MaterialRegions.h` holds the material grouping, which
  reads the mesh only through `ChartRasterOf`.
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
  origin in skin space. A node's bind-pose position (`NodeBindPosition`)
  comes from the first of: the geometry's own skin data, when the node is
  one of its bones; the skin data of any other skinned geometry under the
  actor's root that has the node as a bone (`ActorSkinBindPosition`; a body
  or cuirass mesh carries the spine bones that gloves and boots lack); the
  node's current position relative to the actor's root. The first two read
  the same bind-pose skin space. `RenderInstance::Update` reads a node
  position once per instance and keeps it, like a stable resource, so a
  distance bake runs once per equip. Before 2026-10-15 a node outside the
  piece's skin was read from the current pose on every frame, so every idle
  animation re-baked the fill template's `fromCore` distance: in run
  20261015T212625-1 (12 mannequins) `BakeMesh` took 79% of the plugin's GPU
  time.
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
  share a key); `part` and `chart` carry island id / 255 in x with
  `kNoIsland` vertices at 1 and need the analysis, not the mesh alone.
- `{"mesh": {"noise": {cell, octaves, seed, style, stretch}}}` (`NoiseBake`,
  2026-10-15) is three noise fields (r, g, b) over the bind-pose position.
  Styles (`NoiseStyle`): `blobs` (fractal value noise), `ridges`
  (`|2·blobs − 1|`, zero along veins) and `cracks` (cellular noise, F2 − F1
  over the 27 neighbouring cells, zero along cell borders). `stretch`
  scales the cell per bind-pose axis (x across, y forward, z up). `cell`,
  `style` and `stretch` are parameters: a signal makes them render-plan
  operands of the bake step (`NoiseBakeInputs`), resolved and clamped at
  execution (`NoiseSettingsOf`), so the bake's identity is per instance
  through its operands; literals stay shared per mesh. `octaves` and `seed`
  are literals in the step.
  `BuildBake` writes each vertex's raw position divided by `cell` and marks
  the buffers `noise`; the bake pass draws them with `BakeNoisePS`, which
  evaluates the noise per pixel from the interpolated position, so the input
  has full float precision (the `position` bake stores 8 bits over 256
  units, about one unit per step) and the pattern is continuous across UV
  seams. The constants (`BakeNoiseParams`, b7) carry octaves and seed.
  `mesh/Noise.h` is the CPU reference: the same lattice hash, smoothstep
  fade, trilinear blend, octave seeds (`+7919` per octave) and field
  offsets and seeds (`+101` per field). Its tests pin range, continuity and
  seeding; the shader is checked by the dxc suite, not compared texel by
  texel. A mask animates the fields by cross-fading channels: weights
  `(1 + cos(φ − k·120°)) / 3` sum to 1 and their squares to 0.5, so
  `0.5 + (blend − 0.5) × √2` keeps the spread of a single field.
- `{"mesh": {"geometry": glob}}` (`GeometryBake`) is the mesh coverage (1 on
  every triangle, all partitions) when the rendered geometry's name matches
  the glob (`IsNamedBy`, the selector clause's `GlobMatch`), else 0
  everywhere. It uses the object form like `partition` and `boneWeight`
  because a source row holds exactly one key besides `note`. The name
  arrives as `GeometryInputs::name` → `IdentityFacts::geometryName` →
  `ValueBindings::geometry`. A non-matching bake lowers to a constant 0 that
  `Field` turns into a sampled field (the expression program reads the bake
  as a texture, because the graph marks it sample-dependent). A matching
  bake reuses the ordinary bake pass, keyed `bake geometry coverage`, so
  every glob that matches one mesh shares one texture. The value identity
  and the plan key's static inputs carry whether each geometry bake matches
  this geometry's name, never the name itself, and only in graphs that hold
  a geometry bake.
- The source keys changed on 2026-10-14: `bake` is `mesh` (`MeshSource`),
  its `componentId` and `chartId` are `part` and `chart`, and
  `materialClusters` is `{"material": {"groups": {…}}}`
  (`MaterialGroupsSource`). `kSourceKeys` (`recipe/Words.h`) maps each source
  kind to its key and, for material groups, the member under that key. A
  retired key fails to load and names its replacement (`kRetiredSourceKeys`).
- The `item` signal kind (`ItemSignal`) holds four facts of the worn item:
  `itemHash`, `chartCount`, `geometryIndex` and `geometryCount`. Before
  2026-10-14 the first two were the `piece` source kind; a recipe that still
  writes `{"piece": …}` fails to load and names the signal. Before
  2026-10-07 they were bake words.
- An item fact lowers to `ExternalInput{ItemInput{field}}`, a uniform the
  render path binds per geometry (`NumberValueOf`, `render/RenderInstance.cpp`).
  The graph marks `ItemInput` `backendRequired`, so a signal that reads an
  item fact, directly or through other signals, is not in the tick order.
  `RecipeGraph::IsPerGeometry` names such a signal. Masks, sources and layer
  parameters read it through the render plan, which builds per geometry.
- Output scalars, shell parameters and light parameters resolve on the CPU
  once per recipe instance (`ResolveOutput`, `PoseShell`, `ResolveLight`), so
  validation refuses a per-geometry signal there. A mask that reads the fact
  gives the same result for any scalar that scales texels. The studio shows
  "per geometry" in place of such a signal's value, because the signal state
  holds no value for it.
- `itemHash` is `UnitHash` of the worn armor form's key (0 when the form is
  gone). It is per geometry, not per recipe instance: one instance covers
  every piece that carries its enchantment, and vanilla reuses enchantment
  forms across items. Its render identity is the hash bits, so copies of one
  item share computations and different items do not. Every copy of an item
  reads the same value; the game keeps no stable identity for one copy of a
  non-unique item.
- `chartCount` reads `MeshAnalysis::charts` from the mesh cache at render
  time (`ItemFactOf`); its identity is the mesh identity. A mask uses it to
  choose among the charts a mesh really has. The fill template no longer
  reads it: its `pick` source chooses among the charts its group covers.
- `geometryIndex` and `geometryCount`: `NumberGeometries`
  (`engine/ManagerApply.cpp`) sorts a piece's geometries by name with a stable
  sort and numbers them from 0. The name order does not change between loads,
  while the scene graph's child order can. The identities are
  `geometryIndex:<n>` and `geometryCount:<n>`.
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
  also collapses on some pieces, not only left/right). The debug
  recipe `inspect-bake-position` shows per geometry in game which axes
  collapse. Two verbose log lines measured the left/right and front/back
  overlap until 2026-10-08. Region masking
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
- `DescribeTexel`'s words read low to high with a value at a cut
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
- The mesh cache trims an entry whose mesh data no caller used within
  `kMeshMaxAgeMS` (30 s) of its read (`MeshCache::Sweep`, 2026-10-09). The
  plan key needs only the hash and partition count (`MeshHashIdentityOf`),
  and the chart count uniform only `MeshAnalysis::charts`, so a geometry
  whose recipes never bake kept a full copy for nothing: 56 bytes per vertex
  and 12 per triangle plus the per-triangle chart and component ids, about
  1.5 to 3 MB per armor piece. Trimming keeps the hash, the facts, the
  islands and the counts. A bake that runs after the trim reads the mesh
  again once and keeps it from then on.

## Compositor (`render/Compositor.cpp`, `CompositorSource.cpp`, `CompositorBake.cpp`)

Actors share static render-step outputs by content. `MarkContentKeyedSteps`
gives a **content key** to a step that is static (`ChangingSteps`), outputs a
texture, is not a stack, and has a produced key. Each part of the key below
keeps two steps from matching when their outputs could differ:

| Key part | What it carries | Why |
| --- | --- | --- |
| Produced key (`RenderStep::producedKey`, set in lowering) | The produced value's `TextureKey`: its canonical identity and the requested side reference, format and mip policy | Two values with one identity can be requested at different sides |
| Step kind and requirements | `StepKindName` and the step's own side reference, format and mip policy | Lowering can give one value different step requirements |
| Resolved sides | The pixels of every side the step names, put before the content key by `SharedKeyOf` (`planners/PlanSizes.h`) at execution (the **shared key**) | A plan holds side references, not pixels, and one plan runs at several resolution levels |
| Geometry identity | The mesh content (`mesh:<hash>:<parts>`) when the mesh cache can read it, and the geometry pointer otherwise. `MeshData::hash` covers the vertex and index bytes and, through `HashPartitionsAndBound`, each partition's slot and bone names and the model bound | The partition, bone-weight and position bakes read them |
| Material identity | Each texture's pointer, generation and name (`TextureIdentity` in `render/TextureRef.h`) | A freed address reused while a cache entry lives cannot match a different texture |
| State and signal identity | `graph:context:actor` from the geometry's `ApplicationScope`. The actor is the form ID that keys the manager's `live_` map | State and signals never match across actors |
| Numeric operands | The exact bits of the step's resolved numeric operands, appended by `CacheKeyFor` at execution (the **cache key**) | A node position that follows the current pose (a node outside the skin) is not a changing input, so without it a bake would stay at the pose of its first frame |

The step output cache is the Compositor's `stepOutputs_`. A step whose
content key or cache key is longer than 4096 bytes (`kMaxCacheKeyBytes`)
renders privately. The cache holds at most 4096 entries; when it is full it
stops accepting new keys. A cache entry whose texture is no longer valid is
dropped when found. A shared target is never written again: a miss always
renders into a fresh target, and a hit does not remember the target in the
step's scratch (`ForgetReuseHint`). `TextureView::texture` holds the same
target as `TextureView::target`, so the cache counts two references of its
own when it decides an entry is held only by itself. The cache drops an entry
500 ms after the last sweep that saw another holder (`Sweep` renews
`lastUsedMS` while anything else holds the target); `Manager::Clear` empties
it on load. Renewing only on `Find` and `Publish` dropped a long-held bake on
the first sweep after its instance retired, so every editor rebuild drew its
bakes again (2026-10-05 trace: 0 cache hits and 24 `BakeMesh` passes per
edit). Only a step that produces a recipe value gets a `producedKey`, so a
step a lowering creates internally is uncached unless the lowering keys it.
A ripple's position bake is keyed by `PositionBakeIdentity(<mesh identity>)`
(`planners/ValueIdentity.h`, called from `KeyPositionBake`), which
length-prefixes both parts like every other identity, because it reads nothing but the mesh; before that,
every rebuild and every actor drew its own (2026-10-05: 28 `BakeMesh` passes
per edit for three Vigilants in the same armor). The compositor's step output cache keeps an entry while something else holds
its target, renewing its time, and evicts it 500 ms after the last holder lets
go (`StepOutputRetention`, `kStepOutputIdleMs` in `planners/RetainedCache.h`;
`StepOutputCache::Sweep` applies it). Plan lowering memoizes each value's identity per
build (`IdentitySite`: graph, output, instance, geometry), since the binding
resolver depends on exactly those, and `InlineLayerFields` recounts consumers
only after an inline changes the plan; together they cut a seven-geometry fill
template plan from 12.8 ms to 9.7 ms natively (g++ -O2, 2026-10-05). Measured on 2026-10-01 with twelve
mannequins in the demo cuirass: 841 shared hits, 206 target acquisitions in
the burst instead of 1776, peak 2.2 GB instead of 8.6 GB, steady 136 targets
and 1.4 GB instead of 308 and 4.2 GB.

A render step's idle output is released 500 ms after its last use
(`kReleaseAfterIdleMS` in `render/RenderInstance.cpp`). An earlier grace of
30 instance ticks stretched to seconds while a heavy burst lowered the frame
rate, so memory kept growing while the game was slow.

A re-executed texture step counts as changed, so its consumers run again,
unless `SameRenderValue` proves the content equal. Two proofs exist:

| Proof | Holds when | Why it is safe |
| --- | --- | --- |
| Draw stamp (`TextureView::drawStamp`) | Both views carry the same nonzero stamp, texture and generation | A step that opts in raises its stamp only when it draws. A target reused in place keeps its pointer and generation, so identity alone cannot prove equal content. |
| No target | Neither view owns a target, and texture, generation, sampling and normalize are equal | A view without a target reads an engine texture through its sampling and nothing draws into it |

`DrawRipple` stamps its draws. With no firings `PSRipple` writes
`(0, 0, 0, 1)` at every texel, whatever the time, positions and parameters,
so a ripple that already holds an empty draw returns it unchanged while the
firings stay empty. Its time input still makes the step execute each tick.
A released target returns to the pool through the `shared_ptr` deleter,
which expires the scratch's weak hint, so a held empty draw is always the
step's own target. Measured on 2026-10-01, every loaded recipe on twelve
mannequins: 13 fps before, 44 fps after.

An image or material sample is **direct** (`SampleFieldStep::direct`, set
by `MarkDirectSamples`) when every read of it samples it: a program input, a
mapped value, a vector component, a layer source, or an inlined field's
input. A direct sample returns the image's view with its transform and draws
nothing. A material sample's view is the material texture with its resolved
channel in mesh space. A layer mask read straight from a step keeps only the channel
(`LayerPassFor`), and a reduction needs a target whose extent matches its
domain, so either read keeps the sample drawn. The consumer then samples the
source image at its own texel centres instead of a quantized RGBA8
intermediate, so its values can differ from the drawn chain by filtering and
by one 8-bit step; the stage 3 equality proof does not cover direct samples.

Field inlining (`InlineFields`) inlines an animated producer into its one
consumer, as stage 3 decided, and a static producer into any consumer that is
itself static. An animated producer whose every live reader is an animated
program of the same shape also inlines into each reader
(`IsReadOnlyByAnimatedPrograms`). Each reader then recomputes it, but each
reader runs every tick anyway, and a pass costs more than the arithmetic: in
run `20261016T145353-1` an `EvaluateProgram` cost 12.7 µs of GPU at 512 and
2.4 µs at 256, about 1.5–2 µs of which is per pass. The fill template's
`holeLevel` and `holeField` are read by `holes` and `holeDepth`; inlining them
leaves one animated program pass (`glowArea`) per wearer instead of three. A
producer that a layer or a non-program step also reads stays one target,
because inlining into the programs would not remove its pass. `AnimatedSteps` decides "static": it is `ChangingSteps` with a
stack's base and visibility treated as stable, because the engine imports them
on every render even when nothing changed. A static field stays a cached target
under an animated stack: inlining it there recomputes it every tick. Measured on
2026-10-01 with Arcane Circuit on six vanilla cuirasses: inlining static fields
into its animated stacks raised its 2048 stacks from 1.98 to 5.12 ms of GPU per
tick and its 1024 stacks from 0.81 to 1.60 ms, and removed 14 targets. A chained
link whose earlier link animates receives a new base every tick, which the plan
cannot see; the merge below removes most such links.

Two or more stacks on one slot of one geometry (a **slot chain**) merge into one
pass (`MergeSlotChains`) when every link lowered to a stack with the same
requirements and slot, and the merged stack has at most `kMaxStackLayers` (8)
layers and its fields pack. The merged stack starts from the first link's base
and draws the links' layers in chain order. One-pass stacks quantize after each
layer and a chained link reads its base at texel centres, so the merged pass
equals the chain by the stage 3 argument. Merging runs before inlining, so a
static link's fields are not inlined into a chain that animates. The links'
own stacks stay in the plan; the engine renders the merged stack
(`RenderMergedChain`) with one filter that offsets each link's hidden layers and
hides every layer of a link that is not shown. The actor the studio inspects
(`Manager::InspectedActor`) renders its chains link by link, so its outputs keep
their own textures and diagnostics. A merged slot shows nothing while any link
is pending or failed; a chain shows the links that rendered.
`BuildRenderPlan` keeps a merge only if it adds no animated pass: when a merged
stack still reads an animated program (its fields did not fit), it builds the
plan without that chain and keeps the version with fewer animated passes
(`AnimatedPasses`). Field inlining counts merged results as live consumers;
when it did not, one merged slot left the shared fields of every stack that read
them outside as separate passes.

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
- The import templates are empty: an imported recipe keys on its effect
  shader and draws nothing until it is edited. Successive templates (hashed
  combinations of regions, relief, shell, finish and waves; a cavity glow
  with a travelling ripple and a flare shell) read as out of place on
  ordinary armor. `tests/fixtures/templates/probe.json` holds the rows the
  importer patches, so the importer tests do not depend on the shipped
  templates. Imported recipes are written once; a template change reaches a
  shader's recipe only after its file in `recipes/imported` is deleted.
- Stack composition alternates between its retained target and a target
  leased from the pool for the draw. Initial parity ensures the final layer lands in the retained target;
  a preceding stack remains separate from both destinations.
- Reductions measure all requested texel centers, including uncovered texels.
  The GPU reduces the float field in `PSReduce` passes. Each pass reduces a 4x4
  block to one texel until one texel remains (`planners/GpuReduction`). The
  passes accumulate sum and mean in float32. The CPU divides the sum by the
  texel count for the mean. A sum of float32 values can lose a small term next
  to a large one. The first pass writes 1 to alpha for a texel with a
  non-finite used component, and later passes keep the maximum. A flagged or
  non-finite result fails instead of using a fallback. A float gutter target prevents
  bake dilation from introducing an extra RGBA8 quantization into the
  measurement path.
- `DrawReduction` measures a field stored as `R32G32B32A32_FLOAT` or, for a
  scalar, `R32_FLOAT` (the format `FormatFor` gives a scalar measurement
  field). Until 2026-10-15 it accepted only the four-channel format, so every
  scalar reduction failed at runtime ("reduction could not be drawn") and its
  stack with it; the plan tests could not see it because they never draw.
  The first pass rebuilds alpha from the used channels (`ReductionSample`),
  so a one-channel source's implicit alpha of 1 is not read as a flag.
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
  step. A stack's span also names its slot (`CompositeStack emissive 512
  rgba8`), so stacks of equal size on different slots are timed apart. The
  engine times each frame's share of spread draws as its own tick. The
  engine-free `diagnostics/GpuTiming` ring holds eight ticks; a tick with no
  free slot goes untimed, a disjoint tick is discarded, and at most 2048 spans
  (`kMaxSpans`) are recorded per tick. `CollectTimings` polls with
  `D3D11_ASYNC_GETDATA_DONOTFLUSH` once per frame and never waits, because a
  wait would add the GPU backlog to the frame being measured.
- `InlineFields` (`planners/FieldInlining`) inlines a producer program into
  its consumer's texture input
  when the producer is program-like (`EvaluateProgram`, `MapField`,
  `ComposeVector`), shares the consumer's size and mips, is stored in an
  8-bit format (R8 or RGBA8, `IsUnorm8`) as the consumer is, has exactly
  one live consumer (counted from the stack outputs; preview demands do not
  count), and animates (depends on a changing graph input or a changing
  readback). The inlined code is followed by `kSplat` for a scalar producer,
  because a scalar field is read as `xxx`, and by `kQuantize`, because the
  stored field had 8 bits per channel. A texel-centre read returns the stored
  texel exactly, so the inlined program computes the same value.
- A scalar field is stored as R8 (`StoredFormat`, `RenderPlanLowering.cpp`,
  2026-10-09). The lowering sets each node's format from its own type and
  the request's precision (`FormatFor`): a scalar gets R8, or R32F under a
  reduction, and any other value gets RGBA8, or RGBA32F under a reduction.
  An operand therefore gets its own format, not its reader's. A scalar
  reduction field was RGBA32F before 2026-10-09: 64 MiB at 2048², now
  16 MiB. `PSReduce` reads only the components of the field's type, and the
  non-finite flag lives in the reduction levels, not the field. Writers need no change, because a
  pixel shader's `float4` result stores only `.r` in an R8 target and every
  scalar writer returns the value in `.r` (`PSProgram`, `PSRipple`,
  `PSClusters`, the slope pass). Readers need no change, because every view
  of a scalar step output reads channel R (`View`), and `Pick` with channel R
  returns `.rrr`; an R8 texture samples as `(r, 0, 0, 1)`.
- `BakePS` writes alpha 1 on covered texels, and `DilatePS` reads alpha to
  find the gutter it fills. An R8 texture samples alpha as 1 everywhere. A
  scalar bake into an R8 target therefore draws its triangles into an RGBA8
  **coverage** target leased for the draw, dilates from it into a leased
  gutter, and dilates from the gutter into the R8 target
  (`TextureLab::BakeMesh`, `DilateInto`, `HoldsCoverage`). An R32F bake uses
  an RGBA32F coverage target, so dilation keeps its precision. Without the
  dilate pipeline it draws the triangles into the R8 target, undilated, as
  an RGBA8 bake did. The ripple's position bake holds a vec3 and stays RGBA8
  (`Rgba8`).
- In the fill template, R8 holds all 8 scalar targets. A plan holds 33.3 MiB
  at a 2048² base, against 57.3 MiB with every target RGBA8 and 69.3 MiB
  before the mip change. It keeps the same 13 targets, because inlining
  accepts either 8-bit format. From 2026-10-09 until the coverage target the
  chart id bake stayed RGBA8 (36.3 MiB per plan).
- A stack of up to 8 layers (`kMaxStackLayers`) without a legacy
  curve draws in one `PSStack` pass. The per-layer `LayerPass` and `PSStack`
  both call `ComposeLayer`, so they compute the same value per layer.
  `PSStack` rounds to RGBA8 between layers (`Unorm8`), because the per-layer
  path stores each layer in an RGBA8 target, and leaves the last layer to the
  hardware store. `PSStack` binds its base at t12, its layer sources at t13 to
  t20 and its masks at t21 to t28, clear of the program pass's t0 to t11.
- A stack layer evaluates its source or mask program inside the stack pass
  (a **layer field**) when the field is program-like, animates, is stored
  in an 8-bit format at the stack's size, and every live consumer of it is a stack
  reading it as a layer source or mask. The field then inlines into every
  such stack or into none, so its own draw stops. A stack whose only path to
  the field is a readback does not count as taking it.
- A stack step stores each layer field once in `fields`; a layer's source
  or mask is a `LayerRead`, either a value reference or a `LayerFieldRef`
  into `fields`. Layers that read the same field share it, so the stack
  runs its program once per pixel. Each field records the value it stands
  for, so inspection still reads the original field step.
- A layer field is the field's program followed by `kSplat` for a scalar
  field and `kQuantize`, so it yields the stored RGB. The stack reads it
  with the `kRgb` channel, which equals the stored `kR` read because the
  value is splatted. A program field is drawn in mesh space and read by the
  stack at the same texel centre, so no placement applies.
- `PackLayerFields` concatenates the fields of a stack's visible layers into
  one program constant block with `PackPrograms`: inputs, texture slots
  and lookups are renumbered by offset, and each field keeps a code segment.
  `VisibleLayerFields` lists the distinct fields the visible layers read, in first
  read order, and `SegmentIndexOf` gives a layer read's segment in the pack
  built from that list; a hidden layer's field is not packed.
  The executor keeps the pack per step while the set of visible fields stays
  the same. `RunProgram(first, count, uv)` runs one segment with an empty
  stack; `PSProgram` runs segment 0 to the code length. A stack's fields
  share the interpreter limits (256 instructions, 16 inputs, 8 textures, 4
  lookups); a field that does not fit stays a separate draw. `PSStack` binds
  the program constants at b1 and the program textures and lookups at t0 to
  t11. `StackLayer::sourceCode` and `maskCode` hold each layer's source and mask
  segment as first and count; a count of 0 samples the bound texture.
- A stack that cannot draw in one pass (more than 8 layers, or a legacy
  curve) draws its layer fields into RGBA8 targets first
  (`RenderFieldsToTargets`) and then runs the per-layer chain
  (`RenderLayersOneByOne`). Drawing a field
  needs the program pipeline; without it, a stack with layer fields fails
  with a diagnostic, and a stack without them draws as before.
- Only stack results generate mips. The lowering sets `MipPolicy::kNone` on
  every value it builds, because every intermediate is read at its own size,
  where sampling uses mip 0. A stack draws its layers into targets without
  mips; its final result carries a chain, which the game samples.
- Mips of a published stack are steps of the plan. `AttachMipSteps`
  (`BuildRenderPlan`'s last pass) adds two `GenerateMipsStep`s after every
  published stack result (a `stackOutputs` entry not covered by a merge, and
  every `mergedChains` result) whose requirements ask for mips:

  | Step | Levels | Minimum interval | Input |
  |---|---|---|---|
  | near | 1 to `kNearMipLevels` (3) | `kNearMipIntervalMS` (50 ms) | the stack result |
  | far | 4 to the last | `kFarMipIntervalMS` (500 ms) | the near step |

  The binding's `mips` field names the far step. `RenderedRoots` and
  `LiveConsumers` count it as a root, so pruning, liveness and the cost walk
  see both steps. `RenderInstance::Render` demands the stack result first and
  then `MipsOf` the result, so the published version still moves with every
  stack redraw and a chained stack never waits on mips.
- The executor paces a step whose kind has a minimum interval
  (`MinRunIntervalMS`; only `GenerateMipsStep` has one). When its inputs
  changed but less than the interval has passed on the render clock since its
  last run, the step keeps its output and stays dirty, so the next pull checks
  it again. After the interval it runs once with the newest input, even if no
  input changed since: a stack that stops animating still gets its last
  content into the lower levels within one interval. A step that has never run
  runs at once.
- `GenerateMipRange` (`TextureLab`) generates a level range with D3D11's
  `GenerateMips` on a view whose most detailed mip is the level above the
  range; the target caches one view per range (`RenderTarget::rangeViews_`).
- A stack that draws into a freshly acquired target generates its whole chain
  in the stack step, because the pool may hand it a target whose lower levels
  hold another texture's content. A target the step already held keeps its
  chain between draws, and the mip steps refresh it.
- The near range covers what the game samples at normal viewing distances:
  the resolution levels already size each target to the piece's share of the
  screen, so the game rarely reads past level 3 of it. This is a design
  estimate, not a measurement.
- A target's mips are one of three states (`TextureLab::TargetMips`):
  `kNone` (one level), `kGeneratedOnDraw` (a chain the lab regenerates after
  each draw) and `kGeneratedByCaller` (a chain that its owner regenerates). A
  stack target is `kGeneratedByCaller`, because the stack draws layer by
  layer into it and generates once at the end. `RenderTargetPool` creates a
  `kNone` target with one level and without
  `D3D11_RESOURCE_MISC_GENERATE_MIPS`, and it reuses an idle target only for
  a request with the same chain. `GenerateMips` does nothing on a target
  without a chain.
- Before 2026-10-09 the pool gave every target a full chain, also when the
  step asked for none, and a third of each intermediate's bytes held levels
  that were never written. The fill template held 69.3 MiB per plan at a
  2048² base; it now holds 57.3 MiB. The studio inspector then generated
  mips on an inspected intermediate for its thumbnail. An intermediate now
  has no chain, so its thumbnail samples level 0. A read above level 0 of an
  intermediate clamps to level 0; before, it read levels that were never
  written. The first timing run measured mip generation at
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
- The material analysis cache (`AnalyseMaterial`, `CachedMaterial`) keys a
  record on the two textures' identities (`MaterialKeyOf`, `TextureIdentity`:
  pointer, generation and name), and the record holds only the CPU sample,
  the analysis and any problem. Before 2026-10-09 the key was the two raw
  pointers, and the record held both textures so that a freed address could
  not be reused by another texture while the record lived; that kept the
  game's maps loaded for up to 30 s after use (`kMaterialMaxAgeMS`). A game
  texture reloaded at the same address with the same name is the same file,
  and a plugin target's generation tells a reused target apart.

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
- An `image` source names exactly one of `path` and `effectShader`
  (`ReadImageTexture` in `RecipeRead.cpp`). Both or neither is one error on
  the row, so the row is inert. The model holds the choice as the
  `ImageTexture` variant, so a recipe in memory cannot hold both. An `efsh`
  signal without `record` is written without the key.
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

- A layer may leave out `source` (2026-10-12). It is then **unset**: an empty
  `Ref` (`IsUnset`), written without a `source` key and read back the same.
  Validation warns ("the layer has no source yet; it draws nothing") instead of
  failing, because an error holds back the whole recipe while one new layer
  waits for its source. The lowering (`BindLayer`) binds an unset layer to a
  black constant at opacity 0, which every blend passes through unchanged; it
  keeps the layer in place because the render plan reads layers by index.
  A new layer in the studio starts unset (`DefaultLayer`); the paint scratch
  layer sets white itself.
- A ripple's `direction` is a studio field (`RippleFields`) and part of the
  source row, so a studio edit to a ripple keeps it; before 2026-10-12 the row
  had no direction and every studio edit of a ripple reset it to zero.

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
- ImGui hashes a popup's id with the window that opens it, and closing a
  popup closes every popup it opened. A signal combo can draw inside the
  "typed-value" popup of a tunable field, so its "Wearer value..." choice
  cannot open the wearer-value popup there: the popup would have a different
  id from the one the field level draws, and the commit that closes
  "typed-value" would close it too. `RequestWearerValue` records the field and
  the frame number instead, and `DrawWearerValue`, drawn at field level after
  the typed-value opener, opens the popup when the field and the frame match.
  The frame number keeps a stale request from matching a field that reuses the
  address on a later frame. The removed input wizard had this defect
  (studio review C6).
- The "When hit" connection writes one trigger on `hit.received` with a 1 s
  lifetime and the curve `1 - x`. The removed wizard wrote a trigger plus the
  expression signal `1 - @trigger`; the response graph refuses an expression
  signal, so the property had no graph (studio review G8). A recipe written
  that way still loads and runs unchanged.
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

### Pinned preview windows (`studio/PreviewWindows.h`, `menu/PreviewWindows.cpp`, `render/TexturePreviews.cpp`)

- The studio has no preview column. A pin button on a source, mask, output,
  layer, board cell or mask draft opens a floating ImGui window for that
  texture. `ImGui::Begin` is legal inside the framework's section callback,
  because ImGui keeps a window stack; the windows draw after the Studio or
  Recipes page body, outside its disabled scopes.
- The limit is 8 windows (`kMaxPreviewWindows`). A 512-pixel RGBA8 preview
  target with its mip chain holds about 1.33 MiB, so 8 large windows hold
  about 11 MiB. A ninth pin is refused, not swapped for the oldest window:
  the pin buttons go disabled and the piece bar (Studio) or the board
  (Recipes) says why.
- Window positions live in `MenuState` for the session only.
  `ImGuiWindowFlags_NoSavedSettings` keeps them out of the framework's ini
  file, and `ImGuiCond_Appearing` puts a window back where it was when the
  page is opened again.
- `ManagerSnapshot` publishes geometry rows only for the watched piece. A
  window whose piece is published without geometry (`PieceNotViewed`) waits
  with a message; it closes only when its piece, recipe, geometry or subject
  is gone from a snapshot.
- Output and layer windows name their subject by index. They follow the
  recipe's renumbering (see "Index renumbering" below): a window moves with
  its output or layer and closes when that output or layer is gone. A paint
  reset closes every indexed window. Source and mask windows are named and
  survive all of these. A rename closes a source or mask window, because
  the snapshot that still carries the old name arrives after the rename
  intent.
- Previews are keyed by side as well as texture, channel and context. A
  preview reads the source at `TextureLab::MipThatFits` for its side (the
  smallest mip whose larger side is still at least the preview side), so a
  2048 map previewed at 256 reads mip 3 instead of aliasing at mip 0. The
  channel mode of the lab shader reads its map at `extra.x`, which carries
  that mip. An intermediate step target acquired with `TargetMips::kNone`
  has one level; `SampleLevel` clamps to it, so such a preview still reads
  LOD 0.
- An image source's preview is a 128-pixel sampled render target that the
  snapshot builder requests (`kSampledPreviewSide`); a 256 or 512 window
  scales that image up.
- A layer window shows the layer's own source texture, not the stack
  composited up to that layer. A partial composite needs a stack draw over
  the first N passes outside the render plan; `RenderOutput::LayerTexture`
  has no such target.

### Index renumbering (`studio/Renumbering.h`, `engine/RecipeEditor.cpp`, `studio/MenuState.cpp`)

- Mutes, solos, the inspector subject, the stack's selected layer, Back and
  Forward visits and pinned preview windows name outputs and layers by
  index. One rule moves them all: `RenumberingOf(before, after)` compares
  the recipe before and after a change and maps each old output and layer
  index to its new index, or to nothing when it is gone.
- The comparison needs no record of the edit, so the same rule covers an
  edit batch, a cascade removal, undo, redo, revert and reload.
- Matching, per list (outputs, then the layers of each matched output):
  1. the longest common run of equal entries;
  2. an unmatched old entry takes the first unmatched equal new entry (a
     move);
  3. between two matched entries, unmatched old and new entries pair in
     order when their counts are equal (a value edit). When the counts
     differ, an old entry pairs only with the one similar new entry that is
     similar to it alone. Outputs are similar when they have the same
     surface and slot; any two layers are similar.
  An old entry left unpaired is gone. Two equal layers align in order. A
  list longer than `kMaxRenumberedEntries` (256) maps to nothing.
- `RenumberingOf` returns nothing when no old index moved, so an appended
  layer or output and every value edit leave all indexed state untouched.
- The engine applies the renumbering to its `View` (mutes and isolation)
  when the recipe changes, and publishes it in `RecipeEditResult` (edit,
  undo, redo) and `FileOperationResult` (revert). A solo whose layer or
  output is gone ends through `SoloLayer`/`SoloOutput` off, so a recipe
  isolation the solo introduced ends with it.
- The menu applies the published renumbering once, when it acknowledges the
  pending indexed edit or the revert. Until then it keeps the old indices;
  `pendingIndexedEdit` blocks further edits, undo, solo and mute meanwhile.
  `CanRenumber` decides which edit batches take that gate: the structural
  edits and every removal with `cascade`.
- A stack layer selection with a subject that names no output (the recipe,
  a signal) cannot be renumbered and is cleared when the recipe's indices
  move.
- `pendingSelection` names its recipe. It is dropped when the selected
  recipe or piece changes and when an edit result for its recipe reports an
  error; `seenEditResult` makes each result count once.
- Ctrl+Z and Ctrl+Y post nothing when the recipe's undo or redo depth is 0.

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
  - `DeleteRecipe{recipeID}` — move the user file to `<file>.deleted`
    (`kDeletedSuffix`, replacing an older one), unpublish,
    `View::ForgetRecipe`. The loader reads only `.json` files, so the kept
    copy never loads; the user renames it back to restore it.
- Create, duplicate, rename and delete are tracked editor changes
  (`TrackEditorChange`): the menu's reduction (the focus on the new recipe,
  the retargeted selection) runs only when the editor accepts the request.
  A refused create or duplicate keeps the selection and sets
  `MenuState::creationRefusal`, which the recipe context row shows until
  the next create.
- Revert is disabled for a recipe that has no file yet
  (`RecipeRow::hasFile`, from `LoadedRecipe::ownsFile`).
- Reload asks first when a loaded recipe has unsaved edits, and names up to
  `kMaxNamedUnsaved` (6) of them (`UnsavedRecipesText`), because it drops
  every unsaved edit and every undo history.
- Guards: ids are file stems (`IsStem`) and unused; the paint recipe refuses
  rename/duplicate/delete; imported/shipped files are unpublished, not deleted.
- `SaveRecipe` / `RevertRecipe` are `RecipeEditor` methods called directly from
  `menu/RecipeActions.cpp`, not intents — persistence, not CRUD.
- A **held signal** is an audition: `LoadedRecipe::holds` maps a signal name
  to a value, and the store publishes `WithHeldSignals` of the document (the
  signal becomes a constant of the held value, with no curve) into the applied
  catalog, its fingerprint and its compiled graph (`AppliedRecipeOf`). The
  document, its dirty state, its undo history and every save stay as they
  are; studio rows are built from the document and mark the held value
  (`SignalRow::held`). A hold applies to every wearer of the recipe, as a
  mask peek does. `RecipeEditor::HoldSignal` and `ReleaseHolds` rebuild the
  recipe's wearers like an edit; Return to live releases every hold, and a
  recipe load drops them with the loaded entries. `GraphFor` caches the first
  recipe it is given, so a document row still asks for the graph of the
  applied copy.
- Ids collide without case (`IsIdTaken`), because NTFS opens `Recipe.json`
  and `recipe.json` as one file. Rename may change only the case of its own
  id; the file move then refuses, because the destination exists.
- A recipe owns its file (`LoadedRecipe::ownsFile`) once it was read from that
  path or written there. A save to a path the recipe does not own claims it
  first (`ClaimDestination`): it refuses a path that another loaded recipe
  holds, compared without case, and sets aside an existing file as
  `<file>.unloaded`, as the importer does. Such a file never loaded, usually
  because it does not parse, and only a person wrote it, so the save keeps it
  instead of writing over it. A failed write returns the set-aside file to its
  path.
- The store keeps every file the loader could not use. A file that does not
  read or parse is kept as an unreadable row (`g_unreadable`) with its
  diagnostics; a recipe with recipe-level errors stays loaded but held back
  from the applied catalog. `UnappliedRecipes()` lists both, and the
  snapshot publishes them (`Snapshot::unappliedRecipes`). The recipe combo
  ends with a "Not applied" section, one line each (`UnappliedSummary`: the
  reason, the first error's `where` and message, and a count of the
  rest); the Recipes page lists every diagnostic and the file. They are not
  selectable, because repair in game is not supported. A save that sets an
  unreadable file aside removes its row.
- The importer skips a shader whose recipe id is already loaded, also when
  that recipe no longer carries the shader as a key (a user override that
  dropped the key). Importing again under the same id loaded two recipes
  with one id.


On a load, `ResetViewForLoad` returns the view to live (no isolation,
piece solo, mute, pin, recipe set, freeze or speed) and releases every held
signal. A piece solo from the old save could name a form the new save does
not have, and then every effect stays hidden. When the menu closes with a
preview still on (`View::IsLive` is false, or a signal is held),
`Manager::NoteMenuClosed` shows one game notification; the preview itself
stays on, so it can be watched in play.

On a load, `CancelFileOperationsForLoad` records the load as an active
gesture's reason (`AbandonGesture`) before `FinishActiveGesture` rolls the
recipe back. Finishing publishes the gesture as cancelled, and publishing
replaces only an active entry, so the reverse order showed the menu a
cancel by the user instead of the load.
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
- The mask editor's scope (`MaskStack::scope`) is all geometries or one
  geometry name. A part, chart or material id names a different area on each
  geometry (`IsGeometrySpecific`), so all geometries hides those offers and a
  geometry scope offers that one geometry's `OffersOf`. A geometry-specific
  term added under a scope keeps the name in `Term::geometry` and builds as
  `(TEXT) * @GUARD`, where GUARD is a `geometry` bake for that name (reused
  when the recipe has one, else a new row named after the geometry with
  non-name characters as `_`). Retuning keeps the guard; typing the
  expression makes the term raw and drops it. A kept mask reloads as one raw
  term whose text still holds the guard; the guard field is not recovered.
  Undo and redo restore terms but keep the current scope.
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

### Viewed-actor catalogs (`studio/ActorCatalogs.h`, `engine/GameObjectService.cpp`, `engine/ManagerSnapshot.cpp`)

- The node, texture and anim-event catalogs depend on the viewed actor.
  `IsViewedActorKind` marks them. `GameObjectCatalogOf` returns an empty
  catalog for them. `PublishCatalogs` builds them on every snapshot publish.
- `NodeNamesOf` walks the actor's third-person 3D on the game thread. The walk
  stops at depth 64 and after 4096 nodes. A vanilla skeleton with worn armor
  has a few hundred nodes, so the limits only stop a malformed or looping
  scene graph.
- The node catalog lists the viewed piece's skin bones first, by highest
  coverage across its geometries, and then every other node alphabetically.
  Only the full piece row carries geometries, so the skin bones are that
  piece's.
- `TexturePathKey` folds case, turns `/` into `\` and drops a leading
  `data\`. It keeps `textures\`, because a texture selector clause matches the
  geometry's diffuse path as the NIF stores it, and `Compositor::LoadImage`
  retries a path with `textures\` in front when the plain path fails.
- The texture catalog keeps only paths that end in `.dds`. `SlotWriter::Slots`
  reports a missing texture as `(none)` or `(unnamed)`, and the filter drops
  those names.
- A field with `CatalogPick::kAppend` holds a comma list. A pick from the
  catalog adds the name to the list (`AppendName`) instead of replacing the
  text; a name already in the list is not added twice.

### Material detail axis (`planners/MipLevel.h`, `shaders/Material.hlsl` `NormalDetailAt`)

- **Detail** is how strongly a material's normal map varies at a fixed scale:
  the mean distance, in tangent-space XY decoded to -1..1, between 16 reads of
  the normal map at the mip that fits 256 px and one read two mips coarser,
  over a footprint of 1/64 of the texture, doubled and clamped to 0..1.
  Leather grain, smooth bone and hammered metal differ here where their
  colour and roughness may not.
- Only XY is read, so BC5 maps (XY only) and three-channel maps give the same
  measure. A raw normal direction would split one material by slope; the
  variation does not.
- The 256-px material sample and the cluster shader call the same
  `NormalDetailAt` with the same levels (`NormalDetailScaleOf`), so a
  centroid's detail and a texel's detail are on one scale at any target size.
  The sample writes detail into the alpha of its diffuse copy
  (`PSDiffuseDetail`), so the readback needs no third staging texture.
- A material without a normal map reads detail 0 everywhere.

### Cluster chart (`studio/ClusterReadout.h`, `menu/ClusterChart.cpp`)

- The chart reads the geometry's grouping from the snapshot
  (`GeometryRow::grouping`). The compositor groups each material once, with
  `ClusterSettings{}`, on the grouping worker. A source or term with other
  settings shows a note that the chart uses the defaults.
- `ClusterMemo` holds one chart and rebuilds it only when the grouping
  pointer changes. A new grouping clears the chosen comparison.
- The eight axes are the eight `AxesOfTexel` axes, in their order.
- A band spans the 10th to the 90th percentile of the group's texels on that
  axis. Each histogram has 16 bins over 0..1. A texel belongs to the group
  `LabelTexels` gives it.
- **Separation** on an axis is the centre difference divided by the sum of
  the two half-band widths plus 0.02. The 0.02 keeps a single-valued band
  from dividing by zero. `CanSeparate` is true when any axis reaches 1.0.
- With no chosen comparison, the chart compares the picked group with its
  nearest group by centre distance over the eight axes.

### Material dump (`mesh/MaterialDump.h`, `engine/MaterialDump.cpp`, `TextureLab::CopyMapPixels`)

- The **Dump material** button sits in the cluster term pane and the
  material groups source inspector. It writes one
  folder under `Data/SKSE/Plugins/<plugin>/dumps/`. The folder name is
  `<armor>_<geometry>-<UTC stamp>`, with `-2` to `-9` added when the name
  exists. `SanitisedName` keeps only ASCII letters, digits, `-` and `_`, so
  a name cannot hold a separator or a dot and cannot leave that folder.
- The folder holds `diffuse.tga`, `rmaos.tga`, `normal.tga`, `mesh.json` and
  `meta.json`. A map or the mesh that cannot be written is listed in
  `meta.json` under `skipped` with its reason.
- A map is copied with the lab's copy mode into a square RGBA8 target of
  side `min(512, max(width, height))`, then raised to the pool's 64 minimum.
  The source is read at `MipThatFits` for that side. The readback waits on
  the GPU; the dump is a rare user action, so the stall is accepted.
- The TGA image is type 2 (uncompressed true colour), 32 bits a pixel,
  descriptor `0x28`: bit 5 puts the origin at the top left, and the low
  four bits say 8 alpha bits. Texels are stored blue, green, red, alpha.
  `kMaxTgaSide` is 4096, the largest render target the pool makes.
- `mesh.json` flattens the partitions in order, as `AnalyseMesh` does, so a
  vertex index matches `componentOf` and `chartOf`. Each triangle's chart
  and part are its first vertex's, because the analysis joins the three
  corners of a triangle into one island. `-1` stands for `kNoIsland`.
  Triangles past their partition's vertex count are dropped.
- The caps of 500,000 triangles and 1,500,000 vertices keep `mesh.json`
  under about 60 MB. A worn armor geometry is far below them.

### Material regions (`mesh/MaterialRegions.h`, `mesh/GaussianMixture.h`)

- `MaterialRegions.h` declares the whole grouping. The code is split by
  stage: `MaterialSegments.cpp` (chart-aware SLIC regions),
  `MaterialGroupFit.cpp` (region descriptors, the mixture fit and the
  merge), `MaterialScoring.cpp` (texel scoring, the guided filter and the
  odds) and `MaterialRegions.cpp` (the raster, the chart raster, the cover
  table and `GroupMaterial`, which runs the stages in order).
  `RasterGrid.h` holds the grid walk and coverage tests the stages share.

The method and its prototype comparison are in
`docs/plans/material-regions-2026-10-13.md`. The steps run in this order:

| Step | Function | What it does |
|---|---|---|
| 1 | `RasterOf` | Checks the side (8..1024) and the array sizes, and clamps every channel to 0..1, with a non-finite value read as 0. |
| 2 | `SegmentRaster` | Chart-aware SLIC: **regions** that never cross a UV chart border. |
| 3 | `GroupRegions` | A Gaussian mixture over region descriptors, then the misclassification merge. |
| 4 | `LabelTexels` | Each covered texel takes the group with the highest density. The GPU path adds the guided filter. |

- **Seeds.** A seed sits at every grid point `step/2 + i * step` that lands
  on a covered texel. `step` is `sqrt(covered / segments)`. A chart that no
  grid point lands on gets one seed at its covered texel nearest the chart's
  centre. Each seed then moves to the texel with the lowest gradient in its
  3x3 neighbourhood within the same chart. The gradient is the Sobel
  magnitude summed over the feature channels, with edge texels repeated.
- **Features.** Luma and chroma are CIE L\*a\*b\* (D65) of the diffuse colour.
  The diffuse colour is first stretched to 0..1 by the lowest and highest
  channel value over the covered texels. Roughness, metallic, occlusion,
  reflectance and detail are scaled by 100, so they share the Lab range.
  Each channel is multiplied by its weight; a channel with weight 0 is left
  out. With every weight at 0, luma and chroma count at weight 1.
- **Assignment.** A texel joins the nearest centre of its own chart within
  `ceil(step)` texels. The distance is colour² / compactness² + space² /
  step². Ten rounds move each centre to the mean of its texels.
- **Clean-up.** Unassigned texels take the label of the nearest labelled
  texel in their chart (breadth-first). Each region keeps only its largest
  4-connected piece, and the rest refill. A region smaller than half of
  `step²` joins the neighbouring region of the same chart that borders it
  most. A covered piece with no labelled texel in its chart becomes a new
  region. Labels are then renumbered from 0.
- **Descriptors.** A region is described by nine values: the mean of the
  eight texel axes (`AxesOfTexel`) and the spread of its luma. Each value is
  standardised over the regions. The prototype also repeated the detail
  mean; the port drops the repeat because it doubled detail's weight by
  accident, and the final groups on the eight dumps stayed the same.
- **Group count.** `ChooseMixture` fits 2..8 components, five restarts each,
  and keeps the restart with the highest likelihood. It picks the count with
  the lowest **ICL** (integrated completed likelihood, Biernacki, Celeux and
  Govaert 2000): BIC plus twice the entropy of the region posteriors. BIC
  alone picked one more group on the Bonemold dump, which split the bone
  plate into brightness stripes. With two restarts, BIC values for the same
  count moved by up to 200 between runs.
- **Mixture fit.** Each restart starts from k-means++ seeds and 20 k-means
  rounds, then runs expectation maximisation for up to 100 rounds. It stops
  when the mean log-likelihood changes by less than 0.001. Each covariance
  gets 0.01 added to its diagonal. A component whose mass falls below 1e-8
  fails that restart. On identical points the fit agrees with
  scikit-learn's `GaussianMixture` to within 0.1% of the log-likelihood.
- **Merge.** Texels are standardised over the covered texels. A pair's
  **misclassification** is the larger of the two directions: the share of
  one group's posterior mass on texels that score higher for the other group
  (Hennig 2010, directly estimated misclassification probability). The most
  confused pair merges while its value exceeds `GroupSettings::merge`. The
  larger group keeps its id. The estimate uses at most 40,000 covered texels,
  taken at an even stride. A group with fewer than 16 texels has no Gaussian
  and takes no part.
- **Defaults.** `SegmentSettings` holds the weights the user tuned on the
  Blackguard dump on 2026-10-13: luma 1, chroma 0.75, roughness 1.7,
  metallic 3, occlusion 1.4, reflectance 0.4, detail 0.1, 650 segments,
  compactness 10. The merge default is 0.3: on the dumps it merges
  Blackguard's and Bonemold's redundant groups (0.34 to 0.55) and keeps
  Chitin's straps (0.27 in the prototype).
- **Known gap.** On the Temple Priest robes dump the port settles on seven
  groups and the prototype on four or five, because the ICL values for four
  to seven groups lie close together.
- **Engine flow.** `SubmitMaterialSampleStep` copies the RMAOS and diffuse
  maps into a 256-texel sample (`kSampleSide`) and keeps the mesh and the
  settings in its scratch. When the readback arrives, `CollectGrouping`
  sends a `GroupingJob` to `MaterialGrouper`. The worker rasterises the
  charts (`ChartRasterOf`) and runs `GroupMaterial`. The readback input
  stays pending while the job runs, so the stack waits and does not fail.
- `ChartRasterOf` gives a texel the chart of the last triangle that covers
  its centre, with the triangle's chart taken from its first vertex. A mesh
  that is missing, or that covers fewer than 64 texels, counts the whole
  sample as one chart. Charts past the largest 255 have no id
  (`kNoIsland`), so their triangles are left out.
- **Odds weight.** With smoothing on, a texel's score is the group's density
  without its share, plus the log of the group's smoothed odds. The odds
  already hold the share, so the share counts once. `GroupScoring::logShare`
  carries the log share; the shader subtracts it from `logScale`. The CPU
  labels (`LabelWithOdds`) use the same score, so the shares the studio and
  `pick` read match the drawn mask.
- **Kept size.** A cached grouping keeps the scoring, the standardisation,
  the smoothed odds (8 bytes a texel) and every fourth texel in each axis
  with its label (`LabelledTexels`, `kLabelledStride` 4) for the chart. It
  drops the raster and the per-texel region groups: about 0.7 MB at 256 px.
- **Tickets.** A consumer holds a `GroupingTicket` (the job) until `Poll`
  returns a result. `Poll` submits the job when its key is neither cached
  nor pending, so a job refused by a full queue runs on a later poll, and a
  result evicted before its consumer polled is computed again. Only a worker
  that cannot start is a failure. `MaterialGrouper` lives in `planners/` so
  native tests reach it.
- `MaterialGrouper` keys a result by the sample's bytes, the mesh hash and
  the settings, so actors that wear the same piece share one grouping. It
  keeps the last 64 results and takes 8 jobs at a time. The key mixes one
  32-bit word of the raw RMAOS and diffuse bytes at a time
  (`GroupingKeyOf`); the worker converts the bytes to a sample
  (`MaterialSampleOf`). Until 2026-10-20 the main thread converted all
  65,536 texels and hashed nine floats a texel; the 2026-10-19 session's
  profiler put that (`SampleOf`, `GroupingKeyOf`) in 13 hitches at 21-34 %
  of the main-thread samples.
- The grouper also maps an **identity key** to a result: the
  `TextureIdentity` of both maps, the mesh hash and the settings
  (`IdentityKeyOf`, `Known`). `SubmitMaterialSampleStep` asks `Known` first
  and, on a hit, submits no copy and reads nothing back; `CollectGrouping`
  hands the known grouping to the input. Only file textures have an identity
  key. A render target's `Generation` changes when the pool leases it, not
  when it is drawn, so a map an earlier recipe produced keeps its identity
  while its content changes; such maps take the content key (see the
  material-regions plan for sampling a changed material). Each cluster draw reuses the odds textures of its
  grouping from a 16-entry cache (`GroupOddsOf`).
- The studio's grouping (`Compositor::AnalyseMaterial`) is keyed by the
  material's maps and the mesh hash (`MaterialKeyOf`), as the game's
  grouping is, so two geometries that share maps but not a mesh get their
  own charts in the studio chart. Before 2026-10-14 the key held the maps
  only, and the second geometry showed the first one's charts.

### Shader source check (`planners/ShaderSource.cpp`, `tests/planners/shadersource_tests.cpp`)

- The game compiles every shader at runtime with `D3DCompile` (Shader Model
  5). The native suite `planners_shadersource` compiles each fixed entry
  point with dxc (`-HV 2018 -T ps_6_0`/`vs_6_0`), which catches syntax and
  type errors before the game. dxc targets Shader Model 6, so a shader can
  pass here and still fail under `D3DCompile` on an SM5-only construct.
  `-HV 2018` keeps the HLSL rules `D3DCompile` uses (a ternary over vectors).
- The same suite reads dxc's layout listing (`-Fc`) for each entry point.
  For every `cbuffer` it compares the member names, HLSL types and offsets,
  the size and the register with the C++ struct and `ConstantSlot`
  (`planners/ShaderConstants.h`). It compares every struct used inside a
  `cbuffer` the same way. dxc lists a nested struct's members at offsets
  from the start of the buffer, so the parser subtracts the struct's own
  offset. The table in the test maps each C++ member type to its HLSL type
  name. A `cbuffer` in the source without a row in the table fails the
  check.
- dxc accepts the effects-framework keywords that `D3DCompile` reserves
  (`pass`, `technique`, `compile`, `sampler_state`, `texture`, `string` and
  others in `kEffectKeywords`). A variable named `pass` made `D3DCompile`
  reject the whole source in game (2026-10-15). The suite fails on any of
  these words, compared case-insensitively, in the joined source and in every
  generated shader.
- The suite checks that every `.hlsl` file under `src/shaders/` is embedded
  and appears in `kShaderSource`, so a file left out of `kShaderParts` fails.
- Program and stack shaders (`GeneratedShaders`) append generated text to
  this source. The suite plans the imported fill and bare templates and the
  three example recipes natively, generates each plan's program and stack
  shaders and compiles them. The stack shape it builds takes every layer as
  visible and every non-field source as a texture, so it covers the code
  paths of those recipes, not every runtime visibility combination.

### Area pick (`mesh/AreaPick.h`, `PickSource`, `DrawClustersStep::area`)

- `pick` lights an area level by level (`levels`, `PickLevel`), each from
  what the level before left. A level names its set with `from`
  (`"geometries"`, `"groups"`, `"charts"`; `"by"` before 2026-10-14):
  a level picks from a set. `SupportedLevels` accepts geometries, then
  groups, then charts, each at most once, with groups required; validation
  refuses any other list ("not supported yet"). The list form leaves room
  for other id levels (part, partition) without a format change. Absent
  `levels` means one material group on every geometry.
- `IsGeometryPicked`: geometry `index` of `count` lights when its
  `ShufflePosition` under `HashUnit(seed, 0.5)` is below the geometry
  level's `count`. The seed is hashed first because `ShufflePosition` keeps
  an integer seed (the studio default 0) in place, which would always light
  geometry 0. With no geometry level, `geometries` is `kEveryGeometryCount`
  and every geometry lights. Geometries past index 250 (the shuffle limit)
  never light.
- The group: `PickCluster` with `HashUnit(seed, 1 + index)` and
  `areaBias`. With a chart level, only groups that cover at least one
  pickable chart (`GroupsWithCharts`) compete, so a group that lives only on
  charts past 254 cannot leave a picked geometry empty.
- The charts: `CandidateCharts`, ordered by their `ShufflePosition` under
  `HashUnit(seed, 4096 + 16 × index + group)` (`InShuffleOrder`). The pick
  takes the first `count`, then keeps adding charts in that order while the
  lit share is below `minShare` (a floor, 0 by default, a parameter so a
  signal can scale it). An unreachable floor lights every candidate. The
  floor exists because `minCover` is relative to a chart: a fully covered
  tiny chart qualifies, and the 2026-10-14 in-game run showed 5 of 31 picks
  lighting under 5% of their geometry. With no chart level the whole group
  lights and the draw has no chart filter. Every count rounds and clamps to
  1..what is available.
- `ChartCoversOf` writes the cover table at grouping time: for each chart
  and group, the group's share of the chart's labelled texels in the 256 px
  raster. Only charts 0..254 (`kMaxPickChart`) enter it, because the chart
  bake stores id / 255 in an 8-bit channel and 255 is `kNoIsland`.
- `CandidateCharts` keeps the charts the group covers by at least
  `minCover`. With none, it keeps the group's largest chart, so a picked
  geometry never gets an empty area. It keeps at most 251 charts, the
  `ShufflePosition` limit, largest cover first.
- Every geometry runs the pick on its own, with the same seed. The
  geometry step agrees across geometries because each one computes the
  same shuffle of `count` indices.
- The draw is the cluster draw (`PSClusters`) with the picked group, a
  nearest-sampled chart bake at t3, and a 256-bit lit table in
  `groupLitCharts`; `groupChartFilter` turns the chart filter on. A geometry that
  is not picked draws with group 255, which matches no texel.
- Each pick logs `area pick: geometry i of n lights material group g on k
  chart(s), x% of the geometry` (or "every chart" without a chart level),
  or that the geometry is not picked. The share is the sum, over lit
  charts, of the group's cover of the chart times the chart's share of the
  labelled raster (`ChartCover::chartShare`), so it measures how much of
  the geometry glows before masks narrow it.
- The line is written only with `VerboseLogging` on, and each distinct line
  once per session (`LogAreaPickOnce`, at most 512 remembered, then the set
  starts again). Every cluster-draw step wrote it before, at every plan
  build, resize and reinstall, so one geometry's line came four times (one
  step per size) and again each time: 2,707 lines in the 2026-10-20 session.
  Each line flushes the log file, and that flush appeared in a hitch stack on
  2026-10-19. Two actors whose picks read the same share one line.
- `seed` is required, with no default. A default from the piece identity
  would hide which value fixes the pick; an author mixes `rand()` and
  `@itemHash` in a signal when the item should change the pick.

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
  `tools/package.py` archives and `tools/demo-package.py` reads.
- `cmake -E tar --format=zip --mtime` pins only the modification time. Its
  zip entries still carry the access and change time of each file and the
  packaging user's UID and GID in extra fields, so two runs a second apart
  differ. `tools/archives.py` writes the entries itself instead.
- CMake presets delegate dependency tracking and linking to Ninja
  and execution to CTest. Each test has separate scratch storage. Normal
  and ASan/UBSan builds use separate directories; schema validation is
  required. `BEEF_UPDATE=1` enables intentional fixture updates.
- `python3 tools/compile-db.py` configures Release and writes the first-party clangd
  view only when changed: `src/` entries from the Windows database and, when
  `build/native` is configured, `tests/` entries from the native database.
  `tools/tidy.py` reads the Windows database and runs uncached: a safe cache
  needs each translation unit's complete include set, and the Ninja
  dependency log is stale for a source edited after the last build. Baselines
  deduplicate diagnostics and match file, check and message with digits
  ignored, including headers; line shifts, changed scores and resolved
  findings do not fail checks. A baseline row without a message allows one
  finding of its file and check. `--changed` follows `#include` lines through
  `src/`, `src/extern/` and the `/FI` precompiled header to find the
  translation units a changed header reaches.
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
- The importer never writes over a file (`CreateText`). An import whose
  file exists but covers no shader (the file did not load, or its key was
  edited away) renames that file to `<id>.json.unloaded` (`SetAside`), logs
  an error that names both paths, and imports again. The loader reads only
  the `.json` extension, so it skips the set-aside copy. When the set-aside
  name is taken, the importer moves nothing and skips the shader. Before
  2026-10-07 the importer replaced the file, and a hand edit was lost
  without a message. A format change (W7, 2026-10-07) still repairs itself:
  each old import is set aside and imported again.

## planners (`planners/ActorMatches.h`, `planners/TargetPool.h`, `planners/StackPlan.h`, `planners/BindingPlan.h`, `planners/ActorPlanning.h`, `planners/TextureIdentity.h`)

- `ResolutionLevel` (`planners/ResolutionLevel.h`) divides every side of an
  NPC's plan by 1, 2, 4 or 8 from its screen fraction (render-performance 7c,
  2026-10-07). An actor of half the screen height or more is full
  (`kFullLevelFraction` 0.5); each halving of the fraction halves the side.
  The threshold of 0.5 dates from slot defaults that drew emissive, fuzz,
  glint, coat and subsurface at a quarter and diffuse and rmaos at a half.
  Since 2026-10-13 every slot defaults to full (`DefaultSlotResolution`), and
  the threshold has not been re-tuned. The player is always full, and
  the setting `ScreenResolution` off makes every actor full.
- The setting `NpcResolution` (default `Half`) is the finest level an NPC
  reaches (`ActorVisibility::finest`, set per sighting by `SightingOf`). The
  actor inspected in the studio is exempt, and the player is always full. In
  the usage session of 2026-10-19 one NPC at full held 955-1348 MiB (Belrand,
  a Vigilant of Stendarr); half holds a quarter of that.
- Each tick, the core's lifecycle step asks `LevelStepOf` for each actor with
  a target level (render-performance 7c-3, 2026-10-08; `StepLevel`,
  `orchestration/Lifecycle.cpp`). `LevelStepOf` reads an `ActorVisibility`: the
  screen fraction, `Sighting::shown` and the role. An actor that gets
  `SwapResize` or `StartResize` in the tick takes no level step.
- An NPC in view steps to a finer level as soon as its screen fraction
  crosses a threshold. It goes straight to the level that the fraction needs
  (`LevelFor`).
- An NPC out of view wants eighth (`LevelFor`), so it never steps up. Its
  level does not fall because it left the view. It keeps the detail it has
  when the camera turns back to it.
- An NPC steps down one level when its screen fraction stays below 0.8 of
  the lower threshold of its level for 2 s (`kStepDownBand`,
  `kStepDownHoldMS`, `SmallFor`). The cutoffs are 0.4 for full, 0.2 for half
  and 0.1 for quarter. The step-down rule reads the fraction in and out of
  view, so a camera turn does not start the hold.
- `Shown::smallOnScreen` (a `Hold`) keeps the start of the hold. `SeeActor`
  updates it each tick, and a fraction above the cutoff clears it. A level
  change clears it, because the next `Shown` starts with a new hold.
  `ScreenStepDownDue` reads it with `TickSettings::screenStepDownHoldMs`
  (`kStepDownHoldMS`). Only `Shown` has the hold.
- The cutoffs are 0.8 of the step-up thresholds, so an NPC near a threshold
  does not change level back and forth. The 2 s hold is continuous: a
  fraction that rises above the cutoff starts the hold again.
- 7c-1 (2026-10-08) let only the budget step an NPC down. In that policy an
  NPC far away kept the finest level it reached until it was evicted, and
  the budget held its bytes. 7c-3 reverses that rule. A level change is now
  an unpublished redraw and a swap of 1 to 2 frames (7c-2), not a reinstall.
  Before 7c-1, a coarser step came below 0.8 of a threshold with a 2 s hold
  between changes, and each change reinstalled the actor. In the session of
  2026-10-08 that policy changed one NPC's level 45 times in about 7 minutes
  while the player walked to and from it.
- `ScreenFraction` returns no value when the camera or the actor's 3D is
  missing, when the camera is orthographic, when the actor is 1 unit or less
  from the camera, and when the result is not finite. An **unmeasured** actor
  wants no level and never starts the hold, so it keeps its level.
- `ScreenFraction` clamps the fraction to 1. A fraction of exactly 1.000
  means the actor's bounding sphere is as tall as the screen or taller,
  which happens when the camera is close to the actor.
- A level change rebuilds only the actor's render instance
  (render-performance 7c-2, 2026-10-08; `engine/ActorResize.h`). The level
  reaches only the sides of the instance. Pieces, recipe instances, shells,
  materials, lights, signals and clocks do not depend on it, so a level
  change keeps them. Before 7c-2 a level change reinstalled the actor. In
  game each such reinstall of an NPC with 2 recipes on 7 shelled geometries
  took about 26 ms of sync and about 36 ms of plugin time on the next frame,
  and it restarted the effect.
- A level step writes the trace event `resolution_level` with the levels,
  the parsed fraction and `reason: screen`. `WantLevel` then changes the
  phase from `Shown` to `Queued`. The target level
  (`TargetLevel`, the resize's `to`) changes at once, so a reinstall during
  the resize builds at the new level.
- One resize runs at a time across all actors (`kResizesInFlight`), because
  a camera zoom changes every actor's fraction at once and a resize holds the
  old and the new targets together. The queued actor with the largest screen
  fraction starts first (`NextResize`, `ScheduleResizes`). An actor in view
  starts before an actor out of view, and an unmeasured actor starts last
  (`LargerOnScreen`). Before 7c-2 the limit was two level changes per tick.
- The core issues `StartResize` with the target level. The executor builds
  the new instance from the plan, the `GeometryInputs` and the graphs of the
  current instance (`RenderInstance::AtSizes`) and reports `ResizeStarted`
  or `ResizeNotStarted`. A copy of the plan equals a cache hit, because the
  plan holds side references and no pixels. It does not compute a plan key,
  because the key needs the stack requests, which the actor does not keep
  after placement. `LiveActor::fullSizes` keeps the full-resolution sizes,
  because with the 64-pixel floor the sizes of a coarser level do not give
  back the full sizes.
- The resize rebinds every stack holder to the new instance but draws only
  the stacks the actor renders. A placement output that a merged slot chain
  covers starts as drawn (`DrawnByMergedChains`), as a first draw does,
  because the merged pass draws in its place. `ResizeStarted::drawn` carries
  that count. Before 2026-10-09 the unpublished draw also drew each covered
  link. A stack's output is never released, so each resize of an actor with
  merged chains kept 25 to 40 extra 1024² targets for good. In the session
  of 2026-10-09 they reached 168 targets (896 MiB) outside the estimate, and
  in an earlier session they used up the 1024 presenter slots. The census
  (`target_holders`, `unplanned`) showed them as `CompositeStack` outputs of
  attached instances.
- The new instance draws before it is attached. It holds its own copy of
  every input, so it needs no attached geometry. `DrawResize` draws each
  stack with no hidden layers and an empty base, inside the tick's
  `DrawUnpublished` budget, which it shares with the first draws
  (`DrawPace`). On the swap tick the real draw sets the filter and the
  preceding stack, and only the composite steps draw again. The unpublished
  draw still creates the new targets and draws every step once, so its cost
  moves to the resize ticks and does not go away.
- The core issues `SwapResize` at the start of the lifecycle step once every
  stack has drawn (`SwapDue`), so the tick that swaps writes the new
  textures. An actor out of view swaps at once. A resize that does not
  finish drawing in 60 frames (`kResizeSwapAfterFrames`) swaps anyway, so a
  readback that never arrives cannot hold two instances. Its stacks that
  have not drawn can show nothing for a few frames, as a reinstall did.
- The swap (`FinishResize`) always writes each stack slot as not shown
  (`ConcealStackSlots`). The slot gets its original texture back, and
  `written` releases the old instance's target. The `TickActor` later in the
  same tick writes the new stacks when the actor is in view. Before
  2026-10-09 the slots of an actor out of view kept the old textures until
  the actor came into view. In `soak-fight` one NPC stepped down from full
  to half out of view, and its slots kept 24 full-level stack targets
  (356 MiB) that the estimate no longer counted (`slot_only` in
  `target_holders`).
- Nothing holds a raw view of the old instance's targets. Each material and
  shell slot keeps the `TextureRef` it wrote (`SlotWriter`, `written`), which
  holds the target through a shared pointer. The target goes back to the
  pool only after the next slot write replaces it. A `RenderOutput` holds the
  instance through a weak pointer. The step output cache and the snapshot
  hold shared references.
- A reinstall or a retire erases the actor's render in the core and replaces
  or removes the `LiveActor`, so it drops a queued or running resize.
- A claim during a resize changes the actor's stack outputs. The swap then
  fails (`the stacks changed while the resize drew`) and reports
  `ResizeNotSwapped`. The core returns the actor to `Shown` at its old level,
  records the failed level and abandons the resize (`AbandonResize`).
- The sync log has no `resized` kind. The trace event `resolution_swap` gives
  the actor, the old and new levels (`from`, `level`), the stack and drawn
  counts, and the planned targets and bytes of the new instance (`targets`,
  `target_bytes`). With `VerboseLogging` the text log writes one line per
  swap, `actor … resized: drawn at <level> resolution`. The trace event
  `resolution_levels`, written with the heartbeat, counts the actors at each
  applied level.
- The first install bounds the level by the actor's screen fraction at that
  moment (`InstallBoundsOf`, `orchestration/Budget.h`), so a new actor is not
  built at full and rebuilt one tick later. The render target budget can
  take a coarser level at that install (`RenderBuildAt`).
- The core keeps two levels per render. The target level (`TargetLevel`) is
  the level the core decided, after the budget. A retire keeps it as
  `TrackedActor::reinstallLevel` for the next install. The applied level
  (`AppliedLevel`) is the level of the attached instance; the engine holds the
  same level as `LiveActor::renderLevel`. A render in `Empty` or
  `Planning` has neither.
- The level divides the side after `StackSizeOf`, which raises a
  stack to its slot's base map. A stack draws over the armor's own map and
  replaces it in the material, so a lower level also lowers the armor's own
  texture detail, not only the effect's. That is invisible only while the
  GPU would sample a mip at or below the level's size.
- `EvictionFor` restores an evicted actor only inside 80% of the eviction
  radius (`kRestoreFraction`). This hysteresis prevents repeated
  retire/reapply near the boundary; a non-positive radius disables distance
  eviction. The radius is `EvictDistance` or the smaller budget radius.
- The **render target budget** limits the bytes of the plugin's render
  targets (render-performance 7d, 2026-10-08; `planners/RenderBudget.h`,
  `planners/BudgetRadius.h`, `orchestration/Budget.*`,
  `render/VideoMemory.h`). The pure decisions are in
  `planners/RenderBudget.h` and `planners/BudgetRadius.h`. The core's budget
  step (`RunBudget`) calls them and issues the commands. The engine samples
  the memory and carries out the commands.
- The engine reads the DXGI local segment once per heartbeat (1 s), in
  `Manager::SampleBudget`. `RunTick` takes the sample when the heartbeat is
  due and gives it to the core as `TickFacts::budgetSample`, so `RunBudget` runs in
  that tick only. `VideoMemoryProbe` gets the device from
  `RE::BSGraphics::Renderer::GetDevice`, queries `IDXGIDevice`, calls
  `GetAdapter`, and queries `IDXGIAdapter3`. It then calls
  `QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL)` and keeps
  `budget` and `currentUsage`. The sibling plugins VramProfiler
  (`FrameMonitor.cpp`) and GrassCullingTuning (`main.cpp`) reach the adapter
  the same way.
- The probe keeps the adapter after the first success. When the device has
  no `IDXGIDevice`, no adapter or no `IDXGIAdapter3`, the probe keeps that
  failure and does not ask again. When the budget is unknown, the plugin
  writes one warning (`render target budget: video memory is unknown`), ends
  any pressure, admits every step up and does nothing else. A DXGI budget of 0
  also counts as unknown.
- The **allowance** is the most target memory the plugin may hold:
  `allowance = plugin + (budget - usage) - margin` (`AllowanceOf`).
  `budget - usage` is the **free video memory** (`FreeVideoMemory`). `plugin`
  is the measured target bytes (`BudgetSample::pluginBytes`). The plugin's own targets are part of the DXGI usage. A
  release therefore lowers `plugin` and `usage` by the same bytes, and the
  allowance stays the same. A usage over the budget gives no free video
  memory. Plugin bytes over the usage count only up to the usage. Every sum
  saturates.
- The **margin** is the larger of 512 MiB and 15 % of the DXGI budget
  (`MarginOf`). The plugin samples once a second and cannot see the game's
  own allocations between two samples, for example the textures of a new
  cell. The margin keeps that room free. The 15 % term scales the room with
  the card's memory. The allowance gates growth only: step ups
  (`AdmitGrowth`), install headroom (`HeadroomOf`) and the radius's ring.
  Until 2026-10-19 the margin was 5 %: in that day's usage session every step
  up fit the allowance, and the process still reached 93 % of the budget
  (12.85 of 13.77 GiB) as the game's own usage rose and Windows cut the
  budget from 15.2 to 13.7 GiB. An external profile counted 0.45 hitches per
  second above 85 % against 0.16 below it, with driver stalls in whichever
  D3D call came next.
- **Pressure** compares the planned bytes with a separate **shedding line**
  (`ShedLineOf`, `BudgetState::shed`): the plugin's bytes plus free video
  memory less the larger of 512 MiB and 5 % of the budget
  (`ShedMarginOf`). When that is negative, the game alone is past the line
  by more than the plugin holds, and the line is the plugin's current
  holdings: shedding every effect could not bring usage back under it, so the
  plugin keeps what it has and only stops growing. A single 15 % line, used
  for both growth and shedding on 2026-10-19, gave an allowance of 0 MiB
  while the game held 12.0-12.8 of a 13.8-14.1 GiB budget and the plugin
  37-169 MiB; pressure started six times in six minutes and the radius
  evicted down to 44 units, each eviction freeing 12-17 MiB.
- The shedding line is never below 256 MiB (`kMinShedLine`); a cap set in the
  INI still applies below it. The line is built from the bytes the plugin
  holds and compared with the planned bytes, which include intermediates not
  yet drawn. On 2026-10-19 the game sat at the 5 % margin and the line came
  out at 8-35 MiB against 17-67 MiB planned; pressure started 28 times and
  evicted a follower 18 times, 13-17 MiB each.
- The setting `RenderTargetBudgetMiB` caps the allowance; 0 sets no cap. The
  run file field `renderTargetBudgetMiB` replaces the setting for one
  regression run. The cap applies only while the DXGI reading is known.
- The Setup page sets the cap with a preset combo and a logarithmic slider
  (`Studio::kBudgetCapControl`). The presets are Automatic (0), 512, 1024,
  2048 and 4096 MiB. The slider goes from 128 to 8192 MiB in steps of 32 MiB.
  The INI still accepts 0 to 65536 MiB. A stored value outside the slider
  range shows as stored and stays until the user moves the control. A change
  takes effect at the next heartbeat, because `SampleBudget` reads the
  settings each heartbeat. A change does not reapply actors.
- **Budget readout.** Each heartbeat, `Manager::EmitHeartbeat` builds a
  `Studio::BudgetView` from the sample, `World::budget` and `World::radius`
  (`BudgetViewOf`). It publishes the view as a
  `shared_ptr<const BudgetView>` under `snapshotLock_`
  (`PublishBudgetView`). The heartbeat runs on the game thread. The menu
  draws on the render thread and copies the pointer under the same lock
  (`LatestBudgetView`). The view is immutable after publication, and it does
  not depend on the watch window that gates the snapshot.
- The view's measured bytes are `BudgetSample::pluginBytes`, the figure the
  allowance uses. `OnFrame` emits the heartbeat after the tick that took the
  sample, so the pressure and the radius are the values after that tick's
  `RunBudget`. A regression window edge (`FlushMetricsHeartbeat`) emits at
  once, before the core sees the sample.
- **Pressure** starts when the planned bytes (`BudgetState::planned`)
  exceed the allowance. It ends only when the planned bytes fall below the
  allowance less a **band** (`NextBudget`, `PressureGoal`). The band is the larger of
  256 MiB and 10 % of the allowance, and at most half of the allowance
  (`BandOf`). Without the band, the bytes that a step down frees admit the
  step up that takes them back, and the levels change back and forth. The
  half-allowance limit lets a small cap end its pressure. Under pressure, the
  responses aim below the band, not below the allowance, so that the episode
  ends.
- **Admission.** A step up from the screen level step waits under pressure
  (`AdmitStepUp`, `orchestration/Lifecycle.cpp`). It also waits when the
  planned bytes plus the bytes of the new instance exceed the allowance
  (`AdmitGrowth`). The new instance counts in full on top of the current
  holding, because a resize holds the old and the new targets until the
  swap.
- The core charges an admitted step up to the planned figure at once
  (`Charged`), so a second step up in the same second sees the first. The
  player is always admitted. A refused level (a failed resize) charges
  nothing, because the step is checked against the failed level first.
- A blocked step up writes the trace event `resolution_blocked`, not a log
  line, because it repeats every tick.
- `Shown::waitingForRoomFrom` keeps the level at which a step up of the actor
  is blocked. A blocked step up writes `resolution_blocked` only when the
  actor is not yet blocked at its current level. The event gives the first
  wanted level of the episode. A wanted level that changes with the screen
  fraction does not start a new episode.
- An admitted step up, a step down and a reinstall each end the episode,
  because each gives a new phase and the next `Shown` starts without it.
  Outside `Shown` a blocked step up is silent.
- **Preemption.** Admission blocks a step up at the allowance, and pressure
  starts only over the allowance. Without preemption a blocked step up waited
  until an eviction or an unload freed bytes. In the session of 2026-10-08
  two NPCs stayed blocked for more than 4 minutes while NPCs farther away
  held full.
- On each memory sample without pressure, `MakeRoomForWaitingStepUp`
  (`orchestration/Budget.cpp`) lists the blocked NPCs in view that still
  want a finer level, largest screen fraction first (`WaitingStepUpsOf`).
  For each, it asks `PreemptionCandidate` for one NPC to step down, and it
  acts on the first answer.
- A candidate holds a level finer than it wants. It is out of view, or its
  fraction is under 0.8 of the requester's. The 0.8 keeps two NPCs of about
  the same size from taking bytes from each other. Candidates rank as in
  response 2 (`BetterStepDown`), so an NPC out of view comes first.
- A preemption starts only when it can make room. The **shortfall** is the
  planned bytes plus the bytes of the requester's new instance, less the
  allowance (`ShortfallOf`). Each candidate's **surplus** is the bytes it
  frees when it steps down to the level it wants
  (`BudgetCosts::surplus`). When the surplus of all candidates together
  is under the shortfall, no candidate is chosen.
- Without that rule, an NPC that could not fit took levels for nothing. In
  the session of 2026-10-08 an NPC with 24 stacks at eighth needed 79.8 MB
  for quarter against an allowance of 67.1 MB. Preemption stepped two
  one-stack NPCs out of view from full to eighth, freeing about 1.3 MB each.
  When they came into view they stepped up by screen size, and preemption
  took them down again: 21 step downs in 64 s, and the requester was never
  admitted. After a requester is admitted, its bytes are in the planned
  figure, so a victim that returns to view is blocked and does not take its
  level back.
- A preemption starts no step down while any actor is in `Queued` or `Running`
  (`BudgetActors::resizeBusy`). It steps down one NPC per memory sample
  (`StepDownForBudget`, `orchestration/Budget.cpp`). The requester stays
  blocked until the swap frees the bytes and admission passes. The log line
  ends `(render target budget, for actor <FormID>)`, and the trace event
  `budget_step_down` has `reason: preempted` and `for_actor`.
- **Install level.** The first install of an NPC takes the finest level, no
  finer than its ideal level, whose bytes with intermediates fit the
  **headroom**
  (`FittingLevel`). A reinstall of an NPC takes the finest level, no finer
  than its target level, whose bytes with intermediates fit the headroom. An equip change
  causes a reinstall. A spawned NPC equips piece by piece just after it
  appears, so its first reinstalls follow its first install within a second.
- An NPC out of view at its first install has an ideal level of eighth
  (`LevelFor`). An unmeasured NPC starts from full within the headroom.
  Before
  7c-3 an NPC behind the camera installed at the level of its distance.
- The headroom is the allowance less the planned bytes, with the
  **replaced holding** subtracted from the planned bytes first (`HeadroomOf`).
  The replaced holding is the bytes that the core's estimate counts for the
  actor just before the reinstall retires it (`PlannedBytesOf(TrackedActor)`).
  An actor that has not rendered replaces 0 bytes. Without the credit, an
  actor that fits is pushed down by its own bytes. Under pressure the
  headroom is the replaced holding, so a reinstall does not grow. A first
  install replaces 0 bytes, so under pressure it takes eighth. An unknown
  allowance gives no headroom, and the install takes its bound level.
- `RunSync` computes the bound before it issues the `Install`
  (`InstallBoundsOf`, `orchestration/Budget.h`): the finest level and the
  headroom. The install executor prices the levels after it has the plan and
  before it sizes the stacks (`RenderBuildAt`, `planners/InstallProgram.h`).
  The level reaches only `SizesFor`, so one plan prices every level
  (`LevelCostsOf`).
- When no level fits, the install takes eighth. The budget does not hold back
  an install. Holding it back for memory needs an actor state with a plan but
  no install, which the actor lifecycle (`actors/Lifecycle.h`) does not have.
  The responses and the exhaustion warning then act on the next memory
  sample. Install pacing (7g) delays an install for frame time only, and the
  deferred sync changes no state.
- `RunSync` takes the replaced holding off the planned bytes (`Released`)
  when it issues the `Install`. `RecordSyncResults` charges the install's
  bytes with intermediates (`Charged`, `Installed::chargedBytes`) when the
  result arrives in the next tick, so a second install in the same second
  sees the first. The next memory sample replaces the planned bytes with the
  engine's measured holding (`BudgetSample::heldBytes`, `PlannedTargets`),
  which counts an actor only after its first render.
- The player gets no headroom (`InstallBoundsOf`), so it takes its level
  without the budget and adds no bytes. An NPC whose first install has no
  screen fraction, or has `ScreenResolution=false`
  (`TickSettings::sizeByScreen`), starts from full and takes the finest level
  that fits the headroom (2026-10-11). Before that it got no bound and took
  full without the budget, also under pressure.
- The trace event `actor_place` gives the built `level` and the bound
  `finest_level`. When the bound has a headroom, it also gives
  `replaced_bytes`, `headroom_bytes` and `admitted_bytes`.
- `kResizesInFlight` stays 1. The budget admits each step-up resize by its
  bytes and starts no step down while any actor is in `Queued` or `Running`
  (`AwaitResize`).
- **Response 1: actors out of view release their intermediates.** A release
  frees every texture and bake output of the instance except the stacks and
  the mesh bakes (`RenderInstance::ReleaseIntermediates`, `KeptWhenReleased`,
  the `Releasable` rule of the idle release). A mesh bake stays because it is
  the costly step to restore. In the session of 2026-10-09 a `BakeMesh` call
  took 0.4 to 5.9 ms of GPU on average and up to 42 ms, against 17 µs for a
  1024 mask program, 3 to 36 µs for a cluster draw and 2 to 17 µs for a
  slope. A restored bake would also read a trimmed mesh again and rebuild its
  vertex buffers.
- A release takes a fill plan at a 2048 base from 33.3 to 26.3 MiB
  (`kept on release` in `planners_templatecost`). The released outputs are
  the static outputs that 7b keeps until the instance goes.
- Without pressure, an actor in `Shown` that stays out of view for
  `TickSettings::outOfViewReleaseDelayMs` (5 s) releases
  (`Held::outOfView`, a `Hold`, in `SeeActor`). The core issues
  `ReleaseIntermediates` with `ReleaseReason::kOutOfView`, and the executor
  traces `hidden_release`. A first draw and a resize never release, because
  only `Shown` holds intermediates that may go (`MayRelease`). The delay keeps
  a camera turn from releasing and restoring.
- Under pressure, `RespondToPressure` releases the largest holdings first and
  stops when the planned bytes fit. The executor traces `budget_release`.
- When the actor is in view again, `SeeActor` marks its intermediates held,
  and its next draw restores the released steps. That costs under a
  millisecond of GPU for a fill plan. A released step restores when a draw
  demands it (`RenderExecution::Materialize`) and keeps its change version, so
  a stack draws again only when an input changed.
- The step output cache keeps an unheld target for 500 ms
  (`kStepOutputIdleMs`), and the idle pool keeps at most 64 MiB, so the
  measured bytes fall within about a second.
- While the intermediates are released, the core's estimate counts only the
  stacks and bakes (`HoldingOf`, `PlannedBytes`). The engine holds the same
  fact as `LiveActor::holding`, and the agreement check compares the two.
  From the tick that the actor is in view, the estimate counts all its held
  targets again.
- **Response 2: one NPC steps down one level.** The candidate is the NPC
  whose level is finest against its `LevelFor` (`LevelGap`). An NPC out of
  view wants eighth, so it comes first. An unmeasured NPC counts as at its
  wanted level. The most bytes freed breaks a tie.
- The player is never a candidate. Only an actor in `Shown` is a candidate
  (`MayStepDown`), so an actor with a first draw or a resize is not. A level
  that failed to resize is not offered again (`StepDownFor`).
- The core prices the current and the coarser level from
  `InstalledRender::levelCosts`, which the `Installed` result carries.
- The step down goes through the 7c-2 resize (`WantLevel`, then
  `StartResize`), at most one per memory sample. Each step down writes one
  `info` line, `actor … steps down from <level> to <level> resolution:
  frees … (render target budget)`, and the trace event `budget_step_down`.
  The core does not know names, so the line says `unnamed` when the sighting
  has no name.
- An NPC stepped down rises again only through the screen level step and
  admission.
- **Response 3: the budget radius.** When responses 1 and 2 have nothing
  left (`Exhausted`) and no actor is in `Queued` or `Running`, the budget sets or lowers
  a **budget radius** (`planners/BudgetRadius.h`, `World::radius`). The
  radius is a distance from the player. The effect of an NPC beyond it goes
  through distance eviction. A design review chose the radius over a new
  actor state, because the radius reuses `Advance`, the 0.8 hysteresis and
  the `evicted` sync kind unchanged.
- The shrink removes one NPC per memory sample (`ShrinkRadius`). The candidate is
  the farthest rendered NPC inside the current radius that is not resizing.
  The player has no distance and is never a candidate. A missing, non-finite
  or non-positive distance never counts. The new radius is 0.99 × the
  candidate's distance (`kRadiusShrinkFactor`), raised to the distance of the
  next nearer actor. `EvictionFor` evicts past 1.0 × the radius, so it evicts
  the candidate and keeps every nearer actor. Actors at the same distance
  leave together. A resizing actor beyond the candidate stops the shrink. An
  installed actor that has not rendered yet is not in the list and leaves
  with the candidate when it stands beyond the new radius.
- The shrink issues `Observe` for the actor (`ShrinkBudgetRadius`), so the
  actor arrives stale in the next tick. Its sync writes the `evicted` sync
  kind. An actor beyond the current radius is already leaving, and the next
  shrink skips it.
- Each shrink writes one `info` line, `render target budget: effects beyond
  N units are off: <actor> (<name>) is evicted, frees ~M MiB`, and the
  trace event `budget_radius` with `change` `shrink`, `radius`, `actor` and
  `freed_bytes`. The bytes are the actor's planned bytes at its level
  (`BudgetCosts::planned`). The candidate must be evictable: it is not
  in `Queued` or `Running` (`MayEvict`, `BudgetActor::resizing`).
- **Growth.** Each memory sample under pressure restarts the radius's dwell
  (`BudgetRadius::sinceMs`). Without pressure, the radius tries to grow by
  1 / 0.8 once per dwell (`GrowRadius`, `DwellAfter`). The 1 / 0.8
  step lets every NPC inside the old radius pass the 0.8 restore rule. The
  dwell is 10 s because a pressure episode from an install ends within 1 to
  2 s, and the returning installs reach the planned figure within one
  heartbeat. A 10 s dwell therefore sees any pressure that a growth step
  causes before the next step.
- The dwell doubles with each shrink of one radius (`BudgetRadius::shrinks`,
  `DwellAfter`): 10 s after the first, 20 s after the second, at most 160 s
  (`kMaxRadiusDwellMs`). The count resets only when the radius clears. In the
  2026-10-19 session the radius evicted and returned the follower Belrand
  repeatedly; each return cost a full install (70-130 ms on the main thread)
  and up to 1.3 GiB of new render targets before pressure evicted him again.
- **The ring.** A growth step prices the NPCs it lets return. Before the
  ring, a run with `RenderTargetBudgetMiB=64` and four Vigilants of Stendarr
  grew the radius, let the evicted NPCs return, started pressure and evicted
  them again five times in 455 s. The **ring**
  is the evicted NPCs (a plan and nothing installed, `EvictedActorsOf`)
  that `Returns` does not return at the current eviction distance and returns
  at the eviction distance after the step (`RingOf`). `Returns` is the rule
  that `Actors::InRange` applies to an actor with nothing installed, so the
  ring is the set of NPCs that the next `Advance` brings back. With the radius
  alone, the ring is the NPCs at 0.8 × the radius or farther and nearer than
  0.8 × the grown radius. An NPC exactly at 0.8 × the radius is in the ring.
  An NPC exactly at 0.8 × the grown radius is not. The ring keeps only finite
  distances. A negative distance returns at every radius and is never in the
  ring.
- An evicted NPC has no render. The core therefore records its **return
  bytes** in `TrackedActor::returnBytes` when it stops tracking a render
  (`DropRender`, `orchestration/Sync.cpp`): in the tick it issues `Retire`,
  or when a `Retired` result arrives.
- The return bytes are the bytes with intermediates at the render's target
  level (`ReturnBytesOf`). A return installs no finer than that level
  (`TrackedActor::reinstallLevel`, read by `InstallBoundsOf`) and holds its
  intermediates, so `RenderBuildAt` charges at most these bytes. When the
  render has no cost at its target level, the return bytes are its planned
  bytes (`PlannedBytesOf`). Zero bytes record nothing, so an actor without a
  level cost that never rendered keeps no return bytes.
- An **unpriced** NPC never installed, for example an NPC that spawned beyond
  the radius. It costs the largest return bytes recorded for any actor
  (`LargestReturnBytes`), and at most one unpriced NPC returns per growth
  step (`kUnpricedPerGrowth`). The price can be too low for one NPC. If that
  NPC starts pressure, the radius evicts it and `DropRender` records its
  real bytes, so the same NPC cannot start a second cycle. The limit of one
  keeps the error to one NPC per step.
- The ring must fit the headroom (`HeadroomOf`) less a margin of half the
  band (`RingRoomOf`, `kRingMarginDivisor`). Pressure ends below the
  allowance less the band, so after pressure ends the headroom is more than
  the band and one NPC that fits half the band can return. The other half
  absorbs a step up and a change of the free video memory. A margin of the
  whole band leaves almost no room right after pressure ends and can keep
  the radius small for a long time. Without an allowance the ring is not
  priced.
- NPCs at one distance return together (`FitRing`). When the whole ring
  fits, the radius grows by 1 / 0.8 and writes `render target budget: effects
  return up to N units` and the trace event `budget_radius` with `change`
  `grow` and `returning_bytes`. When part of the ring fits, the radius grows
  to the largest radius that does not return the first NPC that does not fit
  (`RadiusStoppingAt`). `Returns` compares the distance with 0.8 × the radius
  in `float`, and `RadiusStoppingAt` lowers the radius by at most four
  representable steps until 0.8 × the radius is at or below that distance.
- When the nearest ring NPC does not fit, the radius keeps its distance and
  the dwell restarts (`RadiusBlocked`). The first block of an episode writes
  `render target budget: effects stay off beyond N units: the returning NPCs
  plan ~X, the headroom is ~Y less a margin of ~Z` and the trace event
  `budget_radius` with `change` `blocked`, `reason` `ring`, `radius`,
  `ring_bytes`, `headroom_bytes` and `margin_bytes`
  (`BudgetRadius::blockReported`). The episode ends when the radius grows,
  shrinks or clears. The event says `blocked`, as `resolution_blocked` does,
  because the vocabulary keeps "held" for held targets.
- The radius clears when a growth reaches or passes `EvictDistance`. When
  `EvictDistance` sets no limit, it clears when a growth reaches or passes
  the farthest tracked actor with a state (`FarthestTrackedDistance`), or
  when no such actor has a distance. A clear prices its ring too: the ring
  ends at 0.8 × `EvictDistance`, or has no end when `EvictDistance` sets no
  limit. A clear whose ring does not fit is a growth to the fitting radius,
  and a clear whose nearest NPC does not fit is a block. The clear writes
  `render target budget: radius cleared` and the trace event `budget_radius`
  with `change` `clear` and `returning_bytes`. Returning actors install
  through the usual path, so the install level follows the headroom
  (`FittingLevel`).
- When the radius has no candidate either, for example when only the player
  or a resizing actor holds targets, the plugin writes one `warning` per
  pressure episode (`render target budget: no release or step down is
  left`) with the five largest holdings (`Pressure::kOverWarned`).
- Each pressure start and end writes one `info` line with the planned bytes,
  the allowance, the DXGI budget and usage, the plugin's bytes and the cap,
  and the trace event `budget_pressure`. Each release writes the trace event
  `budget_release`. The heartbeat carries `budget_bytes`, `usage_bytes` and
  `allowance_bytes` while the reading is known, and always `planned_bytes`
  and `pressure`.

- `TargetsOf` (`planners/TargetCost.h`) lists the targets a plan holds
  (`withIntermediates`): every step that draws a texture or a stack and that a rendered
  root reaches (`RenderedRoots`, `StepDependencies`). `stacks` lists the
  stacks among them. The rule follows `RenderInstance::BeginFrame`, which
  releases only the output of an animated step (`AnimatedSteps`) that was not
  used for 500 ms of rendered time (`kReleaseAfterIdleMS`, render clock), and
  never a stack's (`Releasable`). A static step keeps its output until the
  instance goes (render-performance 7b, 2026-10-07). A bake's vertex buffers
  (`BuildBakeBuffersStep`) are released on the same 500 ms timer whether or
  not they animate (`IdleReleasable`, 2026-10-09), because only a bake draw
  reads them; a later bake rebuilds them from the cached mesh when the
  executor restores the step. They cost 20 bytes per vertex and 4 per index,
  about 0.5 to 1 MB of CPU memory per bake. Before, static outputs
  were released on the same timer. A stack layer hidden for 500 ms then
  released a static chain (a mask and its mesh bakes), and showing the layer
  again restored the whole chain with new targets. In `soak-fight-orbit
  --quick 60` the orbit window restored 1360 steps with the timer and 0
  without it, and created 5609 against 3051 MiB. Holding the static outputs
  raised the memory in use by 18 % in `soak-enchanted` (3366 to 3979 MiB,
  runs 20261007T175741-1 and 20261007T193002-2); the fill template holds
  69.3 MiB per plan at a 2048² base instead of 58.7 MiB. Under memory pressure
  the render target budget releases the static outputs of an actor out of
  view (render-performance 7d).
- A plan holds no pixel sizes (render-performance 7c, 2026-10-07). Each
  `TextureRequirements` and each reduction domain holds a **side reference**
  (`SideRef`): an index into the list of distinct stack sides.
  `RenderBuildRequestOf` (`planners/InstallProgram.h`) builds that list
  (`SideFor`, `RenderBuildRequest::full`). Two stacks get one side reference
  only when their sides are equal at full resolution.
- The instance gets the sizes as a `PlanSizes` list (`planners/PlanSizes.h`).
  `SizesFor` refuses a list that does not cover every side the plan names.
- Planning compares sides only for equality: demand merging
  (`TextureKey::operator<=>`), field inlining (`InlineField`) and chain merging
  (`MergeSlotChains`). No lowering decision reads a side's pixels. One plan
  therefore serves every resolution level, and the render plan key holds the
  side reference, not the pixels. When the 64-pixel floor
  (`TextureSize::kMin`) makes two sides equal, the plan keeps the steps that a
  fresh plan at that size would merge. That costs targets but draws the same
  result.
- The step output cache and `CostOf` use the shared key (`SharedKeyOf`), not
  the content key, because the content key holds side references. Without
  the pixels, an instance at a lower level would read a full-size output that
  another instance published.
- A layer that reads a field program runs that program inside the stack draw.
  The mask step that the field names is then not reached, and only a studio
  layer preview (`RenderOutput::LayerTexture`) runs it. The fill template has
  six such masks: 19 drawing steps, 13 of them reached.
- An actor out of view is not rendered, so its instances get no `BeginFrame`
  and the idle release frees nothing. The actor keeps every held target until
  the core releases its intermediates (response 1 of the render target
  budget).
- `CostOf` counts one target per content key across any list of held
  targets, because `Compositor::FindStepOutput` returns one published target
  for equal keys. `TargetBytes` adds a third for mips only for a target with
  `MipPolicy::kGenerate`, because `RenderTargetPool` creates a full chain only
  for those (`HeldTarget::mips`).
- The count leaves out the sample and reduction steps, because they hold no
  per-plan target. The 64² sample target returns to the pool when its step
  ends. A sample step keeps two 64² RGBA8 staging textures (32 KiB). A
  reduction step keeps three 1×1 float staging textures (48 bytes).
  `TextureLab::ReductionLevelFor` keeps the reduction levels once per extent
  for the whole lab.
- With the trace on, each heartbeat also writes `target_holders`
  (`EmitTargetHolders`, `engine/TargetCensus.cpp`, 2026-10-09). It takes
  every live target from the lease registry (`LiveTextureTargets`) and
  counts it under the first holder that reaches it (`TallyHolders`): an
  attached or resizing instance's step outputs (`instance`), a live actor's
  stack outputs (`output`), a slot writer's written or original texture or
  a published texture of a live binding (`slot`), a live geometry's or an
  instance's material inputs and imported textures (`material_input`), the
  retired-texture list (`retired`), the step output cache (`cache`),
  previews (`preview`), the idle pool (`pool`), the neutral height
  (`resident`), or none of these (`unreached`). It gives `<holder>_targets`
  and `<holder>_bytes` for each, `live_targets`, the live `RenderInstance`
  count (`live_instances`) and the instances that live actors reach
  (`attached_instances`). `resolution_swap` carries `live_targets` and
  `live_instances` at the swap. `unplanned` lists, by step kind, format and size, the
  step outputs that hold a target outside the steps `TargetsOf` prices
  (`unplanned`), and priced steps whose targets differ from the price
  (`mispriced`, with the bytes over the price). `slot_only` lists, per live actor,
  the targets that only its material and shell slots still hold, with the
  actor's applied level, whether it is in view, the bytes and the sides. In the session of 2026-10-09 live targets
  ran 120 to 136 above the estimate after resolution swaps, until the
  1024 presenter slots ran out; the census names the holder.
- The heartbeat reports the engine's estimate over every live instance of a
  rendered actor (`estimated_targets`, `engine_estimated_bytes`,
  `PlannedTargets`). `estimated_bytes` is the core's estimate (see "The
  engine runs the core"). The measured `target_bytes` minus `pool_bytes` is
  the figure to compare with the engine's estimate. The runs below are from
  before the tick core, when `estimated_bytes` carried the engine's
  estimate. In `soak-enchanted` run 20261007T193002-2 (12 mannequins) the
  steady estimate was 3978 MiB and the measured holdings 3979 MiB; in the
  orbit window of `soak-fight-orbit` run 20261007T193002-1 they were 6919
  and 6920 MiB. Before 2026-10-07 the estimate (`TargetCostOf`) left out
  the stacks and gave 80.0 MiB for the fill template, which holds 69.3 MiB.
- The plain log carries two render-target lines whether or not the trace is
  on (`Manager::ReportRenderMemory`, render-performance 7a). The holding line
  is an `info` line. It is written when the live target bytes rise to a new
  1 GiB step or fall half a step below the step (`NextHoldingStep`,
  `HoldingStep`). It gives the measured
  total and the plans' total (`PlannedTargets`). It names the five actors
  with the most own bytes, each with its shared bytes, its screen fraction
  and its recipes. A shared target has a content key that another rendered
  actor also holds. The own bytes of all actors plus each shared target once
  equal the plans' total. The measured total also counts the pool. The burst line is an `info` line and needs
  `VerboseLogging`. It is written when one heartbeat interval creates
  256 MiB or more; it gives the interval and the syncs of the last 2 s by
  kind.
- Every bound geometry of an actor holds the same `RenderInstance`.
  `InstancesOf` (`ActorBudget.cpp`) lists each instance of an actor once.
  Before 2026-10-08 it listed the instance once per geometry, so an actor's
  bytes were its plan's bytes times its geometry count. The line
  `render targets: 1474 MiB in 250 targets; Vigilant of Stendarr (FF00226C)
  3973 MiB` came from that count. The heartbeat is also flushed at regression window edges, so
  an interval can be shorter than a second. The thresholds of the holding and
  burst lines are fixed. The render target budget (render-performance 7d)
  reads the DXGI budget separately.
- `ScreenFraction` (`ActorObservation.cpp`) is the actor's bound diameter over its
  distance from the camera, divided by the frustum's height at unit distance
  (`NiCamera::GetRuntimeData2().viewFrustum`, `fTop - fBottom`), clamped to
  0..1. The heartbeat reports the least, median and most over the rendered
  actors in view (`screen_fraction_min`, `_median`, `_max`) and the bytes
  created and destroyed in the interval (`targets_created`,
  `target_bytes_created`, `target_bytes_destroyed`).

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
  shell passes as `std::span<const Recipe>`; the pure `ActorMatches` never stores
  a `const Recipe*`, because a long-lived table cannot own a pointer's validity.
  `Merge`'s `PlacedRecipe` holds `const Recipe*` only as a transient function
  argument, never in a table — `GeometryPlacement`/`ActorLightPlan` follow that
  same transient-only rule.

- **Three-table `ActorMatches`** (DECIDED 2026-09-09; `design-actor-state-tables`).
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
  A geometry that another system took is not a flag in these tables; the
  actor core records it as a claim and the planner leaves it unplaced.
  Light-liveness stays a shell check because a light is a `RE::` binding.

- **Row projections belong to `studio/`, not here.** The frozen `BuildSnapshot`
  inlined its row builds. The recipe-definition → row projection is a view-model
  concern owned by `studio/Rows`/`Panels`/`Board`. The planners expose only the
  `ActorMatches` tables and structural queries. The studio `Snapshot` is assembled
  in wave-3 glue, which reads `ActorMatches` for structure and overlays the live
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

## orchestration (`orchestration/World.h`, `Facts.h`, `Commands.h`, `Tick.h`, `RenderPhase.h`, `PhaseChanges.h`, `Budget.h`, `Sync.h`, `Lifecycle.h`, `Agreement.h`)

The tick core of `docs/plans/pure-tick-2026-10-10.md` (branch `pure-tick`).
It decides each tick from the `World` and the `TickFacts` and returns the next
`World`, the commands, the trace records and the log lines. The engine runs it
once per tick. "The engine runs the core" below says how.
`docs/components/orchestration.md` maps the directory.

- An `TrackedActor` without `render` is an actor with nothing installed. A
  `render` in `Empty` is an actor whose install built no render, or
  whose plan job failed. The two differ for the budget radius: only the first
  counts as evicted.
- A phase is created only by `PhaseFor`, from the first event of an install.
  `OnEvent` accepts a creating event (`PlanAwaited`, `Installed`,
  `InstalledWithoutRender`) only in `Planning`, where it is the rebuild
  after a plan job. Anywhere else it is ignored, so a late install result
  cannot replace a running resize and leak its work. An ignored creating
  event is a core bug, not a runtime case: `Installed` only answers `Install`,
  issued after `Retire`, or `BuildAwaitedRender`, issued only from
  `Planning`. The scenario tests assert that it never happens, the
  agreement check finds a render the core does not track, and a regression
  launch fails on an `actor_phase_ignored` record (decision R-7).
- A swap ends `Shown` at the new level with a fresh out-of-view hold, because
  the new render's draw ran every step. Before stage 3 of the ownership plan
  a swap kept `kReleased`, and the actor was never released again (audit 4b).
- `Resize` keeps only the attached render's `IntermediateHolding`, not its
  hold. A resize that returns to `Shown` starts the out-of-view hold again, so
  a hold from before the resize cannot release the actor at once. An actor in
  view during a resize holds again, because the attached render restores its
  intermediates when it draws.
- A failed resize (`ResizeNotStarted`, `ResizeNotSwapped`) records
  `FailedLevel{level, wantedThen}`. `Shown` and `Resize` both carry it, so
  a resize to another level, or back to the old one, does not forget it. The level stays refused until the wanted
  level differs from `wantedThen`. Keying on the wanted level at the time, not
  on the failed level, covers a failed budget step-down too: there the failed
  level is coarser than the screen level, and a rule that cleared on any
  difference would let the budget retry every tick.
- `WantLevel` at the current level returns the phase unchanged. A new `Shown`
  would reset the screen step-down hold and the blocked step-up.
- `UnpublishedDrawn::drawn` is clamped to the draw's `total`.
- `MayEvict` holds in every phase except `Queued` and `Running`, so the budget radius
  can evict an actor in its first draw. `MayStepDown` holds only in `Shown`.
  A level wanted during a first draw waits in `Drawing::next`.
- `RunBudget` (`orchestration/Budget.*`) counts an actor in the estimate and
  in the `BudgetActor` list only after its first rendered frame
  (`InstalledRender::renderedFrame`). The engine's `PlannedTargets` uses the
  same rule. `HeadroomOf`'s replaced holding uses it too, so the core's
  `PlannedBytesOf(TrackedActor)` is the replaced holding of a reinstall.
- A release or a step down does not lower `BudgetState::planned` in the tick
  that issues it. The next memory sample replaces the estimate.
- The step-down and shrink log lines name the actor by `Sighting::name`, or
  say `unnamed` when the name is empty. The exhaustion warning lists each
  holding by form id, with its planned bytes as its own bytes and no shared
  bytes (`HoldingsByActor`).

### The lifecycle step (`orchestration/Lifecycle.cpp`)

- `RunLifecycle` decides in this order:

| Order | Decision | Commands |
|---|---|---|
| 1 | The resize swaps that are due, then the one resize start (`ScheduleResizes`) | `SwapResize`, `StartResize` |
| 2 | For each actor with a render and loaded 3D: the sighting (`SeeActor`), then the screen level step (`StepLevel`) | `ReleaseIntermediates`, `AbandonResize` |
| 3 | The first-draw ends (`FinishFirstDraw`) | none |
| 4 | One `TickActor` per actor with a render and loaded 3D (`TickActorFor`) | `TickActor` |
| 5 | One unpublished draw for every actor in view that has one (`UnpublishedDraws`) | `DrawUnpublished` |

- An actor that gets `SwapResize` or `StartResize` takes no level step in
  the same tick. The engine carries out those commands before their results
  arrive, so a level change in the same tick would abandon or redirect the
  work it started. A level wanted in a tick starts its resize in a later
  tick.
- An actor whose sighting has no loaded 3D gets no decision and no command.
  `RunSync` forgets such an actor earlier in the same tick.
- A resize that gets `SwapResize` no longer counts as running, so the next
  resize can start in the same tick. The swap comes first in the command
  list.
- A resize draws from the tick after `ResizeStarted` arrives.
- A step-up is admitted and charged before `WantLevel`, and only when the
  phase does not refuse the level as failed. A refused level therefore
  charges nothing. A blocked step-up is recorded and traced only in `Shown`,
  because only `Shown` has `waitingForRoomFrom`. Outside `Shown` it is silent.
- `resolution_level` traces the parsed `Sighting::screenFraction`. It reads
  1.0 when `ScreenResolution` is off.
- `InstallBoundsOf` (`Budget.h`) is the one install bound for `Install` and
  `BuildAwaitedRender`: the render's target level, else the reinstall level,
  else the wanted level, with `HeadroomOf(budget, replaced)`. The player gets
  no headroom. A non-player without a known or wanted level starts from full
  with the headroom.
- `RecordRenderResults` sets `renderedFrame` for every actor with a render in
  view with loaded 3D, because its `TickActor` draws it in the same tick.
- `TickActor` conceals the slots in `Planning` as well as in
  `Drawing`. `BuildAwaitedRender` comes from `RecordRenderResults`,
  before the `TickActor` of the same tick, while the phase is still
  `Planning`. The engine builds the render and its first-draw list first.
  A `TickActor` without `concealSlots` would then publish the render before it
  draws. The agreement check over the R3 scenarios found this.
- `TickActor` finishes applications in `Planning` and `Drawing` only
  out of view. In view, a render built from the plan in this tick is
  unpublished, and its applications would report a render the player cannot
  see.
- `RecordRenderResults` drops an actor's events that come before a `Retired`
  for the same actor in one tick's results. The engine runs commands in
  order, so a `BuildAwaitedRender` from step 2 can run before `RunSync`'s
  reinstall retires it. Its `Installed` describes a render that no longer
  exists. Run 20261010T165001-1 recorded the player's first draw as 18 slots
  where the engine held 12, and the first draw never ended.
- `ResizeStarted::drawn` counts the stacks a resize holds as drawn when it
  starts. `StartResize` marks the stacks that merged chains draw as drawn
  already. Run 20261010T165001-1 started a resize with 30 of 55 stacks drawn.
- `BudgetSample::heldBytes` is the bytes of the targets the rendered actors
  hold, measured by the engine with `PlannedTargets`. `CostOf` counts a
  target with a `sharedKey` once across actors. `RunBudget` sets the planned
  bytes from it on each sample.
- The sum of the records' level costs counts a shared target once per actor.
  Run 20261010T172714-1 measured that sum at a median of 1.49 times the held
  bytes and at most 3.03 times, largest in crowds that wear the same recipes.
  The heartbeat's `estimated_bytes` is that sum; `planned_bytes` is the
  budget's figure.
- `actor_phase` carries `from`, `to` and `event`; `actor_phase_ignored`
  carries `phase` and `event`. The phase names are `empty`, `planning`,
  `drawing`, `shown`, `resize_queued` and `resize_running`; the two resize
  names keep the `resize_` prefix so a trace line reads without the type
  names. A phase created from nothing has the phase name `none`. The event names are the `RenderEvent` names in snake case,
  and `plan_built`, `plan_failed`, `seen`, `level_wanted` and
  `first_draw_ended` for the decisions.

### The sync step (`orchestration/Sync.cpp`)

- `RunSync` forgets first: an actor whose sighting has no loaded 3D, whose
  install failed, or that the engine asks to forget (`TickFacts::forgets`) gets
  `Forget` and its record is erased. A forget request for an actor without a
  record does nothing. The engine sends a forget request for an unload event,
  `RetireAll` and the regression retire, where the actor can still have 3D. The engine reports
  no `Retired` for a forget, so the erase happens in `RunSync`.
- Stale actors are decided in order: those already waiting for an install
  first, in `NextInstall` precedence, then the rest. Each goes through
  `Actors::Advance` with the effective evict distance (the setting or the
  budget radius) and the actor's requested reinstall mode.
- An install that `CanInstallThisTick` refuses is deferred: the old installed
  plan's models are kept, the actor joins the waiting installs, and the
  trace records `sync_deferred`. Its state is not recorded, so the next
  observation decides again.
- The core stops tracking a render when it decides to retire it: `RunSync`
  records the render's target level as `reinstallLevel` and its return bytes,
  then erases `render` in the tick it issues `Retire`. Later steps of that tick
  therefore send no `TickActor` and no unpublished draw for it; the engine
  carries out commands after the `Install` of a reinstall, so a `TickActor`
  with the old phase's flags would show the new, unpublished render for a
  tick. A `Retired` result for an actor without a render changes nothing.
- A changed actor gets `KeepModels`, `Retire` when something was installed,
  `Install` with its bound and replaced bytes when the new state installs
  anything, and `SettleApplications`. The replaced bytes are freed with `Released` when the `Install`
  is issued; the new render is charged when its `Installed` arrives.
- Each issued install is charged the full `kInstallBudgetUs`, because the core
  cannot measure install time, so at most one install starts per tick.
- `Observe` asks for a fresh observation: on the pending-model timer
  (`kLoadingCheckMs`), on the recheck timer (`kSlowCheckMs`, `ModelLoadOf`, a
  range change through `InRange`), for each claimed actor, and for the next
  waiting install.
- The sync log names an actor by `Sighting::name`, or by its form id in hex
  when the name is empty.

### Boundary facts (`planners/ViewFacts.*`, `planners/TickClock.h`)

- `InViewOf` and `ScreenFractionOf` turn the engine's readings into
  `Sighting::shown` and `Sighting::screenFraction`. `Manager::SightingOf`
  (`engine/ManagerFacts.cpp`) gathers the readings. The engine reads the
  camera only for a non-player with `CullOutOfView` on (`CameraViewOf`), so
  the `kCull` timer and `actors_in_view` count only those reads. An unread
  camera counts as out of view. The player, the inspected actor, and every
  actor while `CullOutOfView` is off are in view.
- `ScreenFractionOf` gives `1.0` when screen sizing is off, so the core sees
  every actor full-size. The regression override replaces the setting.
- `ClockAfter` builds `TickFacts::now` from 32-bit `NowMS` readings. The first
  reading starts the clock at its own value, so the low 32 bits of `now` stay
  equal to `NowMS`, and `PlannerMs(now)` matches the 32-bit times the engine
  stores. Each later reading adds the unsigned 32-bit difference, so the
  clock runs on past the wrap at 49.7 days.

### The agreement check (`orchestration/Agreement.cpp`)

- `Disagreements` compares each actor's `RenderPhase` with the resources the
  engine holds for it (`HeldResources`). The engine runs it at the start of a
  tick, against `SettledWorld`: the last world after `RecordSyncResults` and
  `RecordRenderResults` of this tick's results. At that moment every command
  of the last tick has run and its results are recorded, so both sides
  describe the same moment.
- `SettledWorld` drops `TickFacts::jobs`. A finished plan job moves an
  `Planning` actor only through `BuildAwaitedRender`, which the engine has
  not run yet.
- An actor in `Empty` may have no store entry, because an install
  that places no recipe stores nothing. A store entry for an actor without a
  `render` is a resource the core does not track.
- `Drawing` has no holding to compare: the core does not track the
  holding of a render before it is published.
- `SettledWorld` also drops `ResizeNotSwapped`. The engine keeps a resize that
  did not swap until it runs the `AbandonResize` that the event implies, in
  the tick after the swap. Without the drop, the check reports that resize for
  one tick.

### The engine runs the core (`engine/ManagerFacts.cpp`, `engine/ManagerCommands.cpp`, `engine/ManagerTick.cpp`)

- `Manager::RunTick` runs on every tick interval, also when no actor is
  installed. It drains the plan jobs, sweeps, gathers the facts
  (`GatherFacts`), checks agreement, calls `Orchestration::Tick`, emits the
  core's trace records and log lines, and executes the commands in order.
- `SpreadTickActors` (`orchestration/DrawSpread`) takes each `TickActor` that
  draws stacks and whose actor no other command of the tick names, and the
  engine queues it (`spreadDraws_`). The tick frame and the frames after it
  each draw `DrawsDue(waiting, framesLeft)` of the queue, where the frame count
  is the number of frames in the last tick interval. A new tick first draws
  whatever is left, so the core's next facts see every draw done. Each
  queued actor ticks its signals at its own draw frame. Without the spread,
  every actor drew on one frame in four at `AnimationFPS=30` and 118 fps: in
  run `20261016T145353-1` that frame carried about 3.4 ms of plugin GPU time
  and the longest frame interval was 14.5–16.2 ms against a 9.2 ms mean. A
  queued `TickActor` returns no results, so drawing it later changes nothing
  the core reads before its next tick. An actor that another command names
  (an install, a retire, a resize, an unpublished draw) draws in the tick,
  after that command, as before.
- Only `world_ = std::move(out.world)` writes `world_`. No executor reads it.
  The heartbeat, the census and the regression facts read it after the tick.
- An executor branches only on whether an engine call succeeded. It looks up
  the actor by form id and keeps no `RE::Actor *` past its own call.
- A sync no longer runs in an SKSE task. The application service's callback
  (`NoteStale`) records the actor and its tokens in `stale_`. The next gather
  observes and plans the actor. `Observe` calls `SyncNow`, so the actor
  arrives as stale one tick later.
- An unload event, `RetireAll`, a failed `MarkReinstall` and the regression
  retire add the actor to `forgets_`. The gather sends a forget request for an
  actor with a record. For an actor without a record, the gather stops its
  animation subscription, abandons its applications and drops its models at
  once, because the core has nothing to forget.
- `stale_` and `forgets_` have no lock. SKSE tasks write them, and tasks do not
  overlap `OnFrame` (run 20261004T023455, see the heartbeat note under
  regression runs).
- Claims are the one resource change outside a command. The gather finds a
  geometry whose material or shell another system replaced, retires it and
  reports `ClaimedGeometry`. The ownership is already lost when the gather
  sees it, so the retire decides nothing.
- `Forget` always abandons the actor's pending applications. A token of an
  actor that the core forgets reads "the actor was retired before rendering
  completed".
- `KeepModels` keeps the models with the gather's observation time
  (`GatheredFacts::observedAtMS`). Models the gather saw in this tick
  therefore stay.
- `SwapResize` always conceals the actor's slots. The `TickActor` later in the
  same tick writes them again when the actor is in view. A swap that fails
  keeps the resize; the core abandons it the next tick.
- `resolution_swap` has no `frames` field. The frame a resize began lives in
  the core.
- An `Install` for an actor that still has a store entry retires the entry
  first and traces `resource_disagreement`.
- The install summary line says `installed` or `reinstalled` from the
  command's `InstallKind`. A changed install reads `reinstalled`.
- The instances' applied level lags a new render or a swap by one tick: the
  core sends `SetAppliedLevel` when it records `Installed` or `ResizeSwapped`.
- The agreement check traces `resource_disagreement` with the actor, its phase
  and the findings, only when the actor's set of findings changes, and logs
  the line once. An executor that finds no resource for its command traces the
  same action with a `command` field. `target_holders` carries
  `resource_disagreements`: the count of both since the game started.
- The heartbeat is due once a second. The next tick drains the metrics,
  samples video memory and gives the sample to the core as
  `TickFacts::budgetSample`, so the core runs the budget in that tick. `OnFrame`
  emits the heartbeat after its frame timer ends. A regression window edge
  emits a heartbeat at once and gives its sample to the next tick.
- `estimated_bytes` is the core's estimate (`PlannedBytesOf(World)`).
  `engine_estimated_bytes` is the planned holding of the engine's renders of
  actors that have rendered (`PlannedTargets`). The core's sum counts a
  shared target once per actor, so it is the larger of the two when actors
  share targets (see the lifecycle step).

### The install program (`planners/InstallProgram.*`, `engine/InstallStages.*`)

- An install runs the stages in one executor call (`Manager::InstallLiveActor`):
  `PlanSurfaces`, `KeepSurfacePlan`, `BindSurfaces`, `ActivateOutputs`,
  `StackSizeOf` for each stack, `RenderBuildRequestOf`, `PlanRouteOf`,
  `RenderBuildAt`, `AttachmentsOf`, `UnpublishedSlotsOf` and `PlanLightsOf`.
  The stages are pure and native-tested. `BindSurfaces`, `ApplyAttachments`
  and `PlaceLights` are the executors that call the engine.
- `BuildAwaitedRender` and `FailAwaitedRender` gather the surface plan again
  from the `LiveActor` (`SurfacePlanOf`, `BoundSurfacesOf`) and run the
  program from `ActivateOutputs`. The gather gives the plan key of the
  install, so the build after a plan job is a cache hit. A geometry that a
  claim retired has an empty plan (`RetireGeometry`), so it requests no stack.
- `GeometryPlacementPlan::placed` holds pointers into the recipe catalog
  (`PlacedRecipe::recipe`). A `RebuildAll` swaps the catalog, so
  `KeepSurfacePlan` does not copy `placed` and `SurfacePlanOf` leaves it
  empty. `ActivateOutputs` and `AttachmentsOf` do not read it.
- The install executor reads a geometry's slot targets right after it binds
  that geometry. A later bind can detach an earlier geometry's material when
  the two geometries share a shader property.
- The adapters find each geometry, placement and output by its id at the
  moment they write. No stage keeps a reference into a `LiveActor`.
- The executor copies the `GeometryInputs` for the `RenderInstance` after it
  clears each geometry's render. A copy that held the new render would keep
  the instance alive through its own inputs.
- **Plan route.** `PlanRouteOf` chooses one of eight routes from the cache
  setting, the key, a cached plan, verification and an open job: `kFresh`
  (the cache is off), `kUncacheable` (no key, verifying), `kSubmitUnkeyed`
  (no key), `kCached`, `kVerifyCached`, `kShareJob`, `kSubmitJob` and
  `kFreshStored` (a verifying miss). A refused keyed submission builds a
  fresh plan and stores it; a refused unkeyed submission builds a fresh plan
  on the main thread.
- A job's `PlanDestination` is its plan key or, for an unkeyed job, the
  `PlanBuildInput` it was submitted with. `DrainPlanJobs` stores a keyed
  result in the cache. It hands an unkeyed result to its actor
  (`HandedOver`; one plan per actor, at most `kHandedPlanCapacity`). The
  actor's next `RenderPlanFor` takes it (`TakeHandedPlan`) only when the
  actor's current input equals the submitted one (`SameBuildInput`: graph
  and output pointers, recipe, output index, placement, geometry, context,
  side, identity facts and chains). A plan built for an older input is
  dropped (`handed_over_stale` in the trace) and the request routes again.
- The plan cache and the job book stay in the engine. They are mutable state
  that actors share, which the boundary rule gives to the core. They are an
  exception because they change when a plan is ready, not which plan an actor
  gets. A cached plan has the structure of a fresh plan, and verification
  (`hit_verified`, `mismatch`) checks it.
- **Stack size.** `StackSizeOf` reads two facts: the largest side of the
  material's native maps (0 when no map has an extent), and the largest side
  of the slot's base map. The engine gives no base side when the slot is
  height and the material has flat displacement, when the base map is a
  placeholder, or when its extent is unknown.
- The requested size is the largest native side (4096 when it is 0), divided
  by the texture scale and then by the slot's resolution. Each step clamps to
  64..4096. Without a base side, the stack takes the requested size. With a
  base side, the stack takes the base side, clamped between the requested
  size and the larger of the native side and the requested size.
- `StackSizeOf` takes the texture scale as a `Resolution`, because the layer
  graph (`tools/gate.py`) does not let `planners/` include `Settings.h`.
  `ScaleResolutionOf` converts `Settings::textureScale`. The two enums divide
  by 1, 2 and 4.
- The executor computes `animated` of a stack's `RenderOutput` as
  `IsAnimated` of the instance graph at the output. The instance graph can
  carry a variant, and no stage sees it. A merged chain's `animated` comes
  from its `SlotStackPlan` (`AttachmentsOf`).
- **Output states.** `PlacedOutput::state` is an `OutputState`. An output
  that holds a stack is pending, rendered or failed with the render failure.
  An output with any other problem holds no stack. So a render outcome
  changes only an output that holds a stack (`RecordRender`,
  `engine/ManagerTick.cpp`), and a lost geometry fails only such an output.
- `ObserveOutput` reports a render failure only after the actor has rendered,
  and any other problem at once. `RetireGeometry` replaces a render failure
  with the retire problem. The studio's output row shows the render failure.
- `Installed::total` is the length of the list from `UnpublishedSlotsOf`:
  every slot of every stack plan, in geometry order. The agreement check
  compares it with the actor's first draw.
- `PlaceLights` writes the light problems of `PlanLightsOf` first. It skips a
  light whose instance has no signals or whose geometries are all gone, and
  the problem "no eligible light output" stays then. `LightProblemOf`
  branches on the engine's result, so it stays in the executor.

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
runs the mutation first, then asks the actors it affects for a reinstall; its
recipe argument scopes both reporting and, when matching is unchanged, actor
selection. Generated compositor outputs and
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
`TESObjectARMA*` alongside `partClone`. `WornArmorOf` (`ActorObservation.cpp`)
keeps one piece per armor and addon, so two attached addons of one armor are
two pieces. The addon becomes `PieceRef::addon` as a plugin/local-ID
`FormKey`; `MatchInputOf` copies it to each geometry of the piece.
Use this association rather than guessing from the armor's addon list or
mesh name. A missing addon stays absent and cannot match an addon-only selector.
`LightEligible` uses that same identity for planning and actual light placement.

## Imported recipes cover every effect

`ArmorEnchantmentShaders` (`engine/RecipeStore.cpp`) collects the effect
shader of every effect of every constant-effect enchantment, not only the
costliest effect's, because the matcher keys on every effect's shader
(`WornKeysOf`): a recipe applies per effect, so each effect's shader needs
one. The game itself shows only the costliest effect's shader. At each load
the log names, per shader, the recipe that covers it.

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
On 2026-10-13 material regions (`mesh/MaterialRegions.h`) replaced the
k-means clustering, and its weights, seed, count and guess left the format.

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

## Regression requests (`engine/RegressionRequests.cpp`, `engine/RegressionRequest.h`)

A request stores scalar data and queues its mutation through Manager's
session task queue. Application completion must belong to a newer actor
attempt, with a rendered surface output on the demo armor; retirement checks
actor-state removal. A game load aborts the current request.

## Unattended regression runs (`regression/Run.h`, `engine/RegressionRun.cpp`)

The run file sits in the SKSE log directory, beside the trace, so the host
writes it without touching the MO2 mods directory. The plugin deletes it on
read and refuses it after `notAfter`, so an ordinary later start never
repeats a run. The plan is
`docs/plans/unattended-regression-2026-10-01.md`.

The player-update hook does not run at the main menu, so a `MenuOpenCloseEvent`
sink on `RE::UI` starts the load. The load runs as an SKSE task through
`BGSSaveLoadManager::Load(name, false)`; `false` skips the missing-content
check, whose dialog would wait for input. `Load_Impl` is protected in
CommonLibSSE-NG, so the call cannot see its result; a failed load reaches the
run as `kPostLoadGame` with null data.

`RE::Main::quitGame` ends the game at the next main-loop pass. The runner sets
it 10 frames after the end line so the result and trace files flush first.

`RunState` lives in process memory, never in the save, so a case can load a
save and continue. The fixture is the demo cuirass, `0x803` in
`BetterEnchantmentEffectsDemo.esp`; the body slot is `BipedObjectSlot::kBody`.

`engine/RegressionWorld.cpp` takes these forms from `Skyrim.esm`:

| Form | Local ID | Use |
| --- | --- | --- |
| `ArmorDwarvenCuirass` | `0x1394D` | The control's plain cuirass; the demo cuirass copies it |
| `PlayerHouseMannequin` | `0x89A85` | Base of the spawned wearer and control: no default outfit, a stand-still AI package |
| `XMarker` | `0x3B` | The return marker placed at the player before travel; an engine-reserved form, found by `TESForm::LookupByID`, which `TESDataHandler::LookupForm` does not find |
| `WarehouseNPCCarryFirewood` | `0xCCE01` | Base of an idle-NPC crowd: Nord, not unique, not a template, no factions, class `CombatWarrior1H`, aggression 0, confidence 2. It carries `CarryActorScript` (firewood `0x6F993`) and the package list `DefaultPackageListLinkedPatrol`; in combat it never readies (four runs on 2026-10-02) |
| `WEAdventurerTemplate` | `0x10553F` | Base of a fighter crowd: Nord, not unique, not a template, no script, no factions, a combat style, three leveled items, aggression 2 (held at 0 from spawn until `StartCrowdFight`) |
| `ArmorIronCuirass`, `ArmorLeatherCuirass`, `ArmorElvenCuirass`, `ArmorOrcishCuirass`, `ArmorEbonyCuirass`, `ArmorDaedricCuirass` | `0x12E49`, `0x3619E`, `0x896A3`, `0x13957`, `0x13961`, `0x1396B` | The vanilla-cuirass dress, in that order; crowd actor *i* wears entry *i* mod 6 |
| `WEEnemiesFactionA`, `WEEnemiesFactionB` | `0xBA0B8`, `0xBA0B9` | The two sides of a crowd fight: each lists the other as enemy, and neither has a relation to `PlayerFaction` |

A spawned actor needs no default outfit: an NPC with one re-equips it over the
fixture after an equip, an enable or a 3D reload, even with removal prevented.
The mannequin base also carries `MannequinActivatorSCRIPT`, which re-equips
the items it was given when its cell loads. Spawned actors are placed with
`forcePersist` so they survive the player's
absence; the runner deletes them (`Disable` then `SetDelete`) at cleanup and
again when the run ends. `LeaveStart` moves the player to the exterior cell
`Riverwood` by name, through the same `CenterOnCell` lookup the `coc` console
command uses; the known good save sits in the interior test cell `QASmoke`, so
leaving unloads the start cell and its spawned actors. The player counts as
away while its cell differs from the return marker's cell. Travel runs as an
SKSE task, because a cell change
inside the player-update hook would change the player's cell during its own
update.

The vanilla-cuirass dress exists only while its crowd does. Spawning it adds
the demo keyword (`0x800`) to each cuirass and sets its `formEnchanting` to
the demo enchantment (`0x802`), so Arcane Circuit's keys match. Despawning
the crowd, the end of the run and a load restore both: a load does not reset
base forms. The keyword is removed only when the run added it. Winterglass
keys on the exact demo form, so it never matches these cuirasses, and
Resonant Ward needs `ArmorHeavy`, which the leather and elven cuirasses
lack. The list leaves out `ArmorDwarvenCuirass`, because it is the untagged
control's cuirass, and `ArmorGlassCuirass`: in the test load order its
material has a coat model, and the plugin refuses Arcane Circuit's fuzz and
glint on it (run 20261001T195212-1).

A crowd is dressed only after every actor is present: an NPC's outfit items
can arrive after placement. Dressing removes the default outfit's items and
every other body-slot armor from the actor's inventory, then equips the
member's armor with removal prevented. The idle NPC carries an iron cuirass
in its base inventory, which the strip removes. In game, it is not yet
verified whether an outfit re-equip after a 3D reload (see above) comes back
through this strip.

A recipe set (`View::recipeSet`, set by `ShowRecipeSet`) filters each
geometry's matched recipes in `Studio::ViewedRecipes`, after isolation. Recipes
outside the set never apply and never enter a plan. An empty set restricts nothing. The
fingerprint is the FNV-1a hash of `SerializeRecipe`, so it changes with the
recipe's content, not its file.

`StartCrowdFight` puts even-numbered crowd actors in `WEEnemiesFactionA` and
odd-numbered ones in `WEEnemiesFactionB`. It raises Aggression to 1 and
Confidence to 4 on each: aggression 0 never starts combat, and lower
confidence lets a hurt actor flee out of the ring. Each crowd NPC gets the
reference's `kEssential` flag, so a beaten actor goes into bleedout instead
of dying. It is not yet verified in game that the reference flag alone gives
bleedout. The fighters have no weapons and fight with their fists.

Combat state is not a fight. `IsInCombat` turns true on the first evaluation
after the faction change, and a combat target follows within 0.1 s, whether or
not the actor ever readies. `AwaitCrowdHurt` passes only when a crowd actor
loses health or bleeds out. Four runs on 2026-10-02 with the idle-NPC base
showed 12 actors in combat, in high AI process and on the navmesh, with no
weapon drawn and no actor hurt in 120 s. Run 20261002T214537-1 with the
fighter base drew all 12 weapons within 1.7 s and hurt an actor 54 frames
after the fight began.

In `QASmoke`, a crowd of idle NPCs or fighters spawns on a grid inside the
cell's only navmesh (Skyrim.esm `0x32D8B`, x 2112 to 2688, y 1856 to 2176,
floor z 6976), three rows deep with a 48-unit margin, and the player moves to
(1904, 2016) until the crowd despawns. The known good save puts the player at
(-957, 1827), outside the room's west wall, where no navmesh exists.

A crowd at `Spot::kAhead` (`soak-distance`) stands in four rows at 150, 400,
800 and 1600 units in front of the player, along the camera's direction.
Member `i` stands in row `i % 4`; the members of a row stand 120 units apart.
The screen fraction is the bound's diameter over the camera distance over the
frustum height (`ScreenFraction`), and the third-person camera sits behind the
player, so the rows are meant to reach the half, quarter and eighth levels. It
is not yet verified in game which level each row gets; the report's
*screen resolution* table gives the counts. The run file's
`screenResolution` replaces the setting until the run ends. The gather reads
it into `TickSettings::sizeByScreen` and into `ScreenFractionOf`, so both
level decisions (`InstallBoundsOf` and the screen level step) see it.
`--screen-resolution on` and `off` runs of one case therefore give the A/B
comparison.

The run reads the camera from `PlayerCamera::currentState->id`: state 0 is
first person, state 9 is third person, and every other state is neither. A run
sets the `[Camera]` setting `fAutoVanityModeDelay` (120 s by default) to 10^9
and restores it at the end: without that, the idle camera took over two minutes
into every soak and ignored `ForceFirstPerson`. `soak-orbit` turns it back on
on purpose (`allow-idle-camera 3`): state 1, the idle camera, orbits the player
and sweeps the view across the crowd ring, so the frustum count
(`out_of_view_pct`) can be checked against a moving camera without anyone at
the game. `hold-idle-camera` and cleanup's switch to third person end the
orbit. The studio uses the same setting (`engine/IdleCamera`): on the SKSE
Menu Framework's `kOpenMenu` event it saves the delay, sets 10^9 and leaves an
active idle camera with `ForceThirdPerson`; on `kCloseMenu` it restores the
saved delay. `Manager` owns that hold (`HoldIdleCameraForMenu`,
`ReleaseIdleCameraForMenu`, `menuIdleHold_`). The events arrive from the
framework's render callback, so both steps run as SKSE tasks on the game
thread. The regression run keeps its own saved delay
(`RunWorld::vanityDelayBeforeRun`). The two holders do not know about each
other: if a run ends while the menu is open, the menu has saved the run's
10^9 and restores it on close, so the idle camera stays off until the game
restarts. One owner for the menu hold, the run hold and the orbit soak's
short delay would remove this; it is not built (2026-10-07). The hold covers the whole framework
menu, whichever mod's page is open; a framework without the
`RegisterEventPriority` export gets no hold and the log says so. On the 2026-10-03 load order,
`ForceFirstPerson` issued 62 ms after the fixture rendered was reverted to third
person 0.77 s later, and the camera then flipped back to first person without
a command; with 60 frames between the render and the switch, both views hold.

A **trace** is a geometry under the actor's third- or first-person 3D whose
name ends in the shell suffix, or a PBR material texture whose name begins
with the presenter folder (`textures\BetterEnchantmentEffects\slots\`). The
presenter textures keep their load path as their name. A rendered fixture
must show at least one trace; an actor at baseline shows none.

`Manager::RegressionActor` reports the newest application revision whose
record reached `kRendered` while the actor's state renders the fixture.
`AwaitRendered` compares it with the revision seen when the last step that
moved that actor started, so an earlier render cannot satisfy a later wait.

An `Expect…` step checks a state that the steps before it should already have
produced. It waits up to `kExpectDeadlineFrames` (600) only so the
observation can catch up, then fails. An `Await…` step waits for work in
progress: `kRenderDeadlineFrames` (1800) for a render, a retirement, an idle
editor or a studio session, and `kCrowdDeadlineFrames` (3600) for the crowd.
An action step waits for its own effect: `kActionDeadlineFrames` (600) for
one engine action and `kTravelDeadlineFrames` (3600) for a cell change.

`BGSSaveLoadManager::Load` called from an SKSE task does not start the load
inside that task: `kPreLoadGame` arrives about 25 ms later, after the tasks
already queued. So an edit or apply requested in the same frame as the load
runs first. The load cases therefore start open work (a gesture, a paint
preview) in an earlier step and wait until the editor reports it active. A
load the run starts itself keeps the run going: `FinishRegressionLoad`
forgets the spawned actors and the marker and counts the load, and the
`LoadDuring` step waits for that count. `AbortWaitingRegressionRequest` aborts only a
request that is still waiting, so a finished result is never rewritten.

The studio cases write `regression-scratch.json` to the user recipe folder,
which MO2 redirects to the profile's output mod (here `SKSE Output`); the
reload launch reads it back from there. Mod Organizer ignores a
`moshortcut://` request sent in the seconds after the game exits, so
`tools/regression-run.py` pauses 15 seconds between launches and repeats an
unanswered launch request once.

A **quick run** (`--quick SECONDS`, the run file's `measuredSeconds`) holds
each measured window (`HoldRole::kMeasured`) for that many seconds and the
baseline and recovery for at most 10 s; the settle keeps its 30 s, so the
measured window still starts settled, and a native test checks that every
measured window follows a settle and precedes a recovery. The start line
records `measuredSeconds`. A quick run is for comparing two builds; the full
soak stays the checkpoint gate, because the budgets and the leak check are
written against its windows. `soak-enchanted --quick 60` took 205 s against
about 305 s for the full case, and its steady plugin time (810 µs per frame)
fell inside the spread of three full runs the same day (740 to 893 µs).

The soak's crowd is observed through `Manager::RegressionActors`, one pass
over one copy of the application snapshot for the roles and every crowd
member, never by walking a member's 3D. The roles' 3D is walked each frame
for residue (shells and presenter textures). This work lowers the game's fps
but not the heartbeat's plugin cost.
The run advances before `Manager::OnFrame` starts its frame timer, so the
heartbeat's plugin cost excludes the runner. `await-crowd-rendered` completes
once every member's application reports rendered, before the GPU work of the
first application lands; the `settle` window after it holds that cost, and
`steady` measures the settled state. First-render admission (in the manager
section) and idle release (in the compositor section) bound the burst's
memory. A heartbeat's `targets` and
`target_bytes` include the targets idle in the reuse pool (at most 16 and 64
MiB, `RenderTargetPool::kMaxIdleTargets` and `kMaxIdleBytes`).

The heartbeat is emitted after `OnFrame` ends its frame timer, so its own
trace writes stay out of `frame_us` and the 8 ms spike count. A load returns
from `OnFrame` early and calls `Metrics::ForgetFrameInterval`, so the load
screen does not count as one long frame interval. Before R4, syncs ran
outside `OnFrame`: the SKSE task queue ran them on six engine job threads in
run 20261004T023455, one at a time, and no frame-thread event fell inside any
of 1570 syncs. Since R4 the tick runs every install, so `frame_us` includes
`sync_us`. `sync_us` stays a sub-measure of the frame. The report's
`plugin_ms_per_s` and `plugin_share_pct` therefore count `frame_us` alone, and
`plugin_frame_us_mean`, the budgeted measure, is the frame alone. `gpu_us_mean` counts only timed
`RenderTick` spans, so the report prints `gpu_ticks_timed` and
`gpu_ticks_untimed` (dropped plus discarded) beside it. A budget never passes
on missing data: a window with no heartbeat, a ratio against a zero baseline,
a window that never closed and a window measured twice are each reported.
The stall report names the last finished step; the run stopped in the step
after it.
- `RoleFacts::liveItems` counts only the run's own items with live
  effects (`kItems`: the fixture, the plain cuirass and the paired
  gauntlets), not every live piece of the actor (2026-10-09). The player in
  `BEEFRegression` wears an enchanted fine hat and fine boots whose effect
  shaders match imported recipes, and imported recipes draw since they use
  the fill template, so counting every live piece failed `unequip-one`
  with 4 live items where the case expects 2. Solo isolation filters what
  is shown, not what matches, so it does not remove them.
  `strayItems` still counts every live item that is no longer worn.
