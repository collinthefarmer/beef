Status: implemented; eight targeted offline suites passed. The user reported
Arcane Circuit and Resonant Ward working. Winterglass rendered acceptance and
visual tuning remain pending.

# Winterglass stacking example

`recipes/examples/winterglass.json` selects the existing demo cuirass through
`0x803~BetterEnchantmentEffectsDemo.esp`, independently of keywords and effects.
It uses stack merge at default armor priority 30. A five-second ramp grows UV
crystals influenced by the original material relief, restricted to metal.
Diffuse RGB becomes pale blue, RMAOS red becomes rougher, and height gains fine
relief. Height scale 0.015 applies to the existing displacement as well; inspect
this visually. Arcane Circuit retains material emissive; Ward retains shell
emissive and its light. No ESP, DLL or first-two-recipe changes were needed.

The actual-file native regression covers parsing and serialization, all three
matches on one piece, distinct output slots, the ward light, exact armor
exclusion, and independent keyword/effect requirements. Parser checks for each
recipe, schema checks, fixture generation, packaging and source inventory
checks also passed under the native-sanitized preset. Only the native regression
executable was rebuilt. Evidence: `/tmp/beef-winterglass-validation.log`.

The complete archive is `build/demo/ThreeRecipes-complete-test.zip`: 1056 files
plus CONTENTS.json, including all 1024 presenter textures. The packager verified
all entry hashes. An independent comparison confirmed its DLL, ESP, Arcane
Circuit and Resonant Ward files are byte-identical to the working two-recipe
bundle. The README contains combined, solo and teardown checks.

Archive SHA-256: `a73cccc8283efaa740b9e5275354d508c89f64e9378fab9a55d9ff5f8ea437b0`.
