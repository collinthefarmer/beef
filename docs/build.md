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
