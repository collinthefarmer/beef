# WornEnchantmentPBR (alpha)

Status (2026-09-04): the runtime is still the proof of concept; the rewrite
it informs is specified in `../../plans/worn-enchantment-pbr-compositor-brief.md`.
Phase 1 of that brief is in: the recipe data model (`src/Recipe.*`), the
signal graph (`src/Signals.*`), the EFSH importer (`src/Importer.*`) and the
recipe store (`src/RecipeStore.*`), all engine-free apart from the store, with
native tests. At `kDataLoaded` the store reads `recipes/*.json` and writes an
imported recipe for every armor enchantment shader that has none; nothing
reads the recipes yet (phase 2). Read the brief and NOTES.md before changing
the structure here. The last in-game confirmations were the light, shell,
inflation and glow-mask outputs; `ShellMode=1` (layers on the shell) is
confirmed as well.

SKSE plugin that shows an equipped armor's enchantment visual by driving the
armor material's PBR emissive channel under Community Shaders (CS) TruePBR,
instead of binding the vanilla effect-shader membrane. It never touches the
per-geometry `BSEffectShaderData` slot, so Dirt and Blood, flesh spells and
scripted `PlayEffectShader` keep working on the same armor.

Scope of the alpha: player and NPC actors, third and first person, worn ARMO
pieces whose biped clone carries a CS PBR material. Non-PBR geometry, weapons,
skin, hair, EFSH particle systems, holes, edge/rim effects and projected UVs
are ignored on purpose.

## How it works

1. On equip / load / NiNode update, the actor's worn armor is mapped to an
   EFSH exactly the way Visible Armor Enchantments does (costliest effect's
   enchant visuals, then its enchant shader, then the first effect with either).
2. For every geometry of the worn clone whose lighting property is a CS PBR
   material, the plugin saves the material's `emissiveTexture`, the property's
   emissive colour and multiplier, and the `kOwnEmit` flag, then points
   `emissiveTexture` at frame 0 of a flipbook baked from the EFSH fill texture.
3. A hook on `PlayerCharacter::Update` (a vtable write, no trampoline) runs
   once per frame on the game thread; at `AnimationFPS` it writes
   `emissiveColor = fillColor * fillScale`, `emissiveMult = fillAlpha * EmissiveScale`
   and swaps the flipbook frame from the scroll offset. CS rebinds
   `emissiveTexture` every draw and vanilla `SetupGeometry` uploads the colour,
   so no shader or CS change is needed.
4. On unequip, unload, retire or pre-load, the saved state is restored, but
   only if the material still holds the last frame the plugin wrote. If another
   system changed it, the plugin leaves it alone and logs (under verbose).

Because CS pools materials by content, each written property first receives
its own material copy (`UniqueMaterial=true`). See NOTES.md for what that
relies on.

## Layout

```
CMakeLists.txt                      CMake + FetchContent build
cmake/clang-cl-xwin.toolchain.cmake Linux -> x64 Windows cross toolchain
setup-xwin.sh / build.sh / install.sh
src/                                plugin sources (see below)
src/cs/                             verbatim CS material header + origin commit
src/extern/                         vendored headers (SKSE Menu Framework, nlohmann/json)
tests/*_tests.cpp, tests/run-native.sh  host-independent checks, see Tests
tests/fixtures/efsh/                vanilla EnchArmor*FXS records as JSON (tools/efsh_dump.py)
tests/fixtures/recipes/             the recipes the importer must produce from them
tools/make_flipbook.py              bakes EFSH fill textures into frames
tools/efsh_dump.py                  dumps EFSH records from a plugin as fixture JSON
recipes/                            example recipes the tests read; not installed (a recipe for the game is written into the MO2 mod folder by hand)
ARCHITECTURE.md                     module map, data flow, threads, ownership, extension points
WornEnchantmentPBR.ini              settings shipped with the mod
dist/WornEnchantmentPBR/            staged mod folder (after a build)
```

