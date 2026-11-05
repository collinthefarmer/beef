# BetterEnchantmentEffects alpha candidate

This is a development candidate, not a runtime-validated release.
`SKSE/Plugins/BetterEnchantmentEffects-build.json` records the build identity,
and `COMPATIBILITY.json` the declared compatibility. The adjacent `.sha256`
file holds the archive hashes. Compilation and archive checksums do not
establish in-game compatibility.

Install the mod archive through Mod Organizer 2 into a separate test profile.
The archive contains the plugin, default INI, templates, presets, validator,
and generated presenter textures. It installs no example recipes. Keep the
separate symbols archive for debugging; it is not needed in the mod profile.

The Windows binaries dynamically import the Microsoft Visual C++ runtime
(`MSVCP140.dll`, `VCRUNTIME140.dll`) and Windows Universal CRT. Ensure the
matching x64 runtime prerequisites are installed; they are not bundled.

Use the SKSE build matching your exact Skyrim executable and storefront,
plus the corresponding Address Library. Community Shaders with the matching
TruePBR layout is required; SKSE Menu Framework is needed for the editor. Confirm the
candidate's tested dependency versions with its release notes before running;
this candidate has no verified runtime matrix yet. Do not infer support for
all SE/AE runtimes from the compiled CommonLib runtime families.

Before upgrading, back up your mod's INI and the entire
`SKSE/Plugins/BetterEnchantmentEffects` directory, including authored recipes.
An archive contains default settings: preserve your existing INI when merging
or replacing an installation. The repository installer preserves an existing
INI; a mod manager's archive installation follows its own replacement choices.
Use a disposable save for alpha testing. To stop testing, close the game,
disable the mod in the test profile, and retain your recipe backup.

Saving a shipped or imported recipe creates or updates
`SKSE/Plugins/BetterEnchantmentEffects/recipes/user/<id>.json`, preserving the
source recipe. User definitions override shipped definitions with the same ID
on reload. Subsequent save, revert and delete operate on that user copy. Deleting
the override reveals the shipped definition again after reload. Recipes already
under `recipes/user/` keep their existing path when saved.

## Where recipes live and how they load

| Path under `SKSE/Plugins/BetterEnchantmentEffects/` | Written by | Holds |
|---|---|---|
| `recipes/<any folder>/*.json` | You or another mod | Recipes. The loader reads every `.json` file below `recipes/`, in all subfolders. |
| `recipes/imported/*.json` | The importer | One recipe for each armor-enchantment effect shader that no recipe covers. |
| `recipes/user/*.json` | The studio | Recipes that you saved in the editor. |
| `templates/fill.json`, `templates/bare.json` | The package | The templates that the importer fills in. They are not recipes. |
| `presets.json` | The package | The mask editor's presets. There is no other preset system. |

- The loader sorts the file paths. It loads every file outside `recipes/user/`
  first, then the files in `recipes/user/`.
- A recipe's identity is its filename without `.json`. The folder and the
  display name do not change the identity.
- When two files have the same identity, the one that loads later replaces
  the earlier one completely.
- A file that cannot be read as a recipe is skipped, and the log names it.
  A recipe with errors in the recipe itself loads but does not apply, and
  the log and the editor report its errors.
- The importer never writes over a file. When an imported file covers no
  effect shader, because it did not load or its key was edited away, the
  importer renames it to `<id>.json.unloaded` and imports again. The log names
  both paths. A recipe format change is repaired the same way.

## How recipes combine on one armor piece

- Every recipe whose keys match a worn piece applies to it. Two recipes can
  match the same key; a key does not belong to one recipe.
- All `keyword` keys of a recipe must match. When the recipe also has other
  keys, at least one of those must match too.
- A match by `enchanted`, enchantment, magic effect or effect shader hides
  the recipes that match only by `default`.
- A match by enchantment, magic effect or effect shader also hides the
  recipes that match only by `enchanted`.
- Recipes on one piece apply in order of priority, lowest first. A recipe's
  `priority` sets it. Without `priority`, the key that matched sets it:

| Key | Priority |
|---|---:|
| `default` | 0 |
| `enchanted` | 5 |
| `material` | 10 |
| `keyword` | 20 |
| `armor` | 30 |
| `effectShader` | 40 |
| `enchantment` | 50 |
| `magicEffect` | 60 |

- Two recipes with the same priority apply in load order.
- `merge` sets how a recipe combines with the recipes before it:
  `stack` adds its layers on top; `replace` removes the earlier layers of the
  slots it writes; `sampled` puts the recipe in a pool, and each piece takes
  one recipe from the pool. The choice depends on the actor, so one NPC
  always shows the same choice.

## Recipe format

- This candidate reads recipe format 1 only. A recipe must state
  `"format": 1`. The loader refuses a file with another format and logs why.
- The format can change during the alpha. A change can make your recipes
  fail to load. Keep your recipes, and expect to edit them after an update.
  Imported recipes are imported again; recipes that you wrote are not
  converted.

## Known limitations

- An equip or an unequip reinstalls all effects of that actor. Its armor
  shows no effect for about one second while the new effects draw.
- An NPC farther than 8192 game units (two exterior cells) loses its effects
  until it comes within 6554 units. `EvictDistance` in the INI sets the
  distance; 0 turns eviction off.
- When the plugin needs more video memory than it has, NPC effects draw at a
  lower resolution and far NPCs lose their effects first. The player's
  effects keep full resolution.

The Windows validator is at
`SKSE/Plugins/BetterEnchantmentEffects/beef-validate.exe`; run it without arguments
for usage, or pass one or more recipe JSON paths. It checks recipe data, not rendered output or game ABI
compatibility. Report problems with the build identity, Skyrim,
SKSE and Community Shaders versions, effective INI, recipe input, reproduction
steps, and relevant plugin log/trace. Review logs for personal paths before
sharing them.

Use [BUG_REPORT.md](BUG_REPORT.md) for the report template and collection
instructions. Copy the plugin log before restarting Skyrim; startup replaces it.

Both archives include `LICENSE`, `COPYING.md`, `THIRD_PARTY_NOTICES.md`, and `licenses/`.
Matching-source publication, the remaining provenance/licensing review,
dependency compatibility checks, and in-game acceptance remain release
prerequisites. This package is for local validation.

If the editor is absent, check the plugin log and SKSE loader log. The plugin
refuses menu registration when SKSE Menu Framework has not loaded or lacks a
required export; reinstall the framework version specified by the candidate
and restart Skyrim. Recipe playback does not require the editor. Missing
Community Shaders disables effects and is reported in the plugin log and,
when available, the editor pages. These checks do not certify binary-layout
compatibility or that TruePBR is enabled.

The archive includes `COMPATIBILITY.json` with the candidate runtime and peer
baselines. Include it with reports alongside the exact installed dependency
versions. The initial profile targets Steam Skyrim 1.6.1170 / SKSE 2.2.6; CS
1.8.3 and Menu Framework 3.13.0 are preliminary baselines, and the Address
Library AE package version still needs recording. These are not tested-support
claims. Runtime acceptance must identify the exact archive checksum and installed peers.

Texture streaming plugins can replace the textures this plugin writes into
armor materials. The plugin shows each effect through files under
`textures\BetterEnchantmentEffects\slots\`, and a plugin that shrinks and
reloads textures by path treats them like any other armor texture. Exclude
that folder from streaming. For Skyrim Performance Solution, add
`betterenchantmenteffects\slots\` to `sExclude` in `SPS.ini`. When a written
texture is replaced, the slot falls back to the original texture and the
plugin log says so once. Texture Downscaler only reduces textures while they
load and needs no exclusion.
