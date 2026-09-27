Status: record. Local candidate archive inventory, checksums, tests, and SKSE
compatibility findings; runtime acceptance remains open.

# Candidate package verification — 2026-09-23

Local artifact verification only; no installation or game execution. The full
release gate, dependency notice audit, and runtime acceptance remain open.

`package-candidate` builds directly from CMake's explicit inventory, bypassing
staging leftovers. It creates a mod ZIP, separate symbols ZIP, and archive
checksums. Both ZIPs contain a manifest with payload sizes/hashes, source/build
identity, runtime-family build flags, CommonLib revision, and generated SKSE
declaration. Every archive is read back and verified before publication.

## Evidence

- Windows Release package target passed, including DLL compilation/linking.
- Five Python regression cases passed through CTest (`tools_package_tests`):
  inventory/symbol separation/checksums/repeatability, removal of obsolete
  assets on repackaging, missing-input preservation of previous output,
  unsafe/duplicate destinations, and tampered/unlisted archive content.
- Actual mod ZIP: 1,033 payload files plus manifest, including 1,024 presenter
  DDS files, DLL, Windows validator, defaults, presets, templates, build
  identity, license, and candidate instructions. No PDB or recipe files.
- Actual archive binaries match the freshly built artifacts byte-for-byte;
  embedded build JSON matches the manifest identity.
- `llvm-readobj` reports x64 PE binaries; DLL exports `SKSEPlugin_Load`,
  `SKSEPlugin_Query`, and `SKSEPlugin_Version`. The validator imports
  `MSVCP140.dll`, `VCRUNTIME140.dll`, and Universal CRT; prerequisites are
  documented but execution on a fresh Windows installation is untested.
- `git diff --check` passed.

Build: `6ed2f3a0692e-5352578473425e30-Release` (working-tree fingerprint).
Artifacts under `dist/archives/`:

| Artifact suffix | Bytes | SHA-256 |
|---|---:|---|
| `-Release.zip` | 2692083 | `82b2d01dbfd7bcbd049d603466bbc38b44fc065d18b101c37480420c1b8cbc66` |
| `-Release-symbols.zip` | 7569674 | `f60a8c4b254f5d1ec5d10568045ebdf2c5dcbf4182a02603f51fcd1a6a73fb21` |

The full filename prefix is
`BetterEnchantmentEffects-0.1.0-6ed2f3a0692e-5352578473425e30`.
Changing packaged assets can replace archives at the same source build ID;
retain these checksums with any subsequent test report.

## Compatibility findings

CommonLibSSE-NG is pinned to `b93280e832f263dbef44e44cbe2936622a02f91a`.
Windows.cmake forces SE and AE enabled, VR disabled. Plugin.cpp.in declares
Address Library compatibility, independent structures, and minimum SKSE zero.
These are compiled declarations, not a tested support matrix. No SKSE binary
is selected or bundled by the build.

The active plan now explicitly tracks compatibility profiles driving loader
declarations, package labels, and effective-configuration build identity.
Profiles remain to be implemented after selecting the intended support matrix
and auditing game/Community Shaders/menu ABI assumptions. Merely changing a
minimum SKSE field cannot establish compatibility. See `docs/build.md` for the
proposed approach and the official SKSE download source.

## Installer follow-up

The developer installer now excludes authored recipes on both copy paths,
including recipes accidentally left in staging. It preserves existing INIs
and INI symlinks, checks for a staged DLL and default INI before creating the
target, and reports copy failures without printing a success message.

Five installer regression cases passed through `tools_install_tests`, covering
fresh install and upgrades, stale staged recipes, missing inputs, backend copy
failures, default-INI copy failure after payload copying, and existing/dangling
INI symlinks. The rsync and tar paths run against temporary directories, with
spaces in their paths. The five package regression cases also passed in the
same CTest run; shell syntax and diff whitespace checks passed.

No real MO2 directory was touched. Copy failures can still leave a partially
updated installation; this is reported, not rolled back. Destination extras
remain intact. Windows locked-file behavior and mod-manager archive upgrades
still require their own checks. Candidate ZIP contents/checksums above are
unchanged by this installer-only follow-up.
## Reporting follow-up

The closed-alpha report template now ships as `BUG_REPORT.md` in the mod ZIP
and local staging. It requests loaded DLL identity as well as package metadata,
exact dependency versions, effective settings, recipe inputs, reproduction
steps, and visual/log evidence. It explains startup log replacement, trace
segments, unsaved drafts, and the still-unverified runtime matrix. Testers
return the report through the conversation that supplied their candidate.

Verified candidate `6ed2f3a0692e-8f13bf41d6049842-Release`: the mod ZIP contains
1,047 payload files and the symbols ZIP contains 15, excluding manifests.
Both passed manifest integrity verification. The packaged report and staged
report matched `docs/bug-report.md` byte for byte, the packaged README linked
to it, and `SKSE/Plugins/BetterEnchantmentEffects/beef-validate.exe` matched
the built Windows validator byte for byte. This verifies delivery, not
execution of that Windows binary or in-game compatibility.
