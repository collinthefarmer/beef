# menu: `.cpp` ownership map

Wave-4 shape output. Every function declared in `src/menu/*.h` maps to exactly
one owning `.cpp`. No declared function is unowned. Fill agents implement one
`.cpp` each, disjoint. These modules are engine-facing (draw over ImGui) and not
native-testable; correctness rides the wave-5 in-game checkpoint. The behaviour
oracle each `.cpp` diffs against is named per file. `docs/wip/wave4-seam.md` is
the frozen interface; if a fill needs to diverge, stop and raise it.

## Headers and the types they define

| Header | Key types |
| --- | --- |
| `Frame.h` | `Frame` (the per-frame context every page reads/writes) |
| `MenuWidgets.h` | `Table`, `ChooserPick`, `SoloMuteChange`, `RuleFilter`, `TextCheck`, `WidgetSize`, `FilterSpec`, `ChooserRowSpec` |
| `FormDraw.h` | `kMaxSignalModalDepth` |
| `BoardPage.h` | (functions only) |
| `ContextRows.h` | `PaneChoice` |
| `StackPanel.h` | (functions only) |
| `ResourcePanels.h` | (functions only) |
| `PaintPanel.h` | (functions only) |
| `StudioPage.h` | (functions only) |
| `Menu.h` | (functions only) |

`Frame` is a plain record of borrowed pointers (`const Snapshot*`, the three
row pointers, `const Names*`, `MenuState*`, `Intents*`) plus `scale`. It carries
no behaviour; `state->selection`, `snapshot->view`, `state->layout` are read
through the `Frame.h` accessors.

## Function → owning `.cpp`

### `src/menu/MenuWidgets.cpp` (diffs `_old/MenuWidgets.cpp`)

The ImGui renderer over the frozen studio spec data. Resolves `Studio::Width`
to pixels, draws `Studio::Column`/`TableStyle` tables, a `Studio::RuleSpec` to a
returned `Studio::RuleClick`, and a `Studio::ThumbnailSpec` image.

- `Table::Begin` (span and initializer-list overloads), `Table::Cell`,
  `Table::End`
- `ResolveWidth`, `NextItemWidth`, `FitWidth`, `BlendWidth`, `WidestOf`,
  `ButtonWidth`, `TextWidth`, `CheckboxWidth`, `ItemSpacingX`, `RowButtonWidth`,
  `RuleHeight`
- `TextField`, `LiveTextField`
- `Thumbnail`, `ThumbnailButton`
- `BlendCombo`, `ChoiceCombo`, `ReferenceCombo`
- `Badge`, `ValueWidget`, `DetailButton`, `ValueText`, `ValueSwatch`
- `ModeBar`, `Section`, `Split`
- `Rule()` (plain separator), `Rule(const RuleSpec&)`, `RuleWithFilter`
- `ChooserRow`, `Toggle`, `DetailModal`, `RightAligned`, `Disabled`,
  `HeldLabel`, `LitButton`
- `RemoveButton`, `SoloButton`, `MuteButton`, `SoloMute`, `DragHandle`,
  `DropTarget`
- `Problem`, `Warn`, `Ok`, `Dim`, `HelpMarker`, `Tooltip`

TU-local (frozen oracle names): `KeyOf`, `Literal`, `TrackActive`, `Colored`,
`PreviewOf` (the one `render/RuntimeTextures.h` `TextureLab::Preview` call),
`ReferenceEntries`, `ProblemLabel`, `ColorSwatchPicker`, `BadgeStyle`/`StyleOf`/
`BadgeFrame`, `DrawRuleLine`, `SquareToggle`.

### `src/menu/FormDraw.cpp` (diffs `_old/ComposePage.cpp` form/signal helpers)

The shared mid-level: draws a `Studio::FormField` per its `FieldKind`, posts the
resulting `RecipeEdit`, and runs the signal-detail modal chain. Pre-placed here
(not in a page) because every editing page composes these.

- `FieldInput`, `PostField`, `DrawRowField`, `DrawFieldTable`, `DrawForm`,
  `DrawFormWithSignals`
- `FirePopup`, `DrawSignalDetail`, `DrawSignalModal`
- `DrawSelector`

### `src/menu/BoardPage.cpp` (diffs `_old/ComposePage.cpp` board block)

- `DrawBoard`, `DrawBoardPage`

TU-local: `DrawCell`, `DrawWrittenCell`, `DrawLightCell`, `CellTooltip`,
`PickOf`, `Joined`.

### `src/menu/ContextRows.cpp` (diffs `_old/ComposePage.cpp` context block)

- `ChoosePane`, `PickedCell`, `DefaultKeyOf`
- `DrawContext`, `DrawPaneRule`, `DrawOutputHeader`, `DrawRecipeSettings`

