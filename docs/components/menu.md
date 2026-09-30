# menu/

The ImGui renderer over the studio widget vocabulary. It draws the Studio,
Recipes, and Setup pages. It turns user clicks into `Studio::Intent`s. It
applies those **intents** through `Manager::Editor()`. Its row in `ALLOWS`
(`tools/gate.py`) is `'menu': ADAPTER + ('engine', 'menu')`. `ADAPTER` names
every engine-free directory, `render`, `PCH.h`, `Identity.h`, `Settings.h`,
and `SettingsFile.h`. Only `main.cpp` includes `menu/`. The module is
engine-facing: it calls Dear ImGui through the `ImGuiMCP` wrapper and SKSE
Menu Framework. No native test covers it.

## What it owns

- The ImGui drawing of the studio's `Studio::FormField`, `Studio::Column`,
  and `Studio::RuleSpec` records.
- The widget toolkit in `MenuWidgets.h`/`.cpp`: tables, **rules**, combos,
  **badges**, buttons, modals, and status text.
- The **form** and **field** drawers in `FormDraw.h`/`.cpp`.
- The page renderers: `StudioPage.cpp`, `RecipesPage.cpp`, `SetupPage.cpp`,
  and `BoardPage.cpp`.
- The panels that the Studio page composes: `Workspace.cpp`,
  `StackPanel.cpp`, `RelationshipPanel.cpp`, `ContextRows.cpp`,
  `ResourcePanels.cpp`, `ResponsePanel.cpp`, and `RecipeActions.cpp`.
- The special-input popups: `ExpressionShelf.cpp`, `Tuning.cpp`,
  `PatternChooser.cpp`, and `InputBrowser.cpp`.
- The **mask** editor in `PaintPanel.cpp`.
- The menu lifecycle in `Menu.cpp`: `RegisterMenu`, `Perform`, `Dispatch`,
  and the shared status strips.

`studio/` holds the pure widget vocabulary: `Width`, `Column`, `TableStyle`,
`RuleSpec`, and `ThumbnailSpec` in `studio/Widgets.h`, and `FormField` in
`studio/Forms.h`. `MenuWidgets.cpp` is the only file under `menu/` that
includes `render/`.

`RegisterMenu` calls `CheckMenuFramework` (`engine/MenuDependency.h`) before
it adds the three section items. `CheckMenuFramework` requires a loaded
framework module and every export in its inventory. On a failure,
`RegisterMenu` logs the problem and registers nothing. `RenderPendingStatus`
draws the page when no **snapshot** exists. It shows `HookProblem()` when
that text is not empty. It reports a missing `CommunityShaders.dll`
otherwise.

## Data

### The per-draw context (`Frame.h`)

Every panel function takes `const Frame &a_frame`. A page renderer builds
one **frame** per draw from the published **snapshot**. Every panel below
the page reads the same record and posts its edits into `intents`.

| Member | Description |
|---|---|
| `Frame` | Holds the `Studio::Snapshot`, the selected `Studio::PieceRow`, `Studio::RecipeRow`, and `Studio::GeometryRow`, the `Studio::Names`, the mutable `Studio::MenuState *state`, the outbound `Studio::Intents *intents`, and the DPI `scale` (default `1.0f`). |
| `ActorOf` | Returns the selected actor's `Studio::FormID`. |
| `SelectionOf`, `ViewOf`, `LayoutOf` | Return the frame's `Studio::Selection`, `Studio::View`, and `Studio::Layout`. |

### Widget records (`MenuWidgets.h`)

These records carry a widget's input or its result. A widget returns the
user's edit as a value, usually an `std::optional`. The caller turns that
value into an **intent**.

