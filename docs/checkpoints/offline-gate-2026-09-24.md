# Offline candidate gate — 2026-09-24

The complete offline gate passed without source fixes or baseline changes.
This is candidate validation, not an alpha release or runtime acceptance.

## Candidate

- Build: `6ed2f3a0692e-b44ee53a7a8c5482-Release-5306dbf4`.
- Source SHA-256: `b44ee53a7a8c548277f877539a297d21070ea9d3b9799433b513d123a540d237`.
- Target profile: `steam-1.6.1170`, minimum SKSE 2.2.6.
- The working tree is dirty. The revision identifies the base commit; the
  source fingerprint identifies the candidate build inputs. These results
  cannot be reproduced from the base commit alone.

[Machine-readable evidence](offline-gate-2026-09-24.json) retains identity,
effective profile, tool versions, commands, test counts, analysis categories,
and archive checksums. Detailed local logs and the complete tidy report are
under `build/offline-gate-2026-09-24/` (ignored build artifacts).

## Results

| Check | Result |
| --- | --- |
| Formatting | All 353 files passed |
| Include layers and source comment convention | Passed |
| ASan/UBSan configuration | 99/99 CTest suites passed; 74.14 seconds |
| Ordinary native configuration | 99/99 CTest suites passed; 65.87 seconds |
| Windows Release | Configure/build passed; existing outputs up to date |
| Full normal clang-tidy | 126 translation units; 63 distinct findings |
| Existing baseline comparison | Passed; no increases by file/check, including headers |
| Mod and symbols archives | Integrity, metadata consistency, checksums, and source-byte comparisons passed |

The test counts include C++ suites, Python tooling suites, and schema validation;
ASan/UBSan instrumentation applies to the configured native C++ targets.
Both native configurations were built before testing. Builds used existing
configured directories; this does not establish a clean dependency/bootstrap
build. Packaging used its explicit inventory, independent of stale staging files.
The CMake install-rule warning remains informational; no install was requested.

## Analysis review

The current categories are 54 function-size, seven cognitive-complexity, one
integer-to-pointer performance, and one boolean-simplification finding. There
are no findings in the other enabled normal-check categories. The baseline
allows 67 findings; it was not regenerated or relaxed.

The size/complexity findings remain maintainability work rather than demonstrated
new defects from this gate. The integer-to-pointer finding is the existing
`TextureOf` conversion from an opaque Studio texture handle back to its engine
pointer; the warning does not validate the pointer's runtime lifetime. The
boolean simplification is the existing `FieldTunable` paint-mode predicate;
its negation expresses the excluded paint state. Neither requires a gate-driven
behavior change. Baseline comparison is by file/check count and cannot prove
that every individual diagnostic is unchanged. The full current report is
retained for follow-up review.

The separate clang static-analyzer mode was not run; the release gate explicitly
runs normal clang-tidy with analyzer checks disabled.

## Archives

Both archives are under `dist/archives/`, with prefix
`BetterEnchantmentEffects-0.1.0-steam-1.6.1170-6ed2f3a0692e-b44ee53a7a8c5482-Release-5306dbf4`.

| Archive suffix | Payload files (excluding manifest) | SHA-256 |
| --- | ---: | --- |
| `.zip` | 1048 | `e49faca88d4c8ba6798943766f0decd8c3c081aad14a4c5ba1618d55ac1ecf45` |
| `-symbols.zip` | 16 | `8ec982e176c8c5284e6c824d7cc96d4d24103f252643ca53bedb8665c06ea6ce` |

Every explicitly listed archive input was compared byte-for-byte with its source,
including the DLL, Windows validator, symbols, identity, and notice bundle.
The adjacent `.sha256` file was checked independently against both ZIPs.
Future repackaging can replace these filenames; use the checksum to identify
these exact artifacts.

## Still open

In-game ABI/dependency acceptance, rendered behavior, performance/soak evidence,
and fresh MO2 installation remain open. Publication still needs matching-source
and licensing review. No installation, game run, or publication was performed.
