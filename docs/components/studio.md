# studio/

The engine-free UI-state layer under the ImGui renderer. Its `ALLOWS` row in
`tools/gate.py` is `'studio': ENGINE_FREE`, so it may include `Core.h`,
`recipe`, `mesh`, `planners`, `diagnostics` and `studio`. It never includes
`engine/` and never names `RE::`, so it compiles natively and runs under
`ctest --preset native`. The `render`, `engine` and `menu` layers include it
through the `ADAPTER` set. `menu/` draws with ImGui over the records that
`studio/` builds.

## What it owns

- The read model. A **snapshot** (`Snapshot.h`) is the per-tick record that
  `menu/` draws. `RecipeSnapshot.h`, `Rows.h` and `SourceRows.cpp` project a
  `Recipe` into its rows.
- The display filter. `View.h` holds **isolation**, muting, the pin, and
  the `Mode` and `Layout` records.
- The gesture vocabulary. An **intent** (`Intent.h`) is one user gesture
  that a widget posts. A **recipe edit** (`Edits.h`) is one document change
  that an intent carries.
- The edit boundary. `PrepareEdits` (`Edits.cpp`) applies an `EditBatch` to
  a copy of a `Recipe` and refuses the batch on a new field error.
  `FieldCheck.h` and `EditChecks.h` hold the checks that it and the widgets
  run.
- The creation path. `Create` (`Create.h`) turns one `Creation` request into
  the edits that add a new row.
- The menu-local state. `MenuState` (`MenuState.h`) holds the selection,
  navigation, draft buffers, **tuning** drags, the **mask** stack and the
  **paint** session. `Reduce` changes it for each intent.
- The mask editor and paint mode: `Mask.h`, `TermTemplates.h`, `Presets.h`,
  `PaintSession.h` and `PaintCommit.h`.
- The compose views: `Board.h`, `Panels.h`, `Selection.h`, `Navigation.h`
  and `SelectorEdit.h`.
- The widget vocabulary: `Widgets.h`, `Fields.h` and `Forms.h`. No ImGui type
  appears in these files.
- The engine-side editor records that need no engine type: document
  revisions (`DocumentRevisions.h`), undo history (`History.h`), tuning
  gestures (`Gesture.h`), edit and file results (`EditResult.h`,
  `FileOperation.h`) and application results (`ApplicationRecord.h`).

## Data

### Snapshot rows

`Manager::BuildSnapshot` (`engine/ManagerSnapshot.cpp`) builds one `Snapshot`
per tick, and `menu/` reads it without change. The rows nest: a `PieceRow`
holds `RecipeRow`s, and a `RecipeRow` holds its resource, **geometry** and
**output** rows. `menu/` never reads the live `Recipe`.