| Type | Description |
|---|---|
| `TextCheck` | A function from typed text to an optional problem text. A text widget shows the problem under the field. |
| `kNewInputChoice` | The reserved combo choice that opens the new-input wizard. `CommitField` (`FormDraw.cpp`) tests for it. |
| `FieldScope` | An RAII ImGui ID scope. It pushes one part of the current **field** key for its lifetime. |
| `WidgetSize` | A `Studio::Width` and DPI scale pair that sizes a **field** widget. |
| `Table` | The column table. `Begin` opens it from `Studio::Column`s and a `Studio::TableStyle`. `Cell` advances one cell. `End` closes it. |
| `RuleResult` | The result of a **rule** header: the `Studio::RuleClick` and the open state. |
| `RuleFilter`, `FilterSpec` | The result and the input of `RuleWithFilter`. `FilterSpec` names the filter box key, hint, and width. |
| `SearchComboSpec`, `SearchPick` | The input and the result of `SearchCombo`. A `SearchPick` is the index of a listed label or a custom string. |
| `CandidateRowsSpec` | The input of `DrawCandidateRows`: the filter, the `Studio::GameObjectCatalog`, and the row cap. |
| `EntryModeTips` | The two tooltips of `ModeBadge`: one for combo entry, one for typed entry. |
| `ResourceCells` | One resource row for `ResourceTable`: a name, a type, and an optional `Value`. |

### Widgets (`MenuWidgets.h`)

Every panel draws with these functions. `MenuWidgets.cpp` implements them.
The functions group by what they draw.

| Member | Description |
|---|---|
| `ResolveWidth`, `NextItemWidth`, `FitWidth`, `WidestOf`, `ButtonWidth`, `TextWidth`, `ItemSpacingX`, `RowButtonWidth`, `RuleHeight` | Size helpers. They turn a `Studio::Width` or a text into pixels. |
| `TextField`, `LiveTextField`, `ChoiceCombo`, `ReferenceCombo`, `ValueWidget` | The **field** widgets. `TextField`, `ChoiceCombo`, `ReferenceCombo`, and `ValueWidget` return the committed text, or `nullopt` when the user made no edit. `LiveTextField` returns the current text on every frame. |
| `SearchCombo`, `DrawCandidateRows` | The searchable combo and the filtered game-object candidate list. |
| `Badge`, `ModeBadge`, `BlendGlyph`, `BlendBadge` | The **badge** glyphs. `Badge` draws a `Studio::FieldKind` glyph. `ModeBadge` toggles a **field** between combo entry and typed entry. `BlendBadge` draws a `Blend` glyph and returns a new `Blend` on a click. |
| `ValueText`, `ValueSwatch`, `SourceValueText`, `ResourceValueText`, `RecipeLabel`, `HeldLabel` | Value and label text. `RecipeLabel` marks a pinned **recipe** as `(pinned here)`. |
| `ResourceTable`, `Thumbnail`, `ThumbnailButton` | A table of `ResourceCells` rows, and a texture thumbnail from a `Studio::ThumbnailSpec`. |
| `Rule`, `RuleWithFilter`, `Banner` | The **rule** headers and the banner strip. A rule draws a title, optional buttons, and optional leading and trailing content. |
| `Split` | Draws two panes with a drag bar. It returns the new ratio when the user drags the bar. |
| `Toggle`, `SquareToggle`, `SoloButton`, `MuteButton`, `PeekButton`, `DetailButton`, `RemoveButton`, `CloseButton` | The buttons. `RemoveButton` takes the reference count of the row it removes. |
| `DragHandle`, `DropTarget` | Row reordering. `DropTarget` returns a `Studio::RowMove` when the user drops a row. |
| `DetailModal`, `ConfirmModal` | The modals. `ConfirmModal` runs its callback only on the affirm click. |
| `RightAligned`, `Disabled` | Layout wrappers around a draw callback. |
| `Problem`, `Warn`, `Ok`, `Dim`, `DimFitted`, `ProblemBadge`, `WarnBadge`, `DimBadge`, `PlaceholderText` | The status text family. |
| `DrawDiagnostics` | Draws a span of `Diagnostic`s and marks held-back edits. |
| `HelpMarker`, `LabelWithHelp`, `Tooltip` | Help text. |

### Form and field drawing (`FormDraw.h`)

These functions turn a span of `Studio::FormField`s into drawn inputs and
posted **intents**. `FieldInput` draws one field's widget. The functions
above it place fields in tables and multi-column **forms**.