Sources: `main.cpp` (SKSE lifecycle), `Events.cpp` (equip / load / NiNode
sinks, the animation graph sink), `Hooks.cpp` (per-frame
`PlayerCharacter::Update` hook), `Timing.cpp` (pure EFSH animation maths,
used by the importer), `Settings.cpp` (INI), `PBRMaterial.h` (CS material
layout mirror and PBR test). Engine-free: `Recipe.*` (data model, JSON,
resolution, variants), `Expression.*` (the one expression language),
`Signals.*` (signal graph), `Importer.*` (EFSH record to recipe). Engine
side: `RecipeStore.*` (recipe files, editor IDs, import at data load),
`EngineForms.*` (form keys, editor IDs, the EFSH an enchantment shows),
`Environment.*` (a live actor as the signals see it), `Manager.*` (pieces,
resolution, apply, tick, retire, snapshot), `Compositor.*` (stacks through
the texture lab), `Binding.*` (the only writer of engine state: material
slots, shells, lights), `RuntimeTextures.*` (the D3D texture lab),
`Menu.cpp` (the recipe studio). The proof-of-concept modules
(`EffectManager`, `Layers`, `Outputs`, `Flipbook`) were deleted on
2026-09-04 once every slot ran from recipes.
`Identity.h` is the only source file that spells the plugin name (it reads
it from CMake's `project()`); every path, node prefix and the menu title
derive from it, and `install.sh` and `tools/make_flipbook.py` read the same
name from `CMakeLists.txt`.

## Building (from WSL, no Windows toolchain)

Why CMake: xmake's clang-cl toolchain insists on a Visual Studio install and
ignores an SDK path, so it cannot drive clang-cl against an xwin SDK layout.
CMake takes a toolchain file, and FetchContent pulls CommonLibSSE-NG, spdlog
and rapidcsv without vcpkg (which cannot target Windows from Linux).

Requirements: `nix` with flakes (everything else comes from nixpkgs) and
network access for the first build.

```sh
cd plugins/WornEnchantmentPBR
./setup-xwin.sh          # once: Windows CRT + SDK into ~/.xwin/splat (630 MB)
./build.sh Release       # configure + build; ~5 min the first time
./install.sh             # copies dist/WornEnchantmentPBR to /mnt/a/mods/SkyrimSE/mods/WornEnchantmentPBR
```

`build.sh` runs, inside `nix shell nixpkgs#llvmPackages.clang-unwrapped
nixpkgs#llvmPackages.llvm nixpkgs#lld nixpkgs#cmake nixpkgs#ninja`:

```sh
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.toolchain.cmake
cmake --build build/Release
```

The toolchain file passes `--target=x86_64-pc-windows-msvc /vctoolsdir
$XWIN_DIR/crt /winsdkdir $XWIN_DIR/sdk` to clang-cl and the matching
`/LIBPATH`s to lld-link. Set `XWIN_DIR` to move the SDK. Only the release CRT
is splatted, so Debug builds also link `/MD`.

Outputs land in `dist/WornEnchantmentPBR/SKSE/Plugins/` (DLL, PDB, INI) and
the test exe in `build/Release/WornEnchantmentPBRTests.exe`.

Pinned dependencies: CommonLibSSE-NG `b93280e8` (CharmedBaryon, 2024-09-03),
spdlog v1.15.3, rapidcsv v8.99. Xbyak is not fetched; the only hook is a vtable
write, which needs no trampoline.

## Installing and first run

1. `./install.sh`, then in MO2 refresh the left pane and tick
   **WornEnchantmentPBR**. It needs SKSE, Address Library and Community
   Shaders (tested against CS 1.8.3, Skyrim 1.6.1170).
2. Start the game. `Documents/My Games/Skyrim Special Edition/SKSE/skse64.log`
   should contain `plugin WornEnchantmentPBR.dll ... loaded correctly`, and
   `WornEnchantmentPBR.log` next to it should start with
   `WornEnchantmentPBR 0.1.0 loading on runtime 1.6.1170.0` followed by
   `kDataLoaded`, a `settings:` line, `event sinks registered` and
   `hooked PlayerCharacter::Update (vfunc 0xad)`.
   If CS is missing you get `CommunityShaders.dll is not loaded; emissive path
   disabled, plugin idle` and nothing else happens.

## Runtime textures

With `RuntimeTextures=true` (default) the layer textures are generated on the
GPU every tick instead of read from frame folders. The EFSH fill texture is
loaded through the engine and a full-screen pass tiles, mirrors, transposes
and scrolls it into render targets we own; each target is presented to CS
through an `NiSourceTexture` shell loaded from a placeholder DDS under
`Textures/WornEnchantmentPBR/slots/` (512 shells; a stack owns one and shares a scratch per size for its intermediate layers). The
scroll is exact per pixel, there are no compression artefacts, and the mean
luminance for normalisation is read back from the target's 1x1 mip.

Three layers per effect:

| Layer | Slot | Content | Extra controls |
|---|---|---|---|
| Glow | `emissiveTexture` | fill texture RGB | as before |
| Sheen map | `featuresTexture1` (fuzz map) | white, alpha = fill luminance; fuzz colour/weight are multiplied per texel | `SheenMapMirror`, `SheenMapPhase` |
| Shimmer | `displacementTexture` + `rimLightPower` | per geometry: the armor's own relief (its displacement map, else RMAOS occlusion) x `ShimmerArmorWeight` plus softened scrolling noise x `ShimmerNoiseWeight`; enables CS parallax on the material | `ShimmerScale`, `ShimmerTranspose`, `ShimmerPhase`, `ShimmerArmorWeight`, `ShimmerNoiseWeight` |

If the lab fails to initialise (no device, shader compile error, missing slot
files) the log says why and the effect falls back to the frame folders below.
Shimmer needs Community Shaders' parallax enabled and works best at small
scales; it displaces the whole armor surface.

The pass runs from the per-frame hook on the immediate context and saves and
restores every pipeline state it touches. The shimmer layer is the first armor-sampling pass: the material's own
displacement or RMAOS texture is the second shader resource of the same
draw, sampled with the raw mesh UV. Roughness- or AO-driven glow masks reuse
that input with a different output slot.

## Frame folders (fallback only)

When the GPU lab cannot initialise, an effect falls back to frame folders
under `Textures/WornEnchantmentPBR/<form key>/` or `<fill texture stem>/`
(`frame_<i>.dds`, optional `meta.ini` with `luminance=`), and without those
to the raw fill texture unscrolled. The mod ships no frames; the baker in
`tools/make_flipbook.py` still produces them (BC1 or BGRA8, mirror and
palette options, see `--help`), and `tools/bsa_extract.py` pulls fill
textures out of BSAs. Frame folders are never consulted while runtime
textures are on.

## Colour and extra layers

Vanilla armor enchant shaders (`EnchArmor*FXS`) keep their colour in the
**edge** effect: magicka blue, health red, stamina green, shock violet, frost
pale blue; the fill keys are grey (magicka, health, shock) or black (stamina,
frost), and fire takes its colour from its palette gradient. The plugin maps
that onto the PBR material as follows (all optional, all restored on retire):

| Layer | Source | Material fields | INI |
|---|---|---|---|
| Emissive glow | fill keys, scroll, alpha; hue from edge colour | `emissiveTexture`, `emissiveColor`, `emissiveMult` | `EmissiveScale`, `[Colors]` |
| Sheen | edge colour and edge alpha (pulse, fade) | `pbrFlags` Fuzz, `fuzzColor`, `fuzzWeight` | `Sheen`, `SheenScale` |
| Gloss | fill alpha pulse; with the gloss map, the armor's own RMAOS re-rendered per geometry with roughness lowered where the scrolling noise is bright | `rmaosTexture` (map) or `specularColorScale` (scalar) | `GlossBoost`, `GlossMap*`, `GlossContrast`, `GlossMapSize` |
| Sparkle | INI only | `glintParameters` | `Glint*` |

Sheen and sparkle share one shader slot in CS and neither fits a material that
already has a clear coat or hair model; those geometries log a skip line.
Per-record colour overrides go in `[Colors]` keyed by the form key printed
in the apply line (`skyrim~092dee`). Frames baked for one record live under
`Textures/WornEnchantmentPBR/<form key>/` and win over the fill-texture folder;
`skyrim~092de7` (fire) ships that way with its palette baked in.

## Output spikes: light and shell

Two outputs outside the material, added to test what the material slots
cannot do (now `src/Binding.*`; the INI's `[Outputs]` keys and the Layers
pages described below belonged to the proof of concept):

- **Light**: point lights per third-person effect on the bones the item is
  skinned to (weighted by vertex count, up to `LightMaxPerEffect`, each at
  its bone's skinned centre), hue from the effect and brightness from its
  pulse. Registered with the shadow scene node and removed when the effect
  retires. A fixed bone can be chosen instead.
- **Shell**: a clone of each driven geometry with a vanilla rim-lit material,
  alpha-blended over the armor, inflated radially about each bone by editing
  the shell's own skin-to-bone transforms, with a rest size and a pulse term.
  The apply line reports whether the clone shares skin and GPU buffers with
  the original and which skin data the inflation edits. With `ShellMode=1`
  the shell instead gets a private copy of the armor's PBR material and the
  glow, sheen, shimmer and gloss layers write that copy; the armor material
  is never touched, the shell blends and inflates as before, and the apply
  line says `material=shell`.

Both log under verbose and show on the Overview effect line as `outputs:`.

## Recipes (format 1)

A recipe is the unit of design: what an enchantment (or a material, keyword
or armor) looks like, as signals, curves, sources, masks and per-output
layer stacks. The specification is the brief's data model section; the
file format is `schema/recipe.schema.json`, and `schema/example-magicka.json`
is the canonical file. The runtime does not read recipes yet (phase 2); the
store loads and validates them and the importer writes them.

Files live under `Data/WornEnchantmentPBR/<Mod>/*.json`, any depth, loaded
in path order; `Data/WornEnchantmentPBR/user/` loads last and is where the
menu will save; the importer writes to `Data/WornEnchantmentPBR/imported/`.
At `kDataLoaded` the store reads every file, resolves editor IDs against the
loaded forms, logs each row problem on its row (`recipe <id>: signal x:
...`), then imports a recipe for every effect shader that a constant-effect
enchantment resolves to and no loaded recipe is keyed to.

Reading a file, the rules that hold everywhere:

- `"@name"` refers to another row (a signal, source, mask or curve). A bare
  string where a reference belongs is an error. Enum values are bare strings.
- Anything discriminated is a one-key object: keys (`{"effectShader": ...}`),
  selector terms (`{"texture": "*iron*"}`), trigger sources, bakes, and every
  signal and source row. Named sections are objects keyed by name.
- Any parameter that takes a reference also takes a literal:
  `"opacity": 1.0` or `"opacity": "@fillLevel"`. Vectors are arrays of
  parameters or one reference to a vector signal: `"tile": [3, 3]`,
  `"scroll": "@scroll"`. Types are scalar, vec2 and vec3; a colour is a vec3,
  and a colour written with a component above 1 is read as 0..255.
- Forms are an editor ID (`EnchArmorMagickaFXS`) or a form key
  (`0x92DED~Skyrim.esm`). Editor IDs of effect shaders need po3's Tweaks at
  runtime, as SPID and KID do; an unresolved one is reported and never matches.
- Globs use `*` only. `//` and `/* */` comments are accepted on read and
  dropped by a save. Unknown keys anywhere outside `meta` are errors.

One language serves signals, masks and curves: numbers, `[r, g, b]`,
`@name`, `+ - * /`, parentheses, comparisons giving 0 or 1, `and`, `or`,
`not`, `if(c, a, b)`, and `abs min max clamp saturate floor ceil frac sqrt
pow sin cos step smoothstep lerp`, with `time` and `pi`. Arithmetic is
component-wise on vectors; division by zero is 0; there is no `^`. A curve
is an expression in `x` (`"flash": "pow(1 - x, 2)"`, with `mean` for
contrast shaping), applied by name on a row or called as `@flash(@hurt)`. A
mask is an expression evaluated per texel where source and mask names are
images: `"polished": "@metallic * smoothstep(@edge - @soft, @edge + @soft, 1 - @roughness)"`.

Top level: `format` (1, required), optional `name`, `author`,
`description`, `version`, `imported` (`"<plugin> <version>"`, written by the
importer; delete it on taking ownership), `meta` (kept verbatim), `keys`,
`priority`, `clock`, `signals`, `curves`, `sources`, `masks`, `outputs`,
`shell`, `variants`.

| Signal kind | parameters |
|---|---|
| `constant` | a number, `[x, y]` or `[r, g, b]` |
| `pulse` | `base`, `amplitude`, `period`, `phase`, `waveform` sine/triangle/square/saw; phase integrated per tick |
| `ramp` | `from`, `to`, `seconds` since apply |
| `efsh` | `field` fillAlpha/fillColor/edgeAlpha/edgeColor/scroll (a vec2), `record` |
| `av` | `"Health"`, or `{"of", "measure": current/base/permanent/temporaryModifier/damage/max}` |
| `actorState` | inCombat/sneaking/weaponDrawn/hostileDistance |
| `enchantment` | magnitude/cost |
| `trigger` | one of `event` (id glob, or `{"id", "filter": {node, arg, value: [min, max]}, "at"}`), `plugin` (id), `when` (`@scalar`, with `value` to sample); plus `lifetime`, `max`. Value: age of the newest live firing over its lifetime, 1 when none |
| `payload` | `trigger`, `field` value/position/normal of the newest firing |
| `counter`, `accumulate` | `trigger`, `reset`/`cap`; `trigger`, `decay` |
| `noise` | `frequency`, `amplitude`, `seed` |
| `gradient` | `t`, `stops: [{at, color}]` |
| `delta`, `smooth` | `"@signal"`; `{"of", "seconds"}` |
| `expr` | an expression |

Built-in event ids are `anim.<graph event>` and `equip`; every other id
(`hit.received`, `hit.dealt`, ...) comes from a provider plugin, two of
which ship with the plugin (vanilla hits, Precision), arriving in phase 3.

| Source kind | parameters |
|---|---|
| `image` | `path` under `Data/Textures`, `channel` rgb/r/g/b/a/luma, `space` tiled/mesh, `scroll`, `tile`, `mirror: [u, v]`, `transpose`, `mip` |
| `material` | diffuseRgb/diffuseLuma/normalSlope/roughness/metallic/occlusion/reflectance/displacement/relief |
| `bake` | `"position"` (one frame for every piece), `"localPosition"` (this geometry's bound), `"worldUp"`, `{"partition": "body"}` (or a slot number), `{"boneWeight": [bones]}`, `"componentId"` and `"chartId"` (the mesh analysis' id map: connected pieces or UV charts, each texel the region id / 255) |
| `uv` | u/v |
| `distance` | a node name, or `{"from": [x, y, z]}`: static bind-pose distance |
| `ripple` | `trigger`, `speed`, `width`, `decay`, `shape` ring/disc: a front per live firing |
| `materialClusters` | `clusters` 1..8 (default 4), `weights` `{roughness, metallic, occlusion, reflectance, luma}` (defaults 1, 1, 0.5, 0.5, 1), `seed` (default 1), `iterations` 1..256 (default 32): the material's cluster map, each texel the id / 255 of its nearest cluster |

Outputs: `{"target": "material" | "shell", "slot": ..., <slot scalars>,
"selector"?, "replace"?, "stack": [layers]}` with slots diffuse, emissive
(`strength`), rmaos, normal, height (`scale`), fuzz (`color`, `weight`),
glint, coat (`roughness`, `level`), subsurface (`color`, `thickness`); or
`{"target": "light", "bones": {"skinned": {max, minShare}} | {"named":
[...]}, "offset", "color", "intensity", "size", "cutoff", "shadow", "bulb"?}`.
A layer is `{"source": "@name" | [r, g, b], "curve"?, "blend"
replace/multiply/add/subtract/screen/lerp/normal, "opacity", "color"?,
"mask"?, "channels"?}`. Merging: stacks on the same slot append in priority
order unless an output says `replace`; the highest-priority recipe naming a
slot scalar wins. The shell (`material` pbrCopy/vanilla, `blend`,
`depthBias`, `alphaTest`, `alpha`, `rimPower`, `emissive`, `pose` with
`inflate`, `offset`, `scale`, `scalePoint`, `spin`, `spinAxis`) is one clone
per geometry per recipe. Variants (`name`, `key: {"armor": ...} |
{"selector": [...]}`, `overrides`) replace named signals with constants.

The importer turns an EFSH into: `efsh` signals for fill and edge level
(through `rest` curves that make 1 the record's steady state), edge colour
and scroll; a constant `glowHue` from the colour policy; constants for the
proof-of-concept knobs and expressions over them; the fill texture as four
fields; `relief` and `metallic` material sources; the `metal` mask; emissive
and fuzz on a PBR-copy additive shell, height and rmaos on the material,
one worn-bones light. Records whose colour comes from a palette (fire)
import with a white glow.

## In-game menu

With SKSE Menu Framework installed, the mod control panel gets a
**Worn Enchantment PBR** section: the recipe studio and two support
pages. The support pages start with the status line (emissive path,
layout check, lab, counts, tick interval, recipe files); the Studio
starts with its mode bar and holds the selection (actor and piece, then
recipe within it), Freeze with a time scrub (freezing holds the current
moment; the slider moves it), and Isolate this recipe in its own tables,
and the other pages show the selection the Studio made. An edit keeps
the recipe's clock where it was. The page holds its selection as keys
(the piece, the recipe id, the geometry name, the target and slot, the
region name; only the layer is an index, clamped each frame), and no
widget writes it: widgets return intents, the page collects them while
drawing and applies them after the frame through one reducer and the
manager, so nothing drawn in a frame outlives the state it came from.
Every edit is one step of a per-recipe history: **Undo** and **Redo** on
the recipe row (Ctrl+Z and Ctrl+Y while no field has the keyboard)
restore whole recipes through the same retire-and-reapply path as an
edit; Revert clears the history and Save keeps it. The design behind the studio is the
Design section of `plans/worn-enchantment-pbr-menu-brief.md`; the modules
are in `ARCHITECTURE.md` under "The recipe studio".

- **Studio**: a mode bar (Compose, Paint, Design) over one set of
  view models, in one narrow column of collapsible sections so the game
  stays in view. Compose starts with two labelled rows. The recipe row:
  **S**, the recipe applied alone (isolate); the **selection** (actor
  and piece); and the **recipe** within it. The edit row: **S**, solo for the
  picked output; the **target** (material, shell or the light, the
  format's word for where an output goes); the **slot** on it with its
  state; the **region** lens (one of the recipe's masks; the stack
  filters to layers it masks and Add layer binds it first); and at the
  far right **Clear**, which
  removes the picked output (a slot's, or the light) so the slot reads
  empty again. The rule under the rows names what the stack pane shows,
  one at a time: the picked slot's stack, or the target's settings. At
  the rule's right edge one button switches to the other, and reads
  **settings** while the stack is shown and **stack** while the
  settings are; the material has no settings and the light no stack, so
  the button is greyed on those. While the settings are shown a
  **Clear** beside it puts them back to the format's defaults (the
  light's placement, its selector and replace, stays). The settings are
  dealt into two field tables side by side so the pane shows them
  without scrolling. The light's settings are the
  **light panel**: colour, intensity, size, cutoff,
  offset, shadow, the bones as a kind (skinned with max and minShare, or
  named with a comma-separated list), each a field like the row fields;
  a recipe without a light offers Add light. The shell's are the
  **shell settings**: material kind (a
  kind change is refused while a shell output writes a slot the new
  kind lacks), blend, depth bias, alpha test, alpha, rim power,
  emissive, and the pose (inflate, offset, scale and its point, spin and
  its axis). When the
  piece has several geometries, the stack's composite is viewed on one of
  them (its tooltip names the geometry) and clicking it views the next;
  edits reach every geometry. A
  footer pinned to the bottom of the page holds the clock: Freeze, a
  **step** button (one tick of the recipe clock, held), a **speed**
  slider that multiplies every recipe's clock, and the scrubber across
  the remaining width. The scrubber follows the
  clock while it runs, shown within the current minute (the clock itself
  runs on, since recipes read `time`); grabbing it freezes at that
  moment, and dragging either way moves the recipe within that minute; unfreezing resumes from the
  scrub (a backward scrub rebuilds the
  signal state and advances it to the moment in one step, so pulses and
  smoothed signals land where a straight run would leave them). An
  empty slot offers Add output; an excluded slot says why. Below it the
  **stack**: the composite beside the slot scalars, then the layers
  in application order, the base first and the last applied at the
  bottom, as a table with a column per
  element: index, remove, drag grip, solo, mute (the same two wherever
  something can be soloed or muted; outputs and the light have S alone),
  the source's type badge, the blend combo, and the source; other recipes' layers on
  the same slot appear greyed above and below in merge order; an empty
  stack shows Add layer alone. Clicking
  the grip or the name (or picking a slot, which selects its top layer,
  or adding a layer, which selects the new one) opens that layer's
  fields beside the layer table, behind a draggable vertical rule, as a
  second table: name, details, value, one row each for source, curve,
  opacity, colour, mask and channels, with the layer's thumbnail under
  them when it has one. A value that may be
  a literal or a signal is one control: its type badge on the left is a
  button that switches the input between a text field and a combo over
  the signals of that type, and gives it the keyboard; a value that
  reads `@name` opens as the combo. The slot scalars use the same
  control. A `...` button beside the type opens that field's
  details in a modal: the source's thumbnail and definition (editable
  when it is a mask), the declared curve's expression, the signal an
  opacity or colour names with its editor, the mask's thumbnail and
  expression. The slot scalars use the same table. Every typed input
  carries a badge at its left edge, a square the height of the field
  with a glyph in the kind's colour: `#` for a scalar, `c` for a colour,
  `v` for a vector, `@` for a reference, `=` for an expression, `x` for a
  curve, `m` for a mask, `ch` for channels. A badge is filled with its
  colour, dark glyph on a coloured square, when a `@signal` may stand
  in for the value (scalar, colour, vector); it is outlined, coloured
  glyph on a dark square, when the value is literal.
  A filled badge is the button that toggles its field between text and
  the signal combo. The full rule, including what is coerced, is the badge's
  tooltip; one number typed into a colour field, a layer's source
  included, stands for all three components. A colour field's text
  starts with a swatch showing the colour it names; clicking it opens a
  colour picker, and a pick lands in the field as `r, g, b` on release.
  The two context rows stay
  in view; under them two panes scroll on their own, separated by
  rules, the stack taking the larger share: the **stack**, and
  **Resources**, a tab per table (Signals, Curves, Sources, Masks). The
  **signal table**: remove (greyed while anything references the row,
  with the count in its tooltip), signal (its kind in the tooltip), edit
  (a constant's number or colour as a literal field, an expression as
  text, the same inputs as the inspector; the kind follows what is
  typed: a number makes a scalar constant, three numbers a colour
  constant, anything else that parses an expression; a row of another
  kind reads "edits in the file"), curve (a combo over the declared
  curves, or none), "=", and the
  live value with its swatch, or "inert" with the reason (a trigger
  row with an event id carries **Fire**, which posts one firing of that
  event for the piece's wearer); and the
  **curves** table (remove, name and expression); the **sources** table
  (remove, name, a `...` button opening the source's form in a modal:
  the kind first, then the kind's own settings, an image's path,
  channel, space, scroll, tile, mirrors, transpose and mip, a bake's
  kind with its partition or bones, and so on; and the definition as the
  file describes it); and the **masks** table (remove, name, expression).
  The Resources rule carries **Add** for the open tab (a constant 0 named
  `signal`, `signal2`, ...; a curve `x` named `curve`, ...; a material
  source named `source`, ...; a mask `1` named `mask`, ...) and that
  tab's name filter at its right edge. Names in every table are fields:
  committing another name renames the row and repoints every reference
  to it, in parameters, variant overrides and expressions; remove is
  greyed while anything references the row. **Validation before
  apply**: every typed field is checked as it is typed, with the file's
  own parsers and against the recipe's names and types, and reads red
  with the message under it until it would be accepted; a failing text
  is not committed. **Creation in place**: a reference combo ends with
  what the field can make, "new image", "new bake", "new mask", "new
  curve", "new constant", "new expression", and "promote to signal",
  which turns the literal the field holds into a constant named after
  the field; picking one creates the row, named after its kind and made
  unique, and binds the field to it. A `...` button beside any value
  field that names a `@signal` opens that signal in a modal: its editor,
  its curve, and the signals its expression reads as buttons that open
  their own modals, to any depth. The
  recipe row's **New** opens a popup that makes an empty recipe under
  the id typed, keyed to one of what the piece carries (its magic
  effect, enchantment or effect shader, the armor, or a keyword), listed
  as `user/<id>.json` until saved; a recipe stays on its piece with an
  empty board while it has no outputs. **Fire** on a trigger row opens a
  popup for the firing's payload: a node from the shape's bones, an
  offset from it in world units, a random scatter radius, and the value.
- **Paint mode** builds a region as a stack of terms, the way Compose
  builds an output as a stack of layers, and previews it through the
  ordinary apply path: opening Paint clones the selected recipe into a
  transient **paint recipe** (its outputs replaced by one emissive
  output whose single white layer is masked by the **scratch** mask,
  keyed to the worn armor, never written, gone when Paint closes),
  applied alone while the session runs, so the armor shows the region
  the way any recipe's emissive shows. Opening Paint also reads the
  viewed geometry's mesh, which is analysed as it is read (its connected
  parts and UV charts, each with the bone that carries it and its share
  of the mesh), and samples and clusters the material's maps, once per
  session for each. The head line holds the recipe combo, which
  keeps naming the recipe being painted for, with **preview on**,
  material or shell, at its right edge: where the paint recipe's
  emissive goes. The pane holds the term table across its width, then
  **Keep** and **Discard**, then, under a rule that carries a filter,
  what the piece can be shown to have as tables in collapsible sections:
  parts, materials, bones, partitions, channels, presets, masks and
  sources, each row with its measurements, greyed with the reason when
  the piece cannot make it; choosing a row adds it as a term. The where
  presets are gone; the shipped `regions.json` keeps only the what
  presets (material thresholds: leather, polished and rough metal,
  cloth, engravings, raised, dark, bright). A term's settings are its
  data and its expression is
  derived: a part is the mesh's `componentId` (or `chartId`) bake tested
  for the id, a material is the `materialClusters` source tested for the
  id, a channel is a threshold with softness, posterize and invert, and
  a chosen term adds the sources it needs, reusing an existing source
  with the same definition. A term's row shows its index, remove, the
  grip to reorder, **S** shows one term alone, **M** leaves one out, the
  **op** (the first term sets the region, each later one is **and**, the
  product, **or**, the maximum, or **not**, the product with the
  complement), its label, its measurements, and a details button whose
  modal holds its settings as fields, its expression text (editing the
  text makes the term raw), and each source or mask it reads as a row
  whose own detail button opens its picture. The stack is page state; its built
  expression is written into the paint recipe's scratch after every
  change. Under the split, **Keep** writes every term (mutes lifted)
  under a proposed or typed name into the recipe being painted for, with
  the sources the expression reads copied across, as one undoable step,
  and ends the session; **Discard** ends it without keeping. On the
  Masks tab, **edit** loads a kept mask into the stack with its terms
  and their settings recovered from the text; any other text comes back
  as one raw term. Leaving Paint mode discards.
- **Recipes**: every loaded file with keys, row counts, state and path;
  the selection's resolved recipes in merge order; the **board**, the
  grid of every slot on the material and the shell for the selected
  recipe (a written cell shows its composite, layer count, masks and a
  solo toggle, with the binding's maps and the scalars in a tooltip;
  clicking a cell picks it in the Studio; an empty cell's "+" adds an
  output; excluded and refused cells say why); Save and Revert for
  the selected recipe with an "edited, not saved" marker; Reload,
  Re-apply all, Retire all; the selected recipe's row problems.
- **Setup**: the save bar (**Save INI**, **Reload INI**, **Re-apply**,
  the **auto** re-apply toggle, "unsaved changes" and "re-apply needed"
  markers); under it every setting as a name and value table, switches
  as checkboxes in their value cell; and under a rule, in the bottom half
  of the page, the **Log**: a filter, auto-scroll and Verbose logging on
  one row, then the last 300 log lines with warnings and errors in amber.
  Settings widgets are generated from the settings table, so a new row
  appears in the menu without menu code.

The framework SDK header is vendored under `src/extern/` (GPL-3.0, see
`SOURCE.txt` there) and resolves everything through `GetProcAddress`, so the
plugin runs without the framework and only logs that no menu is available.

## INI

`Data/SKSE/Plugins/WornEnchantmentPBR.ini`, read at `kDataLoaded`:

| Key | Default | Range |
|---|---|---|
| PlayerOnly | false | |
| EnableShaders | true | |
| ThirdPerson / FirstPerson | true | |
| UniqueMaterial | true | per-clone material copy before writing |
| MaxEffectsPerActor | 32 | 1..256 |
| AnimationFPS | 60 | 15..60 |
| AnimationSpeed | 1.0 | 0.05..4.0 |
| Intensity | 1.0 | 0.1..2.0 |
| NormalizeBrightness | true | same base level for every record |
| EmissiveStrength | 1.0 | 0..20, normalised mode |
| EmissiveScale | 20.0 | 0..100, raw mode only |
| VerboseLogging | true | |
| [Colors] TintWithEdge | true | hue from the EFSH edge colour |
| [Colors] BlackFillAsWhite | true | black fill keys become white before tinting |
| [Colors] `<form key>` | | `r,g,b` override, 0..1 or 0..255 |
| [Runtime] RuntimeTextures / RuntimeTextureSize | true / 512 | GPU layer generation |
| [Layers] GlowMask / GlowMaskChannel | true / 1 metallic | glow multiplied per texel by one RMAOS channel, per geometry |
| [Layers] GlowMaskThreshold / GlowMaskSoftness / GlowMaskInvert / GlowMaskStrength | 0 / 0.1 / false / 1.0 | mask shaping |
| [Layers] SheenMap / SheenMapMirror / SheenMapPhase | true / true / 0.5 | scrolling fuzz map |
| [Layers] Shimmer / ShimmerScale / ShimmerTranspose / ShimmerPhase | true / 0.1 / true / 0.25 | scrolling parallax |
| [Layers] ShimmerArmorWeight / ShimmerNoiseWeight | 1.0 / 0.35 | armor relief vs noise in the height field |
| [Layers] GlossMap / GlossMapMirror / GlossMapTranspose / GlossMapPhase | true / false / false / 0 | per-texel gloss from the armor's RMAOS |
| [Layers] GlossContrast / GlossMapSize | 2.0 / 1024 | patch sharpness; resolution cap of the re-rendered RMAOS |
| [Layers] Sheen / SheenScale | true / 0.5 | fuzz layer from the edge effect |
| [Layers] GlossBoost | 0.25 | 0..1 roughness reduction |
| [Layers] Glint* | off | sparkle parameters (see INI comments) |
| DebugSolidGlow | false | diagnostic: solid white emissive at EmissiveScale on every driven geometry |
| [Outputs] Light / LightIntensity / LightRadius | true / 1.0 / 300 | point lights on the wearer (spike; third person only) |
| [Outputs] LightBone / LightMaxPerEffect / LightUseBound | 0 worn bones / 2 / true | bones from the item's skin weights, or a fixed bone |
| [Outputs] Shell / ShellMode / ShellDepthBias | true / 0 rim / true | shell on, rim shell or the layers on the shell, decal depth bias |
| [Outputs] ShellBlend / ShellDiffuse | 0 additive / 0 white | rim-lit vanilla shell clone over each driven geometry (spike) |
| [Outputs] ShellAlpha / ShellRimPower / ShellEmissive | 0.5 / 4.0 / 0.5 | shell material alpha, rim exponent, emissive multiplier |
| [Outputs] ShellScale / ShellScalePulse | 1.0 / 1.0 | inflation percent, rest and per pulse level |
| [Outputs] ShellScaleAlong / ShellScaleAcrossY / ShellScaleAcrossZ | 0 / 1 / 1 | per-axis weights in bone space (X along the bone) |

`NormalizeBrightness` (default on) makes every record glow at
`EmissiveStrength` for a texture of mean luminance 0.5: the hue still comes
from the record, its pulse and fade-in still shape it, but fill brightness,
colour scale and texture content no longer do. Without it the raw values
apply and black-fill records (stamina, frost) come out about ten times
brighter than grey-fill ones. The baker writes each folder's mean luminance
to `meta.ini`; folders without it are treated as 0.5.

`EmissiveScale` (raw mode only) defaults to 20 because the vanilla armor enchant shaders
(`EnchArmor*FXS`) are additive membranes with fill alpha 0.05..0.10 and grey
fill colours; mapped one to one into emissive they are invisible, which is
also why vanilla armor enchant glow is barely visible in the first place.

## Tests

Five host-independent executables, built as Windows console exes by the
normal build (`build/Release/WornEnchantmentPBR{,Settings,Recipe,Signal,Importer}Tests.exe`)
and natively by `tests/run-native.sh` (uses `clang++` when present, else
`g++`; set `CXX` to choose). Each prints `all N ... checks passed` and exits 0.

```sh
tests/run-native.sh              # all five
tests/run-native.sh --update     # also rewrites tests/fixtures/recipes after an intended importer change
```

- timing: colour key interpolation at and between key boundaries and wrap,
  scale lerp, pulse at zero and non-zero amplitude, persistent-ratio
  fallback, fade-in, intensity clamp, UV wrap, frame selection.
- settings: table uniqueness, serialise/parse round trips of defaults and
  edited values including colour overrides, clamps, case and comment handling.
- recipe: form keys, recipe keys and selectors; globs; the mask grammar
  (every curve and combiner, parse errors, the eight-term limit, format then
  parse identity); a JSON round trip of a recipe holding one of every record
  type, byte-identical on the second serialisation; row-level errors
  (missing fields, unknown references, cycles, blend/slot rules, exclusive
  slots, mask cycles, variant types); resolution order with all key kinds,
  shared keys, unenchanted pieces and explicit priorities; variants; static
  versus animated classification of signals, sources, masks and stacks.
- signal: expression precedence, functions and errors; dependency order,
  cycle and unknown-reference reporting, inert propagation; every curve;
  phase continuity across a period change; trigger ageing, payloads and the
  live cap; counter cap and reset; accumulate decay; gradients at, between
  and past stops; lerp typing; efsh signals against the timing port; noise
  range and determinism.
- importer: the six vanilla `EnchArmor*FXS` records (`tests/fixtures/efsh`,
  dumped from Skyrim.esm by `tools/efsh_dump.py`) import to recipes that
  validate, read back identical, evaluate to level 1 at rest, follow the
  default target policy, carry the hues the proof of concept logged, and
  match the checked-in `tests/fixtures/recipes/<form key>.json` byte for byte.

## In-game checklist

Set `VerboseLogging=true` for all of these.

1. **Equip a known enchanted PBR cuirass.** Expect one line per geometry:
   `apply efsh <EFSH id> armor <ARMO id> actor <actor id> 3rd geometry '<name>' texture=flipbook frames=16 material=private`
   (`texture=fallback frames=1` without a flipbook). The glow appears, pulses
   with the EFSH alpha settings, and scrolls if frames exist. Before that,
   once per session: `PBR material layout check passed` and
   `flipbook: loaded N frames for <name>`.
2. **Two NPCs in the same enchanted armor.** Only the wearer with the
   enchantment glows. If both glow with `UniqueMaterial=true`, the material
   copy did not take (look for `could not give '<geometry>' a private
   material; leaving it shared`) and that is a finding to report, not a knob
   to tune. With `UniqueMaterial=false` both glowing is expected and confirms
   materials are pooled.
3. **Reload a save, change cells, unequip and re-equip, switch first and third
   person.** Expect `kPreLoadGame` then `cleared N actor states`, then
   `kPostLoadGame` and fresh `apply` lines; on unequip
   `actor <id>: retired N effects`; after each equip a second apply about
   100 ms later (the equip finalize). No stale glow, no missing glow.
   The animation only advances while the game is unpaused, because it is
   driven from the player's per-frame update.
4. **Dirt and Blood active and the player dirty.** Dirt renders over the armor
   and the glow renders underneath at the same time. The log must show no
   `dropping '<geometry>': emissive texture replaced by another system` lines.
5. **Cast a flesh spell.** Same as 4.
6. **Verbose log** shows the per-geometry line from step 1 with EFSH form ID,
   armor form ID, perspective (`3rd`/`1st`) and `texture=flipbook|fallback`.
   Non-PBR armor logs once: `armor <id> has non-PBR geometry; those parts are
   left alone` and nothing is bound on it.

Anything that starts with `restore skipped:` means another system took over
the material or the emissive slot and the plugin deliberately did not restore.

Phase 2 stage 2 checkpoint (every slot of the imported recipe is bound;
animation events reach triggers):

14. **Equip the enchanted cuirass.** The apply line now reads `outputs:
    emissive->shell (animated), fuzz->shell (animated), height->material
    (animated), rmaos->material (animated)` with no `[...]` problem; the
    sheen (coloured rim from fuzz), the parallax shimmer (height) and the
    gloss pulse (rmaos roughness dip on the metal) match the proof of
    concept. No `mask '@metal'` warnings.
15. **Unequip**: every slot restores. The Material page (menu) lists each
    written slot with its original and written texture names; after retire
    the log has no `restore skipped`.
16. **Layer curves** (`@crisp` on the relief, `@punchy` on the gloss field)
    render: no `layer curves need the interpreter pass` warning, and the
    relief looks crisper than the raw occlusion on the Compose page's
    composite thumbnail.
17. **Editing** (menu stage 3): on the Signals page, drag `fillLevel`'s
    base or type a new expression for `glowLevel` and press Enter: the
    log shows `retired`, then the piece re-applies with the new value,
    and the Recipes page marks the recipe `edited, not saved`. Save writes
    `Data/WornEnchantmentPBR/user/<id>.json` (the imported file stays),
    logs `recipe <id> saved to ...`, and the next load logs `<user path>
    replaces <imported path>`. Revert to file discards the edits. A bad
    expression logs `recipe <id> signal <name>: ...` and the row shows
    inert; nothing crashes.
18. **Expression masks** (phase 3, the interpreter pass): at start the log
    has no `TextureLab: PSProgram compile failed` line. On the Sources and
    masks page, change the imported recipe's `metal` mask from `@metallic`
    to `@metallic * (0.5 + 0.5 * sin(time * 3))` and press Enter: the glow
    on the metal parts pulses independently of the fill; the mask's
    thumbnail shows the rendered mask and marks it animated. Then set it to
    `if(@relief > 0.5, @metallic, 0)`: the glow keeps only the raised
    metal. A bad expression (`@nothing * 2`) shows its problem on the row
    and the glow falls back to white (unmasked). On the Compose page, set
    the emissive layer's source to `@metal`: the mask itself becomes the
    layer. Every one of these is a menu edit; no file changes until Save.
19. **Geometry bakes** (phase 3): with `examples/bake-showcase.json`
    shipped beside the imported recipes (keyed by the `ArmorCuirass`
    keyword, since a key belongs to the last file loaded with it and the
    magicka key is the imported recipe's), the cuirass's own material glows
    with a position gradient on its body partition, brighter on upward
    faces, lifted where the spine carries the mesh. The log has one `mesh
    '<geometry>': N partition(s), V vertices, T triangles, cpu copy|gpu
    readback` line per geometry and no `bake` warnings. On the Sources and
    masks page for the showcase recipe, the `position` thumbnail shows the
    mesh laid out in UV space as a colour gradient (one frame for every
    geometry: red grows to the actor's right, green forward, blue upward,
    so the chest and the pauldrons agree), `up` as a grey field,
    `body` as white on the body partition's islands, `spine` as a soft
    weight map. The log names any recipe whose key a later file owns
    (`key ... is owned by recipe ...`). Delete the showcase file afterwards
    (or keep it as a reference); it stacks on the magicka recipe.
20. **Distance and ripples** (phase 3): the showcase adds a `fromSpine`
    distance source and two ripples. On equip a ring spreads from the
    piece's centre over about two seconds; a hit taken spreads a faster
    ring from the piece's centre (the vanilla hit event has no position).
    The Sources page shows `fromSpine` as a radial gradient centred on the
    spine and the ring sources as moving bands while they live. A soft
    grey lift sits within about 60 units of the spine.
21. **Parameter slots** (phase 3): the showcase adds a blue clearcoat on
    the body partition (coat map colour and strength from the stack,
    roughness 0.15, level 0.6) and glints on the whole cuirass. The iron
    reads as lacquered blue where the body partition is, with sparkling
    microfacets under a moving light; the Material page lists the `coat`
    slot as written. Fuzz and glint exclude each other, as do coat and
    subsurface; a recipe binding both gets the later one's row marked
    with the reason.
22. **Footfalls**: `examples/step-ripples.json` (keyed by `ArmorBoots`)
    sends a ring out from each foot over the boots at every footfall,
    faster when sprinting; on the Signals page `stepLeft` and `stepRight`
    rise to 1 on each step and fall over a second. The built-in `equip`
    fires once when the piece is put on. Passed 2026-09-05.

Menu studio stage 1 checkpoint (board, stack, inspector, region lens,
modes; the runtime is untouched except layer solo and mute):

23. **The studio.** With the magicka cuirass worn, open the menu: the
    section lists Studio, Recipes, Setup, and the log (under Setup) has
    `SKSE Menu Framework pages registered`. On Recipes the board shows four written
    cells (emissive and fuzz on the shell, height and rmaos on the
    material), the light row, and "+" on every other cell the surfaces
    offer; fuzz's row greys glint on the shell with the reason. Click
    emissive on the shell, then open Studio: the picker reads shell and
    emissive, and the stack shows `fill`, `ring` and `stepRing` in that
    order, with the composite above them. Tick solo on
    that cell: the armor shows the glow alone (sheen, relief and gloss
    gone) and the header reads "isolating ... output 0"; untick to
    return; the log shows a retire and re-apply when the isolated recipe
    changes, since an isolated recipe is applied alone (a lower-priority
    recipe whose outputs another recipe replaced comes back whole while
    isolated). Mute the `fill` row: the glow field disappears and only the
    hit rings remain; unmute. Choose region `@metal`: the stack shows one
    of three layers in the region and the other two dimmed. Drag the
    `ring` row's grip onto `fill`: the order changes on the armor and in
    the file order shown, with no `edit refused` line in the log. Select
    the `fill` row: the inspector shows the fill's
    thumbnail and definition, `@metal` with its expression, and
    `glowHue` editable beside its swatch; in the signal table under
    the stack and drag `glowStrength`: the glow follows. Recipes page: the recipe reads
    "edited, not saved"; Revert restores it.
24. **Conjuration breath** (`examples/conjuration-breath.json`, keyed by
    the `EnchFortifyConjurationConstantSelf` magic effect, priority 60):
    on a Fortify Conjuration piece the log shows `recipe
    conjuration-breath by magicEffect:EnchFortifyConjurationConstantSelf
    (priority 60)` and the imported shader recipe for the enchantment
    binds nothing (every slot is replaced). The metal carries a dim
    violet glow that breathes over seven seconds: on the exhale it blooms
    from the chest outward, the upward polished faces catch a lilac edge
    and the light swells; on the inhale the chest sinks in (parallax on
    the material's height) and the glow retreats into the grooves. The
    leather and cloth stay dark. Solo the height cell to see the sink
    alone.
27. **Creation in place, validation, regions** (menu stage 3). With the
    magicka cuirass worn, press New, type `scratchpad`, choose the
    enchantment key, Create: the recipe combo shows `scratchpad` with an
    empty board. Pick emissive on the shell, Add output, Add layer:
    open the layer's source combo and choose "new material": a source
    named `material` appears in the Sources tab and the layer reads
    `@material`; open its `...` and set the channel to `metallic`. In
    the opacity field type `@nothing`: the field reads red with "'@nothing'
    is not a signal" and Enter does not apply it; choose "promote to
    signal" instead and a constant `opacity` appears in the Signals tab.
    Switch to Paint: the context rows are gone, the head line names the
    recipe, the log reads `paint: previewing <recipe> on the material
    through the paint recipe, keyed by armor:...`, and the armor goes
    dark (the paint recipe is applied alone; its scratch is `0`). Press
    the lit "read mesh" button; choose kind `where`, member `chest`: the
    term table shows `0 set chest @partition * @bones` and the chest
    glows white; kind `what`, member `leather`: a second row `1 and
    leather ...` and the glow shrinks to the leather on the chest. Tick S
    on the leather row: the whole piece's leather glows; untick. Set the
    leather row's op to `not`: the chest glows where it is not leather;
    set it back. Preview on `shell`: the same region glows on the shell
    over the piece. Press Keep: the popup proposes `chestLeather`; Keep
    as it: the log reads `keep: region chestLeather written into
    <recipe> (N edit(s))`, the recipe re-applies with its own outputs,
    and the Masks tab lists `chestLeather` as `(@partition * @bones) *
    ((1 - @metallic) * smoothstep(0.35, 0.6, @roughness))` beside the
    new `partition`, `bones` and `roughness` sources. Undo once: the
    mask and the sources are gone; Redo. Press edit on that row: Paint
    opens with the two terms back and the rule reads `Region:
    chestLeather`; Discard leaves the mask as it was. Back in Compose,
    the layer's mask combo offers it. On the magicka recipe,
    press the `...` beside the strength scalar: a modal opens on
    `@glowLevel` with its expression and a `@fillLevel` button that opens
    that signal's modal over it. On `struck`, press Fire, choose `NPC
    Head [Head]` and Fire now: the ring runs out from the head.
26. **State model, undo, panels, the clock** (menu stage 2). With the
    magicka cuirass worn: pick emissive on the shell, select the `ring`
    row, set its opacity to 0.5, press Undo: the opacity reads `@struck`
    again and the armor's rings return; Redo puts 0.5 back; Ctrl+Z with no
    field focused undoes again. Remove a layer, then Undo: it is back at
    its index and selected. Choose the light target: the panel lists the
    light's fields; set size to 3 and the glow's reach grows; set bones
    to named and type `NPC Head [Head]`: the light moves to the head. Pick
    the shell target: the settings sit above the stack; set inflate to
    `0, 0.05, 0.05` and the shell swells; set material to vanilla: the
    edit is refused in the log while fuzz is on the shell, and accepted
    once the fuzz output is cleared. On the Timeline drag speed to 0.25:
    the breath slows; press step: the clock holds and advances one tick
    per press. In the signal table press Fire on `struck`: a ring runs
    out from the chest. Every step is one log line and one re-apply;
    nothing in the studio goes stale after an edit.
25. **Masked height on a flat-displacement armor.** On a piece whose
    `relief` thumbnail shows occlusion (its displacement map is black,
    NOTES 46), add a height output with one masked layer and a constant
    `scale`, then solo it: the parallax shift appears only under the
    mask, and the rest of the piece is still. Animating `scale` still
    shimmers the whole masked area at once; animate the layer's opacity
    to keep it in the mask. The log has no `neutral height base` warning.

Phase 2 stage 1 checkpoint (recipes drive the emissive output, the shell and
the lights; the proof-of-concept layers no longer run):

10. **Equip an enchanted PBR piece.** Per piece: `armor <id> (<name>) 3rd
    actor <id>: recipe <id> by effectShader:<record> (priority 40)`, then
    one `apply recipe <id> ... geometry '<name>' material=untouched shell
    (PBR copy, additive, skin cloned, buffers shared, ...) outputs:
    emissive->shell (animated), fuzz->shell [slot 'fuzz' is bound in phase
    2 stage 2], height->material [...], rmaos->material [...]` line per
    geometry, and `recipe <id>: light on <bone> x1.00, <bone> x0.xx`. The
    glow looks like the proof of concept's: the fill texture scrolling on
    the metal parts, pulsing, tinted by the record's edge colour, on an
    additive shell that breathes by 1 to 2 percent, with the worn-bone
    lights following the pulse. Sheen, shimmer and gloss are absent until
    stage 2.
11. **Unequip, re-equip, reload, first and third person**: `actor <id>:
    retired N recipe(s)`, no `restore skipped`, no stale shell.
12. **The menu** (Recipes, Signals, Setup, Log): the header names the
    selection and its resolved recipes in merge order; the Signals page
    shows every signal of the selected recipe with its live value
    (`fillLevel` around 1, `scroll` advancing, `glowHue` as a swatch);
    Freeze and scrub stop and move the animation; Isolate silences every
    other recipe.
13. **`WaterBreathingFXS`** on a water-breathing piece: a flat glow in the
    record's hue, no field, no warning line about empty paths.

Phase 1 checkpoint (recipe store, no visual change):

7. **Start the game once.** After `kDataLoaded` the log shows one
   `imported recipe <id> for efsh 00092Dxx (Effects\...) -> ...: reads back
   identical` line per armor enchantment shader (six for vanilla, more with
   mods), then `recipes: N loaded, 0 with errors, U unresolved editor IDs, N
   imported this session, folder <path>`, preceded by `recipes: K editor
   IDs indexed (po3's Tweaks answers the rest)`. With Tweaks the ids are
   editor IDs (`EnchArmorMagickaFXS`) and U is 0; without it they are form
   keys (`Skyrim-92DED`). A record with no fill texture (WaterBreathingFXS)
   imports as a flat glow with no field sources. Any `READ-BACK MISMATCH` or
   `recipe ...: ... :` error line is a finding.
8. **Compare a written file with the fixture.** Under MO2 the files land
   where SKSE output is routed (here `mods/SKSE Output/`) at
   `WornEnchantmentPBR/imported/`.
   `EnchArmorMagickaFXS.json` there must be identical to
   `tests/fixtures/recipes/EnchArmorMagickaFXS.json` (the fixture is the
   Tweaks case; without Tweaks only the `name`, the key and the `efsh`
   records differ, spelled as form keys). Any other difference means the
   game's record differs from the ESM dump (tile, colour scale or flags
   first).
9. **Start again.** The second run logs `N loaded, 0 with errors, 0
   unresolved editor IDs, 0 imported` and one `recipe <id> loaded from ...`
   line per file, each ending `imported, not yet edited`. Files from the
   phase 1 shape under `SKSE/Plugins/WornEnchantmentPBR/recipes/` in the
   overwrite folder are no longer read and can be deleted.
