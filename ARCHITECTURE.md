# WornEnchantmentPBR: architecture and API reference

How the modules fit, what flows between them, who owns what, and how to
add a kind of thing. The headers state each module's types and functions
(types first, then the functions over them); this file states what no
single header can: the shape of the whole. Read it before a refactor, and
update it with one. Facts about the engine and Community Shaders live in
`NOTES.md`; the recipe format in `schema/recipe.schema.json` and the
compositor brief; the code rules in `CLAUDE.md`.

## The one-paragraph model

A recipe is a JSON file: signals (scalars and colours per tick), curves,
sources (images), masks (per-texel expressions over sources), outputs
(layer stacks on a material slot or shell, or a light), shell settings and
variants. The store loads every recipe file. When an actor's worn armor
changes, the manager collects its PBR pieces, resolves which recipes match
each piece by key, and applies each: a compiled signal graph with its
per-actor state, one rendered stack per material output per geometry
(the compositor, through the texture lab), and bindings that write the
results into the engine's material slots, into a shell clone, and into
lights. Each frame the manager ticks: signals, then stacks, then slot
writes. On unequip or reload the bindings restore what they touched. The
menu reads a copied snapshot of all this and edits recipes through the
manager, never directly.

## Module map

Engine-free (compile natively, tested through `tests/run-native.sh`):

