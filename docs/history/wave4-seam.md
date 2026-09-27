Status: history. The `menu/` interface it froze is superseded by
`docs/ui-v2-implementation-plan.md`, which owns that surface now. Names and
paths here predate the critique remediation of 2026-09-14 (Plan C's file moves
and Plan D's renames); the root `README.md` lists the current set.

# wave 4 seam: the menu surface interface

Barrier output. The shape agent froze the `menu/` headers and the ownership map;
this file is the interface the fills follow. Where a decision changed a header,
the header and `docs/wip/menu-ownership.md` already match this file. A fill that
needs to diverge stops and raises it, it does not re-decide.

The menu cluster lives in namespace `BetterEnchantmentEffects::Menu`. Studio
types are referenced qualified (`Studio::Snapshot`, `Studio::FormField`, …).
Every page reads the immutable snapshot and posts intents; none touches engine
state directly (the one exception is the read-only `render/RuntimeTextures.h`
texture-preview call in the renderer, decision R2 below).

## Layering (one-way includes)

```
menu  →  engine  →  render  →  { planners, recipe, mesh, studio }
  │        │
  │        └─ menu → studio (specs, view-model builders, Reduce/Post/State)
  └─ menu → render/RuntimeTextures.h  (renderer only; texture preview, read-only)
```

`menu` includes `engine` (the `Manager` shell, `RecipeStore`, `Settings`),
`studio` (the frozen spec + view-model + `Reduce`), and — from the renderer TU
only — `render/RuntimeTextures.h` for `TextureLab::Preview`. Nothing below menu
includes menu. Within menu, the include DAG is in the ownership map; the barrier
is that pages COLLECT intents into `frame.intents` and only a registered page
frame calls `Menu::Dispatch`.

## Decision 1 — the renderer seam: what the studio specs leave to resolve

The pure widget vocabulary (`studio/Widgets.h`, `Fields.h`, `Forms.h`, `Page.h`)
is data. `menu/MenuWidgets.cpp` + `menu/FormDraw.cpp` are the ImGui renderer that
resolves exactly these four things and nothing else:

- **`Studio::Width` → pixels.** `ResolveWidth(width, scale)` maps
  `kFill`/`kFit`/`kPx` to an ImGui item width (`kFit` measures the text; `kFill`
  is the stretch sentinel). `NextItemWidth` applies it; `Table::Begin` applies
  it per `Studio::Column`.
- **`Studio::RuleButton` press → `Studio::RuleClick`.** A page builds a
  `Studio::RuleSpec { text, span<RuleButton> }`; `Rule(spec)` draws the strip and
  returns `RuleClick { clicked, index }`. The page maps `index`
  (→`RuleButton::action`, a `Studio::RuleAction`) to an intent. `RuleWithFilter`
  is the same for the resources rule, also returning the live filter text.
- **Each `Studio::FormField` kind.** `FormDraw::FieldInput` dispatches on
  `kFieldKinds[kind].input` (`FieldInputKind`) to the matching widget — the six
  builder kinds `ValueField`/`ReferenceField`/`ChoiceField`/`BlendField`/
  `ToggleField`/`TextedField` from `studio/Fields.h` all land here. `PostField`
  turns the committed text into a `RecipeEdit` (via the field's `bind`/`create`)
  or logs a refusal. `CheckField`/`CheckMaskText` (`studio/FieldCheck.h`) are the
  live validators the text widgets call.
- **`Studio::Column`/`TableStyle` tables and `Studio::ThumbnailSpec` images.**
  `Table` draws the grid; `Thumbnail`/`ThumbnailButton` draw the preview.

Everything else the specs deliberately leave alone (labels, ranges, choices,
detail flags) is data the renderer reads, not resolves.

## Decision 2 — the menu↔engine seam: snapshot in, intents out

Read off `_old/ComposePage.cpp` `RenderStudio`/`Dispatch`/`Perform` and reconciled
against the wave-3 `engine/Manager.h`.

**Snapshot in (per frame, per registered page).** Each frame:
`Manager::GetSingleton()->Watch(Studio::RequestOf(State().selection))` then
`std::shared_ptr<const Snapshot> held = manager->LatestSnapshot()`. `Snapshot` is
`Manager::Snapshot`, which is `= Studio::Snapshot` (Manager.h line 141). The page
reads `*held`; it never builds a snapshot (`Manager::BuildSnapshot`/
`PublishSnapshot` are engine-private, wave 3). The status bar reads
`snapshot.status` and `snapshot.tickMS`; the loaded-recipe table reads
`snapshot.loadedRecipes`. At merge, these projections were populated on the
game thread to remove the recovered menu's unsynchronized reads of manager
and recipe-store containers. `snapshot.loaded` remains the list of recipe IDs.

**Intents out.** Pages push `Studio::Intent`s into `frame.intents` (via
`Studio::Post`). At frame end the registered page frame calls
`Menu::Dispatch(intents, State(), snapshot)`, which is the whole return path:

```
Dispatch(intents, state, snapshot):
  for each intent:
    Menu::Perform(intent, state, snapshot.view)   // engine effects
    Studio::Reduce(state, intent)                 // UI-state transition (wave 2)
  Studio::ResolveSelection(state.selection, snapshot)
  intents.clear()
```

`Menu::Perform` is the ONLY place menu calls the engine to mutate. Its arms map
one-to-one to the frozen oracle's `Perform` (now over the wave-2 45-variant
`Intent`): `EditRecipe`→`Manager::EditRecipe`, `Solo*`/`MuteLayer`→`Isolate`/
`UpdateView`, `SetFreeze`/`SetScrub`/`SetSpeed`/`StepClock`→`UpdateView`,
`Undo`/`Redo`→`Undo/RedoRecipe`, `CreateRecipe`→`NewRecipe`,
`RenameRecipe`→`RenameRecipe`, `BeginPaint`/`SetPaintSurface`/`KeepPaint`/
`EndPaint`→the paint methods, `ReadMesh`→`RequestMesh`,
`FireTrigger`→`FireAt`, `PinRecipe`/`PickRecipe`→`PinRecipe`. The
UI-only intents (`Pick*`, `View*`, `Show*`, the mask-editor `*Term`/`*Mask`,
`ScratchRebuilt`) are Perform no-ops handled entirely by `Studio::Reduce`.
`Studio::Reduce`, `Post`, `State` are wave-2 (`studio/MenuState.cpp`); wave 4
does not redeclare or re-implement them.

Every engine entry point Perform and the pages need already exists on
`Manager` / `RecipeStore` / `Settings` — no engine-seam gap:

- apply: `EditRecipe`, `UndoRecipe`, `RedoRecipe`, `SaveRecipe`, `RevertRecipe`,
  `ReloadRecipes`, `NewRecipe`, `RenameRecipe`, `Isolate`, `UpdateView`,
  `PinRecipe`, `BeginPaint`, `SetPaintSurface`, `KeepPaint`, `EndPaint`,
  `RequestMesh`, `FireAt`, `ReapplyAll`, `RetireAll`.
- read: `Watch`, `LatestSnapshot`; status and loaded-recipe summaries are
  copied into the snapshot by the engine, including origin diagnostics and paths;
  `GetSettings`/`GetMutableSettings`/`SetSettings`/`SettingTable`/
  `SettingsDiffer`/`SaveSettingsToDisk`/`LoadSettingsFromDisk`
  (`Settings.h`/`SettingsFile.h`); `g_logRing` (`PCH.h`).

## Decision 3 — the per-frame `Frame` context supersedes `Studio::Page`

Wave 2 pre-placed `Studio::Page` (piece/recipe/geometry/names/intents/layout/
scale) as a page bundle, but every page frame also needs the `Snapshot`, the
mutable `MenuState` (selection, mode, paint, mask, buffers, firing) and the
`View`. `menu/Frame.h` defines `Frame`, a superset of borrowed pointers, and is
threaded through every page function so no signature exceeds the clang-tidy
`ParameterThreshold` of 4. `Studio::Page` and its accessors (`ActorOf`/`BonesOf`/
`ScaleOf`, homed in `studio/Fields.cpp`) remain available but are not the menu's
frame type; `Menu::ActorOf/SelectionOf/ViewOf/LayoutOf(const Frame&)` are the
menu-side accessors. This is not a studio change — `Studio::Page` stays as is.

## Decision 4 — "region" is gone; the mask editor builds a mask

Per the wave-2 rename (memory: "the word region goes"), `MenuState` holds
`mask` (`Studio::MaskStack`) + `maskHistory`, the intents are `LoadMask`/
`ClearMask`/`UndoMask`/`RedoMask`, and the presets are `Studio::MaskPresets`
(`ParsePresets`). `PaintPanel` names follow: `DrawMaskStack`, `DrawMaskRule`,
`EditMaskAsTerms`, `DrawMaskPicture` (frozen: `DrawRegionStack`, `RegionRule`,
`EditMaskAsRegion`, `DrawRegionPicture`). The scratch mask, `kScratchMask`,
`kPaintRecipe`, `PaintOutput`/`PaintRecipe`/`KeepEdits`, `OffersOf`/`BuildTerm`/
`TermForm`/`ScratchEdits` are wave-2 (`studio/PaintSession.h`,
`studio/TermTemplates.h`).

## Decision 5 — resolved name collisions (winning name)

- **`ValueField`.** `Studio::ValueField(ValueFieldSpec)` (wave-2 `Fields.h`
  factory, builds a `FormField`) vs the frozen `Widgets::ValueField` (the
  low-level char*/`FieldKind` value input widget). The renderer's low-level
  widget is renamed **`Menu::ValueWidget`**; `Studio::ValueField` keeps its
  name. `FormDraw::FieldInput` calls `Menu::ValueWidget`.
- **The `Widgets` namespace is gone.** Wave 2 collapsed `Studio::Widgets::*`
  data types into `Studio::*`. The renderer is `Menu::*`, not `Studio::Widgets::*`
  and not `Studio::*`. `Rule`, `Table`, `Section`, `Split`, `Toggle`, `ModeBar`,
  `Badge`, `Thumbnail`, etc. are all in namespace `Menu`.
- **`Rule`.** The frozen `Widgets::RuleLine` (a struct carrying a `std::function
  right`) is replaced by the pure `Studio::RuleSpec` + `Studio::RuleClick`.
  `Menu::Rule()` (plain separator) and `Menu::Rule(const RuleSpec&)` (→ click)
  are arity-disambiguated overloads.
- **`Post`/`PostAll`.** The frozen file-local `Post`/`PostAll` in ComposePage are
  dropped; the tree's one poster is wave-2 `Studio::Post`. A vector-of-edits
  posts as a single `EditRecipe{recipe, edits}` through `Studio::Post`.
- **`ReadMesh`.** `Studio::ReadMesh` (intent) vs `engine::ReadMesh` (MeshReader
  fn) are distinct namespaces; menu references only the intent. `BonesOf` —
  `Studio::BonesOf(Page)`, `BonesOf(MeshData)` — is not declared by menu.

## Decision 6 — new capability with no oracle: the selector editor

`studio/Forms.h::OutputHeaderForm` and `studio/SelectorEdit.h`
(`SelectorViewOf`, `SelectorWithClause`/`WithoutClause`/`WithKind`/`WithOperand`)
are wave-2 additions the frozen menu never drew. `FormDraw::DrawSelector` renders
a `Studio::SelectorView` and posts `SetOutputSelector`/`SetLightSelector`;
`ContextRows::DrawOutputHeader` composes it with the output's `FormField`s. This
is the one page area with no `_old` oracle — fills build it from the wave-2 model,
not from ComposePage.

The merge connected the previously uncalled forms. `OutputRow::selection` and
`LightRow::selection` retain the typed selector, and `RecipeRow::outputs` keeps
all output definitions in the full recipe projection. `DrawRecipeSettings`
draws recipe priority/clock speed and an Output settings modal. That modal is
also available when no geometry matches, so editing a selector never removes
the only path to correct it. Selected surface and light contexts also draw
their selectors inline.

## Merge revision — renderer argument records

The frozen positional widget signatures exceeded the four-parameter gate.
`WidgetSize` groups width and scale; `FilterSpec` holds the filter entry;
`ChooserRowSpec` holds a chooser's displayed data. `ReferenceCombo` and
`ValueWidget` consume the existing `Studio::FormField` record. The renderer
and all callers use these records; ownership and intent flow are unchanged.

## To fold in at merge

- **`main.cpp` wiring (not touched by this stage).** The wave-0 stub does not
  call the menu. At merge, `main.cpp` (or the SKSE messaging init) calls
  `BetterEnchantmentEffects::Menu::RegisterMenu()` once, after data load, exactly
  where `_old` called `RegisterMenu()`. `RegisterMenu` registers the three
  `__stdcall` entries `Menu::RenderStudio` ("Studio"), `Menu::RenderRecipes`
  ("Recipes"), `Menu::RenderSetup` ("Setup") via `SKSEMenuFramework`.
- **`CMakeLists.txt`.** Add `src/menu/*.cpp` to the DLL target (the menu links
  ImGui via the header-only `src/extern/SKSEMenuFramework.h`, and `render` for
  `TextureLab`).
- **REFERENCE.md additions (draft), under a new "menu" heading.**
  - The renderer's one downward-skip include: `menu/MenuWidgets.cpp` includes
    `render/RuntimeTextures.h` and calls `TextureLab::GetSingleton()->Preview`
    to turn a `Studio::TextureHandle` (`RE::NiSourceTexture*`) into an ImGui
    `ImTextureID`. There is no engine wrapper for preview; this is the renderer's
    sole render dependency and is read-only.
  - `static_assert(std::is_same_v<Studio::FieldKey, ImGuiID>)` (frozen
    MenuWidgets invariant): the field-key type is the ImGui id type, so text/
    number buffers key off `ImGui::GetID`. Keep the assert in `MenuWidgets.cpp`.
  - `Mode` has two variants now (`kCompose`, `kPaint`); the frozen `kDesign`
    mode, `Layout::implemented`/`designPanel`/`widgetScale`/`compositeSize`/
    `rowThumbnail` are gone. Widget scale is `Frame::scale` (was
    `Layout::widgetScale`); geometry bones are `GeometryRow::bones`
    (`BoneCoverage`, was the old `BoneRow`).
```