TU-local: `DrawRecipeContext`, `DrawEditContext`, `SelectionCombo`,
`RecipeCombo`, `RecipeLabel`, `IsolateCheckbox`, `KeysPopup`, `UndoRedoButtons`,
`TargetChoice`, `SlotChoice`, `ClearButton`. The merge split recipe actions,
key rows, slot choices and default-setting actions into smaller local helpers.

### `src/menu/StackPanel.cpp` (diffs `_old/ComposePage.cpp` stack block)

- `DrawStack`

TU-local: `DrawComposite`, `DrawScalars`, `BeginLayerTable`, `DrawLayers`,
`DrawStackRow`, `DrawForeignRow`, `DrawAddLayer`, `DrawInspector`,
`DrawInspectorFields`, `DrawDetailModal`.

### `src/menu/ResourcePanels.cpp` (diffs `_old/ComposePage.cpp` resources block)

- `DrawResourcesRule`, `DrawResources`

TU-local: `DrawSignals`, `DrawSignalRow`, `DrawSignalEditor`, `DrawSignalCurve`,
`DrawCurves`, `DrawSources`, `DrawMasks`.

### `src/menu/PaintPanel.cpp` (diffs `_old/ComposePage.cpp` region block)

The mask editor. The frozen "region" vocabulary is gone (see the seam doc);
the terms build a mask.

- `DrawPaintHead`, `DrawMaskRule`, `DrawMaskStack`, `RebuildScratch`,
  `EditMaskAsTerms`

TU-local: `DrawMaskPicture`, `DrawTermRow`, `DrawTermDetails`,
`DrawTermSettings`, `DrawOffers`, `AddTermOfKind`, `BeginTermTable`.

### `src/menu/StudioPage.cpp` (diffs `_old/ComposePage.cpp` `RenderStudio`)

The compose page frame: gets the snapshot, resolves the selection, builds the
`Frame`, runs the view-model builders, draws the mode bar / body / footer, then
`Menu::Dispatch`es the collected intents.

- `RenderStudio`

TU-local: `DrawBody`, `DrawFooter`, `HistoryKeys`.

### `src/menu/Menu.cpp` (diffs `_old/Menu.cpp` registration + `_old/ComposePage.cpp` `Perform`/`Dispatch`)

Registration, the intent-application seam, the shared status/header, and the
`Frame` accessors.

- `Frame` accessors: `ActorOf`, `SelectionOf`, `ViewOf`, `LayoutOf`
- `Perform`, `Dispatch`
- `RenderStatus`, `RenderHeader`
- `RegisterMenu`

### `src/menu/RecipesPage.cpp` (diffs `_old/Menu.cpp` `RenderRecipes`)

- `RenderRecipes`

### `src/menu/SetupPage.cpp` (diffs `_old/Menu.cpp` `RenderSetup`)

- `RenderSetup`

TU-local: `Widget`, `DrawSaveBar`, `DrawValueTable`, `DrawLog`, `Shown`,
`FindSetting`, `MarkReapply`, and the reapply/save-bar file statics.

## Header → owning `.cpp`(s)

- `Frame.h` → `Menu.cpp`
- `MenuWidgets.h` → `MenuWidgets.cpp`
- `FormDraw.h` → `FormDraw.cpp`
- `BoardPage.h` → `BoardPage.cpp`
- `ContextRows.h` → `ContextRows.cpp`
- `StackPanel.h` → `StackPanel.cpp`
- `ResourcePanels.h` → `ResourcePanels.cpp`
- `PaintPanel.h` → `PaintPanel.cpp`
- `StudioPage.h` → `StudioPage.cpp`
- `Menu.h` → `Menu.cpp` + `RecipesPage.cpp` + `SetupPage.cpp`

## Within-menu include DAG (one-way, no cycle)

```
Frame.h
  ├─ MenuWidgets.h
  └─ FormDraw.h ─(needs Frame + MenuWidgets)
       ├─ BoardPage.h
       ├─ ContextRows.h
       ├─ StackPanel.h
       ├─ ResourcePanels.h ─→ PaintPanel.h (EditMaskAsTerms bridge)
       └─ PaintPanel.h
            └─ StudioPage.h ─(includes all five pages + FormDraw)
                 └─ Menu.h ─(RegisterMenu wires StudioPage/RecipesPage/SetupPage)
```

`Menu.cpp` includes `StudioPage.h`, `menu/RecipesPage`/`SetupPage` render
entries are declared in `Menu.h`; no page includes `Menu.h` except through the
`Dispatch`/`Perform` seam, which `StudioPage.cpp`, `BoardPage.cpp` (its
`DrawBoardPage` is dispatched by `RecipesPage`), `RecipesPage.cpp` and
`SetupPage.cpp` call. To keep the DAG acyclic, pages COLLECT into
`frame.intents` and the registered page frame calls `Menu::Dispatch`; a leaf
page never calls `Dispatch` itself.
```
