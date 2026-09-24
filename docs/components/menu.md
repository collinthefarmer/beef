# menu/

The ImGui renderer over the studio widget vocabulary. It draws the Studio,
Recipes, and Setup pages, turns user clicks into `Studio::Intent`s, and
applies those **intents** by calling `engine/Manager`. Per `tools/layers.sh`
it is the top layer: `menu` may include everything (`Core.h recipe mesh
planners diagnostics studio render engine menu`, plus
`PCH.h`/`Identity.h`/`Settings*.h`). It is engine-facing (Dear ImGui through
the `ImGuiMCP` wrapper, SKSE) and not native-tested.

## What it owns

- The ImGui rendering of the studio's `Page`/`FormField`/`Column`
  vocabulary.
- The widget toolkit: `MenuWidgets.*` — **rules**, tables, combos,
  **badges**, diagnostics rendering.
- The **form** and **field** drawers: `FormDraw.*`.
- The page renderers (`StudioPage`, `RecipesPage`, `SetupPage`, `BoardPage`)
  and the panels they compose (`Workspace`, `StackPanel`,
  `RelationshipPanel`, `ContextRows`, `ResourcePanels`, `ResponsePanel`,
  `PatternChooser`, `InputBrowser`).
- The special-input popups: `ExpressionShelf`, `Tuning`, and `PaintPanel`'s
  **mask** editor.
- The menu lifecycle: `Menu.cpp`'s `RegisterMenu`, intent dispatch, and the
  `__stdcall` entry points SKSE Menu Framework calls each frame.

## Data

`studio/` holds the pure widget vocabulary (`Width`, `Column`, `TableStyle`,
`RuleSpec`, `ThumbnailSpec` in `studio/Widgets.h`; `FormField` in
`studio/Forms.h`). `menu/` is the thin ImGui renderer over it
(`REFERENCE.md`, *Menu mechanics*). Nothing under `menu/` includes `render/`
except `MenuWidgets.cpp`.

### The per-draw context (`Frame`)

Every `Draw*` function takes `const Frame &a_frame`. A page renderer builds
one **frame** per draw from the published **snapshot**, and every panel
below it reads the same record. All types are declared in `Frame.h`.

| Member | Description |
|---|---|
| `Frame` | Bundles the **snapshot**, the resolved `PieceRow`/`RecipeRow`/`GeometryRow`/`Names` pointers, the mutable `Studio::MenuState *state`, the outbound `Studio::Intents *intents`, and the DPI `scale` (default `1.0f`). |
| `ActorOf`, `SelectionOf`, `ViewOf`, `LayoutOf` | Accessors that read the frame's current actor `FormID`, `Selection`, `View`, and `Layout`. |

### The widget toolkit (`MenuWidgets.h`)

These are the ImGui-facing widgets every panel draws with. Each widget
returns the user's edit as a value, usually an `std::optional`, and the
caller turns that value into an **intent**. `MenuWidgets.cpp` implements
them and is the module's only `render/` include.

