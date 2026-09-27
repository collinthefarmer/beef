# studio/

The pure UI-state layer beneath the ImGui renderer. Per `ALLOWS` in `tools/gate.py`,
`studio` may include `Core.h recipe mesh planners diagnostics studio` and
nothing else. It never includes `engine/` and never names `RE::`, so it
compiles natively and is unit-tested through `ctest --preset native` alongside
`recipe/`. `menu/` is the thin renderer above it that calls ImGui.

## What it owns

- The **snapshot**/view read model (`Snapshot.h`, `View.h`) the menu renders
  each frame.
- The vocabulary of **intents** (`Intent.h`) and recipe **edits**
  (`Edits.h`) a widget posts instead of mutating a `Recipe` directly.
- Editor bookkeeping that is UI-local, not part of the **recipe**:
  `MenuState.h` and `Forms.h` hold field state, **tuning** drafts, and combo
  toggles.
- The **mask** editor's language: `Mask.h`, `TermTemplates.h`, `Presets.h`.
  `PaintSession.h` and `PaintCommit.h` hold the **paint** gesture and its
  commit into a mask.
- The compose-mode projections: `Board.h` and `Panels.h` project a
  **geometry**'s **outputs** into the **board** grid and the stack views.
  `SelectorEdit.h` is the structured (non-free-text) editor for a
  `Selector`.
- Selection and names: `Selection.h` tracks which
  recipe/output/layer/signal is focused. `Names.h` and `Rows.h` supply the
  name pools and per-row projections that forms bind against.
- Edit-time validation: `FieldCheck.h` re-runs the loader's own validators
  at edit time, so a widget cannot commit a value the loader would reject.
- `History.h` is the undo/redo stack.
- `DocumentRevisions.h/.cpp` owns the editor's document clock and reset epoch.
  Advancing one ID preserves other IDs; reset invalidates every captured
  revision, including IDs that had never been edited, without resetting the clock.
- The widget-spec data: `Widgets.h` and `Fields.h` are pure data; no ImGui
  type appears in `studio/`.
- The creation seam: `Create.h`/`Create.cpp` is the single path that
  manufactures a new **signal**, **source**, mask, **curve**, output,
  **light**, or **layer**.

`MenuState::pendingEditorChange` retains the request and original selection
for recipe rename/delete and selected resource renames. Authoring waits for
the matching edit result. Refusal keeps selection intact; acceptance follows
the renamed resource only if the user has not navigated elsewhere. Recipe
rename/delete selection changes also wait for acceptance. Resource edits
reuse the existing reference rewrite and document undo/redo path.
Acknowledging a recipe rename resets navigation only while that recipe is still
selected; another document's inspector history and scroll survive a late result.
Native integration feeds actual editor results into these acknowledgement
handlers, including stale snapshots, refusal/retry, paint and load cancellation.

## Data

### Widget vocabulary

`Widgets.h` and `Fields.h` state what a table, rule, or **field** shows;
`menu/` decides how it draws, and no ImGui type appears here. The
`*FieldSpec` records are the named-argument inputs to the `FormField`
builders in `Fields.h` (`ValueField`, `ReferenceField`, `TextEntryField`).

| Type | Description | Declared in |
|---|---|---|
| `Width` | One column's width: a `WidthMode` (`kPx`, `kFill`, `kFit`) with the pixel count, fill ratio, or fit text it needs. | `Widgets.h` |
| `Column` | One table column: a label and a `Width`. | `Widgets.h` |
| `TableStyle` | One table's borders, stretch, header row, and row background; the named presets (`kFormTable`, `kGridTable`, …) fix the house styles. | `Widgets.h` |
| `RuleSpec` | A titled horizontal rule: its text, its `RuleButton`s, and whether it collapses. | `Widgets.h` |
| `RuleButton` | One button on a rule: a `RuleAction`, an optional label override, a `Width`, and an enabled flag. | `Widgets.h` |
| `RuleAction` | The closed set of ten rule-button actions (`kNew`, `kRename`, `kUndo`, …); `kRuleActions` gives each its label. | `Widgets.h` |
| `RuleClick` | The renderer's answer for one rule: whether a button was clicked, and which. | `Widgets.h` |
| `ThumbnailSpec` | One preview image: a `TextureHandle`, the `ShaderChannel` to show, a dynamic flag, and a size. | `Widgets.h` |
| `ValueFieldSpec`, `ReferenceFieldSpec`, `TextEntryFieldSpec` | The builder arguments that state a **field**'s shape: name, kind, current text, choice names, and the commit binding. | `Fields.h` |

