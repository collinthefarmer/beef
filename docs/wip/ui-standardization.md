# UI standardization cases

A working list of studio UI inconsistencies to resolve, each phrased as a
**case**: the on-screen element, the code that draws it, and the intended
direction. The next step is to propose shared components/idioms that resolve
groups of these at once, so the cases are grouped by the surface they touch.

Line numbers are as of 2026-09-15 and drift as files change; the function
names are the stable anchors. Menu draws through the widget vocabulary in
`menu/MenuWidgets.h` and the form layer in `menu/FormDraw`; the pure field/row
model is in `studio/`.

## Case 1 — Hierarchical separators with controls

The "Recipe" separator with `[New recipe] [Undo] [Redo] [Rename] [Recipe Keys]`,
and its siblings "Session" / "Audition".

- Separator idiom: `Rule(Studio::RuleSpec)`, `DrawRuleLine`, `RuleWithFilter`
  (`menu/MenuWidgets.cpp:828,293,834`), declared by `RuleSpec`/`RuleButton`/
  `RuleAction` (`studio/Widgets.h`).
- Live callers: `DrawStudioContext` (`menu/ContextRows.cpp`, "Session"/"Recipe"),
  `StudioPage.cpp:150` ("Audition"), `:216` ("Session").
- The recipe buttons are **not** declarative `RuleButton`s — they are
  `SameLine`-chained after the rule: `NewRecipeButton`, `UndoRedoButtons`,
  `RenameRecipeButton`, and a "Recipe keys" button + `KeysPopup`, all in
  `menu/ContextRows.cpp`.

Sub-questions raised:
- **File-actions/status strip** (`"application: rendered"`, `"No unsaved changes"`,
  `[Save]`, `[Revert to file]`) is a different strip, not a rule: `DrawApplication`
  (`StudioPage.cpp:53`) + `DrawRecipeFileActions` (`RecipeActions.cpp`: status `:85`,
  Save/Revert `:96`, Revert edits `:103`). Only folds into the header family if
  status+action bars adopt the rule idiom.
- **Navigation controls** ("Back" `Workspace.cpp:790`; "Discard mask draft"
  `PaintPanel.cpp:634`, paint-owned) are transient action buttons, not separators.

Also appears in — a **second, competing header idiom**: `SeparatorText` is used as
a section header in `RecipesPage.cpp:144,174,181,237`, `InputBrowser.cpp:359`
(wizard step titles), and `ContextRows.cpp:330` (`DrawRecipeSettings`). Converge
`Rule` and `SeparatorText` on one header. The `SetupPage` `DrawSaveBar`
("Save INI", `SetupPage.cpp:138`) is a save-actions strip like the `RecipeActions`
file bar noted above.

## Case 2 — Resource inspector headers (3 lines → one table row)

`Dim("Signal: name")`, an unlabeled `ValueSwatch` (the `1.000`), and
`Dim("Used N time(s)")` stack as three lines. Sites in `Workspace.cpp`:
`DrawSourceInspector` (`:488`), `DrawSignalInspector` (`:503`),
`DrawMaskInspector` (`:530`), `DrawCurveInspector` (`:549`); usage line
`Dim("Used {} time(s)")` at `:497,517,539,559`. Collapse to one header row/table
that labels the value and surfaces name / type / usage.

## Case 3 — Driven by / Used by → collapsible tables

`RelationshipPanel.cpp`: `DrawDrivenBy` (`:94`), `DrawUsedBy` (`:125`),
`DrawRelationships` (`:164`). Already `Table`s, but always-open under a `Dim`
label; want collapsible sections.