| Member | Description |
|---|---|
| `Table` | The column table. `Begin` opens it from `Studio::Column`s and a `TableStyle`, `Cell` advances, `End` closes it. |
| `FieldScope` | An RAII ImGui ID scope. It pushes one part of the current **field** key for its lifetime. |
| `WidgetSize` | A `Studio::Width` and DPI scale pair that sizes a **field** widget. |
| `Rule`, `RuleWithFilter`, `RuleResult`, `RuleFilter`, `FilterSpec` | The **rule** headers. `Rule` returns a `RuleResult` (the `RuleClick` and the open state). `RuleWithFilter` adds the filter box described by `FilterSpec` and returns a `RuleFilter`. |
| `TextField`, `ChoiceCombo`, `ReferenceCombo`, `ValueWidget` | The **field** widgets. Each returns the committed text, or `nullopt` when the user made no edit. |
| `SearchCombo`, `SearchComboSpec`, `SearchPick` | The searchable combo. `SearchCombo` returns a `SearchPick`: the index of a listed label, or a custom string. |
| `CandidateRowsSpec`, `DrawCandidateRows` | The filtered game-object candidate list. The spec carries the filter, the catalog, and the row cap. |
| `ResourceCells`, `ResourceTable` | One resource row (name, type, optional `Value`) and the table that draws a span of them. |
| `Badge`, `ModeBadge`, `BlendBadge` | The **badge** glyphs. `Badge` draws a `FieldKind` glyph. `ModeBadge` draws a clickable `FieldKind` glyph that toggles a **field**'s entry mode, its `EntryModeTips` naming the two states; the value and catalog **fields** use it to switch between a combo and typed entry. `BlendBadge` draws a `Blend` glyph and returns a new `Blend` when clicked. |
| `DetailModal`, `ConfirmModal` | The modals. `DetailModal` draws a body callback. `ConfirmModal` runs its callback only on the affirm click. |
| `Problem`, `Warn`, `Ok`, `Dim`, `DrawDiagnostics` | The status text family. `DrawDiagnostics` renders a span of `Diagnostic`s and marks held-back edits. |

### Form and field drawing (`FormDraw.h`)

These functions turn a span of `Studio::FormField`s into drawn inputs and
posted **intents**. `FieldInput` draws one field's widget, and the wrappers
above it place fields in tables and multi-column **forms**.

| Member | Description |
|---|---|
| `FieldInput` | Draws one **field**'s widget and returns the committed text. |
| `PostField` | Posts one field's committed text as the field's edit **intent**. |
| `DrawRowField` | The single-field entry. It wraps `FieldInput` and `PostField` for panels outside the table path, for example the **mask** inspector. |
| `DrawFieldTable`, `DrawForm`, `DrawFormWithSignals` | Place a **field** span in a table or a multi-column **form**. The first two return the index of the field whose detail button was clicked. |
| `DrawInspectorFields` | Draws a `Studio::Inspector`'s **field** groups through `DrawForm`. |
| `DrawSelector` | Draws one output's `SelectorView`. |
| `NavigateFromInspector` | Navigates the view to an `InspectorSubject`, optionally to one `PropertyLocation`. |

### Special-input popups

Some **fields** need more than an inline widget. Each of these popups opens
from a field or a panel control and edits through the **frame**'s intents,
like the inline widgets do.

| Member | Description | Declared in |
|---|---|---|
| `FieldHasExpressionShelf`, `DrawExpressionOpener` | The expression shelf. The predicate says whether the **field** offers it; the opener draws the button beside the field. | `ExpressionShelf.h` |
| `DrawTuning`, `TuningSink` | The **tuning** control for a numeric **field**. An optional `TuningSink` redirects the commit to a callback. | `Tuning.h` |
| `BeginTuningFrame`, `EndTuningFrame`, `FinishTuning` | The per-frame **tuning** lifecycle over `Studio::MenuState`. `FinishTuning` commits or discards the held value. | `Tuning.h` |
| `DrawPatternChooser` | Draws the filtered list of `Studio::TermOffer` patterns. | `PatternChooser.h` |
| `OpenInputWizard`, `DrawInputWizard` | The new-input wizard: open it, then draw it for the **field** that asked. | `InputBrowser.h` |
| `DrawMaskTask`, `DrawMaskStack`, `DrawTermTuningPane`, `EditMaskAsTerms` | The **mask** editor (**paint** mode): the task bar, the mask stack, the per-term **tuning** pane, and entry into term editing for one mask `TextRow`. | `PaintPanel.h` |

### Panels composed by the pages

Each panel is one free function over the **frame**. A page renderer
composes them, and each panel reads the **snapshot** rows and posts
**intents** for its edits.

