# Studio creation flow: findings and the case for a seam — 2026-09-19

For a fresh context. This separates **objective findings** (what the code does,
with file:line, verified against the tree at commit `5c6a2e8`) from **solution
instincts** (one author's read, explicitly fenced). The goal is to let a new
reader reach their own conclusion about whether — and where — to insert a seam,
without inheriting a preferred answer.

## How this came up

A readiness pass on the UI-v2 checkpoint findings fixed two of them:

- `d43ee71` — **finding 5**: `Edit(SetSource)` no longer validates the blank
  record before storing, so a source's kind can be switched to
  image/ripple/distance and completed afterward (regression test added,
  `tests/studio/edits_tests.cpp`).
- `5c6a2e8` — **finding 4**: adding an output/layer/resource now selects the new
  row.

In-game, `d43ee71` then exposed a dead-end: switching a source to **ripple**
lands on an empty `trigger` with no way to satisfy it. Investigating that dead-
end and the `5c6a2e8` change is what surfaced the creation-flow question below.

## Objective findings

**F1 — Two creation paths exist, and they do not share code.**
- *Reference-creators* (create a resource from inside a field's picker): a
  `FormField.create` callback returns edits; `FormDraw.cpp:243` `PostCreate`
  runs it. Fields opt in via `FormField.creators` (labels shown in the combo):
  e.g. image `Forms.cpp:520` (`kImageCreators`), curve `:538` (`{"new curve"}`),
  mask `:617` (`{"new mask"}`), value/scalar `:567`/`:594`/`:675`
  (`kValueCreators` = "promote to signal", "new constant", "new expression",
  `Forms.cpp:166`).
- *Add-buttons* (the "+" affordances): post an `AddX` edit directly, e.g.
  `AddOutput` (`Workspace.cpp` ~95), `AddLayer` (~193), and the quick-add
  `PostResourceAdd` (`ResourcePanels.cpp`).

**F2 — The reference-creator path derives the new identity from the edits.**
`PostCreate` (`FormDraw.cpp:243`): `create(text) → edits`, then
`CreatedSubjectOf(edits)` (`FormDraw.cpp:254`) yields the created
`InspectorSubject`, then `Post(EditRecipe{...})` and `FocusCreatedSubject`
(`FormDraw.cpp:234`, which sets `state.pendingSelection` — or opens the mask
term editor for a `MaskSubject`).

**F3 — `CreatedSubjectOf` is engine-free, unit-tested, and covers four edits.**
`studio/Navigation.cpp:177` maps `AddSignal`→`SignalSubject`,
`AddSource`→`SourceSubject`, `AddMask`→`MaskSubject`, `AddCurve`→`CurveSubject`
(all by the edit's `.name`). It does **not** handle `AddOutput` or `AddLayer`.
Tests: `tests/studio/navigation_tests.cpp`.

**F4 — The add-buttons do NOT use F2/F3; they set the follow-up subject by hand.**
`5c6a2e8` set `state.pendingSelection` at each add call site to a subject whose
identity is *guessed*: `OutputSubject{outputs.size()}` and
`LayerSubject{output, layers.size()}` (appended index, `Workspace.cpp`), and the
pre-computed `UniqueName` for resources (`ResourcePanels.cpp`). This duplicates
the "figure out the created subject and focus it" logic that F2/F3 already
provide. (`DrawLightAdd`, `Workspace.cpp:443`, predates `5c6a2e8` and uses the
same `OutputSubject{outputs.size()}` guess.)

**F5 — `pendingSelection` resolution.** `state.pendingSelection`
(`studio/MenuState.h:65`) is consumed after the recipe rebuilds by
`ResolvePendingSubject` (`studio/Navigation.cpp:195`), invoked from
`StudioPage.cpp:457`. It navigates to the subject if it now exists, else clears.

**F6 — The ripple `trigger` field has no creator; peer fields do.**
`RippleFields` (`studio/Forms.cpp`, in `SourceForm`'s dispatch at
`Forms.cpp:1329`) builds the trigger as a `ReferenceField` with
`.names = names.triggers`, `.allowEmpty = false`, and **no `.creators`**. Its
combo (`ReferenceCombo` → `ReferenceEntries`, `MenuWidgets.cpp`) therefore lists
only existing trigger-kind signals and offers no "create". When a recipe has no
trigger-kind signal, the field cannot be satisfied in place. The resulting
recipe diagnostic is `CheckTrigger` in `CheckSource` (`recipe/Signals.cpp` ~800;
`SourceWhere`, `Recipe.cpp:44`).

**F7 — Identity is derivable from the edit for names and layers, but not outputs.**
`AddSignal/Source/Mask/Curve` carry the `.name`; `AddLayer{output, layer, at}`
carries the insert index `at`. `AddOutput{surface, slot}` appends — its resulting
index is `outputs.size()` at apply time, which is **not** in the edit. This is
the one identity `CreatedSubjectOf` cannot derive from the edit alone, and the
reason F4's output paths compute the index at the call site.

**F8 — Verification status.** `d43ee71` (finding 5) is covered by a native
regression test. `5c6a2e8` (finding 4) is menu code with no native coverage; its
"selection follows add" behavior is unconfirmed in-game as of this writing. Both
are built and installed (`/mnt/a/mods/SkyrimSE/mods/BetterEnchantmentEffects`).

## Broader context (where this sits)

Of the eight UI-v2 checkpoint findings: 1/3/8 were already fixed; **4 and 5 are
now fixed** (above); **2 (row rename), 6 (rename popup), 7 (blank overview)**
remain open and are believed to need in-game iteration. The five undocumented
constants (`REFERENCE.md`) and `Edits.cpp` over its 1200-line target (currently
~1636) are the remaining non-UI readiness items. See
`critique-followup-handoff-2026-09-14.md` for the full remediation ledger (note:
that doc is stale in places — Plan G/ShellPose is done, its status line lies).

## Solution instincts (one author's read — not findings)

The observation that motivated this doc: the add-buttons (F4) reinvent the
identity-and-focus logic the creator path already has (F2/F3), and the ripple
dead-end (F6) is the same missing idea seen from the other side — "create a
thing and then use its identity" has no single home.

Instinct, **minimal seam**: make `create → edits → CreatedSubjectOf → focus` the
one creation path. Route F4's resource and layer add-buttons through it (extend
`CreatedSubjectOf` for `AddLayer` → `LayerSubject{output, at}`); express the
ripple creator as a `.create` on the trigger field emitting `[AddSignal{trigger
kind}, SetSource{ripple, trigger=@new}]`, which `CreatedSubjectOf` already
focuses. Leave the two *output* adds on the explicit append-index pattern
(F7) since `AddOutput`'s identity isn't in the edit and `DrawLightAdd` already
sets that precedent.

Instinct, **fuller seam** (reserve unless outputs demand it): a studio function
`Create(request, recipe) → {edits, subject}` that computes the subject
(append index included) at creation, so every path — buttons, creators, outputs —
returns identity uniformly and nothing is guessed.

Most of either shape is studio-side and native-testable (`CreatedSubjectOf`, the
`create` functions); the menu rewiring and the creator popup need in-game eyes.

## Open questions for a fresh reader

1. Is `CreatedSubjectOf` (derive-identity-from-edits) the right seam, or is
   "creation returns `{edits, subject}`" (F7's append problem argues for it) the
   better primitive? They imply different homes for identity.
2. Should a trigger *creator* create a bare trigger signal (which is then itself
   incomplete — an origin still to configure), or is that just moving the
   dead-end up a level? What is the intended end-to-end "add a ripple" flow?
3. Do outputs deserve the same treatment as resources/layers, or is
   append-index-at-the-call-site acceptable as an idiom?
4. Is any of this worth doing before the open UI findings (2/6/7), which also
   touch the same inspector/creation surface?
