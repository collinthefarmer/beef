# BetterEnchantmentEffects — the studio UI, API and conventions

How a studio surface is built: the widget vocabulary, the `Frame`, forms and
fields, editing through intents, and navigation. `docs/conventions.md` governs
the whole codebase; this document is the `menu/` and UI-facing `studio/` layer
grounded in the current source. `docs/ui-design-principles.md` holds the *why*
(the surface direction); this is the *how*. Every rule cites a real symbol by
file and name — grep for it before writing a variant.

## Two layers: engine-free vocabulary, thin ImGui renderer

The UI splits the same way the rest of the tree does (functional core, thin
adapter):

- **`studio/`** is engine-free and native-tested. It holds the UI *vocabulary*
  as plain data and pure functions: the widget specs (`studio/Widgets.h`:
  `Width`, `Column`, `TableStyle`, `RuleSpec`/`RuleButton`/`RuleAction`,
  `ThumbnailSpec`), the field/form model (`studio/Fields.h`, `studio/Forms.h`),
  the editor state (`studio/MenuState.h`), the intents (`studio/Intent.h`), the
  selection/navigation reducers (`studio/Selection.h`, `studio/Navigation.h`,
  `studio/Isolation.h`), and the snapshot rows the menu reads
  (`studio/Snapshot.h`, built by `studio/Rows.cpp`/`studio/Panels.cpp`).
  Nothing under `studio/` includes `engine/` or names `RE::`.
- **`menu/`** is the ImGui renderer over that vocabulary. `menu/MenuWidgets.*`
  is the widget toolkit; `menu/FormDraw.*` draws forms; the page renderers
  (`StudioPage`, `RecipesPage`, `SetupPage`, `BoardPage`) and the panels
  (`Workspace`, `StackPanel`, `RelationshipPanel`, `PaintPanel`, `ContextRows`)
  compose them. Each `.cpp` aliases `namespace ImGui = ImGuiMCP;` — ImGui is
  reached only through the `ImGuiMCP` wrapper in `src/extern`, never directly.

A draw function is `Draw<Thing>` and reads as a sequence of domain steps;
mechanical ImGui (variant visiting, id juggling, width math) lives behind a
named helper in `MenuWidgets` so a page function stays domain-shaped.

## The `Frame`: the per-draw context, threaded by `a_frame`

Every draw function takes `const Frame &a_frame` (`menu/Frame.h`). It is the
read model plus the two write channels for one render pass:

```
struct Frame {
  const Studio::Snapshot *snapshot;   // published read model (immutable this frame)
  const Studio::PieceRow  *piece;     // resolved selection…
  const Studio::RecipeRow *recipe;    // …(any may be null — guard before use)
  const Studio::GeometryRow *geometry;
  const Studio::Names *names;         // name pools for the field combos
  Studio::MenuState *state;           // mutable menu-local UI state
  Studio::Intents  *intents;          // the frame's outbound intent queue
  float scale;                        // DPI/font scale for widget widths
};
```

Read it through the accessors, not the raw fields, where one exists: `ActorOf`,
`SelectionOf`, `ViewOf`, `LayoutOf` (`menu/Frame.h`; defined in `menu/Menu.cpp`).
`snapshot`, `piece`, `recipe`, `geometry`, `names` may be null — a page guards
(`if (!a_frame.recipe) return;`) before dereferencing. `state` and `intents`
are always live.

The split is the discipline: `snapshot` is the published, immutable read model
for this frame; `intents` is where a widget *asks* for a change; `state` is
menu-only UI bookkeeping (selection, navigation history, drafts, tuning). A
widget never writes `snapshot`, and never mutates a recipe directly.

## The widget vocabulary (`menu/MenuWidgets.h`)

### Rules — the titled separator that heads a section

`Rule` is the one section-header idiom; `SeparatorText` is retired. Signature:

```
RuleResult Rule(const Studio::RuleSpec &a_spec,
                float a_trailingWidth = 0.0f,
                const std::function<void()> &a_trailing = {},
                const std::function<void()> &a_leading  = {});
```

`RuleSpec` (`studio/Widgets.h`) is `{ text, buttons, collapsible, openByDefault,
leadingSpace }`. A rule draws (optional leading gap) → separator → label, then:

- **leading slot** (`a_leading`, drawn right after the label): the section's
  one-line *status* — `Warn`/`Dim`/`Problem` text, a count, a badge. This is
  where solo/mute, the clock readout, a held-back badge, or an inspector's
  `surface / slot` label go, so status rides the rule line instead of a row
  below it.
- **trailing slot** (`a_trailing`, right-aligned to `a_trailingWidth`): the
  section's *actions* — a `RemoveButton`, a filter field, an "Add" button. Pass
  the total width of what you draw as `a_trailingWidth` so the right-alignment
  reserves correctly (`RowButtonWidth()`, `ButtonWidth(text)`, `ItemSpacingX()`
  are the width helpers).

