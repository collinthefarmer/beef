# Paint panel / mask inspector pass

The deferred pass over the mask-editing surface, phrased as **cases** in the
style of `ui-standardization.md`: the on-screen element, the code that draws
it, and the intended direction. This surface was left untouched by the UI
standardization pass (which noted it as paint-owned) and by the Recipe-rule
work of 2026-09-16, which parked two of its hints here.

Line numbers are as of 2026-09-16 and drift; the function names are the stable
anchors. The mask editor draws through `menu/PaintPanel.cpp` and the inspector
glue in `menu/Workspace.cpp`; its model is in `studio/PaintSession.*` and
`studio/MenuState.*`.

## Already done (do not redo)

- The inspector pane no longer special-cases paint: `DrawInspectorPane`
  (`Workspace.cpp:805`) draws `DrawSubject` + `DrawRelationships` uniformly, and
  the mask editor hosts inside `DrawMaskInspector` via the subject dispatch.
  This was the bulk of Case 13.
- The paint-era word "region" is gone from the paint code; it now means an armor
  coverage area (`docs/conventions.md` glossary).
- `DrawMaskRule` (`PaintPanel.cpp:461`) already uses the standard `Rule` +
  `RuleButton` idiom for Undo/Redo/Clear/Keep/Discard. Reuse it; do not rebuild.

## Case P1+P2 — the draft lifecycle (decided 2026-09-16)

Done together, because they are one design: what a mask draft *is* and how the
controls follow from it. Decided with the user.

**Fact the model rests on: one draft at a time.** `EditMaskAsTerms`
(`PaintPanel.cpp:574`) returns if `state.paint` is already set, and the
inspector's entry is disabled while a draft exists (`otherDraft`,
`Workspace.cpp:634`). So there is never more than one draft.

**The draft persists independent of the editor; the editor follows the
selection.** A draft (`state.paint`) survives until Keep or Discard, whatever
you navigate to. Viewing its mask *is* editing it; navigating away suspends it.
So the studio has three states:

- **A — a mask with no draft.** Its inspector shows the expression field and one
  entry, `Edit terms` (was `Open terms editor...`), disabled with its reason
  (no piece / a draft already exists).
- **B — viewing the drafted mask (editing).** The terms editor (`DrawMaskTask`),
  no banner, live preview on. Its rule carries `Undo · Redo · Clear · Keep ·
  Discard` — Keep and Discard act on the terms in view (P3 folds the errors into
  this rule's status glyph).
- **C — viewing anything else while a draft exists.** A **Banner** at the top of
  the body names the draft and offers `Discard`; the subject you navigated to
  sits below. Not scoped to masks — any subject you are on that is not the draft.

**No Suspend / Resume / Preview controls.** They were three buttons for things
that should be automatic: the editor open/closed state follows the selection
(navigating away suspends; clicking the banner returns), and the live preview
(`SoloRecipe kPaintRecipe`) follows the editor — on in B, off in C. "Resume mask
draft" (`PaintPanel.cpp:613-621`) conflated open-the-editor with turn-on-preview;
both dissolve into navigation. The verbs reduce to **Edit · Keep · Discard**,
plus click-the-banner to return. Keep never appears in C (no committing terms you
cannot see); Discard in C is the one exception — abandoning a known-but-hidden
draft — and confirms when the draft has terms.

**The Banner is a new primitive, distinct from `Rule`.** A `Rule` is a
structural section header; the draft reminder is a persistent callout about
state elsewhere, so it must not read as one. `Banner(label, trailing)` in
`menu/MenuWidgets`: a filled, full-width bar, the label a link back to the
editor, a trailing action slot. Fill is **amber, one colour** — a parked draft
is a standing "don't forget this," and the caveats it can carry (`excluded from
Save`, `destination changed — Keep won't assign`, the two lines from
`DrawMaskDraftHints`) live in its text, not a second colour.

**Where the code changes:**
- New `Banner` in `menu/MenuWidgets.*`.
- `DrawPaintDraftBar` (`PaintPanel.cpp:604`) becomes the State-C banner: shown
  when `state.paint && !MaskTaskActive`, title navigates to the drafted mask +
  `SetMode(kPaint)`, `Discard` → `EndPaint` (confirm when `!state.mask.terms.
  empty()`). The three `SmallButton`s and the loose status/`Warn` go.
- `DrawMaskInspector` (`Workspace.cpp:601`): drop the `Resume terms editor`
  limbo (`:619-628`) — on the drafted mask, enter the editor directly so
  on-the-mask ⇔ editing. Keep the raw `MaskTextField` only as the no-piece
  fallback (P5 / Case 13).
- The mode must track the selection, not just the Forward/Back nav that sets
  `kCompose` today (`StudioPage.cpp`); reconcile it so tree-selecting away from
  the mask also suspends. This is the load-bearing reducer change; gate it on an
  in-game check.
- `DrawMaskRule` (`PaintPanel.cpp:461`) keeps `Undo/Redo/Clear/Keep/Discard`
  (already the standard idiom); its `Discard` (`:484`) is State B's, distinct in
  time from the banner's.

## Case P3 — errors under the mask rule are loose

Below `DrawMaskRule`, three loose lines: the expression build error
(`Problem`, `PaintPanel.cpp:511-513`), the paint problem
(`Problem` + `Retry preview` button, `:514-519`).

**Direction.** Fold the error text into the mask rule's leading status (the
`!!!` severity-glyph + hover pattern from `DrawRecipeRuleStatus`), keeping
`Retry preview` as an action. One status home for the mask rule, mirroring the
Recipe rule.

