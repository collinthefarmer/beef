# Synthetic importer fixtures

The seven `efsh/Synthetic*.json` records were authored for this project on
2026-09-24 from the importer field contract. Their identifiers, plugin name,
texture paths, colors, timing, and alpha values are invented test inputs.
They contain no captured game records or texture assets and use the project's
first-party license. The texture paths need not resolve: these tests parse
and transform recipes without opening texture files.

| Case | Purpose |
|---|---|
| SyntheticTiled | Fill template, unequal UV tiling, positive/negative scrolling, color keys, pulses and fade-in |
| SyntheticBare | Bare template, edge hue and independent alpha baselines |
| SyntheticNeutralEdge | Neutral edge retains the fill hue |
| SyntheticDark | Dark fill and edge select the white fallback |
| SyntheticAlphaFallback | Zero persistent alpha selects full alpha |
| SyntheticZeroAlpha | Zero full and persistent alpha retain safe divisors |
| SyntheticFormKey | Missing editor ID uses the form key for references and recipe identity |

Flags are included in the zero-alpha and form-key cases as ignored input;
the current importer does not interpret EFSH flags. Direct assertions cover
semantic expectations independently of the serialized goldens. Malformed
input, template selection, and every fixture's parse/import/serialize/parse
round trip are covered by `tests/recipe/importer_tests.cpp`.

`recipes/Synthetic*.json` are generated from these records and the project's
`templates/{fill,bare}.json` by that test executable. To intentionally update
them, run `BEEF_UPDATE=1 build/native/recipe_importer` inside `nix develop`,
then rerun the test without `BEEF_UPDATE` and review the resulting changes.
The former captured fixtures remain in Git history; this replacement does
not approve publishing that history.
