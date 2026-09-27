Status: initial demo ESP and recipe built. User confirmed Dwarven PBR
compatibility. SSEEdit 4.1.5f checked the four new records plus header with
zero errors. Rendered acceptance remains pending.

# Arcane Circuit demo sketch

## Fixture and artifacts

- Recipe: `recipes/examples/arcane-circuit.json`.
- Reproducible ESP generator: `tools/demo-plugin.py` reads the user's Skyrim.esm.
- SSEEdit read-only validator: `tools/demo-check.pas`.
- Local MO2 archive: `build/demo/ArcaneCircuit-local-test.zip`.
- Walkthrough and record table: `recipes/examples/README.md`.

The ESP creates KYWD 800, MGEF 801, ENCH 802 and ARMO 803 in
`BetterEnchantmentEffectsDemo.esp`. It retains the vanilla Dwarven cuirass's
armor-addon reference and adds the collection keyword. The enchantment has
vanilla +100 Health first and custom +25 Magicka second. The recipe requires
keyword 800 plus magic effect 801, using plugin-qualified forms. No vanilla
records are overridden.

The local archive includes the already validated current framework DLL
(SHA-256 `9fbdc5d0a77becf94a80095ebb0d0a52f805d886c09990e4ec2fc8a6d135a91b`),
because the installed DLL was older. Load the demo mod after the existing
framework in MO2. Existing settings and framework resources remain supplied
by that framework mod. This is a local test bundle, not a published alpha.
No installed mod or active load order was changed by this work.

## Verification

SSEEdit was run against isolated copies in `build/demo-work/Data`, with the
original Skyrim strings available from its Interface archive. Its script
checks only records owned by the demo plugin. Result:
`DEMO_CHECK records=5 errors=0`. The five include TES4 plus four new records.
Log: `build/demo-work/validation.log`. Missing resource-archive warnings in
that isolated run are not a mesh/render acceptance result.

The initial invocation mixed script and quickedit modes. Removing quickedit
resolved the incompatibility dialog; the subsequent script run completed.
The matching source records in this Skyrim.esm have form version 44.

Generator regressions check record identity, model/stat preservation,
keyword addition, localized-string conversion, effect order and magnitudes,
recipe form linkage, deterministic output, and rejection of malformed input.
CTest checks the actual recipe using the native parser and JSON schema.
All four targeted CTest suites passed (including five generator regressions);
log: `/tmp/beef-demo-validation.log`. The ZIP integrity, five-file inventory,
recipe copy and DLL/ESP hashes were checked after packaging.

## Visual implementation

Magicka fraction drives violet-to-cyan color and brightness. The selected
effect's magnitude scales strength, with 25 as the nominal value. Procedural
UV traces are restricted to metal; a bright band travels across them every
four seconds. Refine trace placement against both model UV layouts after
inspecting the actual meshes. This initial lattice is a prototype for the
final circuit pattern.

## Original validated recipe sketch

The following original sketch passed `beef-validate` and `check-jsonschema`.
The current recipe linked above replaces these editor IDs with plugin-qualified
forms and is the authoritative installable version.

```json
{
  "format": 1,
  "name": "Arcane Circuit",
  "description": "Prototype: moving arcane traces respond to available magicka and the selected enchantment effect's magnitude.",
  "keys": [
    { "keyword": "BEEFDemoCollection" },
    { "magicEffect": "BEEFDemoArcaneCircuitEffect" }
  ],
  "signals": {
    "magicka": { "av": "Magicka" },
    "magickaMax": { "av": { "of": "Magicka", "measure": "max" } },
    "charge": { "expr": "saturate(@magicka / max(@magickaMax, 1))" },
    "magnitude": { "enchantment": "magnitude" },
    "power": { "expr": "clamp(@magnitude / 25, 0, 2)" },
    "strength": { "expr": "@power * (0.15 + 1.85 * @charge)" },
    "hue": { "expr": "lerp([0.35, 0.08, 0.8], [0.05, 0.65, 1], @charge)" },
    "travel": { "expr": "time * 0.25" }
  },
  "sources": {
    "uv": { "bake": "uv" },
    "metallic": { "material": "metallic" }
  },
  "masks": {
    "u": "dot(@uv, [1, 0])",
    "v": "dot(@uv, [0, 1])",
    "rails": "1 - smoothstep(0.08, 0.18, min(abs(sin(@u * 8 * pi)), abs(sin(@v * 12 * pi))))",
    "traces": "@rails * smoothstep(0.25, 0.75, @metallic)",
    "packet": "pow(0.5 + 0.5 * cos((@v - @travel) * 2 * pi), 12)"
  },
  "outputs": [
    {
      "target": "material",
      "slot": "emissive",
      "resolution": "half",
      "strength": "@strength",
      "stack": [
        { "source": [1, 1, 1], "blend": "replace", "opacity": 0.18, "color": "@hue", "mask": "@traces" },
        { "source": "@packet", "blend": "add", "opacity": 0.82, "color": "@hue", "mask": "@traces" }
      ]
    }
  ]
}
```

## September 25 runtime failure and corrected distribution

The first game run loaded and selected `arcane-circuit` on armor `51000803`.
Rendering failed repeatedly with `TextureLab: presenter asset missing:
textures\BetterEnchantmentEffects\slots\slot_00.dds`. The same log reported
missing fill/bare import templates and a missing framework INI. Evidence:
`BetterEnchantmentEffects-trace-1790294102812337.jsonl` and the matching
`BetterEnchantmentEffects.log`, under the user's Skyrim SKSE logs.

The first five-file demo archive relied on an existing framework installation
for those inputs. It is superseded by
`build/demo/ArcaneCircuit-local-test-fixed.zip`, a complete local test bundle.
`tools/demo-package.py` consumes every runtime and notice entry in the normal
framework package manifest, then adds the demo ESP, recipe and walkthrough.
The framework README is retained as `FRAMEWORK_README.md`. The bundle includes
all 1024 presenter DDS files, templates, presets, default INI, build metadata,
validator, compatibility information and license notices.

The archive contains 1054 input files plus a verified `CONTENTS.json` hash
inventory. Regressions reject missing presenters/templates and missing source
files, and check inclusion of every manifest entry. The DLL and demo ESP are
unchanged from the previously validated versions. Restart Skyrim after replacing
the old MO2 demo mod with this archive. Rendering still needs confirmation.