## Case P4 — stacked hint / waiting lines in the terms editor

`DrawMaskTask` (`PaintPanel.cpp:636`) stacks loose `Dim` lines: the assignment
line (`Assign to output N / layer M / mask`, `:646`), the shared-mask caveat
(`:650`), and a chain of waiting/unavailable states — `Waiting for the mask
preview.` (`:653`), `The original armor is unavailable.` (`:660`), `Waiting for
the mask preview geometry.` (`:666`), `No geometry is available...` (`:673`).
`DrawMaskStack` adds `no selection yet: choose a term below` (`:542`) and
`nothing to offer on this geometry` / `reading the mesh` (`:556`).

**Direction.** Give the assignment/name lines a labeled home or a tooltip on the
mask rule (they describe the draft's target). Collapse the mutually exclusive
waiting/unavailable states into one status line or a single placeholder
(`PlaceholderText` fits the "editor has nothing to show yet" cases). Keep the
`HelpMarker` (`:545`) — it is a help affordance, not floating status.

## Case P5 — the inspector's terms-editor seam (finish Case 13)

`DrawMaskInspector` (`Workspace.cpp:601`) shows the terms editor
(`DrawMaskTask`) when actively painting *this* mask (`:619-630`), otherwise an
expression field + `Open terms editor...` button (`:632-642`), with `Finish or
discard the current mask draft first.` when another draft blocks it (`:641`).
Entry needs a live piece (`EditMaskAsTerms` returns when `!a_frame.piece`,
`:575`).

**Direction.** Make the terms editor read as *the* inspector body when a piece
is present; keep the raw `MaskTextField` expression field as the explicit
no-piece fallback (the precondition Case 13 named). Clarify the two disabled
reasons (no piece vs. another draft) so the user knows which applies.

## Case P6 — terminology (grade separately)

One flow carries several names: paint, mask task, mask draft, mask editor.
`DrawMaskTask` is really "the terms-editor body"; `state.paint` /
`Mode::kPaint` / `BeginPaint` / `EndPaint` are the session verbs. The
`design-mask-editor` decision keeps this as an explicit **mask editor**, a UI
concern. A rename toward that vocabulary (e.g. `DrawMaskTask` →
`DrawMaskEditor`) is a larger, higher-risk pass across `menu/` and
`studio/PaintSession.*`; scope it on its own, after the layout cases land.

## Verify (not a UI case) — offer-visibility

`docs/wip/paint-flow-review-2026-09-12.md` (history) reported: selecting an
offer adds its term but sometimes does not show the mask; Solo often restores
it. Confirm whether this still reproduces after the later render-state fixes
before assuming it is gone; it is a rendering-state defect, not part of this
UI pass.

## Finishing the panel — new elements (decided 2026-09-16)

Beyond the layout cases, three features the user wants before the panel is
done. Decided with the user; these are additions, not clean-ups.

### P7 — Filters (invert, feather) as unary transforms, not knobs

**Decided 2026-09-16.** Not a global feather knob. Feather and invert are both
unary transforms on a mask value, best expressed as **filters** — operators in
the term list (or per-term), adjustment-layer style — rather than scattered
settings. But they split by a hard line in the engine:

- **Invert is pointwise** (`1 - x`). The expression language already does
  pointwise math (`smoothstep`, etc.); there is no general invert yet — only
  `ThresholdTerm::invert` (`Mask.h:32`, flips one channel threshold) and the
  `kNot` combine op (subtracts a term). A real filter generalises it: wrap a
  term's (or the whole mask's) expression in a complement. Cheap, lands now.
