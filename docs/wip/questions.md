# Readers and their questions

Built from Stage 1 surfaces only: the installed file tree, `install.sh`,
`BetterEnchantmentEffects.ini`, `schema/recipe.schema.json`,
`schema/example-magicka.json`, `src/Identity.h`, the menu's labels, and
the plugin's log lines.

## What the plugin is

A framework for designing, defining and displaying enchantment effects on
Skyrim items that carry PBR materials and render through Community
Shaders. It widens the design space for enchantment effects and opens a
path for effects that use Community Shaders' rendering techniques.

## Readers

**The effect author** writes and edits recipes. Primary reader. Can edit
JSON and install an SKSE plugin. Has never seen this project. Does not
read C++.

**The player** installs this because a mod they want lists it as a
requirement, the way they install SKSE or Community Shaders. They consume
another author's recipes and never open one. Their surface is the INI and
nothing else; `install.sh` keeps an existing INI across upgrades, so the
INI is a file players are expected to edit and keep. Documenting their
path once lets every recipe author link here instead of writing it
again.

**The framework developer** adds new capability — a signal kind, a source
kind, an output slot — or feeds events in from another SKSE plugin.
First-class here, because extension is what a framework is for.

## Gates every reader hits first

Three facts from Stage 1 belong in the opening paragraph of anything a
reader meets, because each one can make the plugin do nothing at all.

- Community Shaders must be loaded. Without it the plugin logs
  `CommunityShaders.dll is not loaded; emissive path disabled, plugin
  idle` and stops.
- The item must carry PBR geometry. Otherwise the plugin logs
  `armor {:08X} ({}) has no PBR geometry; left alone`.
- The install ships no recipes. `dist/` holds a DLL, a PDB, an INI and
  `regions.json`. A fresh install changes nothing on screen until
  somebody imports or writes a recipe.

## Questions — the player

1. A mod I want lists this as a requirement. What is it, and why does
   that mod need it?
2. What do I install, and in what order?
3. How do I tell it is working?
4. I installed everything and my gear looks the same. What do I check?
5. Can I turn it down, or turn one part of it off, without uninstalling?
6. Does it apply to NPCs? To first person?
7. What does it cost in frame rate?
8. Is it safe to add or remove part way through a save?

## Questions — the effect author

**Getting started**

9. I want this sword to glow. Where do I start?
10. How do I start from a vanilla enchantment rather than a blank file?
11. What is a recipe, and where does the file go?
12. How do I see a change without restarting the game?
13. Where do errors show up?

**Choosing what an effect applies to**

14. How does the plugin decide which recipe applies to which item?
15. What happens when two recipes match the same item?
16. How do I make one armor set look different from the rest?
17. How do I confine an effect to part of a mesh?

**Choosing what an effect looks like**

18. What can I actually change about an item's appearance?
19. What is the shell, and how does it differ from the material?
20. How do I add a light?
21. How do images become an effect?
22. How do layers combine?

**Making an effect move**

23. Where do the numbers come from?
24. How do I make an effect react to health, combat, or sneaking?
25. How do I make something happen on a hit or a footstep?
26. How do I write an expression, and which functions exist?
27. How do I shape a value with a curve?

**Shipping to players**

28. How do my recipes reach a player's game?
29. What do I tell my users to install?
30. What does a player see if their armor carries no PBR textures?
31. What happens when another author's recipes target the same item as
    mine?
32. What are the limits I should design inside?

## Questions — the framework developer

33. How do I add a signal kind, a source kind, or an output slot?
34. What runs once, and what runs every tick?
35. Which parts build and test without the game?
36. How does another SKSE plugin feed events into a recipe?

## Terms met at the surface that Stage 1 could not define

Captured now, while the writer is still ignorant. Each is a term a reader
meets before any document defines it, so each needs a definition placed
before its first use.

recipe, key, signal, source, mask, curve, layer, stack, output, slot,
target, shell, material, region, board, offers, terms, pin, paint,
piece, variant, selector, trigger, firing, ripple, bake, cluster,
partition, chart, component, EFSH, importer, retire, re-apply