| Type | Description | Declared in |
|---|---|---|
| `Snapshot` | The root: `Status`, `View`, the `PieceRow`s, the document `RecipeRow`s (`documents`), the `LoadedRecipeRow`s, the edit, file, paint and gesture results, the `ApplicationRecord`s, and the game-object catalogs. | `Snapshot.h` |
| `PieceRow` | One worn **piece**: its `PieceRef`, actor and armor names, `KeyChoice` list, diffuse paths, `RecipeRow`s, the `RecipeSelection` outcomes, and `previewOverride`. | `Snapshot.h` |
| `RecipeRow` | One **recipe**: id, keys, priority, merge mode, clock speed, the **signal**, **curve**, **mask** and **source** rows, geometries, outputs, light and shell rows, problems, undo and redo depths, `documentRevision`, and `relationships`. | `Snapshot.h` |
| `GeometryRow` | One **geometry**: its mesh facts (partitions, bones, islands, clusters), material and shell `SlotRow`s, source and mask `PictureRow`s, and its `OutputRow`s. | `Snapshot.h` |
| `OutputRow` | One **output**: target, surface, **slot**, replace and merge flags, selector, `ScalarRow`s, `LayerRow`s, and its composite `TextureHandle`. | `Snapshot.h` |
| `LayerRow` | One **layer**: source, mask, blend, opacity, color, curve, channels, a problem string, and its texture. | `Snapshot.h` |
| `SignalRow` | One signal: name, kind, value type, current value, inert flag, reference count, and its full `SignalKind` definition. | `Snapshot.h` |
| `SourceRow` | One source: name, value type, reference count, and a `SourceRowKind` variant (`ImageSourceRow` to `MaterialClustersSourceRow`). A `static_assert` fixes the alternative order to the order of `SourceKindId`. | `Snapshot.h` |
| `TextRow` | One curve or mask: name, text, and reference count. | `Snapshot.h` |
| `PictureRow` | One source or mask preview on a geometry: name, value type, texture, `ShaderChannel`, and problem. | `Snapshot.h` |
| `LightRow` | One **light**: presence, owning output, the parameter texts, shadow and replace flags, selector, and the bone fields. | `Snapshot.h` |
| `ShellRow` | The **shell**: material, blend, depth bias, alpha test, the parameter texts, the scale point and the spin axis. | `Snapshot.h` |
| `Status` | The header counters (actors, pieces, recipes, geometries, shells, lights, loaded files, files with errors) and the flags `emissivePath`, `layoutVerified` and `textureLab`. | `Snapshot.h` |
| `LoadedRecipeRow` | One recipe file on disk: id, keys, per-resource counts, imported flag, diagnostics, and path. | `Snapshot.h` |
| `RecipeRowInput` | The input to `BuildRecipeRow`, which composes the row builders in `Rows.h` (`OutputRowOf`, `LayerRowOf`, `SignalRowOf`, and others): the `Recipe`, matched key, priority, clock time, dirty and pinned flags, undo and redo depths, `ReferenceCounts`, and the optional `RecipeGraph` and `SignalState`. | `RecipeSnapshot.h` |
| `Names` | The name pools for combo boxes and checks: signals and sources with their `ValueType`s, curves, and masks. `NamesOf` gathers them from a `RecipeRow` and, optionally, a `GeometryRow`. | `Names.h` |

`PieceRow::selections` keeps the outcome of the normal actor-driven
resolution. `PieceRow::previewOverride` is true when isolation or a pin
changes the recipes that the piece shows. `ViewedRecipes` (`Selection.cpp`)
resolves only the isolated recipe when the view isolates one, and otherwise
keeps the resolved list it receives.

### View and isolation

A `View` states what the render thread shows. The engine keeps the current
`View`, and the snapshot copies it. `Mode` and `Layout` live on `MenuState`,
not on `View`.

| Type | Description | Declared in |
|---|---|---|
| `View` | Freeze, scrub and speed, an `Isolation`, `soloPiece`, the muted `LayerKey` set, and an optional `Pin`. `RecipeShown`, `OutputShown` and `LayerShown` answer the filter questions. | `View.h` |
| `Isolation` | The one recipe, and optionally one output and layer, that the view shows. `TargetsOutput` and `TargetsLayer` answer the isolation badges. | `View.h` |
| `Pin`, `PieceRef` | `Pin` names a recipe pinned on a piece. `PieceRef` names a piece by actor and armor form ids. | `View.h` |
| `ViewCommand` | One solo command. `ApplyViewCommand` reduces it into a `View`. | `View.h` |
| `Mode`, `Layout` | `Mode` is `kCompose` or `kPaint`. `Layout` is the panel set and sizes of one mode; `kLayouts` and `LayoutFor` give them. | `View.h` |

### Intents and recipe edits

A widget posts an intent instead of changing a `Recipe` or `MenuState`.
`Dispatch` (`menu/Menu.cpp`) reduces each intent into `MenuState` and sends
the engine half to `RecipeEditor`. `Apply` and `PrepareEdits` fold recipe
edits into a `Recipe`.

