# Build and analysis

Run commands inside `nix develop`. The flake pins CMake, Ninja, LLVM, Python,
ccache, and schema validation. Native presets use `NATIVE_CXX` (the Nix
compiler wrapper with the host standard library); the Windows toolchain
uses unwrapped clang-cl with xwin. Windows dependencies use pinned
FetchContent revisions with configuration-local build outputs. Outside Nix, supply an equivalent C++23
compiler and SDK explicitly. `CMakeUserPresets.json` is ignored for local
paths and overrides.

## Native library, tests, and validator

```
cmake --preset native
cmake --build --preset native
ctest --preset native

cmake --preset native-sanitized
cmake --build --preset native-sanitized
ctest --preset native-sanitized
```

Native configuration does not fetch Windows dependencies or require xwin.
CMake builds the core and engine-free services as static libraries, then
links individual suites. Ninja owns header dependencies, compiler-option
changes, and incremental linking. CTest runs the C++ suites, Python tooling
checks, and JSON schema validation. Schema validation is required, not
silently skipped. Each C++ suite has a separate scratch directory.

Build one suite with `cmake --build --preset native --target recipe_signals`,
then select it with `ctest --preset native -R '^recipe_signals$'`.
`ctest --preset native -N` lists suites. Presets cap build/test concurrency
at four. Use `-B` and `--test-dir` for a custom build directory, or a user
preset. Configure compiler overrides with CMake rather than changing `CXX`
under an existing build tree.

Build and run the validator directly after configuring native:

```
cmake --build --preset native --target BeefValidate
build/native/beef-validate file.json
```

## Windows plugin and staging

Run `setup-xwin.sh` once to obtain the Windows CRT and SDK; `XWIN_DIR`
defaults to `~/.xwin/splat`.

```
cmake --preset windows-release
cmake --build --preset windows-release
cmake --build --preset windows-release --target stage
```

`windows-debug` uses a separate directory. Compilation and staging are
explicit commands. Stage copies the DLL,
PDB, build manifest, INI, presets, templates, validator, and generated
presenter textures to `dist/BetterEnchantmentEffects`. It does not ship
recipes. Asset edits are picked up by stage independently of DLL relinking.
`./install.sh` retains its existing behavior and operates on the staged mod.

Build identity tracks source contents, build inputs, and Git revision.
Presenter textures are generated only when their inputs or outputs require
it. First-party compiler caching stays disabled because a previous cache
hit lost header dependencies and mixed incompatible layouts in one DLL.
Third-party caching remains enabled. See `REFERENCE.md`, Build and tools.

## Clang-tidy and editor database

```
python3 tools/compile-db.py
python3 tools/tidy.py src/recipe/Resolve.cpp
python3 tools/tidy.py --changed
python3 tools/tidy.py
python3 tools/tidy-baseline.py --check
```

CMake produces `build/Release/compile_commands.json`; tidy reads it directly.
The compile-db script configures Release and writes a first-party view to
`build/clangd/compile_commands.json` only when its content changes, preserving
clangd and rename-tool integration. Build the Windows target before checking
files that require generated headers.

Tidy does fresh analysis on every invocation; there is no result cache or
second dependency graph. Source selections run just those translation
units. A header change or deletion conservatively runs all first-party
translation units. `--changed` includes tracked changes against HEAD and
untracked source files. `--jobs=N` defaults to four; reduce it under memory
pressure. `--build-dir` can select another configured database for targeted analysis.
A full pass requires every active first-party source in the database; use
the Windows database for that check. Normal runs
exclude the Clang static analyzer as before; `--analyzer` enables it and
writes a separate report.

Each successful invocation publishes `build/tidy/latest.json` (or
`build/tidy-analyzer/latest.json`); logs remain under its `logs/` directory.
A failed invocation invalidates the report. Reports are evidence of that
invocation, not freshness certificates for subsequent source edits.

Baseline checks deduplicate diagnostics and compare counts by source/header
path and check, ignoring line movement and allowing fixes. A full check or
baseline regeneration requires a full successful run. Header findings are
included in targeted gates. After reviewing all findings, run
`python3 tools/tidy-baseline.py` to replace the baseline; never regenerate merely to
make a gate pass. The baseline remains a count allowance, not a guarantee
that every individual finding is unchanged.

## Validation and recovery

`tools/gate.sh` only enters Nix and launches `tools/gate.py`; Git hooks
use this entry point. `tools/gate.sh commit` runs cheap formatting and layer checks against the
working tree; it does not claim to build an isolated copy of the Git index.
Run targeted tidy explicitly during development; full analysis remains an
explicit release check. `tools/gate.sh push` checks formatting,
layering, source conventions, and sanitized native tests.
`tools/gate.sh release` adds the Windows build and full normal tidy plus its
baseline check.
Run `python3 tools/tidy.py --analyzer` separately for static-analyzer review and build
and stage the Windows candidate before release. Neither gate proves in-game
behavior.

For fresh build trees use the presets; for existing trees Ninja automatically
reconfigures when CMake files or source lists change. After changing compilers
or SDK locations, use a separate directory or `cmake --fresh --preset ...`.
After interrupted analysis, rerun tidy; old logs are never input to the gate.

## Verified candidate archives

