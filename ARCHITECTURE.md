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
| `Core.h` | `Vec2`, `Vec3`, `Value` (variant of the three), `ValueType`, `Ref`, `AsScalar`/`AsVec3`, `Match` (variant visit), `Get<T>`/`Is<T>` | nothing |
| `Expression.*` | the recipe language: `Program` (bounded postfix op list), `Parse`, `Check` (types), `Evaluate`, `ParseCurve`, `ApplyCurve`; limits `kMaxExpressionLength/Depth/Ops` | Core |
| `Recipe.h`, `Recipe.cpp`, `RecipeJson.cpp` | format 1 records (`Recipe` and everything in it), `ParseRecipe`/`SerializeRecipe`, `Validate`, `Resolve`, variants, `IsAnimated`, glob and selector matching, biped slot names, text forms for the menu, the slot rules (`SlotsOf`, `ScalarsOf`, `SlotsExclude`, `BlendAllowed`, `ChannelsOf`, `ScalarOf`) that `Validate` and the studio's board both read | Core, Expression, Signals (for graph typing in `Validate`) |
| `Signals.*` | `SignalGraph::Compile` (nodes, order, types, inert), `SignalState` (per applied recipe: `Tick`, `Fire`, `ValueOf`, `Resolve`, `Firings`), `SignalEnvironment` (what a tick reads from the actor), `EventRecord`/`TriggerPayload` | Core, Expression, Recipe |
| `Importer.*` | `EffectShaderRecord` (an EFSH as data), `ImportEffectShader` (record to recipe), `RecipeIdFor` | Recipe, Timing |
| `Timing.*` | the vanilla EFSH animation maths the importer encodes | nothing |
| `Mesh.*` | `MeshData` (bind-pose vertices per partition), `DecodeVertex` (the engine's packed vertex bytes), `BuildBake`/`BuildDistanceBake`/`BuildUvBake` (mesh to UV-space triangles carrying a value) | Core, Recipe |
| `SettingsCore.*` | the `Settings` record and its table | nothing |
| `Snapshot.h` | the menu's read model (`Studio::Snapshot`): rows per piece, recipe, geometry, output, layer, source, mask and slot, typed; textures as opaque handles the lab keeps alive | Core, Recipe |
| `View.h` | `Studio::View`, how the piece is looked at: freeze, scrub, isolate recipe, output and layer, mute set, and the shown/muted predicates the tick and the stack share | nothing |
| `Studio.*` | `Studio::Mode` and `Layout` per mode (one narrow column of collapsible sections, the stack's split ratio); `Selection` and its resolution against a snapshot; the view models `Board`, `StackView`, `Inspector`, `SignalList` as plain records built by pure functions; forms as data: `FieldKind`, `FieldDetail`, `FieldSpec` (name, kind, text, combo names, detail, and a `bind` that turns committed text into a `RecipeEdit` or refuses), built by `InspectorForm` and `ScalarForm` | Snapshot, View, Recipe, Edits |
| `Edits.*` | `Studio::RecipeEdit`, every change the menu makes, and `Apply(Recipe&, RecipeEdit)`, which refuses with a diagnostic and leaves the recipe untouched when the edit does not fit; `Describe`, `DefaultLayer`, `DefaultOutput` | Recipe |

Engine side (compile with CommonLibSSE; thin over the above):

| Module | Owns | Depends on |
|---|---|---|
| `Identity.h` | the plugin name (from CMake), every path and node name derived from it | nothing |
| `EngineForms.*` | `FormKeyFor`, `EditorIdOf` (po3 Tweaks export, else engine), `LookupForm`, `RecordFrom(TESEffectShader)`, `ShaderFor` (the EFSH an enchantment shows) | Recipe, Importer |
| `RecipeStore.*` | the loaded recipes: `LoadRecipes` (files, editor-ID resolution, import of missing EFSH recipes), `LoadedRecipes`, `OriginOf`, `GraphFor` (cached compiled graph), editing: `MutableRecipe`, `Revalidate`, `IsDirty`, `SaveRecipe`, `RevertRecipe` | Recipe, Signals, Importer, EngineForms, Identity |
| `Environment.*` | `ActorEnvironment`: a `SignalEnvironment` over an actor handle and its enchantment (actor values by measure, states, enchantment fields, EFSH parameters) | Signals, EngineForms |
| `MeshReader.*` | `ReadMesh(BSGeometry*)` (partitions, bones, slots; CPU copy or GPU readback), `NodeBindPosition`, `ToRootSpace` | Mesh, RuntimeTextures |
| `RuntimeTextures.*` | `TextureLab`: the D3D passes and their resources: render targets presented through shell textures (`Target`, `Acquire`, `Scratch`), the layer pass (`Render` with `Mode::kLayer`), the interpreter (`RenderProgram`), bakes (`BakeMesh`), ripples (`RenderRipple`), curve lookups (`CreateLookup`), readback (`ReadBuffer`, `ExtentOf`, `MeanLuminance`, `MeanChannel`), previews for the menu | Expression, Mesh, PBRMaterial |
| `Compositor.*` | recipes to textures: `Prepare` (an output's stack on a geometry: sources, masks, curves, bakes, distance, ripples, base map, size), `Render` (per tick, with a `LayerFilter` of hidden layers; a static stack renders again when the filter changes), `RenderedStack`/`RenderedMask`/`RenderedRipple`, `GeometryInputs` (a geometry's material maps plus its caches), `InspectSource`/`InspectMask` for the menu | Recipe, Signals, RuntimeTextures, Mesh, MeshReader, RecipeStore (graph for mask typing) |
| `Binding.*` | the only writer of engine state: `SlotTarget` (interface), `SlotWriter` (slots of one PBR material with save and restore), `MaterialBinding` (a geometry's own material, made private), `ShellBinding` (a clone with a PBR copy or vanilla material, pose), `LightBinding` (point lights with the CS ISL overlay), `PlaceLights` | Recipe, PBRMaterial, Identity |
| `Manager.*` | the object the sinks and the hook call: queues, apply and retire per actor, the tick (which reads the `View` for isolate, solo and mute and hands the compositor a `LayerFilter`), events to triggers, recipe editing on the game thread, `TakeSnapshot` for the menu | everything above |
| `Events.*`, `Hooks.*` | engine event sinks (equip, load, node update, hits, animation graph) and the per-frame hook, each a few lines that call the manager | Manager |
| `MenuState.h` | `Studio::MenuState`, the page state: mode and its `Layout`, selection, text buffers, active and focused field, keyed by `FieldKey` (the ImGuiID of a widget's literal key in the ID scope the page pushes per recipe, output, layer and row); one instance, render thread only | Studio |
| `MenuWidgets.*` | `Studio::Widgets`, every ImGui mechanic in one place: `Width` (fill, fit a text, pixels) and `NextItemWidth`; `Table` (id, `{label, Width}` columns, a `TableStyle`; `Cell` advances, `End` closes); `Section`, `Split` (two resizable columns over a ratio), `Rule`; `Toggle` (a checkbox with a tooltip; solo, mute, isolate and freeze are all it) and `SoloMute`; fields at the layout's scale under literal keys, thumbnails (the one place a texture handle is dereferenced, through `TextureLab::Preview`), blend and reference combos, `ValueField` (a @signal combo and a literal text field as one control), the badges per `FieldKind`, the mode bar, drag handle and drop target, text helpers; widgets return values and never edit | Snapshot, Studio, RuntimeTextures |
| `ComposePage.*` | the studio page: snapshot once, selection resolved, a target-and-slot picker over the stack and the inspector, and the signal table, drawn from `Studio` records under the mode's layout; `DrawForm` draws any `FieldSpec` list as the field table and posts each field's bound edit; `DrawBoardPage` draws the board for the Recipes page; widget results become a `RecipeEdit` posted through `Manager::EditRecipe` or a `View` change | Studio, Edits, MenuWidgets, MenuState, Manager |
| `Menu.*` | registration, the shared header, and the Recipes, Setup and Log pages | ComposePage, Manager, RecipeStore, Settings |
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

Menu (render thread): `TakeSnapshot` copies everything the pages show;
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
  restored around each pass), the store's mutation.
- Render thread (the menu framework): the page functions. They may call
  `TakeSnapshot`, `GetStatus`, `Debug()`, the store's read API, and the
  lab's `Preview`, which records a request under a lock and returns the
  last finished thumbnail without touching the D3D context (NOTES 53);
  the game thread renders requested previews once per tick
  (`RenderPreviews`). They mutate only through the manager's posted
  tasks and the `View`.
  `TakeSnapshot` reads the applied state without a lock; the runtime
  never frees an `AppliedRecipe` outside a posted task, and the snapshot
  copies strings and values, so a torn read shows a stale row at worst.
- Any thread: the event sinks, which only queue.
- Ownership: the store owns recipes; `AppliedRecipe` holds a `const
  Recipe*` into the store's published vector, which is why edits retire
  the wearers first and the vector is never resized between loads. The
  lab owns render targets; `Target`s are pooled through `shared_ptr` with
  a recycling deleter, so whoever holds the pointer holds the texture.
  Bindings hold `NiPointer`s to the geometry, property and clone.
  `GeometryInputs` caches (masks, bakes, ripples, mesh) are `shared_ptr`
  maps per bound geometry, dropped with it.

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

Adding a slot: `Slot` and `kSlotCount` in `Recipe.h`, `kSlots` in
`RecipeJson.cpp`, the schema enum; the texture field in
`TextureFieldOf` and any flag and scalar handling in `SlotWriter`
(`Problem`, `Write*`, `Restore`, `Slots`); the scalar routing in the
manager's `WriteSlot`; the scalar rows in `TakeSnapshot` and the menu's
scalar edit switch; `BaseMapFor` in the compositor if the slot edits an
existing map.

Adding a signal kind: the record and `SignalKind` in `Recipe.h`, parse
and serialise, `KindName`; compile rules and evaluation in `Signals.cpp`
(type, dependencies, `Tick`); `AnimationQuery` in `Recipe.cpp`; a menu edit
control if a designer should tune it.

Adding an event provider: a sink or an API bridge that builds an
`EventRecord` (id, payload with position or node when known) and calls
`Manager::QueueEvent(actorID, record)`. Nothing else changes; recipes
name the event id in a trigger.

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
a builder in `Studio.h` returning `FieldSpec`s (each with its kind, text,
combo names and a `bind` from text to `RecipeEdit`), tested natively,
which the page draws with `DrawForm`. A new snapshot field is added in
`Snapshot.h` and filled in `TakeSnapshot`. A page outside the studio is a
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
section, 2026-09-05). Stage 1 has landed and its modules are in the map
above: `Snapshot.h`, `View.h`, the slot rules in `Recipe.h`, `Studio`,
`Edits`, `MenuState`, `MenuWidgets`, `ComposePage`, the `LayerFilter` in
the compositor. Every studio module lives in `WornEnchantmentPBR::Studio`,
one level deep; the shared core and the slot rules stay in the top
namespace, which the studio depends on and never the reverse.

Still to come, per the brief's stages 2 to 7:

| Module | Owns | Depends on |
|---|---|---|
| `Studio` additions | `Intent` (every page action, recipe edits included) and `Reduce`, applied after render so no derived record outlives the state it came from; `EditHistory` (whole-recipe past and future, capped) for undo and redo; `LightPanel`, `ShellPanel`; the expression tokeniser (absorbs `Literals`) | Snapshot, Edits |
| `EditCheck.*` | text checked against the recipe's names and types before apply | Expression, Recipe, Snapshot |
| `Regions.*` | the preset file, the bone and partition name table, resolvability on a geometry, the edits that materialise a preset | Edits, Snapshot |
| `Pick.*` | ray against a triangle list, barycentric UV | Mesh |
| `Dds.*` | single-channel DDS bytes | nothing |
| `Stage.*` | the scene around the piece: player heading, game hour, weather, third-person camera distance and pitch, as posted tasks | Manager |
| additions | `View`: clock speed, show mask, live source override, project everywhere, muted material slots, preview as. `Manager`: one `EditHistory` per recipe with `UndoRecipe`/`RedoRecipe`; the event queue already takes the Signals tab's fired triggers. `Snapshot`: each geometry's partitions and skinned bones with coverage, the scratch row, the stage's values, the pick result. `Manager`: `NewRecipe(id, key, base)`, `RequestPick(ray)`, `Refresh` honouring preview as. `RecipeStore`: drop the scratch row on save. `Compositor`/`Binding`: the branches that read the new view fields. `RuntimeTextures`: texel and histogram readback, neutral maps, the paint target, strokes. | as today |

Modes are a layout table in `Studio` (`LayoutFor`): Compose (board,
stack, inspector), Signals (the signal table; later triggers and the
debug clock), Paint (region editor and pick tools), Design (tunables on large controls, preview as, project
everywhere, muted slots, the stage). Paint and Design draw a placeholder
until their stages. Designer hints (ranges, labels, groups, the base
recipe) will live in `meta.studio`, which the format keeps verbatim and
the runtime ignores.

## Known debts

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
  placeholder slot textures (`Textures/<plugin>/slots/`), at the stack's
  size, so a crowd multiplies both VRAM and slot use quickly. To do: pack
  several small results into one atlas texture (masks and bakes at 256
  or less share a 1024 sheet with a UV offset and scale the layer pass
  applies), reuse one target for every static stack at a size, and
  release previews the menu no longer asks for. The placeholder slot
  spike in the compositor brief's future spikes is the other half.
- Texture sizes are absolute (`RuntimeTextureSize` 128 to 1024,
  `GlossMapSize` up to 2048) while modded armor ships 2K and 4K maps.
  To do: one `TextureScale` of full, half or quarter, relative to the
  map a stack edits or to the material's diffuse for a stack that
  starts from black, with a 64 floor and a 4096 ceiling; masks, bakes
  and ripples follow their stack. Recorded 2026-09-05 with the crowd and
  slot-texture items in the compositor brief.
- `Compositor.cpp`'s `BlendIndex` has no case for `Blend::kNormal`, so a
  `normal` blend renders as `replace`; the slot rules allow it on the
  normal stack, so the board offers a blend the layer pass does not yet
  implement.
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
- The 512 placeholder slot textures under `Textures/<plugin>/slots/` are
  how targets reach the engine; a spike to replace them is deferred.
- The Precision provider (hit positions) is not written; the vanilla hit
  sink carries no position, so hit ripples start at the piece's centre.
- `TakeSnapshot` reads live state from the render thread without a lock
  (see Threads).
- The installer used to overwrite the INI on every install, resetting
  `PlayerOnly`; it keeps an existing INI now. Settings edited in the menu
  are saved to that file.