| Type | Description | Declared in |
|---|---|---|
| `Intent` | The 50-alternative variant (`kIntentCount`): picks (`PickCell`), mask terms (`SetTermText`), paint (`BeginPaint`, `KeepPaint`), document edits (`EditRecipe`), history (`Undo`), view control (`SoloLayer`), and recipe lifecycle (`CreateRecipe`, `RenameRecipe`). `Post` appends one to the frame's `Intents`. | `Intent.h` |
| `EditRecipe` | A recipe id, a list of `RecipeEdit`s, and an optional expected document revision. | `Intent.h` |
| `RecipeEdit` | The variant of document changes: layer edits (`SetLayerSource`), resource edits (`AddSignal`, `RenameSource`), and output, light and shell edits (`SetShellParam`). | `Edits.h` |
| `EditBatch` | An ordered list of edits that apply as one unit. `PrepareEdits` returns the changed copy, or the first edit failure, or the first field error that `CheckRecipeFields` finds in the copy but not in the original. | `Edits.h` |
| `ReferenceCounts` | The per-name use counts (`signals`, `curves`, `images`) of one recipe. `CountReferences` computes it. | `Edits.h` |
| `Relationship`, `CascadePlan` | A `Relationship` pairs a consumer property with the resource that drives it. `PlanCascade` lists the resources and layers that a removal takes with it, and the relationships that block it. | `Relationships.h` |
| `CheckContext` | The row types and `where` that `CheckScalarRef` and `CheckVectorRef` need. | `EditChecks.h` |

### Forms and field keys

A **form** is a list of `FormField`s that `Forms.cpp` builds from snapshot
rows. Each **field** carries its own commit binding, so the renderer does not
know what it edits. Per-field UI state lives on `MenuState`, keyed by
`FieldKey`.

| Type | Description | Declared in |
|---|---|---|
| `FormField` | One form row: name, `FieldKind`, current text, choice names, the `FieldBinding` that turns committed text into a `RecipeEdit`, an optional `FieldCreator`, ranges, units, help, and the expected document revision. | `Forms.h` |
| `FieldKind` | The fifteen field shapes (`kScalar`, `kColor`, `kExpression`, `kMask`, `kLayerSource`, and others). `kFieldKinds` gives each its glyph, help text, `FieldInputKind`, `FieldCheckKind` and `Swatch`. | `Forms.h` |
| `FieldCheckKind` | The check that `CheckField` (`FieldCheck.h`) runs on a draft before commit. | `Forms.h` |
| `OutputHeader` | The output form: its `FormField`s and the structured `SelectorView`. | `Forms.h` |
| `ValueFieldSpec`, `ReferenceFieldSpec`, `TextEntryFieldSpec` | The named arguments of `ValueField`, `ReferenceField` and `TextEntryField`. The `Bind*` functions in `Fields.h` make the bindings. | `Fields.h` |
| `FieldKey` | A field's identity, a `std::uint32_t`, the same type as ImGui's `ImGuiID`. `HashFieldKey` derives it from the field scope and leaf name. | `MenuState.h` |
| `TuningGesture` | The drag in progress: its id, `FieldKey`, recipe id, subject, current value, and bind closure. | `MenuState.h` |
| `ExpressionDraft` | The draft text of an expression field and the revision it was read at. | `MenuState.h` |

### Menu state and editor acknowledgements

`MenuState` is the one record of UI-local state. `Reduce` changes it, and
`AcceptIntent` refuses an intent that needs a settled editor while an edit
is pending. The acknowledgement functions read the snapshot's results and
finish pending work.

