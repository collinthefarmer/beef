Status: implementation complete; all seven targeted offline suites passed. Arcane Circuit
was reported working by the user, with visual tweaks still desired. Resonant
Ward was subsequently reported working by the user; detailed visual tuning remains.

# Resonant Ward stacking example

`recipes/examples/resonant-ward.json` applies to the existing Arcane Circuit
cuirass. Both `0x800~BetterEnchantmentEffectsDemo.esp` (collection) and
`0x6BBD2~Skyrim.esm` (`ArmorHeavy`, verified against the local master) are required.
No second cuirass, new ESP record or changed existing FormID is needed.

The explicit stack merge uses default keyword priority 20. Ward contributes a
chest-anchored hit ripple and short flare through a PBR shell, plus one named
chest light. Arcane Circuit retains ownership of the material emissive slot
and its magnitude/magicka-dependent multiplier. This avoids changing the base
pattern's brightness when the ward is idle or firing. Solo is the independent
inspection path; both recipes remain eligible on the same piece.

Hit lifetime is 1.2 seconds with at most three live firings. The shell and light
share a clamped envelope. The ripple is chest-centered, not an actual impact
position. This recipe does not alter damage or gameplay ward mechanics.

The complete archive is `build/demo/ArcaneCircuit-ResonantWard-complete-test.zip`.
It contains the unchanged validated DLL and ESP, both recipes, every framework
runtime and notice entry, and a hash inventory. `tools/demo-package.py` now
accepts repeated `--recipe` arguments and rejects duplicate recipe filenames.
The packaged README includes combined, solo and teardown regression steps.

`tests/planners/demostacking_tests.cpp` reads the actual example files, parses
and serializes them, then runs the resolver and composition planner on the same
piece. It checks both matches, separate material/shell contributions, one ward
light, and exclusion when either required keyword is missing. Packaging tests
exercise inclusion of multiple recipe files.

Validation log: `/tmp/beef-ward-validation.log`. The complete ZIP was integrity-
and hash-checked, including all 1024 presenter assets. Its DLL, ESP and Arcane
Circuit JSON are byte-identical to the working corrected bundle. Only a new
native regression-test executable was compiled; no plugin DLL rebuild was
needed for the recipe change.
