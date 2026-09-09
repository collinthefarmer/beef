# What to delete

One thread runs through most of it. A proof-of-concept layer pipeline was
replaced by recipes on 2026-09-04 and its modules were deleted. Its
settings, shader modes, parameter records, importer defaults, flipbook
indexer, two Python tools and two README sections were not. That is one
removal, not ten, and doing it first makes every later change smaller.

## Certain

**58 of 68 settings do nothing.** `GetSettings()` has four call sites and
ten fields are read. The rest are declared in `src/SettingsCore.h:19-89`,
given a table row, parsed, written, and read by nothing: the `GlowMask`,
`SheenMap`, `Shimmer`, `GlossMap` and `Glint` groups, all of `[Outputs]`,
all of `[Colors]`, and `Intensity`/`EmissiveScale`/`NormalizeBrightness`/
`EmissiveStrength`. `ARCHITECTURE.md:386` already says so. Three of the
dead ones — `MaxEffectsPerActor`, `DebugSolidGlow`, `RuntimeTextures` —
are visible in the Setup page, so a player can toggle them and watch
nothing happen. Cascade: 58 fields, 58 table rows, ~110 ini lines,
`Settings::ColorPolicyFor`, `colorOverrides`, `FormKeyOf`,
`Timing::ColorPolicy`, three of six `SettingDesc::Widget` kinds,
`SettingDesc::items`, and `SettingDesc::page` with `Menu.cpp`'s `Shown()`
filter that exists only to hide the dead rows.

Trap: `AnimationFPS` and `AnimationSpeed` are live but sit on the hidden
page. Re-page them or they become invisible and functional.

**Half of `TextureLab::Mode`.** `kSheen`, `kHeight`, `kRoughness` and
`kMaskedGlow` are never constructed. With them go `HeightParams`,
`RoughnessParams`, `MaskParams`, their `LayerParams` members, ~20 lines
of HLSL, and `MapReading::kDisplacementR`/`kOcclusionB`/`kDiffuseLuma`
with the dead `Relief` branches those make unreachable. Trap:
`MaterialChannel::kDiffuseLuma` is a different enum, same variant name,
and live.

**`ImportDefaults`** — twenty fields, both call sites pass nothing.

**Five of six `ShellPose` fields.** Only `inflate` reaches the engine.
`offset`, `scale`, `scalePoint`, `spin` and `spinAxis` are parsed,
validated, editable and never applied — the menu offers five controls
that move nothing. ~53 sites.

**`variants`.** ~80 mentions across eight files, fully plumbed;
`ApplyVariant` is called only from tests. `schema/example-magicka.json`
uses it, so the canonical example demonstrates behaviour the plugin does
not have.

**`LightOutput::bulb`.** Seven sites, never reaches `LightBinding::Create`.
The schema documents behaviour that does not exist.

**`Timing`'s dead exports.** `FrameIndex` (a flipbook indexer; the word
"flipbook" appears zero times in `src/`), `ClampAnimationSpeed`,
`ClampIntensity`, `SegmentAmount`, `LerpColor`, `EvaluateAlpha`,
`kMinIntensity`/`kMaxIntensity`. `Timing::Evaluate`'s `a_speed` and
`a_intensity` are `1.0f, 1.0f` at its one call site.

**`EffectShaderRecord::flags`** with `kGreyscaleToColor` and
`kGreyscaleToAlpha`, which have only their own declarations. **
`Timing::EffectParams::edgeFalloff`** — three sites, no read.

**Five header functions with no caller:** `Widgets::CheckboxWidth`,
`FormKeyOf`, `RecipeDirectory`, `Widgets::LitButton`, `Widgets::SoloMute`
(with `SoloMuteChange`). Plus `Manager::EmissivePathEnabled`,
`Program::UsesMean`, `Settings::ColorPolicyFor`, `Studio::Unresolvable`.

**Fields nothing reads:** `Layout::rowThumbnail`, `Layout::designPanel`,
`Metadata::author`/`version`/`meta` (weakest — they preserve a third
party's metadata across a save, which matters once recipes circulate).

**`Mode::kDesign`** draws "Design mode is not built yet". Removing it
collapses `Layout::widgetScale`, `compositeSize` and `developerSignals`
to constants.

**`blend: "lerp"`** is byte-identical to `replace`; the shader falls
through. `REFERENCE.md:90` says so.

**The `where` region presets.** Thirteen ship in `presets/regions.json`,
parsed, never offered — `OffersOf` iterates `what` only. `README.md:557`
already says they are gone.

**Scaffolding.** `tools/__pycache__/*.pyc` are committed despite
`.gitignore`. `tools/make_flipbook.py` and `tools/bsa_extract.py` served
the deleted flipbook path. `src/History.cpp` contains exactly
`#include "History.h"` and is listed twice in `CMakeLists.txt`.
`README.md`'s frame-folder section and its console-command procedure
document features that do not exist — there is no console handling in the
plugin at all.

## Judgement calls, needing a decision

- **Format vocabulary no recipe uses.** Every recipe in the tree is
  EFSH-import shaped, so this evidence is thin. Two entries are more than
  unexercised: `ShellMaterial::kVanilla` carries its own ~50-line
  material path and the `rimPower`/`emissive` scalars that exist only for
  it; `Blend::kNormal` carries ~7 lines of reorientation maths and a
  table column that exists for one row.
- **Functional with no editing path:** `priority`, `clock.speed`,
  `output.replace`, light `selector` and `replace`, and two of three
  selector kinds. The schema and the editor are two different products
  until these are either built or cut.
- **`SlotSpec::note` and `ChannelsOf`** look like material for a tooltip
  pass that has not happened.
- **`CheckSourceKind`** duplicates `Validator::Sources`. Deleting it
  costs: a bad edit lands and shows as a diagnostic instead of being
  refused.
- **`UniqueMaterial`** is live and does change material sharing, but its
  own ini text says "Turn off only to test that behaviour."

## Looked dead, keep

Each survives a naive grep for the wrong reason.

- **`TextureLab::kProgramStack = 32`** — the bound is real and enforced
  as a bare literal twice in the HLSL string (`float3 st[32]`,
  `if (sp < 32)`). Deleting the constant leaves it nameless. Fix runs the
  other way: interpolate the constant into the shader source.
- **`PBRMaterialLayout`'s six unread fields** are a memory-layout mirror
  pinned by `offsetof` assertions. Deleting one corrupts the material.
- **`SlotScalars::thickness`** is reached through a member pointer in
  `kScalarFields`.
- **`View::FiltersLayers`/`LayerMuted`/`LayerShown`** are methods that a
  depth-tracking parser scopes as uncalled free functions.
- **`Overloaded`** is the visitor `Match` builds; `Match` has 80 callers.
- **`SignalEnvironment`** and **`SlotTarget`** have their second and third
  implementations in tests and in `ShellBinding`.
- **`MaterialChannel::kDiffuseLuma`**, **`Severity::kWarning`**,
  **`MenuState::modeDrawn`**, **`rapidcsv`**, **`tools/efsh_dump.py`**,
  and all 44 `Intent` alternatives.

## Coverage

The write-only field sweep reached 430 of 1,274 declarations — only those
whose names are unique across all structs. Twelve headers went unswept.
Two passes shared a method, so they count as one check, not two. No build,
no clang-tidy, no game run: every claim is static.