| Type or function | Description | Declared in |
|---|---|---|
| `MenuState` | Mode, `Layout`, `Selection`, `Navigation`, the preview pin, pending edits, `TuningGesture`, the `FieldKey`-keyed buffers (`textBuffers`, `numberBuffers`, `comboMode`, `tuningRanges`, `expressionDrafts`), the `MaskStack`, the `PaintSession`, and the mask history. | `MenuState.h` |
| `PendingEditorChange` | A recipe rename or delete, or a rename of the selected resource, with its request id and the selection when it was sent. | `MenuState.h` |
| `PendingIndexedEdit` | A request id and recipe id that wait for their `RecipeEditResult`. | `EditResult.h` |
| `RecipeEditResult` | The engine's answer to one `EditRecipe`: request id, recipe id, and an optional error. | `EditResult.h` |
| `FileOperationResult` | The state of one save or revert (`FileAction`, `FileOperationState`), with path and error. | `FileOperation.h` |
| `AcknowledgeEditorOperations` | Clears pending edits whose results arrived. On success it follows a renamed resource if the selection did not move. On refusal it keeps the selection. | `MenuState.h` |

### Masks and paint

The mask editor builds a mask as a stack of terms that `BuildMask` compiles
into one expression. Paint mode edits that stack against a temporary paint
recipe (`kPaintRecipe`). Keep sends the finished expression as a
`PaintCommitRequest`.

| Type | Description | Declared in |
|---|---|---|
| `Term`, `TermOp` | One stack entry: a `TermOp` (`kSet`, `kAnd`, `kOr`, `kNot`), its expression text, a label, and its `TermKind`. | `Mask.h` |
| `TermKind` | The eight term shapes: `RawTerm`, `ReferenceTerm`, `ThresholdTerm`, `PresetTerm`, `PartitionTerm`, `BoneTerm`, `IslandTerm`, `ClusterTerm`. | `Mask.h` |
| `MaskStack` | At most `kMaxTerms` (64) terms, the selected, solo and muted indices, the mask being edited, and a dirty flag. | `Mask.h` |
| `BuiltTerm` | The expression of one term and the edits that add the sources it needs. `BuildTerm` makes it. | `TermTemplates.h` |
| `TermOffer`, `OfferGroup` | One pickable term in one of nine groups (`kOfferGroups`), with detail and coverage. `OffersOf` lists them for a geometry. `OffersInGroup` filters one group through `OfferMatches` and sorts by coverage. | `TermTemplates.h` |
| `TermField` | One form field of a term and the function that turns its text into a new `TermKind`. | `TermTemplates.h` |
| `MaskPreset`, `MaskPresets` | The `presets.json` model: a named partition, bone list and expression, with the sources it needs. `ParsePresets` reads it. | `Presets.h` |
| `SourceCatalog`, `SourcePlanBuilder` | The sources a term or paint commit can reuse. `ReuseOrAdd` returns an existing equal source or stages an `AddSource`. | `SourcePlan.h` |
| `PaintSession` | The paint session: paint recipe id, surface, read geometries, pending commit and revision, source edits, origin `Selection`, assignment, and peek. | `PaintSession.h` |
| `PaintStartRequest` | The start of a session: recipe id, key, surface, session id and reset id. `RecipeEditor::BeginPaint` takes it. | `PaintCommit.h` |
| `PaintUpdateRequest`, `PaintUpdateResult` | One revision-stamped re-projection of the working expression, and its acknowledgement or refusal. | `PaintCommit.h` |
| `PaintCommitRequest`, `PaintCommitResult` | The Keep request (mask name, expression, sources, optional `PaintAssignment`) and its problem, if any. | `PaintCommit.h` |

`PreparePaintCommit` refuses the mask names `scratch` (`kScratchMask`) and
`peek` (`kPeekMask`). It also refuses an assignment whose document revision
is stale. It checks the commit batch with `PrepareEdits` against the target
recipe. `PaintDependencies` returns only the staged `AddSource` edits that
the current terms and peek read. `CanKeepMask`, `MaskRuleButtonsOf` and
`MaskProblemsOf` give `menu/PaintPanel.cpp` the Keep state, the four rule
buttons and the problem text.

### Selection, board and navigation