`RuleResult` is `{ RuleClick click; bool open }` — `open` is false while a
collapsible rule is collapsed (early-return on it); `click` reports which
declarative `spec.buttons` was pressed. `RuleWithFilter(spec, FilterSpec)` is a
thin adapter that puts a live filter field in the trailing slot. Set
`leadingSpace = false` on the first rule of a pane so it sits flush.

### Tables — info rows, one shared style per role

`Table::Begin(id, columns, style)` opens a table; check `.Open()`, emit cells
with `.Cell()` in column order, `.End()` to close. `columns` is a list of
`Studio::Column{ label, Width }`. Never write a bare `ImGui::BeginTable`.

Column widths and any inline width use `Studio::Width` (`studio/Widgets.h`):
`Width::Fill(ratio = 1)` (stretch share), `Width::Fit(text)` (content width),
`Width::Px(px)` (fixed). `NextItemWidth(width, scale)` sizes the next input.

Table styles are **six shared constants in `studio/Widgets.h`** — never a
per-file `TableStyle` literal:

| Constant | Borders / headers | Used for |
|---|---|---|
| `kFormTable` | inner-horizontal, no headers | field/settings tables |
| `kColumnsTable` | none, no headers | headerless aligned lists (resource lists, inspector header) |
| `kGridTable` | all, headers, row backgrounds | full-page grids (Recipes, Setup, Board) |
| `kFooterTable` | all, headers | the clock footer |
| `kRelationTable` | inner-horizontal, headers | Driven-by / Used-by |
| `kLayerTable` | inner-horizontal, headers, row backgrounds | the paint layer stack |

### Status, values, badges

- `Problem` / `Warn` / `Ok` / `Dim` (`Colored` under the hood) are the four
  coloured one-liners. `DrawDiagnostics(span<const Diagnostic>, heldBack)` is
  the shared "Rows with problems" list (a `Rule` + the held-back line + one
  `Problem`/`Warn` per diagnostic) — call it, do not open-code the loop.
- `Badge(FieldKind)` draws the type indicator left of a value; `ValueSwatch(Value)`
  draws a colour chip + text for a resolved value; `ResourceTable(id,
  span<ResourceCells>)` draws the name / type / value inspector header row
  (`ResourceCells{ name, type, value }`).
- Buttons and layout: `RemoveButton(references) -> bool` (a trash control,
  tooltip carries the reference count), `SoloButton`/`MuteButton`,
  `DetailButton() -> bool`, `Disabled(cond, fn)` (wraps a draw in
  begin/endDisabled), `RightAligned(width, fn)`, `Tooltip(text)`,
  `HelpMarker(text)`. Prefer full-size `Button` over `SmallButton` for
  add/settings controls so sizes match.

## Forms and fields