- **Feather is spatial in general, but pointwise off a smooth field.** A blur
  (neighbour average) is spatial and the expression is pointwise, so there is no
  pointwise *blur*. But feather = `smoothstep` of a smooth value, which *is*
  pointwise — and it already exists where the mask is a gradient:
  `ThresholdTerm::softness` (`Mask.h:30`) and the distance-from-a-point source
  (`BuildDistanceBake`, `mesh/Mesh.cpp:298`; `DistanceSourceRow`). The gap is
  **hard regions** (chart/part/partition/bone — binary coverage): no gradient to
  smoothstep. Feathering them pointwise needs a **distance-to-edge (SDF) bake**
  for the region, then `smoothstep(edgeDistance, 0, width)`. That is a precompute
  in the shape of the existing distance bake — not a new per-frame spatial
  evaluator, and not a blur pass.

**Direction.** Introduce a filter concept (a unary transform applied to a term
or the accumulated mask, sitting in the term sequence). Ship the pointwise
filters first — invert, and feather on already-smooth masks (thresholds, radial
distance), since both are pure `smoothstep`/complement wrapping that round-trips
through Keep for free. Feather on *hard regions* is gated on the edge-distance
bake; scope that as its own precompute piece, reusing the `BuildDistanceBake` /
`DistanceSourceRow` infrastructure, once the filter framing exists to hang it
on.

### P8 — Geometry selection (via the preview pane)

**Ask.** Armor is often several geometries and a mask spans them; expose a
picker so the preview targets each shape, not only the one selected.

**Where — mostly already there.** `DrawPreview` (the rightmost pane,
`Workspace.cpp`) already has a geometry `ChoiceCombo` and previews the selected
subject. It largely subsumes this once the mask thumbnail migrates there (below):
the mask previews on whichever geometry the combo picks. The old click-to-cycle
in `DrawMaskPicture` and `NextGeometry` (`PaintPanel.cpp:81`) become redundant.

### P8b — Migrate the mask thumbnail into the preview pane

**Ask.** Move the editor's inline mask thumbnail (`DrawMaskPicture`) out of the
terms editor and into the existing rightmost preview (`DrawPreview`,
`Workspace.cpp`), which is the selected-subject texture preview (the composite
for an output, the mask for a `MaskSubject`) with a bigger canvas and its own
geometry combo.

**Where / how.** During editing the selection is the drafted `MaskSubject`, so
`DrawPreview` already points at it — but its mask branch (`:767`) resolves the
mask by name, i.e. the **committed** texture. Redirect it to `kScratchMask` when
that mask is the active draft (`state.paint && state.mask.editing == name`), so
the pane shows the **live** draft. Then delete `DrawMaskPicture` from
`DrawMaskStack`. This shortens the editor body (rule + term table + palette) and
folds P8's geometry picker into the preview pane's existing combo.
`DrawMaskPicture`'s `mask of N terms, M shown` summary line is **dropped** — the
term table already shows the terms and their S/M state; its preview-problem line
is already covered by the P3 `!!!` glyph.