`Selection` names what the editor focuses on, and each panel projects the
snapshot through it. The **board** is the surface-by-slot grid of one
geometry's outputs. `Navigation` keeps the history of inspected subjects.

| Type | Description | Declared in |
|---|---|---|
| `Selection` | The focus: piece, recipe, geometry, target, slot, layer, the `InspectorSubject`, a document flag, and an optional property location. | `Selection.h` |
| `InspectorSubject` | The subject variant: `RecipeSubject`, `ShellSubject`, `OutputSubject`, `LayerSubject`, `SignalSubject`, `SourceSubject`, `MaskSubject`, `CurveSubject`. | `Selection.h` |
| `GeometryChoice` | The geometry names of a recipe and the index of the selected one. `GeometryChoiceOf` builds it. | `Selection.h` |
| `ViewedRecipesInput` | The input to `ViewedRecipes`: the resolved recipes, the worn piece, its `PieceRef`, the `View`, and the loaded recipes. | `Selection.h` |
| `Board`, `Cell`, `CellState`, `LightCell` | Each `Cell` has a `CellState` (`kWritten`, `kEmpty`, `kAbsent`, `kExcluded`, `kRefused`), owning output, layer count, composite, scalars, and badges. `BuildBoard` projects one geometry. | `Board.h` |
| `LayerStack`, `LayerStackRow`, `ForeignRow` | One cell's stack view: the recipe's own layer rows and the rows of other recipes above and below it by priority. `BuildStackView` builds it from a `StackViewInput`. | `Panels.h` |
| `Inspector` | One layer's detail view: its row, source and mask pictures, signals, curve, and the name lists its fields choose from. | `Panels.h` |
| `SignalNames` | The scalar, color, vec2 and trigger signal names. `SignalNamesOf` gathers them. | `Panels.h` |
| `Navigation`, `InspectorVisit` | The back and forward visit stacks with their scroll, capped at `kMaxInspectorHistory` (64). | `Navigation.h` |
| `HistoryDirection` | `kBack` or `kForward`. `StepHistory` moves one step and skips each visit that `ReachableVisit` rejects. `GoBack` and `GoForward` call it. | `Navigation.h` |
| `PropertyTarget` | A subject and property location for `NavigateProperty`. | `Navigation.h` |
| `PreviewPin` | The pinned preview `Selection` and its reset id. `PreviewPinFor` pins the previewed output, or a source or mask. `PreviewSelectionOf` returns the pin or the current selection. | `Navigation.h` |
| `SelectorView`, `SelectorClauseRow` | The structured selector editor: match-all flag and one row per clause. `SelectorWith*` functions return the changed `Selector`. | `SelectorEdit.h` |

### The creation path

`Create` turns one request and the current `RecipeRow` into the edits that
make a new row. It computes the new row's name or index when it runs, so
each request carries only its parameters.

| Type | Description |
|---|---|
| `NewOutput` | A new output: surface, slot, and selector. |
| `NewLayer` | A new layer on the output it names. |
| `NewSignal` | A new signal: a name stem (default `"signal"`) and a `SignalKind` (default `ConstantSignal`). |
| `NewSource` | A new source: a name stem and a `SourceKind` (default `MaterialSource`). |
| `NewMask`, `NewCurve` | A new mask or curve: a name stem. |
| `NewLight` | A new light. It carries no parameters. |
| `Created` | The result: the `RecipeEdit`s to post and the `InspectorSubject` to select. |

### Widget vocabulary

`Widgets.h` states what a table, rule or thumbnail shows. `menu/` decides how
it draws.

