# Arcane Circuit + Resonant Ward + Winterglass demos

Three stacking examples for the current BetterEnchantmentEffects build. They use a
separate copy of vanilla Dwarven armor, the existing armor-addon models, and
your installed TruePBR materials. The user has confirmed PBR compatibility;
metal masking, seams and brightness still need an in-game pass.

## Build the fixture

Inside `nix develop`, run:

```sh
python3 tools/demo-plugin.py --master '/path/to/Skyrim.esm' --output build/demo/ArcaneCircuit
```

The writer creates `BetterEnchantmentEffectsDemo.esp` and a `records.json`
provenance report. Copy `arcane-circuit.json`, `resonant-ward.json` and `winterglass.json` into the archive's
`SKSE/Plugins/BetterEnchantmentEffects/recipes/examples/` directory.
The ESP requires Skyrim.esm and adds four records, with no vanilla overrides:

| Local ID | Type | Purpose |
|---|---|---|
| 800 | KYWD | Demo collection requirement |
| 801 | MGEF | Custom Fortify Magicka effect, copied from vanilla |
| 802 | ENCH | +100 Health first; custom +25 Magicka second |
| 803 | ARMO | Arcane Circuit - Dwarven Armor |

The recipe requires both the collection keyword and the custom effect. It
uses plugin-qualified forms, so no editor-ID lookup dependency is needed.
The enchantment's selected secondary effect controls strength: magnitude 25
means nominal power 1. The larger Health effect is intentionally first, with
higher vanilla base cost, so costliest-only selection is visibly incorrect.
Confirm the effective ordering and values in game; file data alone cannot
establish runtime cost calculations.

