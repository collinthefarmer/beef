Status: preliminary. These findings come from repository inspection and
upstream documentation reviewed on this date. No peer plugins were installed,
no supported version matrix was selected, and no in-game compatibility or
missing-dependency behavior was tested. Upstream requirements can change;
recheck the specific versions selected for each alpha compatibility profile.

# Preliminary user dependency findings — 2026-09-23

## Candidate dependency inventory

| Dependency | Preliminary classification | Basis and remaining checks |
|---|---|---|
| [SKSE](https://skse.silverlock.org/) | Required | Must match the Skyrim executable/storefront. The generated plugin declaration currently sets minimum SKSE to zero; this does not establish a supported minimum. |
| [Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/32444) | Required | Our declaration selects Address Library compatibility and engine calls use relocations. Select the matching runtime data. |
| [Community Shaders](https://www.nexusmods.com/skyrimspecialedition/mods/86492?tab=description), with True PBR enabled | Required | `src/main.cpp` gates runtime hooks on CommunityShaders.dll being loaded. Material binding requires eligible CS PBR materials. DLL presence alone does not establish compatible layout or enabled features. |
| [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352?tab=description) | Required for the editor; optional for recipe playback by design | `Menu::RegisterMenu` skips registration when the framework is absent. Validate the chosen API version and missing/incompatible framework behavior. |
| [ImGui Icons](https://www.nexusmods.com/skyrimspecialedition/mods/114790) | Indirect editor dependency | Menu Framework's current description lists it as required starting with version 3.15. Recheck against the selected framework release. |
| [SSE Engine Fixes](https://www.nexusmods.com/skyrimspecialedition/mods/17230) | Indirect requirement | Current Community Shaders installation instructions list it as required. It is not a direct integration in our plugin. |
| Microsoft Visual C++ x64 runtime / Universal CRT | Runtime prerequisite, not a peer plugin | Built Windows binaries import the runtime dynamically; the validator imports MSVCP140.dll and VCRUNTIME140.dll. Verify availability in the fresh test environment. |
| [Crash Logger SSE AE VR — PDB support](https://www.nexusmods.com/skyrimspecialedition/mods/59818) | Recommended for alpha testers | Enables useful crash reporting with the separate symbols artifact; also recommended by CS. Check compatibility with the selected runtime. |
| [Assorted Mesh Fixes](https://www.nexusmods.com/skyrimspecialedition/mods/32117) | Recommended via CS | CS's installation prose calls it strongly recommended, although its requirements table also lists it. Confirm the intended baseline for our fixtures. |

Upstream basis for indirect dependencies and recommendations:
[Community Shaders installation instructions](https://www.nexusmods.com/skyrimspecialedition/mods/86492?tab=description)
and [SKSE Menu Framework description](https://www.nexusmods.com/skyrimspecialedition/mods/120352?tab=description).
These are discovery references, not instructions to install the latest release
without compatibility review.

## Content and version constraints

Material effects need eligible TruePBR armor materials and a matching recipe.
Framework installation alone does not demonstrate the plugin. The candidate
archive currently includes templates and presets but no recipe files. Provide
one documented, reproducible armor-and-recipe setup for alpha acceptance;
validate its meshes, textures, any patching steps, and visible expected result.

`src/cs/SOURCE.txt` records the mirrored CS material layout's provenance and
its match to the v1.8.3 data-member block. This is a source-layout baseline,
not proof of compatibility with all CS versions or the currently installed
binary. Current upstream CS instructions also exclude some older AE runtimes;
our enabled SE/AE compilation flags cannot override that dependency constraint.

The dependency matrix should be part of the planned compatibility profiles:
exact Skyrim runtime/storefront, SKSE, Address Library, CS, Menu Framework,
their additional requirements, and the build's CommonLib revision. Audit the
loader declarations and peer-plugin API/layout assumptions, verify actionable
refusals for missing or incompatible dependencies, then validate in a fresh
profile. Keep this record preliminary until that evidence exists.

## Offline handling audit

Menu registration now requires a loaded framework module and every export
used by the current UI wrappers (including all overloads of used wrapper
names). Disk presence alone no longer counts as availability. A missing
module or export prevents page registration and logs an actionable reason;
this does not disable recipe playback. A native test removes each export
in turn, and a source-coverage test compares the inventory with calls into
the vendored header. Neither check establishes calling-convention, ImGui
structure-layout, or runtime-version compatibility.

Missing Community Shaders still prevents runtime hooks and event sinks.
The log now explains recovery, and the editor pages reports the missing
dependency and disabled effects. DLL presence still does not verify TruePBR
is enabled or that the installed material ABI matches our mirror.

SKSE, Address Library, and CRT failures can occur before our startup/menu
code executes; their loader diagnostics remain necessary. This pass does
not certify indirect dependencies, pin supported versions, or close fresh
profile/in-game acceptance.

The missing-CS message is drawn even before a snapshot exists: absent CS
prevents the hooks that normally publish snapshots. A disabled effects path
after a snapshot exists instead directs users to dependency/layout errors in
the plugin log, without misidentifying a layout failure as a missing DLL.

Offline verification: 132 native dependency checks passed, covering an
unloaded module, a complete export set, and each of the 129 missing exports.
The wrapper/inventory coverage test and build-orchestration regression test
passed. The Windows Release DLL built successfully; targeted clang-tidy
reported no new findings, and formatting/diff checks passed. The in-game
checklist now includes unloaded-on-disk and missing-export cases as well as
the no-snapshot message. None of those visible/runtime cases has been run.