| Member | Description | Declared in |
|---|---|---|
| `DrawWorkspace` | The Studio page's body. It builds the stack view and inspector and draws the panels below it. | `Workspace.h` |
| `DrawStack` | Draws the `LayerStack` and `Inspector` pair. | `StackPanel.h` |
| `DrawBoard`, `DrawBoardPage` | The **board**. `DrawBoard` draws one `Studio::Board`; `DrawBoardPage` draws the Recipes page around it. | `BoardPage.h` |
| `DrawRelationships` | Draws the relationships panel; `HasRelationships` gates it. | `RelationshipPanel.h` |
| `DrawRecipeSettings`, `DrawStudioContext`, `DrawOutputHeader` | The context rows: the **recipe** settings, the studio context line, and one output's header with its scalar **fields**. | `ContextRows.h` |
| `PostResourceAdd` | Posts the add-resource **intent** for one `Studio::ResourceTab`. | `ResourcePanels.h` |
| `DrawResourceRename` | Shared inspector rename popup for signals, sources, masks, and curves. Checks name syntax and collisions, then posts the existing reference-updating recipe edit. | `ResourcePanels.h` |
| `DrawResponse` | Draws one `SignalRow`'s response panel. | `ResponsePanel.h` |
| `DrawRecipeSaveRevert`, `DrawRecipeFileActions` | The **recipe** file controls: save and revert, and the file action row. | `RecipeActions.h` |

### The menu lifecycle

`Menu.h` and `Menu.cpp` own the path from a posted **intent** to an engine
edit, and the SKSE Menu Framework registration. Each page file defines its
own entry point and drains its intents through the shared `Dispatch`.

| Member | Description |
|---|---|
| `Perform`, `IntentPerformer` | `Perform` matches one **intent** onto `IntentPerformer` (`Menu.cpp`), the visitor that turns edit intents into `Manager::Editor()` calls. `IntentPerformer` is the only intent-path caller of `Editor()`; `RecipeActions.cpp`, `Tuning.cpp`, `RecipesPage.cpp`, and `StudioPage.cpp` also reach `Editor()` directly, outside the intent path. |
| `Dispatch` | Drains a frame's `Studio::Intents`. It gates each intent through `AcceptIntent`, performs it, and applies `Reduce` for the UI-local state. |
| `RenderStatus`, `RenderHeader` | Draw the shared status and header strips from the **snapshot**. |
| `RenderStudio`, `RenderRecipes`, `RenderSetup` | The three `__stdcall` entry points SKSE Menu Framework calls each frame. `Menu.h` declares `RenderRecipes` and `RenderSetup`; `StudioPage.h` declares `RenderStudio`. Each is defined in its page file: `RenderStudio` in `StudioPage.cpp`, `RenderRecipes` in `RecipesPage.cpp`, `RenderSetup` in `SetupPage.cpp`. |
| `RegisterMenu` | Registers the three entry points with SKSE Menu Framework. |

## How a frame flows

```
Manager::LatestSnapshot()                                   StudioPage.cpp
  │  Studio::Snapshot (published, immutable this frame)
  ▼
RenderStudio()  ──build Frame{snapshot, piece, recipe,       StudioPage.cpp
                    geometry, names, state, intents}  (scale defaults to 1.0f)
  ▼
DrawStudioFrame(frame)  ──▶ DrawBody(frame) ──▶ DrawWorkspace(frame)  StudioPage.cpp
  │                                                                   Workspace.cpp
  ├─ Studio::BuildStackView/BuildInspector(recipe, geometry, selection)
  │      (studio/, pure: Snapshot rows → LayerStack/Inspector)
  ├─ DrawStack(stack, inspector, frame)  ──▶ DrawInspectorFields       StackPanel.cpp
  │      ──▶ DrawForm ──▶ DrawFieldTable ──▶ DrawFieldInput ──▶ FieldInput   FormDraw.cpp
  │            (each field: ValueWidget, ChoiceCombo, DrawExpressionOpener…)
  │            (DrawRowField is a sibling single-field entry, wraps DrawFieldInput,
  │             called directly by panels outside the table path, e.g. the mask inspector)
  └─ DrawMaskTask / DrawTermTuningPane (paint mode)                    PaintPanel.cpp
  │
  │  user edits a field / clicks a rule action / drags a row
  ▼
Studio::Post(*frame.intents, intent)          -- queues, never mutates --
  ▼
EndTuningFrame(state); Dispatch(intents, state, snapshot)     StudioPage.cpp
  │  per Dispatch:
  ├─ Studio::AcceptIntent  (gate: reject while an edit is pending)  MenuState.cpp
  ├─ Perform(intent, state, view)  ──▶ IntentPerformer::operator()  Menu.cpp
  │        (EditRecipe → Manager::Editor().EditRecipe; ChangeView;
  │         BeginPaint/KeepPaint/EndPaint; Undo/Redo; …)
  └─ Studio::Reduce(state, intent)   (UI-local: selection, drafts)  MenuState.cpp
  ▼
RebuildScratch(frame); Dispatch(intents, state, snapshot)     StudioPage.cpp
  │  (a second pass; RebuildScratch posts UpdatePaint, drained here)  PaintPanel.cpp
  ▼
next tick: engine/Manager republishes a Snapshot; RenderStudio reads it again
```

