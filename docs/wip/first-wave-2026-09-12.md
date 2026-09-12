# First implementation wave — Paint and resource ownership

This wave implements the first three workstreams from the Paint flow review
and engine-facing type survey. It preserves the pre-existing working-tree
changes. It does not complete the second-wave mesh/UV, renderer metadata,
UI draw-resource lifetime, or recipe-specific texture-cache work.

## Behavior and ownership

- Paint stages source definitions with the authored terms, including pending
  frame edits, and submits dependencies with the scratch expression. Session
  and revision acknowledgments distinguish queued work from applied work.
  Keep carries its full expression and dependencies independently of the
  preview's publication timing.
- Save loading cancels Paint and restores isolation. Start requests carry
  the observed load-reset counter so an old UI frame cannot start a new
  session after loading. Temporary gaps in projected actor rows remain a
  waiting state, with a visible return to Compose.
- Keep and Discard return to Compose. Old startup/update results cannot
  resurrect an ended session. Keep's name belongs to the current session,
  and source/expression limits are reported instead of silently accepting a
  partial mask. AND remains the default; Solo/Mute remain preview controls;
  Keep saves every authored term. Cross-geometry scope is unchanged.
- Source-reference remapping operates once on the original tokens. Reusing
  sources with swapped or chained names preserves expression meaning for
  both presets and kept masks. Curve names and ordinary source references
  remain separate, including whitespace before curve-call parentheses.
- Settings readers retain whole values. Setup edits a local draft and
  publishes it before requesting reapply. Apply/tick operations retain a
  coherent settings value; non-finite INI numbers and invalid publication
  values are handled before unsafe conversion or division.
- Created GPU resources have COM owners. Required initialization is prepared
  locally before publication; optional pipelines are available only with all
  their required resources. Failed creation releases previously acquired
  resources. Lookup ownership cannot be copied or moved; target GPU fields
  are private and UI preview access uses a read-only view accessor.

See [Paint review and coverage matrix](paint-flow-review-2026-09-12.md),
[settings publication contract](settings-publication-2026-09-12.md), and
[engine-facing survey](engine-types-survey-2026-09-12.md) for the original
findings, detailed boundaries, and deferred work. The
[GPU validation checklist](gpu-ownership-validation-2026-09-12.md) records
the D3D failure-injection and live-object checks still requiring runtime access.

## Validation

- Windows Release build (`./build.sh Release -j 4`) passed without compiler
  warnings. The staged DLL/PDB hashes match the build outputs.
- The combined ASan/UBSan run passed 1,227 checks across 41 suites plus schema
  validation, with no warnings or sanitizer errors. This includes 51
  Paint-flow and 25 expression-remapping checks. After static-analysis
  cleanup, all 269 affected checks across six suites passed again, and the
  Windows Release rebuild passed without compiler warnings.
- Settings publication passed all 20 checks under both ASan/UBSan and TSan,
  including concurrent publication/read and invalid-number cases.
- All 38 first-wave source/header/test files passed the formatting check.
- Targeted clang-tidy completed for 21 production sources. After fixing new
  nesting, parameter-count and optional-access findings, the final baseline
  gate passed with no new findings. The existing baseline was preserved.
- `git diff --check` passed. The build staged the matching DLL/PDB pair under
  `dist/BetterEnchantmentEffects/SKSE/Plugins`; nothing was installed or committed.
- GPU ownership received independent source review and four Windows syntax
  checks. Compile-time ownership restrictions and an external-construction
  rejection check passed. Real D3D allocation-failure and live-object
  diagnostics have not run.

## In-game checkpoint

1. Enter Paint, choose a partition/bone/material offer, then quickly choose
   another. Solo each term and compare the thumbnail with the worn surface.
   Try AND/OR, mute/unmute, reorder, clear, undo and redo.
2. Change a mask then immediately undo, switch Solo, or Keep. Confirm the
   settled preview agrees with the latest term state. Keep while Solo/Mute
   is active and confirm the saved mask contains all authored terms.
3. Type a Keep name and click Keep directly. Start another session and confirm
   its name is fresh. Discard and Keep should return to Compose without
   automatically starting another Paint session.
4. Switch to Compose while Paint is starting. Re-enter Paint; old results
   must not close or block the new session. Exercise a missing/unavailable
   target and confirm there is a visible error and a recovery path. Load a
   save during startup, a pending preview, and Keep; confirm Paint closes
   and can be started again without stale isolation or a stuck pending state.
5. Change Setup values, Save/Reload INI and reapply. Verify animation timing
   and texture scale change as expected without mixed settings or a crash.
6. Exercise normal textures, expression masks, curve lookups, mesh bakes,
   ripple and classification previews, then re-equip and load another save.
   Look for `TextureLab: ready (runtime layer textures)` and any
   `pipeline unavailable`, `paint`, or `keep refused` diagnostics.

Use the matching DLL/PDB pair for any crash report. A native green suite does
not validate Community Shaders ABI agreement, D3D failure cleanup at runtime,
streaming/shutdown synchronization, or deferred backend texture consumption.
UV alignment and geometry-local identity remain separate second-wave checks.
