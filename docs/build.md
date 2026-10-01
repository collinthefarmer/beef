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

Fetch the Windows CRT and SDK once, which accepts Microsoft's license for them:

```
NIXPKGS_ALLOW_UNFREE=1 nix build --impure .#windows-sdk -o build/windows-sdk
```

The flake's `windows-sdk` package is hash-pinned; `nix develop` sets
`XWIN_DIR` to the `build/windows-sdk` link, which also keeps the SDK from
Nix garbage collection. See `REFERENCE.md` for the pinned versions.

```
cmake --preset windows-release
cmake --build --preset windows-release
```

The `windows-release` build preset builds the `stage` target, which compiles
the DLL and validator and then stages them. Compilation and staging stay
separate targets: `--target all` compiles without touching `dist/`, which is
what `tools/gate.sh release` runs. `windows-debug` uses a separate directory
and does not stage. Stage copies the DLL,
PDB, build identity file, INI, presets, templates, validator, and generated
presenter textures to `dist/BetterEnchantmentEffects`. It does not ship
recipes. Asset edits are picked up by stage independently of DLL relinking.
`./install.sh` copies the staged mod into the MO2 mods directory.

Build identity comes from Git: the revision, plus a hash of the uncommitted
changes to the build inputs when they are dirty. Doc edits do not change it. It is computed on every build and rewrites
its header only when it changes. Presenter textures are copies of the
checked-in `cmake/presenter-slot.dds`, made when CMake configures. First-party compiler caching stays disabled because a previous cache
hit lost header dependencies and mixed incompatible layouts in one DLL.
Third-party caching remains enabled. See `REFERENCE.md`, Build and tools.

## Clang-tidy and editor database

```
python3 tools/compile-db.py
python3 tools/tidy.py src/recipe/Resolve.cpp
python3 tools/tidy.py --changed
python3 tools/tidy.py --changed --base HEAD --only 'readability-*'
python3 tools/tidy.py
python3 tools/tidy.py --check
python3 tools/tidy.py --update
```

CMake produces `build/Release/compile_commands.json`; tidy reads it directly.
The compile-db script configures Release and writes a first-party view to
`build/clangd/compile_commands.json` only when its content changes. The view
holds the `src/` entries of the Windows database and, when `build/native`
exists, the `tests/` entries of the native database, so a rename also reaches
the tests. Build the Windows target before checking files that require
generated headers.

Tidy analyses the named translation units, or every first-party translation
unit when no file is named. A full run fails when a first-party source is
missing from the database. `--jobs N` sets the parallel analyses (default
four; at most eight). Normal runs exclude the Clang static analyzer;
`--analyzer` includes it. Tidy prints each distinct first-party finding once,
as `file:line: [check] message`, sorted by file and line. Findings in
`src/extern`, `src/cs` and dependencies are dropped. A failed clang-tidy
invocation fails the run.

`--changed` selects the translation units that changed since a base revision,
and the translation units that include a changed header. The base is the merge
base of `HEAD` and `main`; `--base REF` sets another one. The comparison
includes staged, unstaged and untracked files. Includes are followed through
`src/`, `src/extern/` and the force-included precompiled header. A change to
`.clang-tidy` selects every translation unit. A changed source that is missing
from the database fails the run. `--changed` does not take file names.

`--only CHECK` runs only the checks that `.clang-tidy` enables and that match
the name or glob. The option is repeatable. A pattern that matches no enabled
check fails the run. Static-analyzer checks match only with `--analyzer`.

`--check` compares the findings with `tools/tidy-baseline.txt`. A finding
matches a baseline row with the same file, check and message. Digits in the
message are ignored, so a changed complexity score does not count as new. The
line number is ignored, so moved code does not count as new. Identical keys
compare by count. A baseline row without a message allows one finding of its
file and check, whatever the message. Each finding beyond the baseline prints
as `file:line: [check] message`, grouped by file. When a key has more findings
than rows, every finding of that key prints, because the baseline cannot tell
which one is new. A summary with the excess count per check follows.
Header findings are included when a single source is checked. Selected runs
(`--changed`, named files, `--only`) compare with the full baseline and pass
when the selection has no finding beyond it.

| Exit code | Meaning |
|---|---|
| 0 | The run finished. With `--check`, no finding is beyond the baseline. |
| 1 | `--check` found findings beyond the baseline. |
| 2 | The run failed: a usage error, a missing source, or a failed clang-tidy invocation. |