- `RecipesPage.cpp`'s `RenderRecipes` (which draws `DrawBoardPage`,
  `BoardPage.cpp`) shares this pipeline: it builds a `Frame` and drains it
  through the same `Menu::Dispatch`.
- `SetupPage.cpp`'s `RenderSetup` is a separate settings page. It builds no
  `Frame`, posts no intents, and calls no `Dispatch`. It mutates `Settings`
  directly (`SetSettings`, `SaveSettingsToDisk`) and calls
  `Manager::ReapplyAll()`.

## The files

| Concern | Files |
|---|---|
| Per-draw context, entry points, intent dispatch | `Frame.h`, `Menu.h` / `Menu.cpp` (`IntentPerformer`, `Perform`, `Dispatch`, `RegisterMenu`) |
| Widget toolkit | `MenuWidgets.h` / `MenuWidgets.cpp` (the sole `render/` dependency; `Table`, combos, badges, rules, modals, `DrawDiagnostics`) |
| Form and field drawing | `FormDraw.h` / `FormDraw.cpp` |
| Pages | `StudioPage.h`/`.cpp` (`RenderStudio`), `RecipesPage.cpp` (`RenderRecipes`), `SetupPage.cpp` (`RenderSetup`), `BoardPage.h`/`.cpp` |
| Studio panels | `Workspace.h`/`.cpp`, `StackPanel.h`/`.cpp`, `RelationshipPanel.h`/`.cpp`, `ContextRows.h`/`.cpp`, `ResourcePanels.h`/`.cpp`, `ResponsePanel.h`/`.cpp` |
| Special-input popups | `ExpressionShelf.h`/`.cpp`, `Tuning.h`/`.cpp`, `PatternChooser.h`/`.cpp`, `InputBrowser.h`/`.cpp` |
| Paint / mask editor | `PaintPanel.h`/`.cpp` |
| Recipe file actions | `RecipeActions.h`/`.cpp` |

## See also

- `REFERENCE.md` → *Menu mechanics* and *Recipe CRUD travels one pipeline* —
  the ImGui ID mechanics (`FieldKey`/`ImGuiID`, `KeyOf`, active-item
  timing), the preview-ticket lifecycle, and the intent-to-`RecipeStore`
  pipeline.
- `docs/ui-api.md` — the `Frame` contract, the widget vocabulary, the
  intent-posting rule, and navigation.
- `docs/conventions.md` — the shared `Diagnostic`/`Reporter`, JSON-boundary,
  and variant-dispatch contracts this module reuses.

Registration checks the loaded framework and the export inventory through
`engine/MenuDependency`; an existing but unloaded DLL is unavailable. Failure
logs recovery instructions and leaves playback independent of the editor.
When Community Shaders is unavailable, the pages explain that
effects are disabled and directs the user to installation/loader diagnostics.

The Recipes page reports nonmatching, fallback-suppressed and sampled-out
identities separately from selected recipe rows. A preview override banner
distinguishes normal matching outcomes from isolation/pinning, and light
replacement diagnostics identify their actor-wide target.
