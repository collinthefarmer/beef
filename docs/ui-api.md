# BetterEnchantmentEffects — the studio UI, API and conventions

How a studio surface is built: the widget vocabulary, the **frame**, forms
and fields, editing through **intents**, and navigation.

- `docs/conventions.md` governs the whole codebase. This document covers the
  `menu/` layer and the UI-facing half of `studio/`.
- `docs/ui-design-principles.md` states the direction. This document states
  the mechanics.
- Every rule cites a real symbol by file and name. Grep for the symbol
  before you write a variant.

## Two layers

The UI splits the same way the rest of the tree does: a functional core and
a thin adapter.

**`studio/`** is engine-free and native-tested. It holds the UI vocabulary
as plain data and pure functions. Nothing under `studio/` includes `engine/`
or names `RE::`.

| Vocabulary | Where |
|---|---|
| Widget specs: `Width`, `Column`, `TableStyle`, `RuleSpec`/`RuleButton`/`RuleAction`, `ThumbnailSpec` | `studio/Widgets.h` |
| Field and **form** model | `studio/Fields.h`, `studio/Forms.h` |
| Editor state | `studio/MenuState.h` |
| Intents | `studio/Intent.h` |
| Selection, navigation, and **isolation** reducers | `studio/Selection.h`, `studio/Navigation.h`, the `Isolation` struct in `studio/View.h` (implemented in `studio/Isolation.cpp`) |
| **Snapshot** rows the menu reads | `studio/Snapshot.h`, built by `studio/Rows.cpp` and `studio/Panels.cpp` |

**`menu/`** is the ImGui renderer over that vocabulary.

- `menu/MenuWidgets.*` is the widget toolkit. `menu/FormDraw.*` draws forms.
- The page renderers (`StudioPage`, `RecipesPage`, `SetupPage`, `BoardPage`)
  and the panels (`Workspace`, `StackPanel`, `RelationshipPanel`,
  `PaintPanel`, `ContextRows`) compose them.
- Every `.cpp` that touches ImGui aliases `namespace ImGui = ImGuiMCP;`.
  ImGui is reached only through the `ImGuiMCP` wrapper in `src/extern`,
  never directly.

A draw function is named `Draw<Thing>` and reads as a sequence of domain
steps. Mechanical ImGui work (variant visiting, id juggling, width math)
lives behind a named helper in `MenuWidgets`, so a page function stays
domain-shaped.

## The `Frame`

Every draw function takes `const Frame &a_frame` (`menu/Frame.h`). A
**frame** is the read model plus the two write channels for one render pass:

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

- Read the frame through an accessor where one exists: `ActorOf`,
  `SelectionOf`, `ViewOf`, `LayoutOf` (`menu/Frame.h`; defined in
  `menu/Menu.cpp`).
- `snapshot`, `piece`, `recipe`, `geometry`, and `names` may be null. Guard
  before you dereference: `if (!a_frame.recipe) return;`. `state` and
  `intents` are always live.
- `snapshot` is the published, immutable read model for this frame.
  `intents` is where a widget asks for a change. `state` is menu-only
  bookkeeping: selection, navigation history, drafts, **tuning**.
- A widget never writes `snapshot`. A widget never mutates a **recipe**
  directly.

## The widget vocabulary (`menu/MenuWidgets.h`)

### Rules

A **rule** is the titled separator that heads a section. `Rule` is the one
section-header idiom; `SeparatorText` is retired.

```
RuleResult Rule(const Studio::RuleSpec &a_spec,
                float a_trailingWidth = 0.0f,
                const std::function<void()> &a_trailing = {},
                const std::function<void()> &a_leading  = {});
```

- `RuleSpec` (`studio/Widgets.h`) is `{ text, buttons, collapsible,
  openByDefault, leadingSpace }`.
- A rule draws: an optional leading gap, the separator, the label, then the
  two slots.
- The leading slot (`a_leading`, drawn after the label) holds the section's
  one-line status: `Warn`/`Dim`/`Problem` text, a count, a **badge**.
  Solo/mute, the clock readout, a held-back badge, and an inspector's
  `surface / slot` label go here, so status rides the rule line instead of a
  row below it.
