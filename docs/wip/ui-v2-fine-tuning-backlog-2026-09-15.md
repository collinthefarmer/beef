# UI v2 fine-tuning backlog — 2026-09-15

Handoff for the fine-tuning round after the in-game test. All 13 slices of the
[wishlist implementation plan](ui-v2-wishlist-implementation-plan-2026-09-14.md)
are implemented on branch `ui-v2-layout` (off `cleanup/stage-0`), gated per
commit, whole-plan native + DLL green, installed to MO2. Nothing is pushed.
S1/S3/S4/S5/S6/S7/S10 landed by hand; S8/S9/S11/S12/S13(+S2) by feature agents
in worktrees, each cherry-picked and consolidated-built.

The structure is in; interaction polish and a few real gaps remain. Work from
this doc plus the user's in-game observations. Seams below are on `ui-v2-layout`.

## Per-slice: deferred and flagged follow-ups

- **S1 domain-identity `FieldKey`** — complete. Keys are `HashFieldKey(scope,
  field, leaf)`; the `FieldScope` guard is pushed at recipe + subject.
- **S3 value-relative ranges** — complete. `kZeroTuningSpan = 1.0` (`Fields.cpp`)
  is tuned by eye; revisit if an unranged fraction gets too wide a default.
- **S4 split shares** — complete. Session-only by design (no disk persistence).
- **S5 Board page** — the stack's click-to-cycle-geometry shortcut was dropped,
  not migrated; re-add to the preview header or Board if multi-geometry cycling
  is wanted. The Board page shares Studio's selection (no own wearer/armor/recipe
  pickers).
- **S6 output outline** — layer rows carry remove/drag/solo/mute/badge/source
  only; **blend combo and type editing stay in the layer inspector**, not on the
  row. Output settings (`ScalarForm`) still draw in the inspector, not inline on
  the output topline. The layer-row solo state uses `isolation.TargetsLayer`
  (approximate — confirm it matches the old stack's `soloed`).
- **S7 resource tabs** — kept the combined "+ resource" add menu; **per-tab add
  buttons are not done** (needs exposing the anon `PostResourceAdd` in
  `ResourcePanels`).
- **S8 header scope bars** — "Session" labels appear twice (header and footer);
  Fire is not in the Audition bar (still in `FormDraw` FirePopup); label
  placement is first-cut.
- **S9 inline openers** — the `123`/`Range` opener glyphs are placeholders
  pending an icon set; on an expression row the order is `[123][badge][field]`;
  the plan's "shrink the exact-entry box" is **not** done (exact entry is still
  the full value field above the slider).
- **S10 relationship tables** — the **Value column is not implemented** (tables
  show Property/Driver and Consumer/Property/Component only).
- **S11 input wizard** — confirm/error text is inline `TextWrapped`, child-region
  sizes are first-cut; the Advanced flat view still uses `TreeNode` rows;
  `DrawSignalWizardButton` posts `EditRecipe` with `expectedRevision = nullopt`.
- **S12 New navigation + wizard wiring** — "New input" shows only when the field
  is in signal-combo mode (badge toggled to `@`); the wizard popup draws inside
  table cells (placement eyeball); the standalone Signals-tab wizard button does
  not select the created signal afterward.
- **S13/S2 terms editor + commit sink** — a term-slider drag records several
  mask-history undo entries (**no mask-draft gesture coalescing**), and
  **Escape-cancel is unimplemented** for the term sink; the term `FieldKey` is
  `recipe + mask + index + param` (not literally `mask + index + param`),
  stable within a paint session but not across a recipe switch mid-paint;
  `DrawTermTuningPane`'s `PaintPreviewFrame` duplicates `DrawMaskTask`'s inline
  frame resolution (consolidate); per-parameter ranges are guesses (`posterize`
  capped at 16 on the slider, hard max 255 via exact entry).

## Checkpoint findings — accurate status

The plan folded all eight in, but the blind implementation only partly reached
some. Verify and finish these:

| Finding | Status | Note |
| --- | --- | --- |
| 1 slider range keyed by layout | **closed** | S1 domain `FieldKey`. |
| 2 row rename unreachable | **partial** | Layer remove reachable (tree `X`); **resource rename** (source/mask/signal/curve inspectors) still not added. |
| 3 row removal unreachable | **partial** | Layer remove reachable; **resource remove** not added. |
| 4 add does not select new row | **partial** | Combo-create navigates via `pendingSelection` (S12); add-layer relies on the reducer setting `selection.layer` — verify the subject follows. |
| 5 `SetSource` refuses a kind change | **open** | `Edits.cpp` `Edit(SetSource)` still runs `CheckSource` and refuses a blank image/ripple/distance kind; not touched. |
| 6 rename popup stale/refusal/selection | **open** | The recipe-rename popup (`ContextRows.cpp`) still seeds stale text and does not surface refusal or follow the new id. |
| 7 blank overview | **closed** | S5. |
| 8 dropped row invisible in Studio | **closed** | S5. |

Resource rename/remove (findings 2/3 remainder), finding 5, and finding 6 are
the concrete un-closed items, distinct from the interaction polish above.

## Picking up

1. Branch `ui-v2-layout`, memory `ui-layout-redesign-2026-09-14`.
2. Work from the user's in-game observations by area; this doc lists what is
   deferred so a report of "X is missing" can be checked against intent.
3. Same cadence: native suite for studio decisions, DLL build for menu changes,
   `tools/gate.sh`/commit hook, batch installs, stop for the user's game run.
4. Nothing pushed; the user pushes when the branch is ready.