### P9 — Peek + the browsable offer palette

**Ask.** See an offer's (or an existing term's) coverage **in the live preview
without adding it**, because a geometry can carry many offers that are tedious
to add-look-remove. Plus make the offer list browsable rather than a flat
scroll.

**Peek — reuses the compositor, no new preview mode.** `PaintOutput` is one
emissive layer masked by `kScratchMask`, whose expression `UpdatePaint` already
rewrites from the terms. Add a **second layer** to that output — a fixed
contrasting colour (magenta), replace blend — masked by a new `kPeekMask`, and
drive `kPeekMask` the same way: write the peeked expression into it, `"0"` when
nothing is peeked. Because it is a separate mask/layer, `state.mask.terms` is
untouched — the draft never changes. An offer's expression can reference
sources, so a peek feeds the peeked item's sources into the paint update too
(the `PreparePaintUpdate` sources path, fed transiently, never committed).

**Interaction: a toggle, radio across all rows.** One peek target at a time,
shared by the chooser rows and the term rows (same mechanism — set `kPeekMask`
to that offer/term's expression). Click a row's peek to switch to it, click the
active one to clear. Not press-hold. State is a single "peeked target" (an offer
identity or a term index) → `kPeekMask` + the highlight layer; two rebuilds per
switch, no hover thrash.

**Organization — no thumbnails.** A thumbnail readable enough to show a
coverage *shape* would multiply the row height, which fights the very problem
(a long list). And it is unnecessary: **peek is the shape identifier** (live on
the armor), so the list only owes navigation and ranking, both text-cheap. The
row is already a compact table — `[Add] | label | detail + coverage% |
[detail-button]` (`DrawOfferRow`, `PatternChooser.cpp:86`), the 2D pattern
preview living in the detail modal, not the row. Improve it in place:
collapsible sections by `OfferGroup` (the Case-3 idiom), sort by coverage within
a group (structural offers above slivers), and the existing filter on top.
Offers already carry `coverage`, `group`, `kind`, geometry (`TermOffer`,
`studio/TermTemplates.h:80`), so the grouping/sorting data is present.

**Row order:** coverage `%` at the **front** (a scannable left column of
numbers), the label + detail in the **middle**, and **peek + Add at the end**.
Coverage stays a number, not a bar.

**Ditch the offer detail modal.** `DrawOfferRow` (`PatternChooser.cpp:86`)
carries a `[detail-button]` opening `###offer-preview`, which shows the detail
text, geometry, coverage % (all already inline in the row) and
`DrawPatternPreview` (a flat 2D pattern image). Peek supersedes that 2D image —
the pattern on the actual armor beats a UV thumbnail — so the modal is redundant.
Drop the detail-button column and the modal; `DrawPatternPreview` becomes dead
code to remove. The one orphan is the per-offer geometry line; ride it as a
tooltip on the label, or let it fall out if the palette ever groups by geometry.

**Detail-string readability pass.** The auto-generated offer names/details
(`AppendChartOffers`/`AppendMaterialOffers`/etc., `studio/TermTemplates.cpp`)
describe by internal identity, not meaning, and several are borderline
unreadable: charts/parts named by raw island id (`chart 5`, `:669`), materials
by `cluster.id` with a raw description (`:683`), channels with a bare `"0.5..1"`
range (`:717`), presets whose detail is the raw mask **expression** (`:725`),
and bones with technical Skyrim names (`:693`). Rework them to say *what the
offer is / covers* — a readable range with units, a dominant-material or
coverage descriptor, a plain part name — and where a bare id is unavoidable,
pair it with something human. Peek now carries "what it looks like", so the
string only has to say what it is. This is an engine-free content pass in
`studio/` (native-testable).

## Suggested order

P2 (or P1+P2 together) → P1 → P3 → P4 → P5, then P6 on its own. Each layout case
gates on an in-game check, since "reads well" is a felt property. The finishing
features are independent: P7 (feather) and P8 (geometry) are standalone knobs;
P9 (peek + palette) is the largest and touches the paint recipe (`kPeekMask` +
the highlight layer) plus the chooser.