- The trailing slot (`a_trailing`, right-aligned to `a_trailingWidth`) holds
  the section's actions: a `RemoveButton`, a filter field, an "Add" button.
  Pass the total width of what you draw as `a_trailingWidth`, so the
  right-alignment reserves the space. The width helpers: `RowButtonWidth()`,
  `ButtonWidth(text)`, `ItemSpacingX()`.
- `RuleResult` is `{ RuleClick click; bool open }`. `open` is false while a
  collapsible rule is collapsed; early-return on it. `click` reports which
  declarative `spec.buttons` entry was pressed.
- `RuleWithFilter(spec, FilterSpec)` puts a live filter field in the
  trailing slot.
- Set `leadingSpace = false` on the first rule of a pane, so the rule sits
  flush.

### Tables

- `Table::Begin(id, columns, style)` opens a table. Check `.Open()`, emit
  cells with `.Cell()` in column order, close with `.End()`. Do not write a
  bare `ImGui::BeginTable`.
- `columns` is a list of `Studio::Column{ label, Width }`.
- Every column width and inline width uses `Studio::Width`
  (`studio/Widgets.h`): `Width::Fill(ratio = 1)` for a stretch share,
  `Width::Fit(text)` for content width, `Width::Px(px)` for a fixed width.
  `NextItemWidth(width, scale)` sizes the next input.
- Use one of the six shared style constants in `studio/Widgets.h`. Do not
  write a per-file `TableStyle` literal.

| Constant | Borders / headers | Used for |
|---|---|---|
| `kFormTable` | inner-horizontal, no headers | field and settings tables |
| `kColumnsTable` | none, no headers | headerless aligned lists (resource lists, inspector header) |
| `kGridTable` | all, headers, row backgrounds | full-page grids (Recipes, Setup, Board) |
| `kFooterTable` | all, headers | the clock footer |
| `kRelationTable` | inner-horizontal, headers | Driven-by / Used-by |
| `kLayerTable` | inner-horizontal, headers, row backgrounds | the paint **layer** stack |

### Status, values, badges

`Problem`, `Warn`, `Ok`, and `Dim` (`Colored` under the hood) are the four
coloured one-line texts.

| Widget | What it does |
|---|---|
| `DrawDiagnostics(span<const Diagnostic>, heldBack)` | Draws the shared "Rows with problems" list: a rule, the held-back line, one `Problem`/`Warn` per **diagnostic**. Call it; do not open-code the loop. |
| `Badge(FieldKind)` | Draws the type indicator left of a value. |
| `ValueSwatch(Value)` | Draws a colour chip plus text for a resolved value. |
| `ResourceTable(id, span<ResourceCells>)` | Draws the name / type / value inspector header row (`ResourceCells{ name, type, value }`). |
| `RemoveButton(references) -> bool` | The trash control. The tooltip carries the reference count. |
| `CloseButton() -> bool` | The modal `[X]`. |
| `SquareToggle(label, bool&, tooltip) -> bool` | The square, label-inside toggle. It fills with the active colour when on. `SoloButton`, `MuteButton`, and the channel toggles are `SquareToggle`s. |
| `DetailButton() -> bool` | The detail opener. |
| `Disabled(cond, fn)` | Wraps a draw in begin/endDisabled. |
| `RightAligned(width, fn)`, `Tooltip(text)`, `HelpMarker(text)` | Layout and help helpers. |
| `LabelWithHelp(label, help)` | A label plus a `(?)` marker, shown only when `help` is non-empty. |
| `ConfirmModal(title, message, affirm, onAffirm)` | The destructive-confirm dialog: a `DetailModal` with a message and right-aligned `[affirm]`/Cancel. Reuse it; do not hand-roll a confirm. |
| `DimFitted(text)` | Draws dimmed text truncated to the cell's available width (through `ExpressionSummary`), so a value column shows what fits and follows a resize. |

Prefer the full-size `Button` over `SmallButton` for add and settings
controls, so control sizes match.

Resource display has one path per job:

