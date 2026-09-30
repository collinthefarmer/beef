# BetterEnchantmentEffects

An SKSE plugin for Skyrim Special Edition. It shows an armor piece's
enchantment on the piece itself.

- Effects apply per wearer, per **piece**: the enchanted gauntlet glows; the
  unenchanted copy on the next NPC does not.
- The plugin writes Community Shaders' TruePBR material **slots** and leaves
  the vanilla effect-shader slot alone, so Dirt and Blood, flesh spells, and
  scripted `PlayEffectShader` keep working on the same armor.
- The unit of authorship is a **recipe**: a JSON file that declares
  **signals** (values that vary per tick), **sources** (values that vary per
  texel), **masks**, the **layer** stacks written into the material slots, a
  cloned **shell**, and point **lights**.
- Authors edit recipes live in an in-game **studio** and ship them as files.
- Community Shaders is required. Without it, the plugin logs that the
  emissive path is disabled and stays idle.

## Reading order

| Document | The question it answers |
|---|---|
| `REQUIREMENTS.md` | What must the plugin do, how is it built, how is it verified? The canonical document; everything else defers to it. |
| `CLAUDE.md` | The three rules the code obeys, in priority order, and the practicalities of working in this repository. |
| `docs/build.md` | How are native tests, Windows builds, staging, and analysis run? |
| `docs/conventions.md` | Which pattern does a new module follow, and what word does it use? |
| `docs/components/` | What is one `src/` directory, and how does a request flow through it? Read the map for a directory before you change it. |
| `REFERENCE.md` | Why does this line look like that? The facts the code cannot state — engine layouts, Community Shaders rules, decompile lines, shader packings, constant reasons — by module. |
| `schema/recipe.schema.json` | What is a legal recipe? The format's contract. `schema/example-magicka.json` is the annotated canonical file. |
| `docs/procedural-pattern-cookbook.md` | How can procedural markings and animation be built from copyable recipe expressions and fitted to armor? |
| `docs/recipe-fragments-cookbook.md` | How can material selectors, spatial fields, resource signals and event responses be combined into reusable recipe fragments? |
| `docs/recipe-resolution.md` | How do identity overrides, sampling, priority, and replacement work? The implemented pre-alpha contract and acceptance cases; rendered verification remains pending. |
| `docs/source-archive.md` | How is the corresponding-source archive produced, and how is a fresh extraction built? |
| `docs/ui-api.md` | How is a studio surface built: the `menu/` widget vocabulary, the `Frame`, forms and fields, intents, and navigation? |
| `docs/ui-design-principles.md` | Which direction does the editor's surface follow? |
| `tests/in-game/README.md` | How do I build and run the optional console-driven lifecycle regression quest? |
| `docs/in-game-regression.md` | What is the repeatable manual run in the game, and which log line does each step print? |
| `docs/alpha-package.md` | What does a tester of a local candidate archive need to know? Shipped in the archive as `README.md`. |
| `docs/bug-report.md` | How is a closed-alpha problem reported, with which evidence? Shipped as `BUG_REPORT.md`. |
| `docs/plans/`, `docs/checkpoints/`, `docs/history/` | Open work, dated records, and superseded documents. Each document begins with a status line; the folder and the dated filename give its kind and age. Source archives leave these folders out. |
| `docs/plans/alpha-preparation-2026-09-22.md` | What is the active work toward alpha and source publication? Older plans, including the deletion inventory, are archived under `docs/history/`. |
| `docs/plans/recipe-program-pipeline-2026-09-25.md` | How do recipe rows become evaluated programs, and which format decisions must be made before the format contract closes? Raised by authoring the cookbooks against the current language. |
| `docs/plans/render-graph-correctness-2026-09-29.md` | Which invariants make the render graph's caching correct and minimal, where does the code break them, and in which order are they fixed? |
| `docs/plans/render-performance-2026-09-29.md` | What costs frame time and memory when effects render, how is it measured, and in which order is it reduced? |
| `docs/checkpoints/render-performance-review-2026-09-30.md` | What did the review of the layer-field and generated-shader commits find, and which findings are fixed? |
| Git history | What did the previous implementation do? The frozen `src/_old/` tree was removed during alpha source cleanup; history retains it. |

## Working in the repository