| Member | Description |
|---|---|
| `FieldInput` | Draws one **field**'s widget by the field's `FieldInputKind` and returns the committed text. |
| `PostField` | Posts one field's committed text as an `EditRecipe` **intent**. It rejects text that the field's `bind` cannot turn into a `RecipeEdit`. |
| `DrawRowField` | Draws one field outside a table, with its input wizard and **tuning** control. The mask and curve inspectors in `Workspace.cpp` use it. |
| `DrawFieldTable`, `DrawForm` | Place a **field** span in a table or in a multi-column **form**. Each returns the index of the field whose detail button the user clicked. |
| `DrawFormWithSignals` | Draws a **form** through `DrawForm`. A clicked signal detail button navigates to that signal. The shell and light inspectors use it. |
| `DrawInspectorFields` | Draws a `Studio::Inspector`'s fields through `DrawForm`. It follows a clicked detail button to the referenced subject. |
| `NavigateFromInspector` | Navigates the view to a `Studio::InspectorSubject`, optionally to one `PropertyLocation`. |
| `FirePopup` | Draws the fire popup for one `Studio::SignalRow`. It posts a `FireTrigger` with the chosen node, offset, random spread, and value. |
| `DrawSelector` | Draws one output's `Studio::SelectorView`. |

### Special-input popups

Some **fields** need more than an inline widget. Each popup opens from a
field or a panel control. Each popup edits through the **frame**'s
intents, as the inline widgets do.

| Member | Description | Declared in |
|---|---|---|
| `FieldHasNumberShelf`, `FieldHasExpressionShelf`, `DrawExpressionOpener` | The expression shelf. `FieldHasNumberShelf` parses the field text with `Program::Parse` and counts its numeric literals. `DrawExpressionOpener` draws the button beside the field. | `ExpressionShelf.h` |
| `DrawTuning`, `TuningSink`, `FieldTunable` | The **tuning** control for a numeric **field**. An optional `TuningSink` sends the commit to a callback. | `Tuning.h` |
| `BeginTuningFrame`, `EndTuningFrame`, `FinishTuning` | The per-frame **tuning** lifecycle over `Studio::MenuState`. `FinishTuning` commits or discards the held value. | `Tuning.h` |
| `DrawPatternChooser` | Draws the filtered list of `Studio::TermOffer` patterns. | `PatternChooser.h` |
| `OpenInputWizard`, `DrawInputWizard` | The new-input wizard. `OpenInputWizard` opens it. `DrawInputWizard` draws it for the **field** that asked. | `InputBrowser.h` |

### The mask editor (`PaintPanel.h`)

The mask editor edits one **mask** as a list of terms in a **paint**
draft. `EditMaskAsTerms` starts a draft, and the other functions draw it.
The draft previews on a reserved recipe, `Studio::kPaintRecipe`.

| Member | Description |
|---|---|
| `EditMaskAsTerms` | Posts `LoadMask`, `SetMode`, and `BeginPaint` for one mask `Studio::TextRow`. It keys the draft with `DefaultKeyOf` (`ContextRows.h`). |
| `DrawMaskTask` | Draws the draft: the head, the rule, and the term stack over the paint recipe's geometry. It posts `ReadMesh` for each geometry not yet read. |
| `DrawPaintHead`, `DrawPaintDraftBar`, `DrawMaskRule` | The draft head, the banner with resume and discard, and the rule with the keep and discard buttons from `Studio::MaskRuleButtonsOf`. |
| `DrawMaskStack`, `DrawTermTuningPane` | The term rows and the per-term **tuning** pane. The preview pane (`DrawPreview`, `Workspace.cpp`) draws `DrawTermTuningPane`. |
| `RebuildScratch` | Posts the `UpdatePaint` intent that `Studio::PendingPaintUpdate` returns. |

### Panels composed by the pages

Each panel is one free function over the **frame**. A panel reads the
**snapshot** rows and posts **intents** for its edits.

