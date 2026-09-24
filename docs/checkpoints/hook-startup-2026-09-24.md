# Hook startup safeguard — 2026-09-24

A zero resolved PlayerCharacter vtable address now refuses hook installation
before CommonLib reads or replaces a slot. `InstallHooks` returns the completed
result, and `std::call_once` prevents retries or duplicate patching. The immutable
failure message is exposed through an atomic flag for menu reads.

Startup keeps the emissive path disabled until dependencies and the hook succeed.
Failure skips event-sink registration. The pending-status menu renders the error
and wrapped instructions to check Skyrim, SKSE, Address Library, and the plugin
target, then restart. This path does not require a runtime snapshot; without the
hook, one may never arrive. The same underlying error is logged once.

## Verification

`engine_hooks` compiles production `Hooks.cpp` against isolated relocation,
logging, and manager test doubles. Separate processes cover failure and success:

- Zero address returns false without a vtable write; the actionable error is
  available, and repeated calls stay failed without retries or repeated logs.
- Nonzero address installs once at slot 0xAD; repeated calls succeed without
  patching twice. Invoking the replacement calls the original update before
  the manager frame.

Both cases pass natively and under ASan/UBSan. The Windows Release build passes;
the four existing menu initializer warnings are unchanged. The compilation
database was refreshed. Fresh analyzer runs for `Hooks.cpp`, `main.cpp`, and
`Menu.cpp` report zero plugin and external findings, including the former
fixed-address diagnostic. Formatting, include layers, and whitespace checks pass.
No baseline or suppression changed.

The tests exercise real hook-installation control flow with platform doubles;
they do not patch a Skyrim vtable or render the menu. Main's startup ordering and
snapshot-independent menu handling were reviewed and compiled. The visible
failure case remains an in-game checklist item requiring a controlled fixture.

## Limits and evidence

This is a zero-address safeguard, not validation of arbitrary nonzero pointers,
the slot number, the original function pointer, or Address Library contents.
CommonLib may terminate earlier for missing/incompatible library data; this
check does not intercept those dependency failures. Runtime acceptance remains open.

Local reports, logs, and identity are retained under
`build/hook-startup-2026-09-24/`.

Final build: `70f7f5bbcb7d-809d02874e22093d-Release-5306dbf4`.
Source SHA-256: `809d02874e22093d0fd331f01547d46c14ed1e6d998607d6634e272301290efd`.

This follows the [static-analyzer review](static-analyzer-2026-09-24.md).
The earlier archives were not repackaged or installed; no game run was performed.
