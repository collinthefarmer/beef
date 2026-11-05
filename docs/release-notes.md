# BetterEnchantmentEffects release notes

## 0.1.0 alpha (draft, not released)

This is the first closed alpha. It is for testers who run a separate Mod
Organizer 2 profile and a disposable save. The recipe format and the
editor can still change.

### What it does

- An armor piece shows its enchantment on the piece itself. The plugin
  writes Community Shaders' TruePBR material slots of each worn piece.
- Effects apply per wearer and per piece. An unenchanted copy of the same
  armor on another actor shows nothing.
- The plugin leaves the vanilla effect-shader slot alone. Dirt and Blood,
  flesh spells and scripted `PlayEffectShader` keep working on the same
  armor.
- A **recipe** is a JSON file that describes an effect. It can write all
  nine PBR slots (diffuse, emissive, RMAOS, normal, height, fuzz, glint,
  coat, subsurface) with their scalars, a cloned **shell** around the
  piece, and point **lights** on the bones the piece is skinned to.
- A recipe reads **signals**, which change each tick. There are 18 kinds:
  `constant`, `wave`, `ramp`, `efsh`, `av`, `actorState`, `enchantment`,
  `item`, `trigger`, `payload`, `counter`, `accumulate`, `noise`,
  `gradient`, `rate`, `smooth`, `toRoot` and `expr`.
- A recipe reads **sources**, which change per texel. There are 7 kinds:
  `image`, `material`, `mesh`, `distance`, `ripple`, `pick` and `filter`.
- Recipes match a worn piece by magic effect, enchantment, effect shader,
  keyword, armor, material, any enchantment (`enchanted`) or `default`.
  Several recipes can apply to one piece; `merge` sets how they combine.
- When no recipe covers an armor-enchantment effect shader, the importer
  writes a recipe for it from the shipped templates.
- An in-game **studio** edits recipes live, with undo and redo, a mask
  editor, previews, a held signal value for auditioning, and validation
  before a change commits. It needs SKSE Menu Framework. Recipes play
  without it.
- `beef-validate.exe` checks recipe files outside the game.

### Performance and memory

- NPC effects draw at a lower resolution when the NPC covers a small part
  of the screen (`ScreenResolution`, `NpcResolution`). The player's effects
  always draw at full resolution.
- `RenderTargetBudgetMiB` caps the video memory of the effects; 0 sets no
  cap of its own. The plugin also stays inside the free video memory. Under
  pressure, NPC effects step down in resolution, and far NPCs lose their
  effects first.
- An NPC farther than 8192 game units (two exterior cells) loses its
  effects, and gets them back inside 6554 units (`EvictDistance`; 0 turns
  eviction off).
- `UnpublishedDrawBudgetMs` (4 ms) limits the time each tick spends drawing
  new effects before they show.
- `StepDownHoldSeconds` (2) and `HiddenReleaseSeconds` (5) set how long an
  NPC must stay small on screen, or out of view, before its effects step
  down or release their intermediate textures.

### Requirements

| Requirement | Version |
|---|---|
| Skyrim Special Edition | Steam 1.6.1170 |
| SKSE | 2.2.6 or later |
| Address Library for SKSE (AE) | TO RECORD at acceptance |
| Community Shaders, with TruePBR | 1.8.3 (candidate baseline); TO CONFIRM at acceptance |
| SKSE Menu Framework (editor only) | 3.13.0 (candidate baseline); TO CONFIRM at acceptance |
| Microsoft Visual C++ x64 runtime | Not bundled |

The DLL is compiled for the SE and AE runtime families. Only the version
above is a candidate; no runtime is tested yet.

### Install and upgrade

- Install the mod archive with Mod Organizer 2 into a separate test
  profile. It installs no example recipes.
- Before an upgrade, back up the INI and
  `SKSE/Plugins/BetterEnchantmentEffects/`, which holds your recipes.
- The studio saves to `recipes/user/`. A saved copy of a shipped or
  imported recipe overrides it; deleting the copy shows the original again.
- The three example recipes (Arcane Circuit, Resonant Ward, Winterglass)
  are in the source repository under `recipes/examples/`. They need the
  demo plugin that `tools/demo-plugin.py` writes from your `Skyrim.esm`.

### Known limitations

- An equip or an unequip reinstalls all effects of that actor. Its armor
  shows no effect for about one second while the new effects draw.
- The recipe format is format 1. A format change during the alpha can stop
  your recipes from loading. Imported recipes are imported again; recipes
  that you wrote are not converted.
- Texture streaming plugins can replace the textures the plugin writes.
  Exclude `textures\BetterEnchantmentEffects\slots\` from them; for Skyrim
  Performance Solution, add `betterenchantmenteffects\slots\` to `sExclude`
  in `SPS.ini`.

### Tested

TO FILL at acceptance: the candidate archive's SHA-256, the installed peer
versions, the regression run IDs on the packaged defaults, and the result
of each case of `docs/in-game-regression.md` (PASS, FAIL or BLOCKED).

### Reporting a problem

Use `BetterEnchantmentEffects-BUG_REPORT.md` in the archive. Include the build identity from
`SKSE/Plugins/BetterEnchantmentEffects-build.json`, `COMPATIBILITY.json`,
the effective INI, the recipe, the steps, and the plugin log. Copy the log
before you restart Skyrim, because startup replaces it.
