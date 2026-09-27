Status: record. Tuning cleanup, diagnostic-report correction, and reviewed
ownership/relocation warnings.

# Static-analyzer review — 2026-09-24

The full analyzer pass covered 126 translation units on commit `70f7f5b`.
It found two plugin-source analyzer warnings and two dependency-header warnings.
One local cleanup and a reporting fix landed; the other warnings were reviewed
without suppressions or baseline changes.

## Findings and disposition

- **Numeric tuning, uninitialized assignment:** `DrawRecipeTuning` checked one
  `TunableValue` parse through `FieldTunable`, then dereferenced a second parse.
  The unchanged input and deterministic parser do not establish an actual
  uninitialized read, but the duplicated computation obscured the invariant.
  Tuning now obtains one optional value, checks it, and uses that value. Field
  availability and drawing share the same context/eligibility checks. The
  optional snapshot is also checked before reading gesture errors. A targeted
  analyzer refresh reports zero findings, including normal checks.
- **Image-source form, potential leak:** the trace allocates a callback in
  Microsoft's `std::function` implementation at `BindSourceMember`. That callback
  moves into `TextEntryFieldSpec`, then `FormField::bind`; the field moves into
  the vector returned by `SourceForm`. There is no raw release or ownership
  cycle in this path. Retained as a reviewed analyzer modeling false positive.
  A new test repeatedly constructs forms with heap-sized names/paths, copies
  and moves the vectors, destroys source rows/forms, invokes a retained binding,
  and releases it. Native and ASan/UBSan runs with leak detection pass. This
  corroborates project ownership semantics using the native standard library;
  it does not execute Microsoft's allocator implementation.
- **CommonLib relocation, fixed address:** the trace assumes a zero resolved
  vtable address and reads at `0xAD * sizeof(void*) == 1384`. The selected profile
  accepts only Steam 1.6.1170, CommonLib classifies that runtime as AE, and the
  PlayerCharacter vtable uses nonzero AE ID 208040. The normal startup path and
  valid Address Library are prerequisites the isolated trace does not prove.
  No defect on the selected target was established, and the relocation code was
  not changed. Actual mapping, vtable slot, and installed dependency acceptance
  remain runtime checks; this is not a blanket dismissal of bad relocations.
- **Microsoft filesystem, enum cast:** `file_size` combines `_Follow_symlinks`
  (0x01) with `_File_size` (0x08). The enum has a fixed unsigned underlying type
  and explicitly supplies bitmask operators. Value 9 is an intentional flag
  combination. Retained as a reviewed standard-library analyzer false positive.

## Reporter correction

The diagnostic regex accepted lowercase checker names only, silently omitting
analyzer checkers such as `NewDeleteLeaks` from JSON. Raw logs retained them.
The parser now accepts mixed-case identifiers, and reports external diagnostics
separately instead of dropping warnings located outside plugin source. Regression
coverage verifies both behaviors, deduplication across translation units, and
separation from normal reports. All six reporter tests pass.

The original full-run summary printed 63 normal findings while omitting the two
plugin analyzer findings. Reprocessing that run's complete logs with the corrected
parser recovers **65 plugin findings and two external findings**. This was log
reprocessing, not a second full analyzer execution.

After the tuning refresh, the combined evidence contains **62 normal findings,
one reviewed plugin analyzer finding, and two reviewed external analyzer
findings**. The full normal baseline was not changed. No claim of zero analyzer
warnings is made.

## Validation and identity

- Windows Release rebuilt successfully after the tuning edit.
- Fresh targeted analyzer execution with the corrected reporter: zero tuning
  findings, including normal checks and external warnings.
- `studio_panels` passes in native and ASan/UBSan configurations with
  `ASAN_OPTIONS=detect_leaks=1`; the existing four missing-initializer compiler
  warnings in that test file remain unchanged.
- Six tooling regression tests pass. Formatting and include-layer checks pass.
- Full original logs, original/reparsed reports, targeted refresh, and test logs
  are retained locally under `build/static-analyzer-2026-09-24/`.

[Machine-readable evidence](static-analyzer-2026-09-24.json) records the initial
and final build identities and dispositions. The final build is
`70f7f5bbcb7d-4f5a0cbc058a6966-Release-5306dbf4`, with source SHA-256
`4f5a0cbc058a6966f3864c666d1bbd4d8fd580d2d90f36f45aa22d9c3c6563e7`.
Coverage is a full initial pass plus a targeted refresh of the only modified
C++ translation unit, not a newly executed full pass after the edit.

The previous offline-gate archives and checksums remain historical and unchanged;
this follow-up was not repackaged or installed. No game run was performed.

Follow-up: the [hook startup safeguard](hook-startup-2026-09-24.md) subsequently
adds zero-address refusal and menu/log reporting. Its targeted analyzer run
removes the previously retained relocation diagnostic; the original full-pass
results above remain historical.