Record layout basis: [xEdit's Skyrim definitions](https://github.com/TES5Edit/TES5Edit/blob/dev-4.1.5/Core/wbDefinitionsTES5.pas).
The writer retains vanilla model, race, armor-stat and keyword data, converts
copied localized text fields to inline strings, and references the existing
armor addon. It does not distribute meshes or textures.

## Install and try Arcane Circuit

The corrected local test ZIP includes the complete framework runtime, including
its DLL, presenter textures, import templates, presets and default INI, plus the
demo ESP and recipe. Replace the previous Arcane Circuit test mod in MO2 with
this ZIP, enable its ESP, and launch through SKSE. If an existing framework mod
is enabled, place this test bundle after it. Preserve any customized INI when
replacing a mod; the first reported test session had no framework INI loaded.
The separate framework README and notices are included in the archive.

In the console:

```text
help "Arcane Circuit" 4
player.additem XX000803 1
```

Replace `XX` with this ESP's load-order prefix from the ARMO result. Equip
**Arcane Circuit - Dwarven Armor** from inventory.

The stock JSON files are the user's configured versions promoted from
`SKSE Output/.../recipes/user/` on 2026-09-27. They retain the saved values
and definitions exactly, including currently unused sources and signals.

Expected behavior from the configured Arcane Circuit:

1. At full magicka, a red packet travels over the luminance/roughness-selected
   material regions. Its travel period is ten seconds.
2. Spending magicka reduces the red packet and increases a blue occlusion-based
   layer. The emissive strength multiplier itself is independent of charge.
3. Height and fuzz outputs accompany the material emissive layers. Inspect them
   under grazing light; the recipe uses height scale 1.
4. The selected effect's magnitude is 25, despite the larger Health effect.
5. Ordinary Dwarven armor does not select this recipe. Check both armor models
   and seams; these configured values are the visual baseline for new tests.

The recipe resolves through magicEffect at priority 60. If selected but dark,
inspect the diffuse-luminance and roughness masks and emissive preview. The
configured file contains three material outputs, not the old emissive-only demo.

Remove the demo armor before disabling the optional demo mod. Promotion and
offline validation do not establish in-game behavior on the current pipeline.

## Build the complete local test archive

Use the generated framework manifest so required runtime assets cannot be
omitted from the demo:

```sh
python3 tools/demo-package.py --manifest build/Release/generated/package.json --fixture build/demo/ArcaneCircuit --recipe recipes/examples/arcane-circuit.json --recipe recipes/examples/resonant-ward.json --recipe recipes/examples/winterglass.json --readme recipes/examples/README.md --output build/demo/ThreeRecipes-complete-test.zip
```

The packager refuses missing inputs, includes every framework runtime and notice
entry, and verifies the ZIP against its `CONTENTS.json` hashes. The first test
archive included only the DLL and demo files; the September 25 log showed missing
presenter textures and import templates. The corrected bundle includes those
assets. A new Skyrim run remains necessary to verify rendered output.

## Resonant Ward: stacking regression

Use the same **Arcane Circuit - Dwarven Armor** (`XX000803`). The ESP and
existing FormIDs are unchanged. Resonant Ward requires both the demo collection
keyword and Skyrim's `ArmorHeavy` keyword. It uses the default stack merge behavior, with the
keyword-only default priority 20. Arcane Circuit still selects its effect at
priority 60.

The configured ward combines shell emissive, alpha, and height outputs with
one chest light. A bone-weight/metallic/luminance mask restricts the response.
The shell uses alpha blending, an inflate vector of `[0.3, 0.3, 0.3]`, and an
offset of `[0, -0.1, 0]`. The ripple starts at the chest skeleton node, not a
measured weapon impact point.

1. Equip the demo cuirass. Receive an actual attack hit and inspect the gold
   ripple, shaped shell, and chest light over the material effects.
2. Spend magicka and repeat: Arcane Circuit's red/blue balance should change
   independently of the ward's hit response.
3. Take several hits close together. At most three firings are retained, each
   with a 1.2-second lifetime. After hits stop, check that the shell and light
   settle rather than remain latched on.
4. Solo the ward to inspect its mask, alpha, height, and light; then restore
   the full stack. Unequip to confirm the shell and light disappear.

The configured light multiplier is 10.2, shell emissive strength is 12, and
ripple speed/width/decay are 80/15/0.001. These saved values are intentional
fixture inputs; their visual acceptance on the current pipeline remains open.

## Winterglass: exact armor and material stacking

Winterglass keys directly to `0x803~BetterEnchantmentEffectsDemo.esp`, with
default stack merge behavior and armor priority 30. It needs no additional item or
ESP change. Its matching is independent of the enchantment and keywords.

A five-second ramp grows a procedural frost mask over metallic surfaces. UV
crystals and original material relief vary where frost appears. The configured
recipe changes diffuse RGB at opacity 1 and roughness at opacity 0.303; it no
longer has a height output. Arcane Circuit owns material height and fuzz.

1. Equip the demo cuirass and watch the five-second frost growth.
2. Compare diffuse color and roughness with Winterglass soloed, then restore
   all recipes and verify Arcane Circuit's red/blue response and the gold ward.
3. Inspect seams and both armor models. Unequip, allow retirement, and re-equip
   to check growth again. Ordinary Dwarven armor should select none of the
   three recipes.

The native demo regression reads and round-trips all three actual recipes,
checks that they select the same piece and occupy separate output slots, and
checks exact-form exclusion independently of effect and keyword matching.
The parser, schema, fixture and package checks complement that test; they do
not validate GPU appearance. No DLL rebuild is needed for these recipe files.

## Material editor regression bundle

`ThreeRecipes-material-editing-test.zip` contains the same three examples and
ESP, with an updated framework DLL. Replace the previous bundle in MO2 while
preserving your customized INI and any locally edited recipe files.

In the terms editor, adjust a cluster setting repeatedly: superseded generated
sources should disappear from the preview recipe, while undo restores the
needed definition. Muting a term retains its dependency. Keeping over a saved
mask removes sources it abandoned only when no other recipe row uses them.
Older unrelated orphan sources are preserved.

Material offers are consolidated across geometries. A shared cluster index can
still represent different materials on different geometries; the chooser says
when its appearance varies. Diffuse now contributes RGB as well as brightness.
The new `color` weight defaults to 1; set it to 0 for the prior brightness-only
diffuse metric. Check color separation in the rendered mask and expect existing
cluster IDs to change when color weighting is enabled.