| Member | Description | Declared in |
|---|---|---|
| `DrawWorkspace` | The Studio page body. It draws a navigator, an inspector, and a preview pane side by side, or behind buttons on a narrow window. | `Workspace.h` |
| `DrawStack` | Draws a `Studio::LayerStack` problem and its `Studio::Inspector` fields. | `StackPanel.h` |
| `DrawBoard`, `DrawBoardPage` | The **board**. `DrawBoard` draws one `Studio::Board`. `DrawBoardPage` draws the Recipes page section around it. | `BoardPage.h` |
| `HasRelationships`, `DrawRelationships` | The relationships panel: what drives the subject and what uses it. | `RelationshipPanel.h` |
| `DefaultKeyOf`, `DrawRecipeSettings`, `DrawStudioContext`, `DrawOutputHeader` | The context rows. `DefaultKeyOf` returns a piece's armor key, or its first key. The others draw the **recipe** settings, the studio context line, and one output's header with its scalar **fields**. | `ContextRows.h` |
| `PostResourceAdd`, `DrawResourceRename` | Resource actions. `PostResourceAdd` posts the add intent for one `Studio::ResourceTab`. `DrawResourceRename` checks the new name and posts the rename edit. | `ResourcePanels.h` |
| `DrawResponse` | Draws one `Studio::SignalRow`'s response graph. | `ResponsePanel.h` |
| `RecipeFilePending`, `DrawRecipeRuleStatus`, `DrawRecipeSaveRevert`, `DrawMaskDraftHints`, `DrawRecipeFileActions` | The **recipe** file controls. `RecipeFilePending` is true while a file operation on the recipe runs. | `RecipeActions.h` |

### The menu lifecycle (`Menu.h`)

`Menu.cpp` owns the path from a posted **intent** to an engine edit. It
also owns the SKSE Menu Framework registration. Each page file defines its
own entry point and drains its intents through `Dispatch`.

| Member | Description |
|---|---|
| `Perform` | Matches one **intent** onto `IntentPerformer` (`Menu.cpp`). `IntentPerformer` turns edit and view intents into `Manager::Editor()` calls. It is the only intent-path caller of `Editor()`. `RecipeActions.cpp`, `Tuning.cpp`, `RecipesPage.cpp`, and `StudioPage.cpp` also call `Editor()` outside the intent path. |
| `Dispatch` | Drains a frame's `Studio::Intents`. For each intent it calls `Studio::AcceptIntent`, `FinishTuning`, `Perform`, `Studio::Reduce`, and `FollowPickedSubject`. |
| `RenderStatus`, `RenderHeader`, `RenderPendingStatus` | Draw the shared status and header strips from the **snapshot**, and the page shown before the first snapshot. |
| `RenderStudio`, `RenderRecipes`, `RenderSetup` | The three `__stdcall` entry points. `StudioPage.h` declares `RenderStudio`. `Menu.h` declares `RenderRecipes` and `RenderSetup`. Each page file defines its own. |
| `RegisterMenu` | Registers the three entry points with SKSE Menu Framework. |

## How a frame flows

