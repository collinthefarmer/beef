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
