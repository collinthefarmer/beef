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
| `docs/conventions.md` | Which pattern does a new module follow, and what word does it use? |
| `docs/components/` | What is one `src/` directory, and how does a request flow through it? Read the map for a directory before you change it. |
| `REFERENCE.md` | Why does this line look like that? The facts the code cannot state — engine layouts, Community Shaders rules, decompile lines, shader packings, constant reasons — by module. |
| `schema/recipe.schema.json` | What is a legal recipe? The format's contract. `schema/example-magicka.json` is the annotated canonical file. |
| `docs/README.md` | Which document under `docs/` is canon, which is an open plan, and which is history? The complete index; it owns each document's description and status. |
| `docs/plans/deletions.md` | Which apparently dead code must not be dropped, and which is genuinely dead? |
| `src/_old/` | What did the previous implementation do? The frozen behaviour oracle each new module is diffed against. Excluded from the build. |

## Working in the repository

Run every command inside the development shell (`nix develop`). Outside the
shell, a script stops with a message that names the missing tool.

| Command | What it does |
|---|---|
| `./build.sh Release -j 4` | Cross-compiles the DLL with clang-cl against the xwin CRT and SDK, and stages `dist/BetterEnchantmentEffects/`. More than four jobs exhausts WSL's memory and kills the instance. |
| `tests/run-native.sh` | Compiles and runs every engine-free suite natively. `SUITE=<substring>` selects suites; `BEEF_SANITIZE=1` builds them under ASan and UBSan. |
| `./install.sh` | Copies the staged mod folder into the MO2 mods directory. It copies the INI only when the mod has none, because the INI holds the user's settings. |
| `setup-xwin.sh` | Downloads the CRT and SDK (about 630 MB), once, before the first build. |
| `tools/gate.sh {commit,push}` | The format and clang-tidy gate the git hooks call. |
| `tools/layers.sh` | Holds the only copy of the include graph, and reports violations. |

## Where things live at runtime

Under the game's `Data` folder. For a Mod Organizer 2 install, that is the
mod's own folder:

```
Data/SKSE/Plugins/BetterEnchantmentEffects.dll
Data/SKSE/Plugins/BetterEnchantmentEffects.ini        preferences
Data/SKSE/Plugins/BetterEnchantmentEffects/
    presets.json                                      mask-editor presets
    recipes/<anything>/*.json                          shipped recipes
    recipes/imported/*.json                            written by the importer
    recipes/user/*.json                                written by the studio, loads last
textures/BetterEnchantmentEffects/slots/slot_*.dds     presenter textures
```

- The loader reads every `.json` below `recipes/` recursively, as a recipe.
  `presets.json` therefore sits outside `recipes/`, at the plugin folder's
  root.
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
  engine/      how an effect reaches an actor
  render/      how an effect becomes pixels
  menu/        the editor's surface
```

- `recipe/`, `mesh/`, `planners/`, `studio/`, and `diagnostics/` compile and
  are unit-tested without the engine. `engine/`, `render/`, and `menu/` are
  thin adapters over them.
- `src` is the only first-party include root, so every project include names
  its directory. Vendored code (`src/extern`) and generated headers are
  separate system include roots. `tools/layers.sh` enforces the graph;
  `REQUIREMENTS.md` states the rule the graph serves.

## Licence

- GPL-3.0. See `LICENSE`.
- `src/cs/` and `src/extern/` are vendored third-party copies under their
  own terms, recorded beside them.