A form is a `std::vector<Studio::FormField>` (`studio/Forms.h`) built by a pure
`*Form` function (`InspectorForm`, `SignalForm`, `SourceForm`, `LightForm`,
`ShellForm`, `RecipeHeaderForm`, `OutputHeaderForm` …). A `FormField` is
declarative data: `{ name, kind (FieldKind), text, names, detail (FieldDetail?),
value, bind, creators, create, range, workingRange, units, integral }`. Fields
are constructed by the small factories in `studio/Forms.cpp` —
`ParamField`/`ValueField` (typed values), `ChoiceField`, `ToggleField`,
`TextEntryField`, `MaskTextField`, `CurveTextField` — each wiring a `bind`
(commit) closure and, where offered, `creators`/`create` (the "new mask", "new
source", "promote to signal" entries). `FieldKind` and `FieldInputKind` +
`kFieldKinds` (`studio/Fields.h`) map a kind to its badge glyph, help text, and
input widget.

The menu renders a form with `DrawForm(id, form, frame, columns = 1) ->
optional<size_t>` (returns the index whose detail button was clicked),
`DrawFormWithSignals` (same, plus resolves a clicked detail to a navigation),
or a single field via `DrawRowField`. Under them: `DrawFieldTable` lays out
`name | detail | value` rows (the tuning slider draws inline in the value cell
when `FieldTunable(field, frame)`); `DrawFieldInput` → `FieldInput` /
`ValueWidget` draws the actual input; `CommitField` posts the field's `bind`
result. The value input width threads through `DrawFieldInput`/`FieldInput`/
`ValueWidget` as a `Studio::Width` so a tunable field can split the cell.

The **expression shelf** is the "n" number editor: `FieldHasNumberShelf(field,
minCount = 1)` (parses, has ≥ `minCount` numeric literals, has a bind, not a
whole reference) gates `DrawExpressionOpener`, which is drawn beside any value
field with editable numbers; `FieldHasExpressionShelf` (= `HasExpression &&
FieldHasNumberShelf`) additionally routes the field through the expression text
path (`menu/ExpressionShelf.*`). **Tuning** is `DrawTuning(field, frame, sink =
nullopt)` (`menu/Tuning.*`): a range slider that previews live and commits one
undo step on release.

## Editing is intents — a widget never mutates

A widget that changes anything **posts an intent**; it never writes the recipe
or the engine. `Studio::Post(*a_frame.intents, intent)` queues one
`Studio::Intent` (the variant in `studio/Intent.h`); `Studio::Post(intents,
recipeID, RecipeEdit)` is the shorthand for an `EditRecipe`. The frame's queue
is drained after the pass:

- `Perform(intent, state, view)` (`menu/Menu.cpp`) applies each intent through
  the `IntentPerformer` visitor, which is the *only* place the menu calls
  `Manager::Editor()` — recipe edits, view/isolation changes (`ChangeView`),
  paint session commands, undo/redo. Add a new intent's effect as one
  `operator()(const NewIntent &a_intent)` overload there.
- The menu-local reducers live in `studio/MenuState.cpp`: `AcceptIntent`
  gate-keeps (e.g. `RequiresSettledEditor` rejects edits while an indexed edit
  or paint commit is pending), `Reduce` applies UI-state changes (selection,
  resource tab, drafts), and `ResolveEditorSelection` re-resolves the selection
  against the new snapshot each frame. These are engine-free and tested.

Because the whole variant is dispatched by `Match` with no catch-all arm, a new
intent is a compile error at every visitor (`IntentPerformer`, the
`MenuState.cpp` reducers, `RequiresSettledEditor`) until handled — which is the
point. `kIntentCount` beside the variant asserts the count.

## Navigation — two contexts, one history

Navigating the inspector to a subject splits by where the click came from; both
push onto the shared `Navigation` history (`studio/Navigation.h`) so Back/Forward
work:

- **From the navigator tree** — `NavigateFromNavigator(frame, subject)`
  (`menu/Workspace.cpp`): `Navigate`, then switch paint mode back to compose.
  `PickSubject` (the selectable tree/resource rows) uses it.
- **From the inspector** — `NavigateFromInspector(frame, subject, property =
  nullopt)` (`menu/FormDraw.cpp`): resets the revealed property, captures scroll,
  `Navigate` or `NavigateProperty`, restores scroll. `Follow` (relationships)
  and the detail-button destinations delegate to it.

The reducers behind both are pure (`studio/Navigation.cpp`): `Navigate` /
`NavigateProperty` push the current visit onto `back` and clear `forward`;
`GoBack` / `GoForward` move a visit between the `back`/`forward` stacks through
the shared `StepHistory`; `InvalidateIndexedSubjects` prunes both when indices
shift. To select a resource that does not exist yet (just created), set
`state->pendingSelection` — `ResolvePendingSubject` navigates to it once it
appears in a snapshot; `FocusCreatedSubject` (`menu/FormDraw.cpp`) is the
create-then-focus helper the field creators use.

## UI conventions (in addition to `conventions.md`)

- Draw functions are `Draw<Thing>(const Frame &a_frame, …)`; the `a_` parameter
  prefix, `k`-prefixed constants, complete signatures (no `auto` in a signature),
  and one-name-per-concept all still hold. ImGui is reached only as `ImGuiMCP`
  (`namespace ImGui = ImGuiMCP;` per file).
- Editing goes through an intent; reading goes through the `Frame`'s snapshot;
  UI-only bookkeeping goes in `MenuState`. Do not reach for `Manager` from a
  widget — post an intent and let `IntentPerformer` reach it.
- A shared widget/style/reducer lives in one place: the six `k*Table` styles in
  `studio/Widgets.h`, the status list in `DrawDiagnostics`, the navigation
  helpers above. Grep before adding a per-file copy.
- Text a person sees is written once — a field's help text is `kFieldKinds`
  (`studio/Fields.h`), a diagnostic's is its `Diagnostic::message`; a widget
  shows the projection (`ProblemText`), it does not author the sentence.
- The no-comments gate excludes `src/studio` and `src/menu` today (see
  `docs/conventions.md`, Gates); keep the code self-describing anyway — a fact
  the code cannot state goes in `REFERENCE.md`, not an inline comment.

## Where things live

The live headers are authoritative: this document is grounded in the current
source (verified 2026-09-16), and the API here wins over any older UI note it
disagrees with. Grep the header before trusting a pointer below.

- `src/menu/MenuWidgets.h`, `src/studio/Widgets.h`, `src/menu/Frame.h`,
  `src/menu/FormDraw.h`, `src/studio/Forms.h`, `src/studio/Intent.h`,
  `src/studio/Navigation.h` — the real API surfaces this doc summarises.
- `docs/ui-design-principles.md` — the surface direction the primitives serve
  (direction, not API; read for intent).
- `docs/wip/ui-standardization.md` — the standardization case list (all closed)
  and what each case changed; current as of this pass.
- `docs/wip/ui-primitives-ownership.md`, `docs/wip/menu-ownership.md`,
  `docs/wip/studio-ownership.md` — per-file ownership maps. These **predate the
  standardization pass** (functions moved, were deleted, or were renamed since),
  so treat them as history: when one disagrees with the tree, the tree is right.