| Function | Resolves |
|---|---|
| `SourceKindName(kind)` | The canonical `image`/`material`/… label. |
| `SourceValueText(kind)` | A **source**'s value text: a per-kind `Match`, one arm per source kind. Edit an arm to change how that kind reads. |
| `ResourceValueText(recipe, ref)` | Any resource's display value. |
| `ExpressionSummary(text, max)` (`recipe/Expression.h`, engine-free) | A shortened expression: whitespace collapse and word-boundary middle elision. |

The resource tabs and the Driven-by/Used-by tables use these for their value
columns. Used-by also shows a discrete Type column and descriptive owner
names (`material emissive`, `material emissive / layer 2`).

## Forms and fields

A **form** is a `std::vector<Studio::FormField>` (`studio/Forms.h`), built
by a pure `*Form` function: `InspectorForm`, `SignalForm`, `SourceForm`,
`LightForm`, `ShellForm`, `RecipeHeaderForm`. `OutputHeaderForm` returns an
`OutputHeader`; its `fields` member is that vector.

A **field** (`FormField`) is declarative data: `{ name, kind (FieldKind),
text, names, detail (FieldDetail?), value, bind, creators, create, range,
workingRange, units, integral }`. `FieldKind` and `FieldInputKind` with
`kFieldKinds` (`studio/Forms.h`) map a kind to its badge glyph, help text,
and input widget.

The field factories construct fields. Each factory wires a `bind` (commit)
closure and, where offered, the `creators`/`create` entries ("new mask",
"new source", "promote to signal"):

| Factory | Defined in |
|---|---|
| `ValueField`, `ChoiceField`, `ToggleField`, `TextEntryField` | `studio/Fields.cpp` |
| `ParamField`, `CurveTextField`, `MaskTextField` | `studio/Forms.cpp` |

The menu draws a form through `menu/FormDraw.*`:

| Function | Behaviour |
|---|---|
| `DrawForm(id, form, frame, columns = 1) -> optional<size_t>` | Draws the form. Returns the index whose detail button was clicked. |
| `DrawFormWithSignals(…) -> void` | Same layout; resolves a clicked detail to a navigation itself. |
| `DrawRowField` | Draws a single field. |
| `DrawFieldTable` | Lays out the `name \| detail \| value` rows. The tuning slider draws inline in the value cell when `FieldTunable(field, frame)`. |
| `DrawFieldInput` → `FieldInput` / `ValueWidget` | Draws the actual input. |
| `CommitField` | Posts the field's `bind` result. |

The value input width threads through `DrawFieldInput`, `FieldInput`, and
`ValueWidget` as a `Studio::Width`, so a tunable field can split the cell.

The **expression shelf** is the editor for the numbers inside an expression:

- `FieldHasNumberShelf(field, minCount = 1)` gates `DrawExpressionOpener`.
  The gate: the text parses, holds at least `minCount` numeric literals, has
  a bind, and is not a whole reference. The opener draws beside any value
  field with editable numbers.
- `FieldHasExpressionShelf` equals `HasExpression && FieldHasNumberShelf`.
  It additionally routes the field through the expression text path
  (`menu/ExpressionShelf.*`).

**Tuning** is `DrawTuning(field, frame, sink = nullopt)` (`menu/Tuning.*`):
a range slider that previews live and commits one undo step on release.

## Editing is intents

A widget that changes anything posts an **intent**. It never writes the
recipe or the engine.

- `Studio::Post(*a_frame.intents, intent)` queues one `Studio::Intent` (the
  variant in `studio/Intent.h`). `Studio::Post(intents, recipeID,
  RecipeEdit)` is the shorthand for an `EditRecipe`.
- The frame's queue drains after the pass:
  - `Perform(intent, state, view)` (`menu/Menu.cpp`) applies each intent
    through the `IntentPerformer` visitor. `IntentPerformer` is the only
    place the menu calls `Manager::Editor()`: recipe **edits**,
    view/isolation changes (`ChangeView`), **paint** session commands,
    undo/redo. Add a new intent's effect as one
    `operator()(const NewIntent &a_intent)` overload there.
  - The menu-local reducers live in `studio/MenuState.cpp`. `AcceptIntent`
    gatekeeps: `RequiresSettledEditor` rejects edits while an indexed edit
    or paint commit is pending. `Reduce` applies UI-state changes:
    selection, resource tab, drafts. `ResolveEditorSelection` re-resolves
    the selection against the new snapshot each frame. All three are
    engine-free and tested.