```
RenderStudio()                                                 StudioPage.cpp
  │  Manager::LatestSnapshot() → Studio::Snapshot (immutable this frame)
  │  no snapshot → RenderPendingStatus()                       Menu.cpp
  │  BeginTuningFrame(state, snapshot)                         Tuning.cpp
  │  build Frame{snapshot, piece, recipe, geometry, names, state, intents}
  ▼
DrawStudioFrame ──▶ DrawBody ──▶ DrawWorkspace                 StudioPage.cpp
  ▼
DrawWideWorkspace / DrawNarrowWorkspace                        Workspace.cpp
  ├─ DrawNavigator, DrawPreview (DrawTermTuningPane)
  └─ DrawInspectorPane ──▶ DrawInspectorBody ──▶ DrawSubject
       ├─ OutputSubject ──▶ DrawOutput
       │     Studio::BuildStackView, Studio::BuildInspector     (studio/, pure)
       │     DrawOutputHeader                                   ContextRows.cpp
       │     DrawStack ──▶ DrawInspectorFields                  StackPanel.cpp
       ├─ LayerSubject ──▶ DrawLayerInspector ──▶ DrawInspectorFields
       └─ MaskSubject ──▶ DrawMaskInspector
             ──▶ DrawMaskTask (draft open)                      PaintPanel.cpp
             ──▶ DrawRowField (otherwise)                       FormDraw.cpp
  ▼
DrawInspectorFields ──▶ DrawForm ──▶ DrawFieldTable            FormDraw.cpp
  ──▶ DrawFieldInput ──▶ FieldInput
        (Badge, TextField, ChoiceCombo, ReferenceCombo, ValueWidget) MenuWidgets.cpp
  ──▶ CommitField ──▶ PostField ──▶ Studio::Post(EditRecipe)    FormDraw.cpp
        (queues the intent; changes no state)
  ▼
EndTuningFrame(state)                                          Tuning.cpp
Dispatch(intents, state, snapshot), for each intent:           Menu.cpp
  ├─ Studio::AcceptIntent  (rejects while an edit is pending)  studio/MenuState.cpp
  ├─ FinishTuning(state, true)                                 Tuning.cpp
  ├─ Perform ──▶ IntentPerformer ──▶ Manager::Editor()          Menu.cpp
  ├─ Studio::Reduce  (UI-local state; runs before Perform       studio/MenuState.cpp
  │                   for EditRecipe, Undo, and Redo)
  └─ FollowPickedSubject                                       Menu.cpp
  ▼
RebuildScratch(frame) posts UpdatePaint                        PaintPanel.cpp
Dispatch(intents, state, snapshot)                             StudioPage.cpp
  ▼
next tick: Manager publishes a new Snapshot; RenderStudio reads it
```

- `RenderRecipes` (`RecipesPage.cpp`) uses the same pipeline. Its
  `DrawSelection` builds a `Frame`, draws `DrawBoardPage` (`BoardPage.cpp`),
  and drains the intents through `Dispatch`. `DrawResolved` lists each
  `RecipeSelection` whose `SelectionOutcome` is not `kSelected`.
- `RenderSetup` (`SetupPage.cpp`) builds no `Frame` and posts no intents.
  It changes `Settings` through `SetSettings` and `SaveSettingsToDisk`. It
  calls `Manager::ReapplyAll` after a change that needs it.

## The files

| Concern | Files |
|---|---|
| Per-draw context, entry points, intent dispatch | `Frame.h`, `Menu.h`/`.cpp` (`IntentPerformer`, `Perform`, `Dispatch`, `RegisterMenu`) |
| Widget toolkit | `MenuWidgets.h`/`.cpp` (the only `render/` include) |
| Form and field drawing | `FormDraw.h`/`.cpp` |
| Pages | `StudioPage.h`/`.cpp`, `RecipesPage.cpp`, `SetupPage.cpp`, `BoardPage.h`/`.cpp` |
| Studio panels | `Workspace.h`/`.cpp` (navigator, inspector subjects, preview), `StackPanel.h`/`.cpp`, `RelationshipPanel.h`/`.cpp`, `ContextRows.h`/`.cpp`, `ResourcePanels.h`/`.cpp`, `ResponsePanel.h`/`.cpp`, `RecipeActions.h`/`.cpp` |
| Special-input popups | `ExpressionShelf.h`/`.cpp`, `Tuning.h`/`.cpp`, `PatternChooser.h`/`.cpp`, `InputBrowser.h`/`.cpp` |
| Mask editor | `PaintPanel.h`/`.cpp` |

## See also

- `REFERENCE.md` → *Menu mechanics* and *Recipe CRUD travels one pipeline*:
  the ImGui ID mechanics, the preview lifecycle, and the path from an intent
  to `RecipeStore`.
- `REFERENCE.md` → *Menu dependency preflight*: why registration checks the
  loaded framework and its exports.
- `docs/ui-api.md`: the `Frame` contract, the widget vocabulary, the
  intent-posting rule, and navigation.
- `docs/conventions.md` → *Diagnostics*, *Variants and closed sets*
  (*Dispatch with `Match`*), and *Gates* (*Layers*).