A pipe reports the exit code of its last command. Run `--check` without a pipe,
or read `pipestatus`, when the exit code matters.

`--update` rewrites the baseline and requires a full run without `--analyzer`,
`--changed` or `--only`. Each row holds the file, line, check and message.
Run it only after you review all findings, never to make a gate pass.

Tidy has no result cache. A safe cache needs the complete include set of each
translation unit. The Ninja dependency log is stale for a source that was
edited after the last build, and the headers include CommonLibSSE, the
Windows SDK and the force-included precompiled header. Use `--changed` for a
fast focused pass.

## Validation and recovery

`tools/gate.sh` only enters Nix and launches `tools/gate.py`; Git hooks
use this entry point. `tools/gate.sh commit` checks the staged content of
each staged first-party C++ file in `src/` and `tests/` (not `src/extern` or
`src/cs`): clang-format, the include layers in the `ALLOWS` table of
`tools/gate.py`, and the comment rule. The comment rule skips string,
character and raw-string literals, allows `NOLINT` lines, and exempts only
the exact license notice at the top of a file. `tools/gate.sh push` runs the
same checks over every first-party file in the working tree, then the
sanitized native configure, build and tests, and records the tested commit
when the working tree matches `HEAD`. `tools/gate.sh ship` runs `push` on a
clean tree and then `git push`; the `pre-push` hook runs only
`tools/gate.sh prepush`, which repeats the fast checks and requires that
record for each pushed commit. `tools/gate.sh release` adds the
Windows build and `tools/tidy.py --check`. `tools/gate.sh fix` runs
clang-format in place over every first-party file.
Run `python3 tools/tidy.py --analyzer` separately for static-analyzer review and build
and stage the Windows candidate before release. Neither gate proves in-game
behavior.

For fresh build trees use the presets; for existing trees Ninja automatically
reconfigures when CMake files or source lists change. After changing compilers
or SDK locations, use a separate directory or `cmake --fresh --preset ...`.

## Candidate archives

Run `cmake --build --preset windows-release --target package-candidate` inside
Nix. It builds required artifacts, and `cmake/Package.cmake` writes a mod ZIP,
a separate PDB ZIP, and one `.sha256` file for both to `dist/archives/`. It uses
the explicit inventory that `cmake/Stage.cmake` writes to
`build/Release/generated/package.json`, not the contents of `dist/BetterEnchantmentEffects`.
Obsolete staging files therefore cannot enter a candidate archive. The
`stage` target also copies the notice bundle for the developer installer.

Both archives carry `LICENSE`, `COPYING.md`, `THIRD_PARTY_NOTICES.md`, and every
file in `licenses/`. When a dependency changes, update its row in
`THIRD_PARTY_NOTICES.md` and its text in `licenses/`. Packaging stops on a
missing input or two entries with the same destination, before it replaces
either archive.

The mod ZIP carries `COMPATIBILITY.json` (the selected profile) and
`SKSE/Plugins/BetterEnchantmentEffects-build.json` (the build identity). The
`.sha256` file identifies the delivered archives; `sha256sum -c` repeats the
check. Hashes check integrity, not authenticity. Entries have a fixed
timestamp and sorted order, so identical inputs produce identical ZIPs; this
is not a claim that independently compiled binaries are reproducible.

Archive filenames include the project version, the compatibility profile and
the build ID. Repackaging
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

The selected profile is copied to `COMPATIBILITY.json` in the mod archive, and
its name is part of the archive filenames and of `build-identity.json`.
CMake stops configuration when the profile lacks a field it uses or when a
vendored peer header differs from the profile's SHA-256; it does not validate
other profile fields. `runtime_verified` remains false: record actual candidate acceptance separately
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

## Source archives without Git metadata

A `git archive` extraction builds without `.git`. `.gitattributes` marks
`cmake/source-revision.txt` for `export-subst`, so the archive carries the
commit hash there, and the build ID becomes `<revision>-archive-<config>`.
The build stops when that file is not stamped. The identity does not detect
edits made after extraction. `tools/source-archive.py` (see
[source-archive.md](source-archive.md)) writes such an archive together with
the pinned dependency sources. Native-only builds do not consume plugin build
identity.
