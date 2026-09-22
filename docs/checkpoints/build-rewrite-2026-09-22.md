# Build and tidy rewrite — 2026-09-22

Implementation checkpoint for [Alpha preparation](../plans/alpha-preparation-2026-09-22.md).
The active plan owns remaining work. Commands and recovery live in
[Build and analysis](../build.md).

## Result

The build now uses CMake presets, Ninja dependency tracking, and CTest:

- The native configuration builds a static core, engine-free services,
  validator, and individual test executables. Sanitizers use a separate
  configuration. Tests have independent scratch directories and preserve
  the former source-root working directory for fixtures.
- The Windows configuration retains the pinned dependencies and xwin/clang-cl
  ABI setup. Dependency binaries belong to each configuration. The SKSE
  declaration is generated with `configure_file` instead of CommonLib's
  unconditional write, preserving timestamps when metadata is unchanged.
- Generated identity and presenter outputs have explicit dependencies.
  Identity tracks both contents and file-list changes, plus Git revision.
  Staging is explicit and includes the Windows validator. Compilation alone
  does not copy a mod into `dist` or install anything into MO2.
- Tidy runs fresh analysis with four workers by default. Header/deletion
  selections expand conservatively to a full pass; a full pass refuses an
  incomplete source database. Reports are published only after success.
  Baselines include deduplicated header findings, compare file/check counts,
  and allow line movement and fixes. They are still count allowances, not
  identities of individual diagnostics.
- Commit hooks run cheap formatting/layer checks; targeted tidy is explicit,
  push retains sanitized tests, and explicit release validation adds the
  Windows build and full normal analysis. Analyzer mode
  remains a separate review command as before.

First-party compiler caching remains off. The historical failure to record header
dependencies has not been declared fixed. There is no custom tidy
cache to invalidate. Old ignored build directories and logs are not consumed
by the replacement and have not been destructively purged.

## Verification

- The revised push gate passed after the comment-only cleanup, including
  the complete sanitized suite.
- Windows Release DLL, PDB, validator, and explicit staging built successfully.
  Final staged binaries/assets match their inputs; all 1024 presenters are
  present, and the staged manifest matches the current source fingerprint.
  Export inspection confirms `SKSEPlugin_Load`, `SKSEPlugin_Query`, and
  `SKSEPlugin_Version`. Ninja records 2048 dependencies for
  `TextureLabLifecycle.cpp`, including `TextureLab.h`.
- All 78 CTest checks passed under ASan/UBSan after fixing a pre-existing test
  lifetime bug: three pointers in `tests/recipe/signals_tests.cpp` referred
  to expired temporary `AnchorOf` results. The fixture now retains the
  values. No production code or sanitizer suppression was needed for it.
- Native checks initially exposed two fixtures that assumed source-root
  working directories; CTest now preserves that runner contract.
- Build-graph tests use the actual CMake rules in small temporary projects:
  unchanged builds preserve executable timestamps; header edits affect the
  executable; source renames/deletions and compiler-option changes propagate.
- Generated-output tests cover unchanged runs, touched-but-identical inputs,
  changed/deleted source contents, Git commits, and missing presenter files.
- Tidy tests cover empty selections, headers, incomplete databases, failed
  runs invalidating prior reports, diagnostic deduplication, line movement,
  and rejection of partial baseline regeneration/checks.
- Repeated Windows configuration preserved both the filtered compilation
  database and generated plugin declaration timestamps.
- Full fresh tidy analyzed 120 translation units and reported 64 distinct
  findings. The unchanged baseline passed: no increased source/header
  finding counts. No warning allowance was regenerated. This full pass
  preceded the final comment-only source cleanup.
- Shell syntax checks, the include-layer check, and formatting of the test
  fix passed. The push gate also exposed five existing comment lines in
  `Eviction.h` and `Edits.cpp`; these were removed without behavior changes,
  with the hysteresis rationale retained in `REFERENCE.md`. Existing
  unrelated source/editor changes were retained.

## Measurements and limits

Tools: CMake 4.3.4, Ninja 1.13.2, LLVM 21.1.8, from the pinned Nix shell.
Logs and timing records are under ignored `build/tooling-baseline/`.

| Workflow | Observed elapsed time |
|---|---:|
| Original unchanged native runner | 77.0 s |
| Replacement native build and full tests (77 checks at that point) | 13.3 s |
| Final sanitizer test execution (78 checks) | 10.9 s |
| Replacement unchanged Windows build (final verification) | 0.058 s |
| Targeted fresh tidy, `recipe/Resolve.cpp` | 7.5 s |
| Full fresh tidy, 120 translation units | 846.2 s |
| Revised push gate, including recompilation after comment cleanup | 59.1 s |

These are session observations, not controlled benchmark claims: some runs
shared the machine with other build work, test coverage grew during the
rewrite, and the original build was incremental rather than a clean build.
The unchanged Windows log explicitly reports `ninja: no work to do`.

The full clean-build/source-edit/header-edit/peak-memory comparison matrix
and historical full-gate timings remain uncompleted. No claim is made that
uncached tidy is faster than the old warm cache. Its current benefit is
simpler, conservative validity and an explicit place in the workflow.

No game was launched and no mod was installed. Existing source warnings
must be assessed separately; changing the build does not certify runtime
correctness or close the alpha release gates.

## Tooling consolidation

The follow-up removes `build.sh`, `tests/run-native.sh`, and
`tools/validate.sh`. Configure/build/test commands now use CMake and CTest
presets directly; the validator runs from the native build directory.
`tools/tidy.py`, `tools/tidy-baseline.py`, and `tools/compile-db.py` are direct
Python entry points. The obsolete tidy `--force` compatibility flag is gone.
`tools/gate.sh` only enters Nix and invokes `tools/gate.py`; Git and editor
hooks retain that environment entry point. Formatting, layer audits,
installation, and SDK setup retain their existing shell implementations.

Active guides and component documentation use the new commands. Historical
entries retain the commands used at the time. Generated build identity no
longer depends on the removed build wrapper. The installer's missing-stage
message now points to the explicit CMake stage target.

The revised push gate passed all 78 CTest checks, including ASan/UBSan,
Python tooling regression tests, and schema validation. The direct Python
compile-database command produced the expected 120 first-party entries.
The Windows release build also passed after removing the wrapper from
identity inputs. Direct Python tidy on `src/recipe/Resolve.cpp` completed
with zero findings and passed the targeted baseline check. Full-tree tidy
was not repeated for this orchestration-only follow-up.