Run every command inside the development shell (`nix develop`). Outside the
shell, a script stops with a message that names the missing tool. The Windows
build also needs the Microsoft CRT and Windows SDK (about 80 MB, 630 MB
unpacked). Fetch them once with
`NIXPKGS_ALLOW_UNFREE=1 nix build --impure .#windows-sdk -o build/windows-sdk`;
running that command accepts Microsoft's license for them. The flake pins
their versions and content hash. Native tests and the commit and push gates
do not need them. The SDK is not redistributable: do not copy its store path
to a binary cache.

| Command | What it does |
|---|---|
| `cmake --preset windows-release` then `cmake --build --preset windows-release` | Cross-compiles the DLL with clang-cl against the xwin CRT and SDK and stages the mod into `dist/`. `--target all` compiles without staging. More than four jobs exhausts WSL's memory and kills the instance. |
| `cmake --preset native`, `cmake --build --preset native`, `ctest --preset native` | Configures, builds, and runs native tests. Use `native-sanitized` for ASan/UBSan; CTest `-R` selects suites. |
| `./install.sh` | Copies the staged mod folder into the MO2 mods directory. It copies the INI only when the mod has none, because the INI holds the user's settings. |
| `tools/gate.sh {commit,push,release,fix}` | Checks formatting, include layers and comments (commit: staged content; push: the tree plus sanitized native tests; release: adds the Windows build and full tidy). `fix` formats the tree. `tools/gate.py` holds the only copy of the include graph. |
| `python3 tools/tidy.py [--check\|--update] [--changed] [--only CHECK]` | Runs clang-tidy over the Windows database and compares with `tools/tidy-baseline.txt`. `--changed` limits the run to the sources changed on the branch and in the working tree. |
| `python3 tools/source-archive.py` | Writes the corresponding-source archive of HEAD with the pinned dependency sources. |
| `tools/rename.py Old New [--apply]` | Renames a C++ symbol through clangd. Run `python3 tools/compile-db.py` first to write the database it reads. |
| `python3 tools/trace-report.py <trace.jsonl>` | Summarizes an in-game diagnostic trace and its rotation segments. |

## Where things live at runtime

Under the game's `Data` folder. For a Mod Organizer 2 install, that is the
mod's own folder:

```
Data/SKSE/Plugins/BetterEnchantmentEffects.dll
Data/SKSE/Plugins/BetterEnchantmentEffects.ini        preferences
Data/SKSE/Plugins/BetterEnchantmentEffects/
    presets.json                                      mask-editor presets
    templates/{fill,bare}.json                         import templates the importer patches
    recipes/<anything>/*.json                          shipped recipes
    recipes/imported/*.json                            written by the importer
    recipes/user/*.json                                written by the studio, loads last
textures/BetterEnchantmentEffects/slots/slot_*.dds     presenter textures
```

- The loader reads every `.json` below `recipes/` recursively, as a recipe.
  `presets.json` and `templates/` therefore sit outside `recipes/`, at the
  plugin folder's root.
- The installer stages no recipes.
- `src/Identity.h` is the only place the plugin name is spelled.

## Layout of the source

```
src/
  main.cpp  Identity.h  Core.h  PCH.h    the process, the name, the value shapes
  Settings.*  SettingsFile.*             preferences and their INI text
  SettingsPublication.h                  the published-settings seam

  recipe/      what an effect is                              (pure)
  mesh/        what an effect is applied to                   (pure)
  planners/    how recipes are placed, merged, and bound      (pure)
  studio/      the editor's model                             (pure)
  diagnostics/ bounded trace recording                        (engine-free)
  validator/   the standalone recipe validator, beef-validate  (engine-free)
  engine/      how an effect reaches an actor
  render/      how an effect becomes pixels
  menu/        the editor's surface
```

- `recipe/`, `mesh/`, `planners/`, `studio/`, and `diagnostics/` compile and
  are unit-tested without the engine. `engine/`, `render/`, and `menu/` are
  thin adapters over them.
- `src` is the only first-party include root, so every project include names
  its directory. Vendored code (`src/extern`) and generated headers are
  separate system include roots. `tools/gate.py` enforces the graph;
  `REQUIREMENTS.md` states the rule the graph serves.

## Licence

- GPL-3.0-only with the modding/linking permission in [COPYING.md](COPYING.md).
  The GPL text is in `LICENSE`; third-party terms remain independent.
- `src/cs/` and `src/extern/` are vendored third-party copies under their
  own terms, recorded beside them.
- [Third-party notices](THIRD_PARTY_NOTICES.md) lists each dependency's
  version, license, and copyright holder.
