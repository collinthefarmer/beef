# UI v2: early framework checkpoint

Status: framework integrated; Release and automated checks passed. The user
reported the in-game checkpoint looks good on 2026-09-13. This is the deliberately incomplete framework checkpoint from the
[implementation plan](../ui-v2-implementation-plan.md), not the complete editor.

## Ownership and baseline

The user authorized agent implementation. Three agents worked on navigation,
field metadata, and file results/workspace; the coordinating agent integrated
them and reviewed their boundaries. Agents also reviewed one another's changes.

Starting HEAD: `a98378789c00642f7fd1f519cbfc3e87a22ead95`, with existing uncommitted
cleanup/render work. A source/test/tool copy and hash manifest were preserved at
`/tmp/ui-v2-baseline-20260913`. The separate cleanup pass continued editing
`Expression.cpp` and its tests; those changes are preserved and are not UI work.
The initial automated run overlapped integration, so it is not an isolated
pre-change baseline result. Final-source checks are recorded separately below.

## Implemented

- Workspace navigator, inspector, geometry/texture preview, resizable columns,
  narrow-window Browse/Preview dialogs, and Back with scroll history.
- Existing output board, stack, source/signal/mask/curve forms, short modals, and
  retained thumbnail submission reused. Exact authored output indices are checked
  before inspection; unavailable secondary light details never edit the first light.
- Save/Revert controls shared by Studio and Recipes. Results distinguish pending,
  completion, failure, and cancellation. Save failure restores premature in-memory
  imported metadata changes; buffered writes are flushed/closed before success.
- Typed inspector subjects, index invalidation after structural edits/history,
  correlated edit acknowledgments, and temporary mutation gates while snapshots
  settle. Fresh loads and selection fallback discard stale positional history.
- Field units, optional working ranges, and integral metadata extend existing
  forms/validation. Whole-field promotion remains the existing implementation.
- Existing clock controls and solo/mute state visible in Try; Return to live
  restores normal speed and clears isolation/mutes through the runtime view path.
  Clock/view scope is explicitly global at this checkpoint.

## Deliberately unfinished

Gesture sliders, property-level addressing, input discovery, operand tuning,
response graphs, independent unmatched documents, mask draft suspension, the
pattern chooser, scoped audition, and armor overlays are not complete. Paint
still uses its existing mode/session path. The output inspector still embeds
the old stack layout. Navigator search and preview pinning are not wired yet.

## Validation record

- Installed successfully to `/mnt/a/mods/SkyrimSE/mods/BetterEnchantmentEffects`;
  the existing INI was preserved.
- Release build passed with no C++ compiler warnings. Build identity:
  `a98378789c00-fd6c2e689a2a84a1-Release`.
- Source SHA256:
  `fd6c2e689a2a84a1d623be2ddb743e510535aeb3ca075f6da6cbee0595402b41`.
  A fresh identity calculation matched the built manifest.
- All 56 native suites passed with AddressSanitizer and UndefinedBehaviorSanitizer.
  Schema validation and six Python tests also passed. No compiler warnings or
  sanitizer failures in the final native run.
- Targeted clang-tidy completed. New findings were corrected; remaining size
  findings are in the existing `RecipeStore::SaveRecipe` and
  `RecipeEditor::BeginPaint` functions.
- Formatting and scoped whitespace checks passed for all 35 UI-owned code/test
  files. The frozen proposal and separate cleanup changes were preserved.

Logs: `/tmp/ui-v2-release-final.log`, `/tmp/ui-v2-native-verified.log`, and
`/tmp/ui-v2-tidy*.log`; per-file lint results are under `build/tidy/`.

The first Release compile exposed Windows min/max macro expansion in the new
workspace; parenthesized calls correct it. Review also corrected same-slot output
lookup, secondary-light targeting, isolation restoration, reduction/pending
ordering, and load invalidation.

New native coverage exercises navigation/history, exact index resolution,
pending result matching, load/fallback invalidation, and field range semantics.
File I/O and Skyrim rendering adapters require the integration check below.

## Early in-game check

Confirm the startup log contains
`build: a98378789c00-fd6c2e689a2a84a1-Release`.

Use an existing working recipe; this checkpoint does not require building a new
effect from scratch. Keep a known file copy for comparison.

1. Open Studio with enchanted PBR armor equipped. Resize wide/narrow; open Browse
   and Preview dialogs. Verify fields, footer, scrolling, and game visibility.
2. Select an output, a layer, and a source or signal. Follow a layer reference;
   use Back. Verify subject, geometry, and field contents agree. Navigation alone
   must not change the armor's appearance.
3. Edit one existing numeric field with exact text entry. Confirm the live effect
   and dirty state, Undo/Redo, then Save. Confirm file completion is separate from
   runtime application status. Test Revert and its confirmation.
4. Reorder/remove a layer on a disposable recipe. While waiting, index-based edits
   must be unavailable. Re-select only after acknowledgment; no neighboring layer
   should receive an accidental edit.
5. Try solo/mute and freeze/scrub. Return to live must restore the full effect and
   normal clock. Enter/leave existing Paint once; do not expect suspended drafts.
6. Reload a save and reopen Studio. Verify no stale subject, frozen operation gate,
   or wrong texture remains. Repeat thumbnail browsing across several geometries.

Report the interaction, selected recipe/geometry, and visible result for failures.
Use the plugin log's `edit refused`, `save failed`, and existing application/trace
records when present; absence of log errors does not prove the rendered result.
