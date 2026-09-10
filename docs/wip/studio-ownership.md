# studio/ `.cpp` ownership map

Every function declared in a `src/studio/*.h` header maps to exactly one owning
`.cpp` file. Frozen at the shape step; fill agents own disjoint files. Two
headers are pure data (`Snapshot.h`) or header-only template (`History.h`) and
own no translation unit.

## Headers and the types they define

| Header | Key types |
| --- | --- |
| `View.h` | `PieceRef`, `Pin`, `LayerKey`, `View`, `Mode`, `Layout` |
| `Snapshot.h` | `TextureHandle`, all `*Row` records, `Status`, `Snapshot` |
| `Edits.h` | the `Set*`/`Add*`/`Remove*` edit structs, `RecipeEdit`, `EditBatch`, `ReferenceCounts` |
| `Forms.h` | `FieldKind`+`kFieldKinds`, `FieldInputKind`, `FieldCheckKind`, `Swatch`, `FieldDetail`, `FormField`, `FieldBinding`, `FieldCreator`, `OutputHeader` |
| `FieldCheck.h` | (functions only, over `Names`) |
| `Names.h` | `RowKind`, `Names` |
| `Selection.h` | `Selection` |
| `Board.h` | `CellState`, `Cell`, `LightCell`, `Board` |
| `Panels.h` | `LayerStackRow`, `ForeignRow`, `LayerStack`, `Inspector`, `SignalNames`, `SignalList` |
| `SelectorEdit.h` | `SelectorClauseRow`, `SelectorView` |
| `Rows.h` | (functions only — pure per-row projections) |
| `Mask.h` | `TermKind` alternatives, `TermKind`, `TermOp`, `Term`, `MaskStack` |
| `Presets.h` | `MaskPreset`, `MaskPresets` |
| `TermTemplates.h` | `Existing`, `BuiltTerm`, `TermField`, `OfferGroup`+`kOfferGroups`, `TermOffer` |
| `PaintSession.h` | `PaintSession` |
| `Intent.h` | `MenuState`, `FiringDraft`, `ResourceTab`, the 45 intent structs, `Intent`, `Intents` |
| `History.h` | `History<T>` (template, inline), `EditHistory` |

## Function → owning `.cpp`

### `Edits.cpp`
`LightParamName`, `LightVectorName`, `ShellParamName`, `ShellVectorName`,
`ShellPointName`, `Apply(Recipe&, const RecipeEdit&)`,
`Describe(const RecipeEdit&)`, `Apply(Recipe&, const EditBatch&)`,
`Describe(const EditBatch&)`, `CountReferences`, `RenameInExpression`,
`DefaultLayer`, `DefaultOutput`.

### `FieldCheck.cpp`
`CheckField`, `CheckSignalValue`, `CheckCurveText`, `CheckMaskText`.
Reuses recipe/'s `RowTypes` queries (`SignalTypeOf`, `TexelTypeOf`,
`MaskTypeOf`, `NamesTrigger`, `CheckSource`/`CheckLayer`/…) rather than
re-deriving "does this name a known row of the right type" — see the row-check
note below.

### `MenuState.cpp` (the reducer)
`Reduce`, `Post(Intents&, Intent)`, `Post(Intents&, const std::string&,
RecipeEdit)`, `ResourceTabName`, `ModeName` (`View.h`), `LayoutFor` (`View.h`).

### `Board.cpp`
`CellAt`, `BuildBoard`.

### `Panels.cpp`
`BuildStackView`, `BuildInspector`, `SignalNamesOf`, `BuildSignalList`,
`LightRowOf`, `ShellRowOf`, `SourceRowOf`, `SourceKindOf`, and every `Forms.h`
builder: `FieldDetailName`, `RowNameField`, `CurveTextField`, `MaskTextField`,
`InspectorForm`, `ScalarForm`, `SignalForm`, `SignalValueEdit`, `SourceForm`,
`RecipeHeaderForm`, `OutputHeaderForm`, `LightForm`, `ShellForm`,
`LiteralColor`, `LiteralColorText`.

### `SelectorEdit.cpp`
`SelectorViewOf`, `SelectorWithClause`, `SelectorWithoutClause`,
`SelectorWithKind`, `SelectorWithOperand`.

### `Selection.cpp`
`SelectedPiece`, `SelectedRecipe`, `SelectedGeometry`, `SelectedOutput`,
`RequestOf`, `ResolveSelection`, `ViewedRecipes`, and the out-of-line `View`
members `View::RecipeIDs`, `View::RenameRecipe`, `View::ForgetRecipe`.

### `Names.cpp`
`RowKindName`, `NamesOf`, `TakenNames`, `UniqueName`, `ReferenceText`,
`ReferenceName`, `NameMatches`, `GeometryLabel`.

### `Rows.cpp`
`SignalRowOf`, `CurveRowOf`, `MaskRowOf`, `LayerRowOf`, `ScalarRowsOf`,
`OutputRowOf`.

### `Mask.cpp` (the mask model)
`TermKindName`, `TermOpName`, `ParseTermOp`, `BuildMask`.

### `Presets.cpp`
`ParsePresets`.

### `TermTemplates.cpp`
`ExistingOf(const RecipeRow&)`, `ExistingOf(const Recipe&)`, `MaterialiseTerm`,
`BuildTerm`, `TermLabel`, `TermLabelOf`, `ProposedMaskName`, `TermForm`,
`OffersOf`, `OffersOfRecipe`, `TermDetailOf`, `ScratchEdits`.

### `PaintSession.cpp`
`ScratchOf`, `PaintOutput`, `PaintSurfaceEdits`, `PaintRecipe`, `KeepEdits`.

### No `.cpp` (owned by the header)
`History.h` — `History<T>` and its members are an inline template; `EditHistory`
is an alias. `Snapshot.h`, `View.h` (inline queries), and the `State()`
singleton in `Intent.h` are inline/data and need no translation unit.

## The one row-check has one home

`_old` wrote "does this field name a known row of the right type" three times
(`Validator`, `CheckSourceKind`, `EditCheck`) and they diverged. In the new
tree the canonical typed-row query already lives in `recipe/Signals.h` over
`RowTypes` (`SignalTypeOf`, `TexelTypeOf`, `MaskTypeOf`, `NamesTrigger`, and the
`Check*` family), owned by `recipe/Signals.cpp`. `FieldCheck.cpp` and the recipe
loader both call those; studio never re-derives them. `FieldCheck.cpp` owns only
the studio-facing text validators (`CheckField` and friends) built on top. This
satisfies the brief's "one row-check, one home" — the home is `recipe/`, and
studio consumes it — which is a reconciliation point for the barrier (the brief
named `FieldCheck.cpp` as the home before wave 1 shipped the `RowTypes` checks).