| Type | Description | Declared in |
|---|---|---|
| `Width`, `WidthMode` | One column width: `kFill` with a ratio, `kFit` with fit text, or `kPx` with pixels. | `Widgets.h` |
| `Column` | One table column: a label and a `Width`. | `Widgets.h` |
| `TableStyle` | Borders, stretch, header row, and row background. `kFormTable`, `kGridTable` and the other presets fix the house styles. | `Widgets.h` |
| `RuleSpec`, `RuleButton`, `RuleClick` | A titled rule with its buttons, one button (`RuleAction`, optional label, `Width`, enabled flag), and the renderer's answer. | `Widgets.h` |
| `RuleAction` | The ten rule-button actions (`kNew`, `kRename`, `kUndo`, and others). `kRuleActions` gives each its label. | `Widgets.h` |
| `RowMove` | One drag reorder: from index and to index. | `Widgets.h` |
| `ThumbnailSpec` | One preview image: a `TextureHandle`, a `ShaderChannel`, a dynamic flag, and a size. | `Widgets.h` |

### Engine-side editor records

`engine/RecipeEditor.cpp` and `engine/ApplicationService.cpp` keep these
records. They live in `studio/` because they need no engine type and the
native tests exercise them.

| Type | Description | Declared in |
|---|---|---|
| `DocumentRevisions` | A clock, a reset epoch, and one revision per recipe id. `AdvanceRevision` changes one id. `ResetRevisions` moves the epoch, so every revision captured before the reset no longer matches. | `DocumentRevisions.h` |
| `History`, `EditHistory` | A bounded undo and redo stack (`kCap` 100). `EditHistory` holds `Recipe`s; `MenuState::maskHistory` holds `MaskStack`s. | `History.h` |
| `RecipeGesture`, `GestureResult`, `GesturePhase` | One tuning drag on the engine side: the original and applied recipe, the revision, and the property id. `GestureResult` reports its phase in the snapshot. | `Gesture.h` |
| `GestureMailbox`, `GestureDelivery` | The mailbox keeps only the latest update and finish request. `QueueGestureUpdate` replaces the pending batch. | `Gesture.h` |
| `ApplicationRecord`, `ApplicationToken`, `ApplicationPhase` | The result of applying one recipe revision to actors: `kQueued`, `kPrepared`, `kRendered`, `kFailed`, `kUnmatched` or `kCancelled`. These types are in the `BetterEnchantmentEffects` namespace. | `ApplicationRecord.h` |
| `ResolvedOutput`, `ResolvedLight` | The scalars, color and opacities of one output, and the parameters of one light, at the current signal values. `engine/ManagerTick.cpp` calls `ResolveOutput` and `ResolveLight`. | `ResolveOutput.h` |

### Game catalogs and inputs

These records describe game objects that a field can name. The engine fills
them, and `menu/InputBrowser.cpp` reads them.

| Type | Description | Declared in |
|---|---|---|
| `GameObjectCatalog`, `GameObjectCandidate`, `GameObjectKind` | One catalog of candidates (actor values, anim events, keywords, and five more kinds). `Snapshot::catalogs` holds one per kind. | `GameObjects.h` |
| `ActorValueSample`, `InputMeasureInfo` | One actor value sampled under each `Measure`. `kInputMeasures` describes each measure. | `InputCatalog.h` |
| `InputConnectionSpec`, `InputConnectionKind` | A request to connect a field to an actor value. `CreateInput` and `ConnectInput` turn it into an `EditBatch`. | `InputConnections.h` |
| `ResponseGraph` | 65 sampled values of one signal over time, with the live value and a caption. `BuildResponseGraph` builds it. | `ResponseGraph.h` |

## How editing flows

