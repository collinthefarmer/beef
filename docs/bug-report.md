# Reporting an alpha problem

Copy the report below into the closed-alpha thread or conversation where you
received the candidate. Include files as attachments. If an item is unavailable,
say so; a crash before the plugin loads may produce no plugin log.

This candidate has no verified runtime/dependency matrix yet. Record exact
versions rather than “latest” or “AE.” Follow any narrower instructions supplied
with your candidate. Community Shaders with a compatible TruePBR layout is
required for effects; SKSE Menu Framework is required for the editor. The
compiled SE/AE families do not establish support for every Skyrim executable.

## Collect the evidence

1. Keep the candidate ZIP name, its adjacent `.sha256` file, and its
   `manifest.json`. The manifest identifies the packaged build. For an installed
   copy, also attach `SKSE/Plugins/BetterEnchantmentEffects-build.json` from the
   mod folder. The plugin log's `build:` line identifies the DLL actually loaded;
   include it even if it differs from the archive or installed metadata.
2. Copy `BetterEnchantmentEffects.log` from SKSE's log directory **before
   restarting the game**: the plugin replaces this log at startup. The
   `diagnostic trace:` line gives the trace location. For a loading problem,
   also include the SKSE loader/plugin logs that describe the failure.
3. Attach the active `SKSE/Plugins/BetterEnchantmentEffects.ini` and record
   any settings changed in the editor during the run. The INI alone may not
   describe the effective settings at the moment of failure. Include the
   startup `settings loaded from` block and any relevant settings screenshots.
   If available, attach the matching trace: its `settings` events contain an
   `effective` settings snapshot, including changes made during the session.
4. Attach the recipe JSON files involved, their paths relative to the plugin's
   `recipes/` folder, and any other recipes with the same identity or matching
   the same armor. For unsaved edits, include the visible values and error
   messages; the last saved JSON does not contain an unsaved draft. Include
   custom presets if the reproduction depends on them.
5. For a visual problem, include screenshots or a short recording of the
   expected and actual result, the armor/TruePBR mod and version, actor,
   enchantment, camera perspective, and relevant forms/plugins. For a crash,
   attach a crash report if one already exists. Do not upload a whole save or
   game assets unless the maintainer asks for them.

For a requested diagnostic rerun, back up settings, enable
`DiagnosticLogging=true` under `[General]` in the active INI while the game is
closed, then reproduce in a disposable test profile/save. Collect all retained
`BetterEnchantmentEffects-trace-<run>*.jsonl` files for that run, including
numbered segments, and the matching plugin log. Traces are bounded and older
segments can be discarded; capture them promptly. Restore the prior setting
afterwards. Note that diagnostics can affect performance measurements; report
whether they were enabled. Do not repeatedly reproduce a destructive save or
authored-file failure just to obtain more evidence.

Review attachments for personal paths or unrelated information before sharing.
Keep build identifiers, relative recipe paths, error text, and timestamps where
possible; mention any redactions. Include the complete relevant log as well as
the short excerpt near the failure.

## Report template

### What happened

Short description:

Expected result:

Actual result and exact error text:

### Candidate and environment

- Candidate ZIP name, build identity, and archive SHA-256:
- Loaded DLL's `build:` log line:
- Skyrim executable version and storefront:
- SKSE version:
- Address Library version/edition:
- Community Shaders version and TruePBR configuration:
- SKSE Menu Framework version, or absent:
- Windows version, GPU, and driver version:
- Mod manager/profile; new installation or upgrade (previous candidate):
- Armor/TruePBR mods and other relevant effect/material mods:
- Active INI, in-session settings changes, diagnostics enabled or disabled:

### Reproduction

1. Starting save/location, actor, armor, and enchantment:
2. Recipe files and relevant overrides; saved or unsaved edits:
3. Exact actions, including camera changes, equip/unequip, reload, or save load:
4. Point where the result differs:

- Frequency and approximate time in the attached log:
- Last candidate where it worked, if known:
- Smaller reproduction tried and result, or not attempted:
- Any authored-file loss; retained backup/original and failed output:

### Attachments

- Manifest/build metadata and checksum file:
- Plugin log; relevant SKSE logs if loading failed:
- Active INI and effective-settings evidence:
- Recipe JSON, relative paths, and required custom presets:
- Screenshots/recording; crash report if available:
- Matching trace segments if diagnostics were enabled:
- Missing evidence and redactions:

The archive includes `COMPATIBILITY.json` with the candidate runtime and peer
baselines. Include it with reports alongside the exact installed dependency
versions. The initial profile targets Steam Skyrim 1.6.1170 / SKSE 2.2.6; CS
1.8.3 and Menu Framework 3.13.0 are preliminary baselines, and the Address
Library AE package version still needs recording. These are not tested-support
claims. Runtime acceptance must identify the exact archive checksum and installed peers.