### Snapshot/view read model

`Snapshot` is the read model `menu/` renders each frame; the engine rebuilds
it each tick, and a widget never mutates it. Its rows nest: a `PieceRow`
holds `RecipeRow`s, and a `RecipeRow` holds the resource, **geometry**, and
**output** rows under it. `View` filters what the render thread shows.

| Type | Description | Declared in |
|---|---|---|
| `Snapshot` | The per-tick root: the `PieceRow`s, the document `RecipeRow`s, the `LoadedRecipeRow`s, `Status`, `View`, the **edit** and **paint** results, and the game-object catalogs. | `Snapshot.h` |
| `PieceRow` | One worn **piece**: its `PieceRef`, actor and armor names, `KeyChoice` list, and the `RecipeRow`s applied to it. | `Snapshot.h` |
| `RecipeRow` | One **recipe**: id, keys, priority, clock speed, the **signal**/**curve**/**mask**/**source** rows, geometries, outputs, light and shell rows, problems, undo and redo depths, and `documentRevision`. | `Snapshot.h` |
| `GeometryRow` | One **geometry**: its mesh facts (partitions, bones, islands, clusters), material and shell `SlotRow`s, **source** and **mask** `PictureRow`s, and its `OutputRow`s. | `Snapshot.h` |
| `OutputRow` | One **output**: target, surface, **slot**, replace and merge flags, selector, `ScalarRow`s, `LayerRow`s, and its composite `TextureHandle`. | `Snapshot.h` |
| `LayerRow` | One **layer**: source, mask, blend, opacity, color, curve, channels, a problem string, and its texture. | `Snapshot.h` |
| `SignalRow` | One **signal**: name, kind, value type, current value, inert flag, reference count, and its full `SignalKind` definition. | `Snapshot.h` |
| `SourceRow` | One **source**: name, value type, reference count, and a `SourceRowKind` variant (`ImageSourceRow` … `MaterialClustersSourceRow`) whose alternative order matches `SourceKindId`. | `Snapshot.h` |
| `LightRow` | One light: presence, owning **output**, the parameter texts (color, intensity, size, cutoff, offset), shadow and replace flags, selector, and the bone fields. | `Snapshot.h` |
| `ShellRow` | The shell: material, blend, depth bias, alpha test, the parameter texts, and the scale point and spin axis. | `Snapshot.h` |
| `Status` | The header counters (actors, pieces, recipes, geometries, shells, lights, loaded files, files with errors) and the pipeline flags (`emissivePath`, `layoutVerified`, `textureLab`). | `Snapshot.h` |
| `LoadedRecipeRow` | One **recipe** file on disk: id, keys, per-resource counts, imported flag, diagnostics, and path. | `Snapshot.h` |
| `View`, `Isolation`, `Pin` | The display filter: freeze, scrub, and speed, an `Isolation` naming the one **recipe** (and optionally **output** and **layer**) shown, `soloPiece`, the muted `LayerKey` set, and an optional `Pin` naming a **recipe** pinned on a **piece**. | `View.h` |
| `Mode`, `Layout` | `Mode` is compose versus **paint**; `Layout` is the per-mode panel set and sizes (`kLayouts`, `LayoutFor`). Both are declared in `View.h`; the current values live on `MenuState` (`mode`, `layout`), not on `View`. | `View.h` |

### Intents and edits

A widget posts an **intent** instead of mutating a `Recipe` or `MenuState`.
`Intent.h` names every gesture, and `Edits.h` names every document change;
`Dispatch` in `menu/Menu.cpp` reduces intents into `MenuState` and forwards
the document **edits** to the engine.

| Type | Description | Declared in |
|---|---|---|
| `Intent` | The 50-alternative variant a widget posts: picks (`PickCell`), **mask**-term edits (`SetTermText`), **paint** (`BeginPaint`), document edits (`EditRecipe`), history (`Undo`), and **recipe** lifecycle (`CreateRecipe`). `Post` appends one to the frame's `Intents`. | `Intent.h` |
| `RecipeEdit` | The variant `EditRecipe` carries: **layer** edits (`SetLayerSource`), resource edits (`AddSignal`), and **output**, light, and shell edits (`SetShellParam`). One **edit** folds into a `Recipe` through `Apply(Recipe&, const RecipeEdit&)`. | `Edits.h` |
| `EditBatch` | An ordered group of **edits** applied as one unit through `Apply(Recipe&, const EditBatch&)`; `PrepareEdits` applies the whole batch to a copy, then runs the typed field checks used by `Validate` when loading. New field-domain or collection-limit errors refuse the batch atomically; incomplete rows stay editable and receive semantic diagnostics on publication. Single edits and tuning gestures use this boundary too. | `Edits.h` |
| `ReferenceCounts` | The per-name use counts (`signals`, `curves`, `images`) of one **recipe**; `CountReferences` computes it, and removal checks read it. | `Edits.h` |

### Fields and field keys

A **form** is a vector of `FormField`s built from the **snapshot** rows.
Each **field** carries its own commit closure, so the renderer does not know
what it edits. Per-field UI state lives on `MenuState`, keyed by `FieldKey`.

| Type | Description | Declared in |
|---|---|---|
| `FormField` | One **form** row: name, `FieldKind`, current text, choice names, the `FieldBinding` that turns committed text into a `RecipeEdit`, an optional `FieldCreator`, ranges, units, help, and the expected document revision. | `Forms.h` |
| `FieldKind` | The fifteen **field** shapes (scalar, color, expression, mask, …); the `kFieldKinds` table gives each its glyph, help text, input style, and check. | `Forms.h` |
| `FieldCheckKind` | Names which loader validator `CheckField` (`FieldCheck.h`) re-runs at **edit** time, so a widget cannot commit a value the loader would reject. | `Forms.h` |
| `OutputHeader` | The **output** form's header: its `FormField`s plus the structured `SelectorView`. | `Forms.h` |
| `FieldKey` | A **field**'s identity: a `std::uint32_t`, the same type as ImGui's `ImGuiID`; `HashFieldKey` derives it from the field scope and leaf name. | `MenuState.h` |
| `textBuffers`, `numberBuffers`, `comboMode`, `tuningRanges` | The `FieldKey`-keyed maps on `MenuState`: each **field**'s draft text, draft numbers, combo-open toggle, and **tuning** range. | `MenuState.h` |
| `TuningGesture` | The in-flight drag: one `std::optional` on `MenuState`, naming its **field** by `FieldKey` and carrying the **recipe** id, subject, current value, and bind closure. | `MenuState.h` |

### Masks and paint

Keep refuses the reserved names `scratch` and `peek`, which belong to temporary
preview masks and are removed during save preparation. A refusal leaves the draft
available for retry under a valid name. Commit preparation reuses equivalent
destination sources and rewrites their references while adding missing sources;
mask creation and optional layer assignment form one undoable edit batch.

A **mask** is authored as a stack of terms that compile into one expression.
**Paint** mode edits that stack against a temporary paint **recipe**, and the
commit records land the finished expression as a `SetMask` **edit**;
`TermTemplates.h` and `Presets.h` feed the term picker.

| Type | Description | Declared in |
|---|---|---|
| `Term`, `TermOp` | One stack entry: a `TermOp` (`kSet`, `kAnd`, `kOr`, `kNot`), its expression text, a label, and its `TermKind`. | `Mask.h` |
| The `TermKind` payloads | `RawTerm`, `ReferenceTerm`, `ThresholdTerm`, `PresetTerm`, `PartitionTerm`, `BoneTerm`, `IslandTerm`, `ClusterTerm`: each holds one term shape's parameters. | `Mask.h` |
| `MaskStack` | The editor's stack: at most `kMaxTerms` (64) terms, the selected, solo, and muted indices, the **mask** being edited, and a dirty flag; `BuildMask` compiles it to one expression. | `Mask.h` |
| `BuildTerm`, `BuiltTerm` | Turns a `TermKind` into its expression plus the `RecipeEdit`s that create any **sources** it needs. | `TermTemplates.h` |
| `TermOffer`, `OfferGroup` | One pickable term with its group (parts, bones, presets, … — nine groups), detail, and coverage; `OffersOf` lists them for a **geometry**. | `TermTemplates.h` |
| `MaskPreset`, `MaskPresets` | The `presets.json` model: a named partition, bone list, and expression, with the **sources** the preset requires. | `Presets.h` |
| `PaintSession` | The in-progress **paint** gesture: the paint **recipe** id, surface, read geometries, pending commit and revision, source **edits**, origin `Selection`, and peek. | `PaintSession.h` |
| `PaintCommitRequest`, `PaintCommitResult` | The keep gesture: **mask** name, expression, sources, and optional **layer** assignment out; the commit's problem, if any, back. | `PaintCommit.h` |
| `PaintUpdateRequest`, `PaintUpdateResult` | One live re-projection of the working expression while painting: revision-stamped out, acknowledged or refused back. | `PaintCommit.h` |

### Selection, board, navigation

`Selection` names what the editor focuses on, and every panel projects the
**snapshot** through it. The **board** is the surface-by-**slot** grid of one
**geometry**'s **outputs**; `LayerStack` and `Inspector` are the views under
one cell, and `Navigation` keeps the history of inspected subjects.

| Type | Description | Declared in |
|---|---|---|
| `Selection` | The focus: **piece**, **recipe**, **geometry**, target, **slot**, **layer**, the `InspectorSubject`, a document flag, and an optional property location. | `Selection.h` |
| `InspectorSubject` and the `*Subject` structs | The per-kind focus variant: `RecipeSubject`, `ShellSubject`, `OutputSubject`, `LayerSubject`, `SignalSubject`, `SourceSubject`, `MaskSubject`, `CurveSubject`. | `Selection.h` |
| `Board`, `Cell`, `CellState` | The surface-by-**slot** grid: each `Cell` carries its `CellState` (`kWritten` … `kRefused`), owning **output**, **layer** count, composite, scalars, and badges; `Board` adds the light cell and shell name, and `BuildBoard` projects one **geometry**. | `Board.h` |
| `LayerStack` | One cell's stack view: the **recipe**'s own **layer** rows plus the foreign rows above and below it by priority. | `Panels.h` |
| `Inspector` | One **layer**'s detail view: its row, **source** and **mask** pictures, **signals**, **curve**, and the name lists its **fields** choose from. | `Panels.h` |
| `StackViewInput` | The inputs `BuildStackView` needs: **piece**, **recipe**, **geometry**, `Selection`, and `View`. | `Panels.h` |
| `Navigation`, `PreviewPin` | The back and forward `InspectorVisit` stacks with their scroll, capped at 64 visits; `PreviewPin` holds the pinned preview `Selection` and its reset id. | `Navigation.h` |

### The creation seam

All types are declared in `Create.h`. `Creation` is the variant naming every
interactive new-row gesture, and `Create` turns one request plus the current
`RecipeRow` into the `RecipeEdit`s that perform it. The new row's identity
(its name or index) is computed at creation time, so each alternative
carries only the request's parameters: a placement for an **output**, an
owner for a **layer**, a name stem for a resource.

| Type | Description |
|---|---|
| `NewOutput` | A new **output**: surface, **slot**, and selector. |
| `NewLayer` | A new **layer** on the **output** it names. |
| `NewSignal` | A new **signal**: a name stem (default `"signal"`) and its `SignalKind` (default `ConstantSignal`). |
| `NewSource` | A new **source**: a name stem and its `SourceKind` (default `MaterialSource`). |
| `NewMask` | A new **mask**: a name stem. |
| `NewCurve` | A new **curve**: a name stem. |
| `NewLight` | A new light; it carries nothing, and the creator fills the defaults. |
| `Created` | The result: the `RecipeEdit`s to post and the `InspectorSubject` to select. |

### Names and rows

These types build the read model's rows and the word pools the **forms**
bind against. `Names` gathers a **recipe**'s referenceable names once, so
combo boxes and validators share one list; `Rows.h` and `RecipeSnapshot.h`
project a `Recipe` into the rows above.

| Type | Description | Declared in |
|---|---|---|
| `Names` | The combo-box word pools: **signals** and **sources** with their `ValueType`s, **curves**, and **masks**; `NamesOf` gathers them from a `RecipeRow` and optionally its `GeometryRow`. | `Names.h` |
| `RecipeRowInput` | The inputs `BuildRecipeRow` needs: the `Recipe`, its matched key, priority, clock time, dirty and pinned flags, undo and redo depths, its `ReferenceCounts`, and the live `SignalGraph` and `SignalState`. | `RecipeSnapshot.h` |
| `BuildRecipeRow` | Builds one `RecipeRow`, and every row under it, from a `RecipeRowInput`. | `RecipeSnapshot.h` |
| The row builders | The per-row projections (`OutputRowOf`, `SignalRowOf`, `LayerRowOf`, …) that `BuildRecipeRow` composes. | `Rows.h` |

## How editing flows

```
Recipe (engine/RecipeStore)
  │  Manager::BuildSnapshot                  engine/ManagerSnapshot.cpp
  ▼
Studio::BuildRecipeRow(RecipeRowInput)  ──▶  RecipeRow, GeometryRow, ...   RecipeSnapshot.cpp / Rows.cpp / Panels.cpp / Board.cpp
  │        (published as Snapshot; read-only, one tick lag; render thread never
  │         touches the live Recipe)
  ▼
menu/ draws Frame{snapshot, piece, recipe, geometry, names, state, intents}
  │        a widget reads Snapshot/MenuState, never mutates either
  ▼
user acts (types, drags, clicks) ──▶ Studio::Post(intents, Intent)          Intent.h
                                        e.g. SetTermText, PickCell, BeginPaint,
                                        EditRecipe{recipeID, EditBatch}
  ▼
menu/Menu.cpp::Dispatch                                                     Menu.cpp
  │  Studio::AcceptIntent  → refuse if invalid this frame
  │  Studio::Reduce(state, intent)     mutates MenuState (selection, drafts, history)
  │  Perform(intent, state, view)      the engine-facing half: posts to RecipeEditor
  ▼
engine::RecipeEditor::EditRecipe(id, EditBatch, expectedRevision?)          RecipeEditor.h/.cpp
  │  task queue, revision-stamped; row edits go through Studio::Apply first,
  │  interactive creation goes through Studio::Create beforehand
  ▼
engine::RecipeStore                                                        RecipeStore.h/.cpp
  (the only disk layer; writes the file, bumps the document revision)
  │
  └── next tick's BuildSnapshot picks up the change, closing the loop
```

`studio/` never reaches past `Perform`'s call site. `Studio::Apply` checks
and folds a `RecipeEdit` into an in-memory `Recipe`. `Studio::Create` turns
a `Creation` request into the `RecipeEdit`s for it. Posting them to the
engine's `RecipeEditor`/`RecipeStore` is `menu/`'s job, not `studio/`'s.

## The files

| Concern | Key files |
|---|---|
| Snapshot / read model | `Snapshot.h`, `RecipeSnapshot.h/.cpp`, `Rows.h/.cpp`, `ResolveOutput.h/.cpp`, `SourcePlan.h/.cpp`, `SourceRows.cpp` |
| View / mode / isolation | `View.h`, `Isolation.cpp` |
| Intents and edits | `Intent.h`, `Edits.h/.cpp`, `EditChecks.h/.cpp`, `EditResult.h/.cpp`, `FieldCheck.h/.cpp`, `FieldParsing.h/.cpp` |
| Creation seam | `Create.h/.cpp` |
| Widget vocabulary / fields | `Widgets.h`, `Fields.h/.cpp`, `Forms.h/.cpp` |
| Menu-local editor state | `MenuState.h/.cpp`, `History.h`, `ResponseGraph.h/.cpp` |
| Mask editor | `Mask.h/.cpp`, `TermTemplates.h/.cpp`, `Presets.h/.cpp` |
| Paint mode | `PaintSession.h/.cpp`, `PaintCommit.h`, `Gesture.h/.cpp` |
| Selection / board / panels / navigation | `Selection.h/.cpp`, `Board.h/.cpp`, `Panels.h/.cpp`, `Navigation.h/.cpp`, `SelectorEdit.h/.cpp` |
| Names | `Names.h/.cpp` |
| Recipe-file lifecycle records | `ApplicationRecord.h/.cpp`, `FileOperation.h`, `Relationships.h` |
| Game-adjacent catalogs (engine-free types) | `GameObjects.h/.cpp`, `InputCatalog.h/.cpp`, `InputConnections.h/.cpp` |

## See also

- `REFERENCE.md` → *studio*, *Field ranges, cascade removal, channels
  (2026-09-17)*, *Menu mechanics*, *Recipe CRUD travels one pipeline* — the
  facts these types cannot state: why a name changed, why a field is text,
  the `FieldKey`/`ImGuiID` identity, the intent and edit dispatch
  discipline.
- `docs/ui-api.md` → *Two layers*, *The `Frame`*, *Forms and fields*,
  *Editing is intents* — how `menu/` consumes this vocabulary each frame.
- `docs/conventions.md` → *Runtime identities and editor commits* and
  *Variants and closed sets* — the patterns `Intent`, `RecipeEdit`, and the
  `*Kind` variants follow.

Recipe merge/output help explains grouped replacement and actor-wide light
scope. `PieceRow` retains normal actor-seeded selection outcomes separately
from recipe rows affected by preview isolation or pinning. An unisolated
`ViewedRecipes` preserves the supplied actor selection; isolation resolves
only its explicit single recipe and is not a normal sampled-pool decision.