```
Recipe (held by engine/RecipeEditor)
  │  Manager::BuildSnapshot                         engine/ManagerSnapshot.cpp
  │    Studio::BuildRecipeRow(RecipeRowInput)       studio/RecipeSnapshot.cpp, Rows.cpp
  ▼
Snapshot (read-only; one tick behind the Recipe)
  │  menu/ draws a Frame{snapshot, piece, recipe,   menu/StudioPage.cpp
  │  geometry, names, state, intents}; forms from   studio/Forms.cpp, Panels.cpp, Board.cpp
  ▼
widget commits text ──▶ FormField::bind ──▶ RecipeEdit
  │  Studio::Post(intents, recipeID, edit)          studio/MenuState.cpp
  ▼
Dispatch(intents, state, snapshot)                  menu/Menu.cpp
  │  Studio::AcceptIntent    refuse while unsettled studio/MenuState.cpp
  │  Studio::Reduce          change MenuState       studio/MenuState.cpp
  │  Perform ─▶ IntentPerformer                     menu/Menu.cpp
  │  Studio::TrackEditorChange                      studio/MenuState.cpp
  ▼
RecipeEditor::EditRecipe(id, EditBatch, expected)   engine/RecipeEditor.cpp
  │  task queue: Studio::CheckEditRevision          studio/Gesture.cpp
  │  RecipeEditor::ApplyEdits
  │    Studio::PrepareEdits  copy, apply, check     studio/Edits.cpp
  │    Manager::ChangeAndRebuildActors
  │      history push, replace Recipe,
  │      Studio::AdvanceRevision                    studio/DocumentRevisions.cpp
  ▼
next Snapshot carries editResults
  │  Studio::AcknowledgeEditorOperations            studio/MenuState.cpp
  ▼
MenuState follows or keeps the selection
```

`Dispatch` reduces `EditRecipe`, `Undo` and `Redo` before it performs them,
and performs every other intent before it reduces it. An edit changes the
`Recipe` in memory only. `RecipeEditor::SaveRecipe` writes the file through
`engine/RecipeStore.cpp` as a separate operation. Paint follows the same
path: `KeepPaint` reaches `RecipeEditor::KeepPaint`, which calls
`PreparePaintCommit` and then `ApplyEdits`.

## The files

| Concern | Files |
|---|---|
| Snapshot rows | `Snapshot.h`, `RecipeSnapshot.h/.cpp`, `Rows.h/.cpp`, `SourceRows.cpp`, `Names.h/.cpp` |
| View and isolation | `View.h`, `Isolation.cpp` |
| Intents and recipe edits | `Intent.h`, `Edits.h/.cpp`, `EditChecks.h/.cpp`, `Relationships.h` |
| Field checks and parsing | `FieldCheck.h/.cpp`, `FieldParsing.h/.cpp` |
| Creation path | `Create.h/.cpp` |
| Widget vocabulary and forms | `Widgets.h`, `Fields.h/.cpp`, `Forms.h/.cpp` |
| Menu state | `MenuState.h/.cpp`, `EditResult.h/.cpp`, `FileOperation.h` |
| Mask editor | `Mask.h/.cpp`, `TermTemplates.h/.cpp`, `Presets.h/.cpp`, `SourcePlan.h/.cpp` |
| Paint mode | `PaintSession.h/.cpp`, `PaintCommit.h` |
| Selection, board and navigation | `Selection.h/.cpp`, `Board.h/.cpp`, `Panels.h/.cpp`, `Navigation.h/.cpp`, `SelectorEdit.h/.cpp` |
| Engine-side editor records | `DocumentRevisions.h/.cpp`, `History.h`, `Gesture.h/.cpp`, `ApplicationRecord.h/.cpp`, `ResolveOutput.h/.cpp` |
| Game catalogs and inputs | `GameObjects.h/.cpp`, `InputCatalog.h/.cpp`, `InputConnections.h/.cpp`, `ResponseGraph.h/.cpp` |

## See also

- `REFERENCE.md` → *studio*, *Menu mechanics*, *Recipe CRUD travels one
  pipeline*, and *Field ranges, cascade removal, channels (2026-09-17)*.
- `docs/ui-api.md` → *Two layers*, *The `Frame`*, *Forms and fields*,
  *Editing is intents*, and *Navigation*.
- `docs/conventions.md` → *Runtime identities and editor commits* (*Source
  planning*, *Board and selection*, *Row projection*, *Edit commits*, *Paint
  commits*), *Studio file split*, and *Variants and closed sets*.