Run `cmake --build --preset windows-release --target package-candidate` inside
Nix. It builds required artifacts and writes a mod ZIP, a separate PDB ZIP,
and SHA-256 checksums to `dist/archives/`. It uses the explicit inventory in
`cmake/Stage.cmake`, not the contents of `dist/BetterEnchantmentEffects`.
Obsolete staging files therefore cannot enter a candidate archive. The existing
`stage` target also copies the notice bundle for the developer installer.

Both archives carry `LICENSE`, `THIRD_PARTY_NOTICES.md`, and the explicit
`licenses/inventory.json` file list. Update that inventory and provenance
when dependencies change; `tools_licenses_tests` checks notice/header hashes
and reviewed dependency pins. Packaging refuses absent notice inputs and
destination collisions before replacing either archive.

Each ZIP includes `manifest.json` with every payload file's size and SHA-256,
source/build identity, compiled runtime families, CommonLib revision, and the
actual generated SKSE plugin declaration. Archives are reopened and checked
before replacing their output files. Missing inputs fail before publication.
Run `python3 tools/package.py verify <archive.zip>` to repeat the integrity
check; the adjacent `.sha256` file identifies the delivered archives. Hashes
check integrity, not authenticity. Identical inputs produce identical ZIPs;
this is not a claim that independently compiled binaries are reproducible.

Archive filenames include the project version and build identity. Repackaging
changed runtime assets at the same build identity can replace that filename;
retain the archive checksum with test evidence. `runtime_verified: false`
means packaging makes no game-compatibility claim. Matching-source delivery,
the remaining publication review, the full release gate, and in-game testing
remain outstanding; see the [license checkpoint](checkpoints/licenses-2026-09-23.md).

### SKSE and targeted releases

`cmake/compatibility/steam-1.6.1170.json` is the initial candidate profile:
Steam Skyrim 1.6.1170, minimum SKSE 2.2.6, Address Library AE (package version
still unverified), Community Shaders 1.8.3 / TruePBR source baseline, and
SKSE Menu Framework 3.13.0 editor baseline. These are candidate constraints,
not an in-game acceptance result.

The profile owns CommonLib/spdlog/rapidcsv pins, compiled families, the explicit
runtime whitelist, minimum SKSE, and peer-header fingerprints. CMake generates
both the loader declaration and startup guard from it. SE/AE are compiled;
only the listed runtime is accepted. Address Library remains required for
relocations despite the explicit whitelist in the loader declaration.

Select a reviewed profile in its own build directory:

```sh
nix develop -c cmake --preset windows-release -B build/steam-1.6.1170 -DBEEF_COMPATIBILITY_PROFILE=steam-1.6.1170
nix develop -c cmake --build build/steam-1.6.1170 --parallel 4 --target package-candidate
```

Add a reviewed JSON file before selecting another target. Changing the installed
SKSE alone cannot establish a new ABI's compatibility. New runtime families,
CommonLib revisions, peer headers, and layout assumptions need source review
and runtime acceptance. See the [profile audit](checkpoints/compatibility-profiles-2026-09-24.md).

Effective profile contents enter configuration-sensitive build identity and
`COMPATIBILITY.json` in both archives. Filenames include the profile name.
Packaging rejects disagreement between the profile, build identity, and generated
loader declaration; archive verification also checks their consistency.
`runtime_verified` remains false: record actual candidate acceptance separately
with archive checksums and the exact installed dependency versions.

### Native authoring integration

`ctest --preset native -R '^engine_editorintegration$'` runs the production
`RecipeEditor.cpp` and `RecipeStore.cpp`, compiled directly into that test
executable. The same suite is available under `native-sanitized`. Only this
target adds `tests/engine/platform` ahead of the normal include directories.
Those test doubles replace PCH logging, Skyrim form/catalog access, and the
manager's two editor-facing operations. The manager double posts through the
production `SessionQueue` and runs mutation callbacks synchronously, matching
the manager's mutation contract without retiring or rebuilding actors.

The test uses a disposable working directory and real recipe files under the
normal `Identity` paths. Editor/store command bodies, validation, revisions,
history, journals, file I/O and publication are production code. Form lookups
return no matches and shader enumeration is empty; shader import generation,
SKSE scheduling, actor/render integration and displayed UI remain game tests.
The platform headers are not used by the DLL or other native test targets.

`engine_liveretirement` similarly compiles the production `LiveActor.cpp` and
real `LiveActor.h` against isolated doubles under `tests/engine/lifetime` for
engine pointers, bindings, and compositor resources. It checks retirement order,
resource release, sibling/instance preservation, invalid placement indices, and
external leases. It does not emulate Skyrim ownership takeover or D3D execution.

### Developer installer checks

`install.sh` preserves an existing INI and excludes the authored `recipes/`
directory from both rsync and tar copies, even if staging contains stale
recipes. It requires a staged DLL and default INI before creating the target.
Copy failures return nonzero and report an incomplete installation; they do
not print success. Copies are not transactional: a failed install can leave
some updated binaries/assets, so close the game and rerun after resolving the
failure. Unknown destination files are retained; this is not obsolete-file
cleanup or an uninstaller. Existing INI symlinks are preserved as well.

`tests/tools/install_tests.py` runs the real script with disposable source and
MO2 directories and controlled tool paths, exercising both copy backends.
It never uses the configured real MO2 directory. These tests do not establish
Windows file-lock behavior or mod-manager archive upgrade behavior.