| Module | Owns | Depends on |
|---|---|---|
| `Core.h` | `Vec2`, `Vec3`, `Value` (variant of the three), `ValueType`, `ShaderChannel` (one component, rgb or luminance of a sample, valued as the lab's shader picks it: the one channel vocabulary of the compositor, the lab, the snapshot's previews and the menu's thumbnails), `Ref`, `AsScalar`/`AsVec3`, `Match` (variant visit), `Get<T>`/`Is<T>`, the word-table readers (`Named<E>`, `NameOf`, `FromName`, `RowOf`, `Choices`, `WordsOf` over any table whose rows have `value` and `name`) | nothing |
| `Vocabulary.h` | the words the format spells, one `inline constexpr` table per enum in enum order (`kSurfaces`, `kShellMaterials`, the signal field and source enums); the tables that carry rules beside their words, each with a `static_assert` on its row count: `kSlots` (`SlotSpec`: channels and their note, the scalars and whether a recipe must give them, the slots CS cannot evaluate beside it, the material map it edits), `kScalarFields` (`ScalarFieldSpec`: the `SlotScalars` member, number or colour, and the one fallback the menu fills in and the binding writes when a file leaves the field out), `kBlends` (`BlendSpec`: the layer shader's mode, normal-stack only), `kImageChannels` (`ImageChannelSpec`: the `ShaderChannel`), `kMaterialChannels` (`MaterialChannelSpec`: the material map and its shader channel, or `kNone` for the two the compositor derives; what a texel reads as, which decides what a threshold can test) `kSignalKinds` (`SignalKindSpec`: one row per alternative of `SignalKind`, in the variant's order, and whether the board's tunable list shows the row as designer-editable text) and `kKeyKinds` (`KeyKindSpec`: the merge/specificity priority, the operand shape — none, form or glob — whether the kind can only match an enchanted piece, and, for a form-shaped kind, the `WornPiece` member it reads: one field for every kind but keyword, which reads a list); the `XName`/`ParseX` and rule functions declared in `Recipe.h` read these, the parser and writer loop over them, and a studio choice field lists them with `WordsOf`; no word, slot fact, shader index, channel fact or key-kind fact is spelled anywhere else | Core, Recipe |
| `Expression.*` | the recipe language: `Program` (bounded postfix op list), `Parse`, `Check` (types), `Evaluate`, `ParseCurve`, `ApplyCurve`; limits `kMaxExpressionLength/Depth/Ops` | Core |
| `Recipe.h`, `Recipe.cpp`, `RecipeJson.cpp` | format 1 records (`Recipe` and everything in it), `ParseRecipe`/`SerializeRecipe`, `Validate`, `Resolve`, variants, `IsAnimated`, glob and selector matching, biped slot names, text forms for the menu, the slot rules (`SlotsOf`, `ScalarsOf`, `ScalarRequired`, `SlotsExclude`, `BlendAllowed`, `ChannelsOf`, `SlotChannelNote`, `BaseMapOf`, `ScalarOf`, `ScalarFallback`) the blend and channel rules (`BlendShaderMode`, `ShaderChannelOf` for image and material channels, `MaterialMapOf`, `MaterialChannelType`, `Thresholdable`) and the signal-kind rules (`SignalKindId`, `SignalKindOf` matching `SignalKind`'s alternative, `SignalKindName`, `ParseSignalKind`, `SignalKindTunable`), each a read of one row of a `Vocabulary.h` table, that `Validate`, the studio's board, the parser, Regions, the compositor and the bindings all read. The key-kind rules (`KeyOperandOf`, `DefaultPriority`, `EnchantmentDerived`) are the same kind of read of `kKeyKinds`; `KeyMatches` (parse-independent, resolution-only) and the parser's key reader dispatch on a row's `operand` rather than switching on `KeyKind`, and `KeyChoicesOf` walks the table highest-priority first to list the forms one `WornPiece` could be keyed to (its member-pointer columns need `WornPiece` complete, so `KeyKindSpec` sits in the resolution section of `Recipe.h`, forward-declaring `WornPiece` where `KeyKind` itself is declared) | Core, Expression, Signals (for graph typing in `Validate`) |
| `Signals.*` | `SignalGraph::Compile` (nodes, order, types, inert), `SignalState` (per applied recipe: `Tick`, `Fire`, `ValueOf`, `Resolve`, `Firings`), `SignalEnvironment` (what a tick reads from the actor), `EventRecord`/`TriggerPayload` | Core, Expression, Recipe |
| `Importer.*` | `EffectShaderRecord` (an EFSH as data), `ImportEffectShader` (record to recipe), `RecipeIdFor` | Recipe, Timing |
| `Timing.*` | the vanilla EFSH animation maths the importer encodes | nothing |
| `Mesh.*` | `MeshData` (bind-pose vertices per partition, every triangle inside its partition, the hash of the bytes read), `DecodeVertex` (the engine's packed vertex bytes), `TrianglesWithin` (the decode-time check), `HashBytes` (FNV-1a, chained), `BuildBake`/`BuildDistanceBake`/`BuildUvBake` (mesh to UV-space triangles carrying a value), the bake cache keys by definition and `TextureSize` (`BakeKeyOf`, `DistanceKeyOf`, `UvKeyOf`; `DefinitionOf` is the size-less half the snapshot looks up by; `KeyDefinition`, `KeySize`) | Core, Recipe, TextureSize |
| `SettingsCore.*` | the `Settings` record and its table | nothing |
| `Snapshot.h` | the menu's read model (`Studio::Snapshot`): rows per piece, recipe (with its undo and redo depths, its light and shell rows), geometry, output, layer, source, mask and slot, typed; `SignalRow.kind` is a `SignalKindId`, not a spelled word; textures as opaque handles the lab keeps alive | Core, Recipe |
| `View.h` | `Studio::View`, how the piece is looked at: freeze, scrub, speed, isolate recipe, output and layer (with whether an output or layer solo began the recipe isolate, so that solo ending clears it), mute set, and the shown/muted predicates the tick and the stack share | nothing |
| `Studio.*` | `Studio::Mode` and `kLayouts`, one `Layout` row per mode (one narrow column; the stack's split ratio and the panes' height shares; `implemented` gates the page's "not built yet" placeholder), `LayoutFor` a lookup over the rows; `Target`, `Selection` (keys: piece, recipe id, geometry name, target and slot, region; the layer index) and its resolution against a snapshot; the view models `Board`, `LayerStack`, `Inspector`, `SignalList` as plain records built by pure functions; `LightRowOf`/`ShellRowOf` (the panels' rows from a recipe), `SourceRowOf`/`SourceKindOf` (a source as texts and back), `SignalNamesOf`; names (`UniqueName`, `NameMatches`, `ReferenceText`, `GeometryLabel`) | Snapshot, View, Recipe |
| `Forms.h` (implemented in `Studio.cpp`) | forms as data: `FieldKind` (scalar, colour, vector, reference, expression, curve, mask, channels, toggle, choice, text), `FieldDetail`, `FormField` (name, kind, text, combo names, detail, and a `bind` that turns committed text into a `RecipeEdit` or refuses), built by `InspectorForm`, `ScalarForm`, `SignalForm`, `SourceForm`, `LightForm`, `ShellForm`, with `creators` (what a field can make in place) and `create` (the edits that make and bind it); `LiteralColor`/`LiteralColorText`; `kFieldKinds`, one engine-free row per kind (glyph, whether a @signal may stand in, the tooltip's rule, the `FieldInputKind` widget and the `FieldCheckKind` shape), read by `ComposePage`'s `FieldInput`, `MenuWidgets`' `StyleOf` (which adds the ImVec4 colour, keyed by kind) and `EditCheck`'s `CheckField` instead of each switching on the kind | Studio, Edits |
| `MenuState.*` | `Studio::MenuState`, the page state: mode and its `Layout`, `Selection`, the `RegionStack` (Paint's terms with selection, solo, mute, the mask being edited, and a dirty flag the page clears by rewriting the paint recipe's scratch), the `PaintSession` (the recipe painted for and the preview surface), text buffers, active and focused field, keyed by `FieldKey` (the ImGuiID of a widget's literal key in the ID scope the page pushes per recipe, output, layer and row); `Intent` (every page action: the mode, picks, edits, solo and mute, freeze, scrub, speed, step, undo, redo, a new recipe, a fired trigger, a mesh read, the region stack's add, set, remove, move, pick, solo, mute, load, clear and settings, the paint session's begin, surface, keep and end) and `Reduce`, the state change of an intent as a pure function (an edit moves the layer selection with its row; a term move or removal carries the stack's indices along) | Studio, Edits, TermStack |
| `History.*` | `Studio::EditHistory`: undo and redo as whole recipes, past and future, capped at 100 | Recipe |
| `Edits.*` | `Studio::RecipeEdit`, every change the menu makes (layers, outputs, scalars, signals, curves, sources, masks: add, set, rename with every reference repointed, remove refused while referenced; the light's and the shell's fields), and `Apply(Recipe&, RecipeEdit)`, which refuses with a diagnostic and leaves the recipe untouched when the edit does not fit; `Describe`, `CountReferences`, `RenameInExpression`, `DefaultLayer`, `DefaultOutput` | Recipe, Expression |
| `EditCheck.*` | validation before apply: `Names` (the recipe's rows by kind and type, from the snapshot) and `CheckField`, why a field's text would be refused, with the file's own parsers, dispatching on the field's `kFieldKinds` row (`FieldCheckKind`) rather than switching on its kind; `CheckSignalValue`, `CheckCurveText`, `CheckMaskText` for the tables' texts | Forms, Expression, Recipe, Snapshot |
| `Regions.*` | the scratch mask's name; `PartitionsOf` and `BonesOf` (a mesh's facts for the snapshot); the preset file (`ParsePresets`: capped lists, every what expression parsed at load; plain names), `Unresolvable` on a geometry's facts, `ExistingOf` (a recipe's sources by definition and every taken name, the one reuse rule), `ScratchOf` and `ScratchEdits` (the scratch row as it stands and the edits that write the stack's built expression into it), `MaterialiseTerm` (the sources a preset needs, twins reused, and the expression that reads them), `TermLabel` and `TermsOfMask` (a term's origin recovered by matching), `ProposedRegionName`; the term templates: `BuildTerm` derives a term's sources and expression from its `TermKind` (one exact spelling per template), `ReadTerm` recovers the template from that spelling over the recipe's source rows, `TermLabelOf` its words, `TermForm` its settings as fields; `OffersOf` lists what the geometry's analysis, the presets and the recipe can offer the Paint stack, as `TermOffer` rows grouped by `OfferGroup` (`kOfferGroups`: each group's word and whether its section opens by default, in display order); the paint recipe: `PaintRecipe` (a clone of the active recipe with one masked emissive output, `PaintOutput`, keyed alone at `kPaintPriority`) and `KeepEdits` (the region and the sources it reads copied into the active recipe, twins reused, taken names made unique); `MeshFacts`/`FactsOf` (a mesh's partitions and bones as snapshot rows, computed once per read) | Analysis, Edits, Forms, TermStack, Mesh, Snapshot |
| `Analysis.*` | what a piece can be shown to have, measured from its own mesh and maps: `AnalyseMesh` (connected components welded by position and UV charts welded by UV, by union-find; each region's share, dominant bone and bind-pose centroid; per-vertex id tables capped at `kMaxIslands`), `BuildIslandBake` (an id map as a bake), `ClusterMaterial` (k-means++ over a low-mip `MaterialSample`, seeded by an LCG and capped, clusters described by fixed bands), `NearestCluster`, `DescribeTexel` (one `TexelBand` table per axis: ascending cuts, one more word than cuts, and which side of a cut is inclusive), `SettingsOf`/`SourceOf` (the file's flat `materialClusters` record and the settings are one idea), `kIslandSources` (each region source's format word, its plain word for labels and the `BakeKind` its id map bakes: `IslandSourceName`, `PlainIslandSourceName`, `IslandBakeOf`); deterministic on the same input; never spells the mask language | Core, Mesh, Recipe |
| `TermKind.h` | a term's settings as data: `RawTerm`, `ReferenceTerm`, `ThresholdTerm` (channel, low, high, softness, posterize, invert), `WhatPresetTerm`, `PartitionTerm`, `BoneTerm`, `IslandTerm`, `ClusterTerm`; the expression is derived from the recipe by Regions' `BuildTerm` and recovered by `ReadTerm` | Analysis, Recipe |
| `TextureSize.h` | the one size of a render target, bake, mask or stack: `Clamp` is the only way to make one (64 to 4096), so nothing sizes at zero; engine-free, so the key functions take it | nothing |
| `TermStack.*` | a region as terms with ops (`set`, `and`, `or`, `not`), each term carrying the `TermKind` its text was built from: `BuildRegion` (the one expression, with solo and mute applied; one term bare, more in a fixed wrapped shape; never past the expression length, a term that would push it past stops the build) and `ParseRegion` (that shape back to terms; anything else one raw term; bounded by length and a term cap) | Expression |

Engine side (compile with CommonLibSSE; thin over the above):

| Module | Owns | Depends on |
|---|---|---|
| `Identity.h` | the plugin name (from CMake), every path and node name derived from it | nothing |
| `EngineForms.*` | `FormKeyFor`, `EditorIdOf` (po3 Tweaks export, else engine), `LookupForm`, `RecordFrom(TESEffectShader)`, `ShaderFor` (the EFSH an enchantment shows) | Recipe, Importer |
| `RecipeStore.*` | the loaded recipes: `LoadRecipes` (files, editor-ID resolution, import of missing EFSH recipes, the region presets beside the DLL), `LoadedRecipes`, `LoadedPresets`, `OriginOf`, `GraphFor` (cached compiled graph), `ReferencesOf` (reference counts recounted on every republish, so the snapshot copies rather than re-parses), editing: `MutableRecipe`, `Revalidate`, `IsDirty`, `SaveRecipe` (the scratch mask dropped), `RevertRecipe`, `NewRecipe` (an empty recipe under user/, dirty), `AddTransientRecipe`/`DropTransientRecipe`/`IsTransient` (the paint recipe: in the list so it resolves, never written, not listed) | Recipe, Signals, Importer, EngineForms, Identity |
| `Environment.*` | `ActorEnvironment`: a `SignalEnvironment` over an actor handle and its enchantment (actor values by measure, states, enchantment fields, EFSH parameters) | Signals, EngineForms |
| `MeshReader.*` | `ReadMesh(BSGeometry*)` (partitions, bones, slots; CPU copy or GPU readback; hashed), `MeshIdentity`/`IdentityOf` (the skin partition, buffer pointers and vertex count a read came from), `CompareWithGpu` (the stale-copy diagnostic), `MeshEntry` (one geometry's mesh, its `MeshFacts`, its bakes keyed by definition and size, last use) and `MeshCache` (`Get` reads once and re-reads on a changed identity, `Cached` never reads, `Sweep` by age sparing bound geometries, `Clear`), `NodeBindPosition`, `ToRootSpace` ; `MeshEntry` carries the `MeshAnalysis` computed with the read, and the read's log line names components and charts | Mesh, Regions, RuntimeTextures |
| `RuntimeTextures.*` | `TextureSize` (64 to 4096; `Clamp` is the only constructor, so no pass is asked for a 0 px target); `TextureLab`: the D3D passes and their resources (`ProgramPass` and `RipplePass` are fixed arrays with counts, so a tick allocates nothing; `SampleMaterial` copies the RMAOS and diffuse maps at the mip that fits 64 px and reads them back as a `MaterialSample`; `RenderClusters` is the classify pass, nearest centroid per texel under the analysis' weights as id / 255, in step with `NearestCluster`): render targets presented through shell textures (`RenderTarget`, `Acquire`, `Scratch`, both by `TextureSize`), the layer pass (`Render` with `Mode::kLayer`), the interpreter (`RenderProgram`), bakes (`BakeMesh`), ripples (`RenderRipple`), curve lookups (`CreateLookup`), readback (`ReadBuffer`, `ExtentOf`, `MeanLuminance`, `MeanChannel`), previews for the menu (`Preview`, the channel pass over one `ShaderChannel` or, with `slope`, the input's relief as a normal map) | Core, Expression, Mesh, PBRMaterial |
| `Compositor.*` | recipes to textures: `Prepare` (an output's stack on a geometry, sized by a `TextureSize` pair: sources, masks, curves, bakes, distance, ripples, base map, size), `DerivedMaps` (the normal map's slope, and the material's cluster map for the `materialClusters` source: the stored sample re-clustered on the CPU for other settings, then the classify pass; each rendered once per apply on first use), `MaterialInputs` (the maps and the flat-displacement measure), `AnalyseMaterial`/`CachedMaterial` (the material's `MaterialSample` and default `MaterialAnalysis`, read back once per pair of maps and kept for the session; never at apply, since the readback stalls the game thread on the GPU: Paint's read of a shape asks for it and a `materialClusters` source asks at prepare), `Render` (per tick, with a `LayerFilter` of hidden layers; a static stack renders again when the filter changes), `RenderedStack`/`RenderedMask`/`RenderedRipple`, `MaterialInputs` (the material's maps, the displacement measured once at apply), `GeometryInputs` (a geometry's material maps plus the masks and ripples of one apply), the one `MeshCache` (`MeshOf` reads through it on the game thread, `CachedMesh` for the snapshot, `SweepMeshes` every 5 s from the tick keeping bound geometries, `ClearMeshes`), bakes stored on the mesh entry under their definition key, `InspectSource`/`InspectMask` (const cache reads for the snapshot: no load, read, bake or render; a row nothing rendered reports `kNotRendered`) | Recipe, Signals, RuntimeTextures, Mesh, MeshReader, Settings, RecipeStore (graph for mask typing) |
| `Binding.*` | the only writer of engine state: `SlotTarget` (interface), `SlotWriter` (slots of one PBR material with save and restore), `MaterialBinding` (a geometry's own material, made private), `ShellBinding` (a clone with a PBR copy or vanilla material, pose), `LightBinding` (point lights with the CS ISL overlay), `PlaceLights` | Recipe, PBRMaterial, Identity |
| `Manager.*` | the object the sinks and the hook call: queues, apply and retire per actor (every PBR geometry a recipe applies to is recorded, bound or not, so an empty recipe stays on its piece), the tick (which reads the `View` for isolate, solo, mute and speed and hands the compositor a `LayerFilter`), events to triggers, recipe editing on the game thread with one `EditHistory` per recipe (`EditRecipe` pushes, `UndoRecipe`/`RedoRecipe` restore, Revert clears), `NewRecipe` (retires everything around the store's list growing), `RequestMesh` (a shape's read for Paint: its mesh, analysed as it is read, and its material's sample and clusters, once per session), `FireAt` (a firing placed at a node), the paint session (`BeginPaint` adds the paint recipe and isolates it, `SetPaintSurface`, `KeepPaint` applies the keep edits to the active recipe as one history step, `EndPaint` drops it), the menu's read side (`Watch`, `LatestSnapshot`, the snapshot built at the tick's end and published whole; `UpdateView` for the view's posted changes) | everything above |
| `Events.*`, `Hooks.*` | engine event sinks (equip, load, node update, hits, animation graph) and the per-frame hook, each a few lines that call the manager | Manager |
| `MenuWidgets.*` | `Studio::Widgets`, every ImGui mechanic in one place: `Width` (fill, fit a text, pixels) and `NextItemWidth`; `Table` (id, `{label, Width}` columns, a `TableStyle`; `Cell` advances, `End` closes); `Section`, `Split` (two resizable columns over a ratio), `Rule` (a rule with a text line above and below, the line's right group anchored on the edge by its exact `ButtonWidth`/`CheckboxWidth`); `Toggle` (a checkbox with a tooltip; isolate and freeze are it), `LitButton` (lit until a step is taken), `Disabled` (a greyed scope), `RightAligned` (a group ending on the line's right edge), `HeldLabel` (a value shown uneditable), `DetailModal` (the one detail modal: opened by name, a definition's width, a close button); the row buttons of a stack, each a square of `RowButtonWidth` (the badge's size, so a column of that width is filled): `RemoveButton` (greyed while referenced), `SoloButton`, `MuteButton` and `SoloMute` (the pair), `DragHandle`; fields at the layout's scale under literal keys, thumbnails (the one place a texture handle is dereferenced, through `TextureLab::Preview`), blend and reference combos, `ValueField` (a @signal combo and a literal text field as one control), the badges per `FieldKind` (`StyleOf` reads glyph, takes-signal and rule from `Forms`' `kFieldKinds` row and keys the ImVec4 colour on the kind itself), the mode bar, drag handle and drop target, text helpers; widgets return values and never edit | Snapshot, Studio, RuntimeTextures |
| `ComposePage.*` | the studio page: snapshot once, selection resolved, the context rows (piece, recipe, New, Undo, Redo; target, slot, region, Clear; in Paint a head line naming the recipe painted for instead) over three scrolling panes (the stack with the inspector, or the light panel or the shell settings by target, switched and reset from the pane's rule, or in Paint the term table across the width (each term's label, its measurements, and a details button whose modal holds its settings, its expression and what it reads), Keep and Discard under it, then what the piece offers as tables in collapsible sections under a rule carrying the filter; the preview surface sits on the head line; the signals; the curves) and the Timeline footer, drawn from `Studio` records under the mode's layout; widgets return `Intent`s into a per-frame list and `Dispatch` runs each through `Reduce` and `Perform` (the manager's edit, undo, redo, new recipe, fire and isolate, and the `View`'s solo, mute, freeze, scrub, speed and step) after the frame; `DrawForm` draws any `FormField` list as the field table, or as several side by side; `DrawBoardPage` draws the board for the Recipes page | Studio, Forms, Edits, MenuWidgets, MenuState, Manager |
| `Menu.*` | registration, the status line every page but the studio starts with, and the Recipes and Setup pages (Setup: the save bar, every setting as a name and value table with switches as checkboxes, the log under them) | ComposePage, Manager, RecipeStore, Settings |
| `Settings.*` | INI load and save around `SettingsCore` | SettingsCore, Identity |
| `PBRMaterial.h` | the layout mirror of CS's `BSLightingShaderMaterialPBR` and its flag bits | nothing |
| `main.cpp` | SKSE lifecycle: logging, hooks, sinks, data-loaded (settings, recipes, menu), pre-load-game (clear) | all |

Dependency direction is downward in each table and from the engine side
to the engine-free side, never back. `Compositor` reaching into
`RecipeStore::GraphFor` for mask typing is the one sideways edge worth
knowing; it exists so masks can read signals by name with the right type.

## Data flow

Load (`kDataLoaded`, game thread): settings from the INI; `LoadRecipes`
reads every `.json` under `Data/<plugin>/` (path order, `user/` last),
resolves editor IDs against loaded forms, imports a recipe under
`imported/` for each armor-enchantment EFSH no recipe is keyed to, and
publishes `LoadedRecipes()`. A key belongs to the last file loaded with
it; the store warns about the rest. The menu registers its pages.

Apply (`Manager::Refresh`, game thread, from a posted task): retire the
actor's previous state, then for the third person and (player only) first
person: `CollectPieces` walks the worn armor's PBR geometries into a
`WornPiece` (armor, addon, enchantment, magic effect, effect shader,
keywords, diffuse paths), `Resolve` picks the matching recipes in merge
order, and for each an `AppliedRecipe` is built: the store's compiled
graph, a fresh `SignalState`, an `ActorEnvironment`, then per geometry
`ApplyGeometry`: the material's maps into `GeometryInputs`, one
`MaterialBinding` or `ShellBinding` per surface as outputs need them, a
`RenderedStack` per material output from `Compositor::Prepare`, and after
the geometries one `LightBinding`. Higher recipes' `replace` outputs drop
lower recipes' outputs on that slot. Every problem is a string on the row
and a log line; nothing throws.

Tick (`Manager::OnFrame`, game thread, from the `PlayerCharacter::Update`
hook, at the settings' rate): `Compositor::BeginTick`; per applied
recipe, `SignalState::Tick` with the environment (a frozen scrub that
moves backwards rebuilds the state and advances it to the moment in one
step, since the state only integrates forward), then per geometry per
output `Compositor::Render` (ripples and masks the stack reads first, then
the layers, alternating the stack's own target with the lab's shared
scratch), then `WriteSlot` into the surface: the composite texture and
the slot's scalars, or the originals and zeros when the output is hidden
by isolate. Then the shell's pose and the light's parameters. Static
stacks render once.

Events (any thread to the game thread): the sinks call `QueueEvent`,
which posts `Fire` to the game thread; `Fire` gives the `EventRecord` to
every applied recipe's `SignalState`, whose triggers accept it by glob and
filter and record a firing with the payload (position, node, arg, value).
`anim.<tag>` comes from the animation graph sink (re-watched on every
apply), `hit.received`/`hit.dealt` from the hit sink, `equip` from the
finalize refresh after an equip event.

Retire (`Manager::Retire`): the recipes' clocks are noted per actor and
recipe, and a re-apply within two seconds (an edit, isolate, re-apply
all) resumes them, so a change never restarts the animation; erasing
the actor's state destroys the bindings in declaration order; each restores what it saved if it still
owns the slot (`StillOwned`), and says so if not.

Menu (render thread): `LatestSnapshot` hands the pages the snapshot the tick built;
the studio page builds its view models from the snapshot and the page
state with `Studio`'s pure functions, draws them through `Widgets`, and
turns what comes back into a `RecipeEdit`, which `EditRecipe` posts to the
game thread: retire the wearers of the recipe, `Apply` it to the store's
copy (a refused edit is a warning and no change), `Revalidate`, re-apply.
View changes (solo, mute, isolate, freeze) write the manager's `View`
directly; the next tick reads them.

## Threads and ownership

- Game thread: everything the manager does, all bindings, the compositor,
  the lab's rendering (the engine's own D3D context, state saved and
  restored around each pass, and the engine's renderer lock held for
  every pass and readback, since the render thread uses the same
  context; NOTES 57), the store's mutation, the `View`, and the building
  of the menu's snapshot.
- Render thread (the menu framework): the page functions. The rule is
  one sentence: a page reads the latest snapshot and posts intents,
  nothing else. `Manager::LatestSnapshot` hands back an immutable
  `Snapshot` (rows, a copy of the `View`, a version) built on the game
  thread at the end of the tick while a page is watching; `Watch` is
  called every frame a page draws with the selected piece, so the tick
  builds full rows for that piece and light rows for the rest, and a
  second without a call stops the building. Every change goes through a
  posted task: recipe edits, `Isolate`, `RetireAll`, `ReapplyAll`, the
  paint session, and the view's freeze, scrub, speed and mutes through
  `UpdateView`. The lab's `Preview` records a request under a lock and
  returns the last finished thumbnail without touching the D3D context
  (NOTES 53); the game thread renders requested previews once per tick.
  The store's read API (`LoadedRecipes`, `LoadedPresets`) is still read
  from the pages for the Recipes page's table and the preset lists; the
  list is only moved inside posted tasks with every actor retired first.
- Any thread: the event sinks, which only queue.
- Ownership: the store owns recipes; `AppliedRecipe` holds a `const
  Recipe*` into the store's published vector, which is why every move
  of that vector (a load, a new recipe, the paint recipe added or
  dropped) retires every actor synchronously first (`RetireEveryActor`).
  The lab owns render targets; `Target`s are pooled through `shared_ptr`
  with a recycling deleter, so whoever holds the pointer holds the
  texture. Bindings hold `NiPointer`s to the geometry, property and
  clone. `GeometryInputs` caches (masks, ripples, the derived normal-slope map) are `shared_ptr` maps
  per bound geometry, dropped with it. The compositor's `MeshCache`
  holds a geometry's mesh, facts and bakes across applies, keyed by the
  geometry it keeps alive, until the geometry has been unbound for 30 s
  or the game is cleared; `Get`, `Sweep` and `Cached` all run on the game
  thread (`Cached` from the snapshot build). `InspectSource` and
  `InspectMask` are const and read only the caches; they too run inside
  the snapshot build.

## Invariants

- Malformed input never reaches undefined behaviour: parsers bound their
  depth and size, every engine pointer is null-checked at use, every form
  is looked up, every index is range-checked. An unusable row is inert
  and reported.
- The recipe format is what the file says: `ParseRecipe` then
  `SerializeRecipe` reproduces the file (tests), and defaults are omitted
  on write.
- Names are identifiers; `@` never appears in one, so internal cache keys
  may use it (`@position@<size>`).
- A stack without a base map starts transparent black, except a height
  stack over a flat displacement map, which starts from the neutral 0.5
  (`Compositor::NeutralHeight`); alpha means fuzz
  weight, coat strength or subsurface thickness.
- A hidden output writes its slot's original map and zero scalars, and
  turns its feature flag off; a shown one turns it on.
- Feature exclusions (fuzz vs glint, coat vs subsurface, hair takes none)
  are decided by `SlotWriter::Problem` at apply and reported on the row.
- The shell is a clone named `<geometry><ShellSuffix()>`; the apply
  traversal skips those names.
- A key belongs to the last file loaded with it; `priority` orders the
  merge; `replace` drops lower recipes' outputs on a slot.
- Only `Identity.h` spells the plugin name.

## Extension points

Adding a source kind: a record in `Recipe.h` and the `SourceKind`
variant; parse and serialise in `RecipeJson.cpp` (and the schema); its
type in `SourceType`; its animation in `Recipe.cpp`'s `AnimationQuery`;
`DescribeSource`; then in `Compositor::PrepareSource` a branch that yields
a `PreparedSource` (a texture plus sampling, or a rendered object with a
target), and if it renders per tick, a render step in `Compositor::Render`
and in `RenderMask` for masks that read it. Bake-like kinds go through
`BakeInto` with a builder in `Mesh.cpp`, which is testable natively.

Adding a slot: `Slot` and `kSlotCount` in `Recipe.h`, its row in
`Vocabulary.h`'s `kSlots` (word, channels, note, scalars, exclusions, base
map; a new scalar field is a row of `kScalarFields` with its member and
fallback) and the schema enum; engine side, the texture field in
`TextureFieldOf`, any flag handling in `SlotWriter` (`Problem`, `Write*`,
`Restore`, `Slots`), and the binding call for its scalars in the manager's
`WriteSlot`. The parser, the writer, `Validate`, the board, the snapshot's
scalar rows and the compositor's base map read the rows.

Adding a signal kind: the record, an alternative of `SignalKind`, and a
value of `SignalKindId` (in the variant's order) in `Recipe.h`; its row in
`Vocabulary.h`'s `kSignalKinds` (word, and whether the board's tunable
list shows it); a branch in `SignalKindOf`, the parse branch and the
`SignalToJson` key in `RecipeJson.cpp`; compile rules and evaluation in
`Signals.cpp` (type, dependencies, `Tick`); `AnimationQuery` in
`Recipe.cpp`; a menu edit control if a designer should tune it.

Adding an event provider: a sink or an API bridge that builds an
`EventRecord` (id, payload with position or node when known) and calls
`Manager::QueueEvent(actorID, record)`. Nothing else changes; recipes
name the event id in a trigger.

Adding a blend: `Blend` and `kBlendCount` in `Recipe.h`, its row in
`Vocabulary.h`'s `kBlends` (word, shader mode, normal-stack only), the
schema enum, and the mode's arithmetic in the layer shader's `Blend`
function. Adding a material channel: `MaterialChannel` and its count, a
row of `kMaterialChannels` (word, map and shader channel or `kNone` for
a derived one, type), the schema enum; a derived channel is decided in
the compositor's `PickMaterialChannel`.

Adding a lab pass: an entry point in the HLSL string, compiled in
`CompileShaders` with its own failure path (a pass that fails to compile
costs what reads it, never the layer passes), a constants struct and
buffer, a `Render*` method that captures and restores the context state
through `SavedState`, and a `*Available()` query the compositor checks.
Validate the HLSL offline with dxc before shipping (the plan file
records the command).

Adding to the studio: a new view model is a record and a pure builder in
`Studio.h` with a test in `tests/studio_tests.cpp`; a new edit is a
record in the `RecipeEdit` variant with its `Edit` overload in
`Edits.cpp` and a test that applies it to the canonical recipe; a new
ImGui mechanic is one function in `MenuWidgets`; the page in
`ComposePage.cpp` composes them. A new group of typed inputs is a form:
a builder in `Studio.h` returning `FormField`s (each with its kind, text,
combo names and a `bind` from text to `RecipeEdit`), tested natively,
which the page draws with `DrawForm`. A new snapshot field is added in
`Snapshot.h` and filled in `BuildSnapshot` on the tick. A page outside the studio is a
`__stdcall Render*` function registered in `RegisterMenu` that draws
from the snapshot through `Widgets`.

## Tests and tools

`tests/run-native.sh` builds and runs the engine-free suites with the
host compiler: expression (77), recipe (467; includes a round trip of the
canonical file, every shipped recipe under `recipes/`, the text forms),
signal (64), importer (158; goldens in `tests/fixtures/recipes`), bake
(37), timing, settings; and validates the checked-in recipes against the
schema when `check-jsonschema` is on the path. The same sources build as
host executables through CMake. `tools/efsh_dump.py` writes EFSH fixtures
from a plugin file. There is no engine-side test; the in-game checkpoints
in `README.md` (items 7 onward) are the engine-side verification, and
each records what to look at and which log lines to expect.

## The recipe studio (the rest, planned)

The menu redesign in `plans/worn-enchantment-pbr-menu-brief.md` (Design
section, 2026-09-05). Stages 1 and 2 have landed and their modules are
in the map above: `Snapshot.h`, `View.h`, the slot rules in `Recipe.h`,
`Studio`, `Forms.h`, `Edits`, `MenuState` (with `Intent` and `Reduce`),
`History`, `MenuWidgets`, `ComposePage`, the `LayerFilter` in the
compositor. Every studio module lives in `WornEnchantmentPBR::Studio`,
one level deep; the shared core and the slot rules stay in the top
namespace, which the studio depends on and never the reverse.

Still to come, per the brief's stages 4 to 7:

| Module | Owns | Depends on |
|---|---|---|
| `Studio` additions | the expression tokeniser (absorbs `Literals`) | Snapshot, Edits |
| `Pick.*` | ray against a triangle list, barycentric UV | Mesh |
| `Dds.*` | single-channel DDS bytes | nothing |
| `Stage.*` | the scene around the piece: player heading, game hour, weather, third-person camera distance and pitch, as posted tasks | Manager |
| additions | `View`: live source override, project everywhere, muted material slots, preview as. `Snapshot`: the stage's values, the pick result. `Manager`: `NewRecipe` from a base recipe, `RequestPick(ray)`, `Refresh` honouring preview as. `Compositor`/`Binding`: the branches that read the new view fields. `RuntimeTextures`: texel and histogram readback, neutral maps, the paint target, strokes. | as today |

Modes are a layout table in `Studio` (`LayoutFor`): Compose (board,
stack, inspector, and the signal table under a rule below them; later triggers
and the debug clock), Paint (the region stack; later the pick tools), Design (tunables on large controls, preview as, project
everywhere, muted slots, the stage). Design draws a placeholder
until its stage. Designer hints (ranges, labels, groups, the base
recipe) will live in `meta.studio`, which the format keeps verbatim and
the runtime ignores.

## Known debts

- The readbacks at apply and on request (the flat-displacement measure,
  the material sample, a streamed mesh's GPU copy) hold the renderer
  lock across a `Map` that waits for the GPU, so the render thread
  stalls for that long; making them asynchronous (a copy now, a
  non-blocking map polled on later ticks) removes the stall.
- A freeze on 2026-09-07 with `PlayerOnly=true`: in Paint, a mod stripped
  every piece and re-equipped them; the game froze inside the first
  geometry's apply of the paint recipe on the re-equipped cuirass (the
  recipe line logged, the geometry line never did). The step then new on
  that path was the material sample: two renders and a staging readback
  per geometry at apply, with the menu drawing on the render thread. The
  sample is off the apply path now and logs `material '<map>': sampling`
  before its readback, so a repeat names the step; the synchronous
  readbacks that remain at apply (the flat-displacement measure, a
  streamed mesh's GPU copy) are the same class and the next suspects.
- The crowd freezes the game. With `PlayerOnly=false` in a city, three
  starts on 2026-09-05 froze during the burst of NPC applies, at a
  different actor each time, with nothing in any log; with
  `PlayerOnly=true` the same build and place ran. The suspect is the
  animation graph sink, removed and re-added on every apply and retire
  of every NPC (`WatchAnimationEvents`), which takes the graph manager's
  spin lock while an NPC's graph may be in use or mid-load on the
  behaviour thread. Unproven; a freeze leaves no stack. To do: watch a
  graph only when it is loaded and once per actor, not per apply; cap
  actors, distance and targets; a scope that keeps keyword-, material-
  and armor-keyed debug recipes to the player. The compositor brief's
  crowd measurement is due.
- Slot textures are one target each. Every stack, mask, bake, ripple and
  preview holds a whole render target presented through one of the 512
  presenter textures (the placeholders under `Textures/<plugin>/slots/`), at the stack's
  size, so a crowd multiplies both VRAM and slot use quickly. To do: pack
  several small results into one atlas texture (masks and bakes at 256
  or less share a 1024 sheet with a UV offset and scale the layer pass
  applies), reuse one target for every static stack at a size, and
  release previews the menu no longer asks for. The placeholder
  spike in the compositor brief's future spikes is the other half.
- Texture sizes are absolute (`RuntimeTextureSize` 128 to 1024,
  `GlossMapSize` up to 2048) while modded armor ships 2K and 4K maps.
  To do: one `TextureScale` of full, half or quarter, relative to the
  map a stack edits or to the material's diffuse for a stack that
  starts from black, with a 64 floor and a 4096 ceiling; masks, bakes
  and ripples follow their stack. Recorded 2026-09-05 with the crowd and
  slot-texture items in the compositor brief.

- A muted layer whose source is a ripple stops rendering its ripple while
  hidden, so the front resumes from the next render instead of continuing
  under the mute.
- The snapshot's `SlotRow.problem` is filled only for slots the binding
  lists in `Slots()` (the written ones); an empty cell that the material
  would refuse is found out at add, not before.
- Stacks on the same slot from several recipes do not append into one
  composite, and two recipes binding one geometry's material fight: each
  installs its own private copy, the second copy is made from the first,
  and at the first tick the first recipe's `StillOwned` fails and its
  whole geometry (every material output and its shell) is dropped with
  "dropping '<geometry>': its material or shell was replaced by another
  system". `replace` works. Isolating a recipe sidesteps this by applying
  it alone (`Manager::Isolate`). The fix is one material binding per
  geometry shared by the piece's recipes, which the merge in the
  compositor brief needs anyway.
- `worldUp` is the bind-pose normal; the wearer's pose is ignored.
- The interpreter reads `x` as 0 and `mean` as 0.5 (masks have neither).
- Settings still carry the proof of concept's rows (sheen, gloss, glow
  mask, flipbook); nothing reads them. Phase 5 of the compositor brief.
- The 512 presenter textures (placeholders) under `Textures/<plugin>/slots/` are
  how targets reach the engine; a spike to replace them is deferred.
- The Precision provider (hit positions) is not written; the vanilla hit
  sink carries no position, so hit ripples start at the piece's centre.
- The store's read API (`LoadedRecipes`, `LoadedPresets`) is still read from the pages while a posted task may move the list or reassign the presets on reload; the next step is to carry the recipe table and the preset lists inside the snapshot too.
- The Compose page keeps its recipe row when the selected recipe is
  bound to no geometry of the piece, so the combo away from it is always
  drawn; the board and stack under it are not, since they need a shape.
- Paint: a term's coverage (its share of the shape's texels) is not yet
  measured; the design has the tick measure the rendered scratch once
  per rebuild and the offers carry their coverage, and neither is built.
- Paint: `PaintKeyOf` keys the paint recipe to the armor, else the
  piece's first key, and a piece with no key cannot be painted on (a
  warning in the page); the reference walker stays private to
  `Edits.cpp` (`CountReferences` is the one export), so `Validate` and
  the animation classifier enumerate reference sites themselves.
- `TextureSize` stops at the lab and the private prepare helpers:
  `Compositor::Prepare`, `RenderedStack::size_` and `CreateTarget` still
  take raw sizes and re-clamp with literals, and the key functions in
  `Mesh.h` take `std::uint32_t` because the type lives in the engine
  header.
- `RenderMask` builds three vectors per animated mask per geometry per
  tick though `ProgramPass` bounds them at compile time.
- The installer used to overwrite the INI on every install, resetting
  `PlayerOnly`; it keeps an existing INI now. Settings edited in the menu
  are saved to that file.
