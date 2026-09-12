# Settings publication — 2026-09-12

`SettingsPublication` copies a complete settings value under a mutex. `Read`
returns an independent value; publishing later does not change previously read
values. The engine adapter's `GetSettings` returns that value and no longer
exposes writable shared storage.

Setup edits a local draft. A changed widget normalizes and publishes the draft
before requesting reapply. Reload replaces both the publication and local draft,
so the remainder of that UI frame sees the reloaded values. Save retains the
existing explicit INI persistence behavior.

Refresh takes one settings snapshot and passes it through piece collection,
recipe instance creation, surface/shell placement, and light placement. OnFrame
uses one snapshot for its tick interval and animation updates. Logging and
snapshot-reporting helpers can independently read a newer settings value;
those reads do not control apply/tick behavior.

Publication uses SettingTable's numeric bounds: animation FPS 15–60 and speed
0.05–4. Non-finite speed resets to its default; an invalid texture-scale enum
resets to Full. The INI parser rejects non-finite numeric text before integer
conversion. TickIntervalMS defensively bounds FPS even for an unpublished draft,
preventing division by zero. These changes preserve the existing finite INI
clamping policy.

`tests/settingspublication_tests.cpp` covers retained snapshots, draft isolation,
new-operation visibility, numeric limits, non-finite input, invalid enum values,
INI round trips, and concurrent whole-value reads/publications. It runs two
writers and two readers with synchronized starts and 20,000 operations each.

The focused suite passed all 20 checks under both AddressSanitizer/UBSan and
ThreadSanitizer in the Nix environment. These tests establish the native
publication contract; Windows compilation and an in-game Setup edit/reload/
reapply pass remain integration checks. The test does not exercise ImGui or live
engine scheduling.
