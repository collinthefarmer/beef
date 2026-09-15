# UI v2 fine-tuning backlog — 2026-09-15

Handoff for the fine-tuning round after the in-game test. All 13 slices of the
[wishlist implementation plan](ui-v2-wishlist-implementation-plan-2026-09-14.md)
are on branch `main` (off `cleanup/stage-0`), gated per commit, whole-plan
native + DLL green, installed to MO2.

A 2026-09-15 fine-tuning pass then landed a batch of in-game feedback and bug
fixes (summary below). This doc now lists only what remains.

## Landed 2026-09-15

- Expression opener moved into the type-indicator slot, glyph `n`, frame-height.
- Output tree: per-surface `[+ output]` scoped to type; Shell/Light `[settings]`
  popups; `Remove output` on each slot, `Remove light` on the light; the
  Recipe/overview "Output settings" modal dissolved.
- Per-tab resource add buttons; Signals `New input` beside `[+ signal]`; the
  combined `+ resource` menu removed.
- Recipe/Resources tab container with the search box and both tab bars pinned;
  only the tree and resource rows scroll.
- Layer blend restored: a clickable glyph on each stack row (`= o * s + - ~`)
  plus the inspector field.
- Light singleton encoded in `schema/recipe.schema.json`.
- FieldKey collision class fixed: `FieldScope(field.name)` in `FieldInput` and
  the shelf branch, plus per-index scopes for expression operands and selector
  clauses. See memory `fieldkey-keying-convention`.

## Per-slice: remaining follow-ups

- **S5 Board page** — the stack's click-to-cycle-geometry shortcut was dropped,
  not migrated; re-add to the preview header or Board if multi-geometry cycling
  is wanted. The Board page shares Studio's selection (no own wearer/armor/recipe
  pickers).
- **S6 output outline** — the layer-row solo state uses `isolation.TargetsLayer`
  (approximate — confirm it matches the old stack's `soloed`).
- **S8 header scope bars** — "Session" labels appear twice (header and footer);
  Fire is not in the Audition bar (still in `FormDraw` FirePopup); label
  placement is first-cut.
- **S9 inline openers** — the `Range` opener glyph is a placeholder pending an
  icon set; the plan's "shrink the exact-entry box" is **not** done (exact entry
  is still the full value field above the slider).
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
  `recipe + mask + index + param`, stable within a paint session but not across a
  recipe switch mid-paint; `DrawTermTuningPane`'s `PaintPreviewFrame` duplicates
  `DrawMaskTask`'s inline frame resolution (consolidate); per-parameter ranges are
  guesses (`posterize` capped at 16 on the slider, hard max 255 via exact entry).

## Checkpoint findings — remaining

| Finding | Status | Note |
| --- | --- | --- |
| 2 resource rename unreachable | **open** | source/mask/signal/curve inspectors still have no rename. |
| 3 resource removal unreachable | **open** | output/light removal added 2026-09-15; source/mask/signal/curve removal still missing. |
| 4 add does not select new row | **partial** | combo-create navigates via `pendingSelection` (S12); add-layer relies on the reducer setting `selection.layer` — verify the subject follows. |
| 5 `SetSource` refuses a kind change | **open** | `Edits.cpp` `Edit(SetSource)` still runs `CheckSource` and refuses a blank image/ripple/distance kind. |
| 6 rename popup stale/refusal/selection | **open** | the recipe-rename popup (`ContextRows.cpp`) still seeds stale text and does not surface refusal or follow the new id. |

## Open feature work

- **Offer coverage preview** — show what the selected offer would mask on the
  target geometry, in the rightmost pane above the term tunables, and drop the
  offer `[...]` detail button. Needs a "selected offer" concept in mask state and
  a render-depth decision: texture-plus-coverage-% (cheap, partial) versus a live
  on-mesh coverage render (heavier, the paint-preview pipeline).

## Picking up

1. Branch `main`, memory `ui-layout-redesign-2026-09-14` and
   `fieldkey-keying-convention`.
2. Work from the user's in-game observations by area; this doc lists what is
   deferred so a report of "X is missing" can be checked against intent.
3. Same cadence: native suite for studio decisions, DLL build for menu changes,
   `tools/gate.sh`/commit hook, batch installs, stop for the user's game run.
4. Nothing pushed; the user pushes when the branch is ready.
