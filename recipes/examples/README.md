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

Expected behavior:

1. At full magicka, cyan traces glow on metallic plates and a bright band moves
   through them every four seconds.
2. Spend magicka by casting. The traces dim toward violet; regeneration restores
   cyan brightness. Empty magicka retains a faint glow.
3. The selected effect's magnitude is 25, despite the larger Health effect.
4. Ordinary Dwarven armor does not select this recipe.
5. Check male/female armor and seams. The procedural lattice is an initial
   pattern; UV layout can mirror or interrupt the moving highlight.

In the editor, the recipe should resolve through magicEffect at priority 60,
with one emissive output. If absent, confirm the demo ESP, recipe file and current
framework DLL are loaded. If selected but dark, inspect the metallic source and
emissive preview before changing the pattern.

Remove the demo armor before disabling the optional demo mod. No game test is
claimed by the offline validation.

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
keyword and Skyrim's `ArmorHeavy` keyword. It uses `merge: stack`, with the
keyword-only default priority 20. Arcane Circuit still selects its effect at
priority 60.

Ward owns a shell emissive output and one chest light. Its warm gold ripple and
flare overlay the existing cyan/violet material pattern. It does not write the
material emissive multiplier, so the magicka response remains controlled by
Arcane Circuit. The ripple starts at the chest skeleton node; it is not a
measurement of the weapon's impact point. The recipe adds a visual response,
not damage reduction or a gameplay ward spell.

1. Equip the demo cuirass with both recipes active. At rest, Arcane Circuit
   continues normally; the ward shell and light are dark.
2. Receive an actual attack hit. Expect a gold ripple spreading from the chest,
   a brief shell flare, and a synchronized chest light. These fade over 1.2 s.
3. Spend magicka and receive another hit: the ward should still flash while the
   underlying Arcane Circuit changes color and brightness normally.
4. Take several hits close together. At most three live ripple firings are kept;
   the shell/light envelope is clamped. After hits stop, the ward should settle
   back to dark rather than remain latched on.
5. Solo Resonant Ward and repeat a hit to inspect its shell/ripple/light alone.
   Solo Arcane Circuit to inspect the original pattern, then restore both.
6. Unequip the demo cuirass: its shell and light should disappear. Ordinary
   Dwarven armor lacks the demo keyword and should select neither recipe.

The user reported Arcane Circuit working after the complete-bundle correction,
with visual tuning still desired, and subsequently reported Resonant Ward working.
Winterglass and the three-way combination still need in-game validation.

## Winterglass: exact armor and material stacking

Winterglass keys directly to `0x803~BetterEnchantmentEffectsDemo.esp`, with
`merge: stack` and default armor priority 30. It needs no additional item or
ESP change. Its matching is independent of the enchantment and keywords.

A five-second ramp grows a procedural frost mask over metallic surfaces. UV
crystals and the original material's relief vary where the frost appears.
The mask cools and pales diffuse RGB, raises only the roughness channel of
RMAOS, and adds subtle height. Metallic, occlusion and reflectance channels
remain unchanged. Height uses scale 0.015, including any existing displacement;
check its appearance on both armor models. The original base maps seed the
stacks; frost contributions begin at zero. Height scale is applied immediately.
Arcane Circuit continues to own material emissive, and Ward owns its shell and
light. These are visual frost effects; no frost resistance is granted.

1. Replace the previous test bundle with `ThreeRecipes-complete-test.zip` in
   MO2, retaining a customized INI. Keep the same demo ESP enabled and equip
   the existing **Arcane Circuit - Dwarven Armor** (`XX000803`).
2. Watch for five seconds: frost should spread over the metal, cooling its
   color and softening reflections, with fine relief under grazing light.
3. Spend magicka and take a hit. Cyan/violet traces and the gold ward response
   should remain visible over the frosted cuirass.
4. Solo Winterglass to inspect frost, roughness and relief. Compare all three
   slots in the editor, then restore all recipes. Check seams and both models.
5. Fully unequip, allow the effect to retire, and re-equip: inspect growth again.
   Ordinary Dwarven armor should select none of these recipes. Unequipping
   should remove the frost along with the glow, shell and light.

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