- `Match` dispatches the variant with no catch-all arm, so a new intent is a
  compile error at `IntentPerformer` and at the `MenuState.cpp` reducers
  until it is handled. `kIntentCount` beside the variant asserts the count.
- `RequiresSettledEditor` is an `Is<…>` disjunction, not a `Match`. A new
  intent passes it silently. When the new intent edits the recipe, add it to
  that list by hand.

## Navigation

Navigating the inspector to a subject splits by where the click came from.
Both paths push onto the shared `Navigation` history
(`studio/Navigation.h`), so Back and Forward work:

- From the navigator tree: `NavigateFromNavigator(frame, subject)`
  (`menu/Workspace.cpp`) navigates, then switches paint mode back to
  compose. `PickSubject` (the selectable tree and resource rows) uses it.
- From the inspector: `NavigateFromInspector(frame, subject, property =
  nullopt)` (`menu/FormDraw.cpp`) resets the revealed property, captures
  scroll, calls `Navigate` or `NavigateProperty`, and restores scroll.
  `Follow` (relationships) and the detail-button destinations delegate to
  it.

The reducers behind both are pure (`studio/Navigation.cpp`):

| Reducer | Behaviour |
|---|---|
| `Navigate` / `NavigateProperty` | Push the current visit onto `back`; clear `forward`. |
| `GoBack` / `GoForward` | Move a visit between the `back` and `forward` stacks through the shared `StepHistory`. |
| `InvalidateIndexedSubjects` | Prune both stacks when indices shift. |

To select a resource that does not exist yet (just created), set
`state->pendingSelection`. `ResolvePendingSubject` navigates to the subject
once it appears in a snapshot. `FocusCreatedSubject` (`menu/FormDraw.cpp`)
is the create-then-focus helper the field creators use.

## UI conventions

These hold in addition to `docs/conventions.md`:

- A draw function is `Draw<Thing>(const Frame &a_frame, …)`. The `a_`
  parameter prefix, the `k` constant prefix, complete signatures (no `auto`
  in a signature), and one name per concept all still hold. Reach ImGui only
  as `ImGuiMCP` (`namespace ImGui = ImGuiMCP;` in each file that touches
  it).
- Editing goes through an intent. Reading goes through the frame's snapshot.
  UI-only bookkeeping goes in `MenuState`. Do not reach `Manager` from a
  widget; post an intent and let `IntentPerformer` reach it.
- A shared widget, style, or reducer lives in one place: the six `k*Table`
  styles in `studio/Widgets.h`, the status list in `DrawDiagnostics`, the
  navigation helpers above. Grep before you add a per-file copy.
- Text a person sees is written once. A field's help text is `kFieldKinds`
  (`studio/Forms.h`). A diagnostic's text is its `Diagnostic::message`. A
  widget shows the projection (`ProblemText`); it does not author the
  sentence.
- The no-comments gate excludes `src/studio` and `src/menu` today
  (`docs/conventions.md`, Gates). Keep the code self-describing anyway. A
  fact the code cannot state goes in `REFERENCE.md`, not in an inline
  comment.

## Where things live

The live headers are authoritative. When this document and a header
disagree, the header is right. Grep the header before you trust a pointer
below.

- `src/menu/MenuWidgets.h`, `src/studio/Widgets.h`, `src/menu/Frame.h`,
  `src/menu/FormDraw.h`, `src/studio/Forms.h`, `src/studio/Intent.h`,
  `src/studio/Navigation.h` — the API surfaces this document summarises.
- `docs/ui-design-principles.md` — the surface direction the primitives
  serve. Read it for intent, not API.
- `docs/checkpoints/ui-standardization.md` — the standardization case list
  (all closed) and what each case changed.
- `docs/history/ui-primitives-ownership.md`, `docs/history/menu-ownership.md`,
  `docs/history/studio-ownership.md` — per-file ownership maps. They predate
  the standardization pass; functions moved, were deleted, or were renamed
  since. When one disagrees with the tree, the tree is right.