- The **Component** column (`DrawUsedBy`, `:154-158`) shows
  `link.consumer.component` as `[N+1]` — which sub-component of the property a
  driver feeds (a vector's Y, a colour channel). Blank means the driver feeds
  the **whole** property, which is the common case. If confusing, render "—" or
  drop the column when no row has a component.

## Case 4 — Field-row layout

Move sliders inline with their target text inputs; move detail buttons to the
right of the selected value; keep flush to mirror the type indicator (badge).

- Field table: `DrawFieldTable` (`menu/FormDraw.cpp:341`) — columns are
  `name | detail button | value`; the detail button sits in its own middle
  column, not right of the value.
- Sliders: `DrawRangeSlider` (`SliderFloat "##tune"`, `menu/Tuning.cpp:99`) with
  its "Range"/"Use range" buttons (`:61,74`), driven by `DrawTuning`/
  `DrawRecipeTuning`/`DrawSinkTuning` (`:205,228,245`) — drawn as a separate row
  below the field, not inline with the input.
- Type badge: `Badge` (`menu/MenuWidgets.cpp`), left of the value in `FieldInput`
  (`menu/FormDraw.cpp:207`).

- **Curve field has no combo:** `FieldKind::kCurve` is `FieldInputKind::kValue`
  (`studio/Fields.h:127-134`) — a typed/expression widget, not a combo. The
  declared-`@curve` dropdown (`DrawSignalCurve`, a `ReferenceCombo`) lived in the
  deleted `ResourcePanels` grid. Re-adding means giving `kCurve` combo-capable
  input or `names`/`creators` like references.

## Case 5 — "Build mask for this layer" is redundant

`Workspace.cpp:472` (`DrawLayerInspector`) duplicates a "New mask" creator in the
mask field. Folds into Case 13.

## Case 6 — Misplaced / mis-scoped controls

- **"Return to live"** (`StudioPage.cpp:218` → `ReturnToLive()` `:116`) sits under
  the "Session" rule; should be inline with session controls.
- **"Solo recipe"** (`StudioPage.cpp:152`, `IsolateCheckbox` in `DrawAuditionBar`
  `:148`) is a recipe-scoped control under the "Audition" rule, away from the
  other recipe controls.

## Case 7 — Button sizing

`SmallButton` add/settings controls should match the regular `Button` size:
`"+ output"` (`Workspace.cpp:86,354`), `"settings##shell"`/`"settings##light"`
(`:103,109`), `"+ layer"` (`:190`), `"+ signal"`/etc via `ResourceAddLabel`
(`:250`).

## Case 8 — Surface tree rows lit when populated (DECIDED: option B)

`DrawRecipeTree` (`Workspace.cpp:327`): `Dim("Material")` (`:331`) and
`Dim("Shell")` (`:338`) are unconditionally dim; Light is a `PickSubject` when
present (`:351` dim when empty).

**Decision:** all three surface headers become lit-when-populated group labels,
**none** a `PickSubject`. Light drops its `PickSubject` (redundant now that its
settings button navigates to the light's `OutputSubject`). Per-output rows and
the settings/+output buttons carry the interaction. Lit test: any output for the
surface (`std::ranges::any_of(recipe->outputs, …)`, predicate already at `:320`);
Light: `!lights.empty()`. This is label-text emphasis, not the deleted
`LitButton` widget.

## Case 9 — Resource-tab lists → info-bearing tables

`DrawResourceRows` (`Workspace.cpp:201`) draws one `PickSubject(name, subject)`
per row (`:210` signals, `:221` curves, `:232` sources, `:243` masks) — plain
selectables. Surface usage / current value / type as columns. Data is on the
rows: `SignalRow.references`/`value`/`live`/`kind`; `SourceRow.references` + kind
(`SourceKindOf`/`DescribeSource`); curve/mask `TextRow.references`. This is the
table the deleted `ResourcePanels` grid showed, minus the modals.

Model to mirror: `RecipesPage.cpp:44-72` already renders each loaded recipe as a
table row with id / keys / a counts cell ("N signals, N curves…") / a
`Problem`/`Warn`/`Ok` status cell — the exact info-table shape this case wants.
`InputBrowser.cpp:212-294` is the largest plain choice-list (`Selectable`s inside
the input wizard).

## Case 10 — Vec2/vec3 expression fields miss the "n" (number-editor) button

Gate: `FieldHasExpressionShelf` (`menu/ExpressionShelf.cpp:113`) → `HasExpression`
(`:20`); drawn only when true, in `DrawFieldInput` (`menu/FormDraw.cpp:314`).
`HasExpression` whitelists `kExpression`/`kMask`/`kCurve`/`kSignalValue` only
(and `kSignalValue` is filtered out when the text parses as a param or
`LiteralColor`). The typed value kinds `kScalar`/`kVector`/`kVec2`/`kColor`/
`kLayerSource` — assigned by `ParamField` (`studio/Forms.cpp:259`) — never
qualify, and a vec3 shaped like `a,b,c` is swallowed by the `LiteralColor`
exclusion. Fix direction: gate on "text parses as an expression with numeric
literals" rather than a kind whitelist.

## Case 11 — Output inspector loose controls need a labeled (Rule) home

All in the output-subject inspector, drawn bare with no section heading:
- `[?][]` — the "replace" toggle (`OutputHeaderForm`, `studio/Forms.cpp:1325`,
  `ToggleField("replace", …)` `:1329`), drawn via `DrawForm("output-header", …)`.
- "applies to every geometry of the piece" — `DrawSelector` (`menu/FormDraw.cpp:531`).
- `[Add Match]` — `DrawSelector` (`:535`).
- `[Remove Output]` — `DrawOutputHeader` (`menu/ContextRows.cpp:354-357`).

Assembly point: `DrawOutputHeader` (`menu/ContextRows.cpp:347`). Selector controls
want a "Selector"/"Applies to" rule; replace/remove want an output-level home.
The **light** inspector shares the shape (`OutputHeaderForm` light toggle
`:1375`; "Remove light" in `Workspace.cpp` `DrawOutput` light branch) — same
treatment.

## Case 12 — Resource inspectors made consistent with Case 11

The five center-panel inspectors carry the equivalent loose buttons + unlabeled
`Dim` lines. Sites in `Workspace.cpp`: `DrawLayerInspector` (`:458`; "Build mask"
`:472`), `DrawSourceInspector` (`:488`), `DrawSignalInspector` (`:503`; `FirePopup`
`:522`; `DrawResponse` `:527`), `DrawMaskInspector` (`:530`; "Edit mask as terms"
`:543`), `DrawCurveInspector` (`:549`). Assembly: `DrawSubject` (`:564`) dispatches;
`DrawInspectorPane` (`:707`) appends `DrawRelationships`.

Target: every inspector (output + these five) shares one shape — a labeled header
row (Case 2), the form, a labeled home for its action button(s) (Fire /
Edit-as-terms / Build-mask, mirroring Case 11's Add Match / Remove), and the
relationships/response sections under their own rules. The response section is
`DrawResponse` (`menu/ResponsePanel.cpp:53` → `PlotLines "##response"` `:65`) —
an unlabeled plot with no section heading today.

## Case 13 — Mask inspector *is* the edit-as-terms panel

`DrawMaskInspector` (`Workspace.cpp:530`) should host the terms editor inline as
its body — not an expression field + a button that jumps to a separate paint-mode
page.

- Becomes: Case-2 header (name / usage), the **terms editor** as the body, and
  the Case-3 Driven by / Used by tables — one panel.
- Terms editor content: `DrawMaskTask` + `EditMaskAsTerms` (`menu/PaintPanel.cpp`),
  currently the paint-mode terms surface, moves in.
- Removes: "Edit mask as terms" button (`Workspace.cpp:542-544`) and the paint-mode
  special-case in `DrawInspectorPane` (`:711-715`, where `DrawMaskTask` replaces
  the subject view and suppresses `DrawRelationships`). "Build mask for this layer"
  (`DrawLayerInspector`, `:471-483`, Case 5) collapses to create-a-mask-and-select.
- Relationships: `DrawDrivenBy`/`DrawUsedBy` rendered for the mask on this panel.

Dependencies/flags:
- Terms editor is **paint-owned** (`DrawMaskTask`/`EditMaskAsTerms`), so the bulk
  lands in the deferred paint pass; menu-side glue is `DrawMaskInspector` + the
  `DrawInspectorPane` branch.
- Precondition: entering terms needs a live piece — `EditMaskAsTerms` is disabled
  when `!a_frame.piece` (`:471,542`). Define the no-piece fallback (likely the raw
  `MaskTextField` expression field shown today).

## Case 14 — Details button for value fields that embed resource references

A vector-typed value (or any value) whose expression *embeds* signal/resource
references — e.g. `[time, @speed, 0]` — has no way to navigate to those
resources. The detail button is gated on the field being a **whole** reference:
`ParamField` (`studio/Forms.cpp:264-276`) sets `detail = kSignal` only when
`valued && IsWholeReference(text) && name in names`, so an embedded-reference
vector gets `detail = nullopt` and no button.

Want: give such value fields a details button that navigates to the referenced
resource(s). One reference → navigate directly (as the Case 11/12 detail
resolution does). More than one → an **anchored popup** listing each referenced
resource with a navigate-to entry.

- Reference extraction: `Program::References()` (`recipe/Expression.h:42`).
- Existing per-reference navigation to reuse: `DrawSignalReads`
  (`menu/FormDraw.cpp:155-176`, currently dormant) enumerates `References()` and
  draws a navigable entry per read; `PaintPanel.cpp:186-198` uses the same
  enumeration. Each entry resolves its kind → subject (signal/source/mask/curve)
  like the `DrawInspectorFields` destination switch does for a single ref.
- Popup follows the anchored-popup idiom (colour/blend/fire/number editors).

Relation: extends Case 10 (both trigger on "value text is an expression, not a
whole reference / bare literal") and Cases 11/12 (detail-button navigation), and
the navigate-to entries are the same gesture as `RelationshipPanel::Follow`
(`:73`) — see Case 17.

## Case 15 — Duplicate `TableStyle` constants

The same table styles are re-declared per file, several byte-identical:
`kFooterStyle` (`StudioPage.cpp:48`), `kRelationStyle` (`RelationshipPanel.cpp:89`),
`kContextStyle` + `kFormStyle` (`ContextRows.cpp:33,38`), `kFormStyle` +
`kColumnsStyle` (`FormDraw.cpp:38,42`), `kGridStyle` (`SetupPage.cpp:38` **and**
`RecipesPage.cpp:32`, identical), `kBoardStyle` (`BoardPage.cpp:39`). The
info-table idiom (Cases 2/3/9) should ship a small set of shared named styles
instead of these per-file copies.

## Case 16 — Shared diagnostics-list component

`ContextRows.cpp:330` (`DrawRecipeSettings`) and `RecipesPage.cpp:153` both render
a "Rows with problems" `SeparatorText` followed by a `Problem`/`Warn`-per-
`Diagnostic` loop. One component ("diagnostics list for a span of `Diagnostic`")
covers both. Ties to Case 1 (header idiom) and the `Problem`/`Warn`/`Ok` status
cells reused in Case 9's model (`RecipesPage`).

## Case 17 — Shared "navigate to subject" link/button

`RelationshipPanel::Follow` (`:73`, `SmallButton` → `Navigate`/`NavigateProperty`),
`PickSubject` (`Workspace.cpp:70`), the resource-list rows (Case 9), and Case 14's
multi-reference entries are all "clickable thing → `Navigate` to an
`InspectorSubject`." One component would unify them (and carry the scroll/paint
handling that `NavigateFromNav` / `NavigateFromInspector` already split by
context).
