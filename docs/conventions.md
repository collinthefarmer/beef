# BetterEnchantmentEffects — code conventions

How the code is written. The pure core (`recipe/`, `mesh/`) established these
patterns; every later module (`studio/`, `planners/`, `engine/`, `render/`,
`menu/`) reuses the same helpers.

- Every rule cites a real helper by file and name. Grep for the helper before
  you write a variant.
- `CLAUDE.md` holds the three rules: memory safety, readability for a non-C++
  developer, lean performance.
- `REQUIREMENTS.md` holds Architecture, Safety, and Constraints.
- This document states the patterns, grounded in the current source.

## Diagnostics

A **diagnostic** is a record of one failure: `Diagnostic{severity, where,
message}` (`recipe/Recipe.h`). The `where` string names the **row** that
failed, for example `signal glowLevel` or `output 2 layer 0`. The menu uses
`where` to show the failure on its row.

The contract, for every module:

- A failure that a person may need to see becomes a `Diagnostic`.
- A function that can fail once returns `std::optional<Diagnostic>` (nullopt
  means success) or `std::expected<T, Diagnostic>`.
- A function that can fail many times collects into
  `std::vector<Diagnostic>` through a `Reporter`.
- No function returns `bool` or an empty string to mean failure.
- A **snapshot** row carries a `std::string problem`. `ProblemText`
  (`Recipe.h`) makes that string from a `Diagnostic::message`. A snapshot row
  displays a failure; it does not originate one.

### The `Reporter`

A `Reporter` (`recipe/Recipe.h`) collects diagnostics for one scope. It holds
the output vector and the scope's `where`. `Error` and `Warn` append one
diagnostic. `At(word)` returns a child `Reporter` with a narrower `where`.
Example: `ctx.At(std::format("output {}", i))` in `ReadOutputs`
(`RecipeRead.cpp`).

Rules:

- Construct one `Reporter` per scope and call `Error`/`Warn`. Do not
  construct a `Diagnostic{…}` and `push_back` it. Example: `CheckSource` and
  `CheckLayer` (`Signals.cpp`) write `const Reporter report{out, where};`
  then `report.Error(…)`.
- To return one diagnostic, construct it with
  `MakeDiagnostic(severity, where, message)` (`recipe/Recipe.h`).
  `Reporter::Error` and `Warn` use the same factory. Do not fill a vector to
  return its first element.
- `RecipeGraph` reports through `Reject` and `ReportSignal` (`RecipeGraph.cpp`),
  which wrap a `Reporter` at the affected row. Source input validation returns
  diagnostics that the compiler merges without duplicating existing entries.

### The `where` words

- The helpers beside `Reporter` in `Recipe.h` spell each row word once:
  `SignalWhere`, `CurveWhere`, `SourceWhere`, `MaskWhere`, `OutputWhere`,
  `LayerWhere`, `VariantWhere`, `KeyWhere`. The parser, the validator, the
  editor, and the store all build `where` through them.
- `RowLevel` separates a row diagnostic from a recipe-level one. The store
  applies a **recipe** with row errors and holds back a recipe with
  recipe-level errors.
- Each `RecipeStore` operation (`SaveRecipe`, `RevertRecipe`, `NewRecipe`,
  `RenameRecipe`, `AddTransientRecipe`, `DropTransientRecipe`) returns a
  `Diagnostic` whose `where` is `recipe <id>`. `RecipeEditor` copies the
  `message` into the `FileOperationResult` or `RecipeEditResult` that reaches
  the menu.
- `SlotTarget::Problem` returns a `Diagnostic` whose `where` is the slot's
  name.

### Logging

- `Trace::Emit` records structured events for the diagnostic **trace**.
  `logger::` writes the human log.
- Log a failure once, at the boundary that turns it into a `Diagnostic`.
  Do not log it again downstream.
- A file that needs both uses `Trace` for state and `logger` for the human
  sentence.

`diagnostics/Trace.h` is the trace recorder:

- `Trace::Emit(Event, fields)` appends one structured line per event. The
  events: `kStartup`, `kSettings`, `kRecipe`, `kCommand`, `kPage`, `kQueue`,
  `kLoad`, `kApplication`, `kRetire`, `kBinding`, `kRestore`, `kTexture`,
  `kShell`, `kPreview`, `kCaptureFailure`.
- The trace file rotates in 32 MiB segments; the recorder keeps the last two.
- `Trace::Scope` tags the events of one command with a command id.
- `Trace::Safely` wraps a capture. A capture that throws becomes a
  `kCaptureFailure` event, not a crash.
- `tools/trace-report.py` reads the file.

One stated exception: `SettingsFile.cpp` logs a bad INI line and keeps the
default. Settings are not recipe data, no menu row shows them, and a wrong
value must never stop the plugin from starting. That file has no diagnostics
and no `Reporter`.

## The JSON boundary

`recipe/Binders.h` publishes the one JSON boundary. A module includes
`Binders.h` to read or write a JSON document; no other module names
`nlohmann`. `RecipeRead.cpp`, `RecipeWrite.cpp`, and `studio/Presets.cpp`
build on it. Both vocabularies work over `nlohmann::ordered_json`, so a
round-trip preserves key order.

Entry points:

- `ParseObjectDocument(text, Reporter)` parses a whole file. It refuses text
  nested past `kMaxRecipeDepth` before parsing, detects duplicate keys while
  parsing, and reports "not a JSON object" for any other document shape.
- `ParseSourceKind(Reader &)` and `SourceKindToJson` publish the
  **source**-kind pair. A second format (presets) reads and writes sources
  through the recipe's own table instead of an if-chain of its own.

### `Reader`, the parse binder

A `Reader` tracks which keys it consumed. `Finish()` reports every unconsumed
key as `unknown key`, so the `Read`/`Child`/accessor calls against a `Reader`
fully specify its object.

| Group | Members | Behaviour |
|---|---|---|
| Typed getters | `Number`, `Integer`, `Boolean`, `String`, `Reference`, `Parameter`, `Vector2`, `Vector3`, `Point`, `Literal` | Return `std::optional`; absent key returns nullopt. |
| Mandatory | `Required(key)` | A mandatory string; reports when absent. |
| Assign-if-present | `Read(key, out)` overloads (`Param`, `Ref`, `Vec2Param`, `Vec3Param`, `bool`, enum-table) | Assign into a field only when the key is present. |
| Enums | `Enum(key, table)`, `Read(key, table, out)` | Resolve a word through `FromName`; report `Choices(table)` on a miss. |
| Bounded | `IntRange(key, lo, hi, out)` | Read an integer inside `[lo, hi]`. |

Free helpers over `Reader`:

| Helper | Purpose |
|---|---|
| `ReadObject(v, word, ctx, fill)` | Assert `v` is an object, run `fill` over an inner `Reader`, `Finish()`. The body of most per-kind parsers. |
| `ReadRows(array, word, ctx, out, parse)` | Parse an array into a vector; `RowCapReached` caps it at `kMaxRecipeRows`. |
| `NamedRows(root, section, rowWord, out, parse)` | The same over a name-keyed object (`signals`, `curves`, `sources`, `masks`). |
| `OneKey(j, ctx, what, common)` | A variant object with exactly one kind key plus optional `common` keys. Returns the kind key and value, or reports "no kind key" / "two kind keys". |
| `EnumShorthand(v, table, what, ctx)` | A bare string that resolves to an enum. Example: `"material": "roughness"`. |

### `Writer`, the write counterpart

| Group | Members | Behaviour |
|---|---|---|
| Plain | `Set`, `Write` (Param/Vec3Param), `WriteText`, `WriteRef`, `WriteEnum` | Write the field. |
| Conditional | `WriteTextIf`, `WriteRefIf`, `WriteEnumIf`, `WriteIf` (value-vs-default), `WriteNumberIf`, `WritePointIf` | Omit a field equal to its default. |

The `*If` forms enforce "defaults are omitted on write" (`REQUIREMENTS.md`)
field by field.

### Symmetry rules

- `ParseRecipe` then `SerializeRecipe` reproduces the file byte for byte.
  The round-trip test fails when a field has a reader without its writer, or
  a writer without its reader. Add both together.
- One field has one reader and one writer. `ReadMetadata`, `ReadKeys`,
  `ReadOutputs`, `ReadVariants` (`RecipeRead.cpp`) read the sections;
  `SerializeRecipe`'s `named` lambda writes the name-keyed sections.

## Variants and closed sets

### One spec table per enum

- Every enum with enumerators carries exactly one `constexpr` spec table in
  `Words.h`, one row per value in enum order, with
  `static_assert(Complete(table, kFooCount))`. Examples:
  `kSignalKinds`/`kSignalKindCount`, `kSlots`/`kSlotCount`, `kScalarFields`,
  `kMaterialChannels`.
- `Complete` (`Core.h`) checks that the table has `kFooCount` rows and that
  every value below the count appears in exactly one row. A table that
  passes makes `NameOf`'s `"?"` branch unreachable.
- Every closed-set enum in `Recipe.h` has a `kFooCount` beside it for that
  assert.
- An `enum class` with no enumerators is not a closed set. It is a typed
  value or index space (`GeometryId`, `SlotContributor`). It carries no
  `Complete` count; the boundary that builds one keeps it in range.

### Variant closed sets

A variant's table takes one of two forms:

- a `Named<Id>` table over an id enum whose enumerators follow alternative
  order — `kSourceKindWords` over `SourceKindId`, with `SourceKindIdOf`
  reading the id from `variant::index()`;
- a word array asserted against `std::variant_size_v` — `kBakeKindWords`,
  `kTriggerOriginWords`.

Both carry a run of `std::is_same_v<std::variant_alternative_t<I, V>, …>`
asserts that pin table order to alternative order. The pin matters:
`DefaultSourceKind` builds an alternative from its id, and `ParseSourceKind`
indexes `kSourceParsers`. `kSourceParsers` is built from one
`ParseSourceAlternative<T>` specialisation per alternative, so an alternative
without a parser fails at link time instead of dispatching to a wrong entry
at runtime.

### Table operators (`Core.h`)

| Operator | Purpose |
|---|---|
| `NameOf(table, value)` | Value to word. |
| `FromName(table, word)` | Word to value. |
| `RowOf(table, value)` | Value to its spec row. |
| `Choices(table)` | The word list for an error message. |
| `WordsOf(table)` | The word list for a combo. |
| `AlternativeAt<Variant>(index)` | Default-construct an alternative by index. |

A spec accessor is `RowOf(table, v)` followed by a field read. Examples:
`DefaultPriority`, `BlendShaderMode`, `MaterialMapOf` (`Vocabulary.cpp`).
Do not write a hand-maintained switch for a spec lookup.

### Dispatch with `Match`

- `Match` (`Core.h`) dispatches over a variant by index, through
  `std::get_if`. It does not use `std::visit`.
- `Match` does not throw. The `kAlternativesNothrowMovable` `static_assert`
  proves the variant can never be valueless, so `Match` inside a `noexcept`
  path cannot reach `std::terminate`.
- `Get<T>` and `Is<T>` are the get-if and holds-alternative shorthands.
- Write one lambda per alternative in a `Match` over a recipe variant
  (`SignalKind`, `SourceKind`, `BakeKind`, `Output`, `Bones`, `VariantKey`).
  Leave an arm empty-bodied where nothing applies. Examples: `CheckSource`
  (`Signals.cpp`), `VisitSourceParams` (`recipe/Visit.h`).
- Do not write a `[](const auto &)` arm. A catch-all arm absorbs a new
  alternative silently. Without one, overload resolution fails at every site
  that must handle the new kind.

### Adding a source kind

The compiler reports these sites when `SourceKind` gains an alternative
(recorded by adding a fake eighth alternative, critique Plan B, 2026-09-14):

| Site | What fails |
|---|---|
| `Recipe.h` | `kSourceKindCount` against `variant_size_v`; `SourceKindId` needs its enumerator. |
| `Words.h` | `Complete(kSourceKindWords)` and the alternative-order asserts. |
| `RecipeRead.cpp` | Undefined `ParseSourceAlternative<T>` at link time. |
| `Vocabulary.cpp` | `SourceType` and `AnimationQuery::Source`. |
| `Signals.cpp` | `CheckSource`. |
| `RecipeWrite.cpp` | `DescribeSource` and `SourceKindToJson`. |
| `recipe/Visit.h` | `VisitSourceParams`. |
| `studio/SourceRows.cpp` | `SourceRowOf` and `SourceKindOf`. |
| `studio/Forms.cpp` | `SourceForm`. |
| `render/CompositorSource.cpp` | `SourcePreparer` and `SourceInspector`. |
| `tests/recipe/schema_tests.cpp` | Fails until `schema/recipe.schema.json` lists the new word. |

`SourceRow` holds a `SourceRowKind` variant with one per-kind row struct per
`SourceKind` alternative, pinned to `SourceKindId` order by the same
alternative-order asserts (Plan B3, 2026-09-14). `SourceKindOf`
(`studio/SourceRows.cpp`) and `SourceForm` (`studio/Forms.cpp`) dispatch over
it with `Match` and no catch-all arm, so a new alternative is a compile error
at both sites instead of a silently ignored row.

### Big arm against tiny arm

- When each per-kind arm has a substantial body, extract per-kind functions
  and dispatch through an enum-ordered function-pointer table with a size
  assert. Examples: `kSignalParsers` and `kSourceParsers`
  (`RecipeRead.cpp`), each asserted against its kind count; `SignalFrom` and
  `SourceFrom` index the tables. The serializers route through per-kind
  `*ToJson` helpers behind one `Match` (`SignalToJson` in `RecipeWrite.cpp`,
  `SourceKindToJson` in `Binders.cpp`).
- When each arm is one line, keep one exhaustive `Match`. The compiler then
  demands an arm when a kind is added.
- `SignalState::Evaluate` and the per-texel expression `Evaluate` switches
  stay flat by decision. A function-pointer table was considered and
  rejected: the flat switch compiles to an inlined jump table, and the
  indirection would cost more than it removes.
- Make illegal states unrepresentable at load and edit time. On
  per-tick/per-texel paths, keep each check cheap, explicit, and tested.

## Multi-phase algorithms

### Named phases

Write a multi-step algorithm as a sequence of named phases, not as one long
function. `RecipeGraph::Compile` (`RecipeGraph.cpp`) uses a builder to register
rows and applied functions, parse programs, bind dependencies, order nodes,
and analyze their properties. The dependency traversal uses an explicit stack
and enforces the recipe depth bound without recursive C++ calls.

### Bounds

Bound every recursion. Cap every list.

| Bound | Declared in | What it caps |
|---|---|---|
| `kMaxRecipeDepth` | `Recipe.h` | Parsing depth and compiled dependency-path depth. `RecipeGraphBuilder::Order` uses an explicit stack and marks the offending row inert. |
| `kMaxRecipeRows` | `Recipe.h` | Every **row** list. `RowCapReached` checks it inside `ReadRows`/`NamedRows` and in the ad-hoc loops (`selector`, `stops`, `boneWeight`, `overrides`). |
| `4 * kMaxRecipeRows` | `RecipeGraph.cpp` | Total compiled named rows and anonymous inline functions; checked before graph allocation. |
| `MaxNestingDepth` | `Binders.cpp` | JSON nesting, rejected before parsing. The `DuplicateFinder` parse callback (`Binders.cpp`) reports duplicate keys. |
| `kMaxExpressionLength`, `kMaxExpressionDepth`, `kMaxExpressionOps` | `Expression.h` | Expression text, parse depth, and op count. |
| `kMaxMaterialClusters`, `kMaxClusterIterations`, `kMaxChannelWeight` | `Recipe.h` | Material-cluster requests and the clustering itself. |
| `kMaxIslands` | `Islands.h` | Island segmentation. |

### Parse, don't validate

Turn untrusted input into a typed record once, at the boundary. The atomic
parsers: `FormKey::Parse`, `FormRef::From`, `ChannelSet::Parse`,
`ParseParam`/`ParseVec3Param` (`Vocabulary.cpp`). Past them, the code trusts
the type.

## Structure

- A module's header lands first and is frozen: data types, then complete
  function signatures (`docs/history/buildup-plan.md`). Fill work then owns
  disjoint `.cpp` files.
- A header states its data types first, then the free functions over them.
  Examples: `Recipe.h`, `Signals.h`, `Mesh.h`, `Merge.h`.
- Every declared function has one assigned `.cpp` home. There is no
  catch-all translation unit.

`recipe/` splits by concern:

| File | Owns |
|---|---|
| `Recipe.cpp` | Record accessors (`Recipe::Find*`), the `where` builders, the diagnostic predicates (`RowLevel`, `HasErrors`, `HasRecipeErrors`, `ProblemText`). |
| `Variants.cpp` | `VariantApplies` and `ApplyVariant`. |
| `Vocabulary.cpp` | The static vocabulary and text forms over the `Words.h` tables (every `…Name`/`Parse…`/spec accessor), and `IsAnimated` (the `AnimationQuery` walker). |
| `Resolve.cpp` | **Piece** matching and resolution: `Resolve`, `KeyMatches`, `Matches`, `GlobMatch`, `KeyChoicesOf`. |
| `Binders.cpp` | The `Reader`/`Writer` binders `Binders.h` publishes. |
| `RecipeRead.cpp` / `RecipeWrite.cpp` | The JSON boundary, verified by round-trip against the canonical files. |
| `Signals.cpp` | Graph compile, per-tick evaluation, validation. |

### Functional core, thin adapter

- The pure decision compiles natively and is tested. The engine adapter
  calls existing APIs and null-checks pointers each frame
  (`REQUIREMENTS.md`).
- `SignalEnvironment` (`Signals.h`) is the seam: an abstract interface the
  pure `SignalState::Tick` reads through. Tests use `NullEnvironment`; the
  engine supplies the live implementation.
- The `Plan*` functions in `Merge.h` are pure **planners** over records. The
  engine adapter executes their **plans**.
- A planner decides over value records and opaque handles. It never takes an
  engine (`RE::`) pointer. Example: `Merge`'s `Plan*` take
  `PlacedRecipe`/`SlotContributor`; an actor planner takes an `ActorPlan`
  over opaque ids.
- When a decision needs a live pointer, the split is wrong: move that logic
  into the adapter. This keeps the pure core testable without the engine and
  the adapter thin.
- Give distinct index spaces distinct types. Example: `Merge.h` declares
  `enum class SlotContributor` and `LightContributor` with
  `SlotContribution`/`LightContribution`, so a **light** index cannot be
  used as a **slot** index.

## Reuse across modules

- A downstream module calls `recipe/`'s published surface. It does not write
  its own copy. The surface: `ParseRecipe`/`SerializeRecipe`, `Validate`,
  `Resolve`, `Recipe::Find*`, `Reporter`, and `Reader`/`Writer` behind the
  JSON boundary.
- Validation composes once. `Validate` (`Signals.cpp`) builds the
  `RecipeGraph`, wraps it and the recipe in a `RowTypes`, and runs the
  public `Check*` functions (`CheckCurve`, `CheckSource`, `CheckMask`,
  `CheckLayer`, `CheckOutput`) over `RowTypes`.
- A consumer that validates an **edit** calls those `Check*` functions over
  `RowTypes`. It does not re-derive "does this name a known row of the right
  type".
- The frozen tree wrote one row check three times (`Validator`,
  `CheckSourceKind`, `EditCheck`). The copies diverged, and the editor
  accepted values the loader rejected. One check over `RowTypes` prevents
  that divergence.
- Name lookup calls `Recipe::FindSignal`/`FindCurve`/`FindSource`/`FindMask`
  (`Recipe.cpp`). Do not re-iterate `recipe.signals`.

## Runtime identities and editor commits

### Source planning

`SourcePlanBuilder` (`studio/SourcePlan.h`) serves both **paint** transfers
and term templates:

- It prefers an existing **source** with the requested name and definition,
  then an equivalent source under another name, then reserves a unique name
  and stages an `AddSource` edit.
- A newly staged source is available to later requests in the same
  operation.
- The builder owns its working copy. It does not change its caller's input.
- `ReuseOrAdd` returns the selected name. `TakeEdits` consumes the staged
  additions.
- `SourceCatalogOf` adapts recipe records and studio snapshots to a
  `SourceCatalog` of reusable source records and reserved names.
- **Mask** names are reserved too, although masks are not reusable sources.

### Board and selection

- The **board**, the inspector, and the selection code share `WritesCell`
  (`studio/Rows.h`) to match surface **outputs** to **slots**.
- `SelectedOutput` returns surface outputs only.
- **Isolation** indicators use `Isolation::TargetsOutput` and
  `TargetsLayer`. Visibility and muting belong to `View`.

### Handles

- `ActorPlan::geometries` is flat. Each planner `Geometry` describes one
  renderable **geometry**, and its `GeometryId` also identifies the
  corresponding geometry in the runtime's nested traversal order.
- `LiveActor::pieces` groups runtime geometries by worn armor part.
  `LivePieceId` indexes those groups. The two handle types stay distinct
  even when their numeric values match.
- `LocateGeometry` returns a runtime geometry and its owning **piece**
  together, from one definition of their traversal order.
- `MatchesForPiece` combines **placements** over a contiguous range of
  geometry ids to build the studio's armor-piece view.

### Planning

- `planners/ActorPlanning` matches recipes and builds geometry and light
  plans through `PlanGeometryPlacement` and `PlanActorLights`. It does not
  apply them to the engine.
- `RecipesOfInactiveInstances` is an `ActorPlan` query. It reports one
  recipe id per **instance** without live geometry, so a result can repeat a
  recipe id or name a recipe that still has another active instance.

### Row projection

- `BuildRecipeRow` (`studio/RecipeSnapshot`) projects a recipe and optional
  live signal data into a snapshot row.
- The individual row builders, including the light and shell projections,
  live in `studio/Rows`. Panel assembly stays in `Panels`.

### Edit commits

- Live instances and prepared **layers** borrow recipe storage.
- `PrepareEdits` applies a batch to a candidate recipe without modifying the
  original. The native batch `Apply` and the runtime editor both use it.
- The runtime returns before retiring actors when preparation fails or the
  candidate equals the current recipe.
- `Manager::ChangeAndRebuildActors` is the one application path. It includes
  applied and loaded actor candidates, retains targets across pending
  revisions, retires them before mutation, then queues refreshes.
- Keep recipe-store mutations inside these lifetimes. The callbacks run
  synchronously; the refresh is queued.
- For prepared edits, retirement happens before the recipe is replaced and
  republished through `RefreshRecipeDerivedState`. That operation validates
  the recipe, resolves forms, invalidates the signal graph, recomputes dirty
  state, and republishes derived data.
- Retirement does not mutate recipe storage, so the target stays valid
  through the commit callback.

### Application results

`engine/ApplicationService` owns the engine-free application tokens, the
actor targets, and the queued/prepared/rendered/failed/unmatched/cancelled
results.

- A model commit does not establish render success. Refresh stamps live
  state with captured tokens; the compositor reports success explicitly;
  snapshots publish outcomes even when no geometry row exists.
- An old-token report cannot complete newer work.
- Solo commands reduce against the owning thread's view.
- Recipe isolation runs before duplicate-key resolution. Output filters run
  before replacement planning and first-light selection. Layer filters run
  in the shared compositor.
- A geometry-local mask or **ripple** cache keys on owning recipe id, local
  name, and texture size. Recipe retirement discards it before mutation.

### Paint commits

- One `PaintCommitRequest` carries the requested expression, the target, and
  the mask name.
- Preparation checks a copy of the paint recipe. Applying the resulting
  batch commits the target atomically.
- Only a successful apply ends the runtime preview.
- The menu keeps its session until a snapshot carries the matching
  `PaintCommitResult`. An older result cannot close a newer session.

## Component ownership

### `Manager`

- Owns actor application, retirement, event delivery, task scheduling, and
  snapshot publication.
- `engine/LiveActor.h` holds the runtime records. Helpers over those records
  include the header without depending on the manager.

### `RecipeEditor` (owned by `Manager`)

- Owns recipe history, paint commits, and the editor view. Menu commands go
  through `Manager::Editor()`.
- It queues work through the manager and uses the manager's retirement
  callbacks before changing recipe storage. The callbacks stay synchronous;
  actor refresh stays queued. `RecipeEditor` has friend access for these
  scheduling and lifetime operations.
- Snapshots and rendering read editor state through const queries on the
  game thread.
- The editor cannot be copied or moved while queued tasks refer to it.
- Runtime events live in `ManagerEvents.cpp`, mesh inspection in
  `ManagerInspection.cpp`, recipe editing in `RecipeEditor.cpp`.

### `TextureLab`

- Owns shader setup, render passes, and readback. It delegates texture
  allocation, **presenter** slots, scratch **targets**, and reuse to
  `RenderTargetPool`.
- Every drawing path uses `TextureLab::RenderPass`, a noncopyable,
  nonmovable scope that owns the renderer lock and the captured D3D state
  together. Context commands go through the pass. The destructor restores
  state before the lock member releases the renderer. Capture includes the
  bindings D3D can unbind implicitly and the fields the pass sets.
- Staging resources have COM owners. A successful read mapping has a scoped
  unmap.
- An outstanding target returns through a weak reference to the pool's
  shared return cache, never through a captured pool address. A target
  returned after the cache expires is destroyed directly. Cache access is
  locked; target destruction and shared-pointer construction happen outside
  the cache lock.
- Inside the pool, scratch targets are destroyed before the return cache.
- Recycling from a shared-pointer deleter cannot throw. When caching a
  returned target fails, local unique ownership releases it.
- Each render target owns its replacement presenter metadata with
  `unique_ptr`. Teardown restores the original presenter pointer while it
  still points at that target's replacement, before resources are released.

### `TexturePreviews`

- Owns preview requests, generations, locking, and delayed release. It uses
  the renderer's public API and holds no shader or device state.
- The renderer destroys previews before the pool, so a preview target can
  still return to the pool during destruction.
- Clearing runs in this order: scratch release, preview retirement,
  unused-target release, readback-cache reset.
- Preview retirement retains targets for the eight-tick interval.
- Preview entries and work batches retain their source textures.
- `Manager::Snapshot` extends the studio's value snapshot with owning
  texture references. Menu code keeps the shared snapshot alive while it
  uses the borrowed texture handles. The references preserve texture object
  lifetime, not an immutable copy of rendered pixels.

### `PbrMaterial` and writes

- `PbrMaterial::Bind` checks the shader property before it constructs an
  owning PBR record.
- `SlotWriter` requires that record. It cannot be default-constructed or
  copied. `MaterialInputs::From` requires the record too.
- The record keeps the material and the property alive and exposes no raw
  layout access.
- Replacing a property's material can release the previous material. A write
  checks geometry and material attachment identity first.
- A temporary engine material or scene clone gains an owning reference
  immediately, including on paths that fail before installation.
- A private skin copy owns each copied bone-weight array and stays
  destructible at every allocation failure point.

### Load lifecycle

- The runtime starts loading on the load notification and resumes on a
  successful post-load or new-game notification.
- `BeginLoad` pauses ticks and task submission, invalidates queued tasks
  through the generation, unwatches actors, clears **bindings** and caches,
  and publishes an empty snapshot.
- `FinishLoad` resumes work and requests loaded actors through
  `ApplicationService`.
- `ApplicationService` owns the only runtime `SessionQueue`. Its actor
  callback creates lifecycle tokens and captures the current recipe-attempt
  tokens before it invokes the `Manager` adapter. Equipment finalization and
  coalesced reruns use the same callback.
- `SessionQueue` owns the loading state, the generation, the pending
  refreshes, the follow-ups, and the equip deadlines behind one mutex. The
  manager submits actor ids and owned command values; it does not handle
  epochs.
- A failed submission releases its pending markers and notifies the
  service's rejection mailbox while holding the queue generation lock. The
  notification appends actor ids only. It must not re-enter the queue or
  call an engine API.
- The owning thread drains failures before it starts newer attempts.
  Loading cancels the queue before it clears the mailbox.
- A queued callback holds a weak queue-state reference and cannot run after
  the queue is destroyed. The native runner tests this engine-free queue
  adapter directly.
- Queue callbacks and load transitions run on the game thread. The mutex
  protects submission bookkeeping and is released before engine calls,
  because an engine call can re-enter the queue.
- A live actor retains its actor handle and resolves an owning reference
  during each tick. An unloaded or deleted actor retires through the same
  cleanup path as an explicit retire.
- A worn-piece enchantment is a form id, resolved when read.
- A **light** retains the shadow scene that registered it and removes itself
  from that scene during teardown.

### Studio file split

- `studio/Panels.cpp` builds the stack, inspector, and signal-list views.
- `studio/Forms.cpp` builds form fields and their edit bindings.
- `studio/Rows.cpp` and `studio/SourceRows.cpp` project recipe records into
  rows. Source reconstruction lives beside its projection.
- `studio/FieldParsing` holds the parsers shared by source reconstruction
  and forms.
- Row conversion and source planning do not depend on panel assembly or form
  construction.

## Performance discipline

- Write load-time paths (parse, validate, resolve) for clarity. A recipe is
  read once. Do not optimise a load-time path on speculation
  (`REQUIREMENTS.md`, Constraints).
- Write per-tick and per-texel paths (signal evaluation, mask
  interpretation, the binding's writes) for cost, and measure them.
- The flat switches in `Evaluate` (`Signals.cpp`) and the expression
  evaluator, and the branch-cheap `Match` dispatch, follow from this rule.
- `IsAnimated` (`recipe/Vocabulary.cpp`) classifies a **stack** as static or
  animated. A static stack bakes once and caches; an animated stack
  re-renders each tick.

## House rules

- Write no comments in the C++ source: no banner, no section rule, no member
  note, no trailing aside. The code says it through a name, a type, or a
  small named helper. A fact the code cannot state (an engine layout, a CS
  rule, a decompile line, a packing, the reason for a constant) goes in
  `REFERENCE.md` under the module's heading. This rule is for the source;
  markdown documents are text.
- Write complete type signatures: no `auto` in a signature, no `Any`-like
  escape hatch. Mark every pure query `[[nodiscard]]`.
- Use one name per concept for the whole codebase. `Identity.h` is the only
  place the plugin name is spelled. Rename with
  `tools/rename.py Old New --apply`.
- Name code for what it is now, never for the change that produced it.

### Glossary

One word, one meaning, for the whole codebase. New code uses these words
with these meanings and no other word for the same thing.

| Word | Meaning |
|---|---|
| **slot** | A PBR texture slot only — `Slot` in `recipe/Recipe.h`, one of the nine channels a material writes (`kSlots`, `SlotName`). Never an equipment slot, a partition, or a pool index. |
| **biped slot** | An armor equipment slot, 30 to 61 (`enum class BipedSlot`, `kBipedSlots`, `BipedSlotFromName`). Parsed once at the JSON boundary. The NIF's raw partition field is not this type (`REFERENCE.md`, mesh). |
| **partition** | A run of triangles inside one mesh (`MeshPartition`). A `PartitionBake` selects one by its `bipedSlot`. |
| **contributor** | The placed recipe that won a slot or a light (`SlotContributor`, `LightContributor` in `recipe/Merge.h`), paired with an output index in a `SlotContribution`/`LightContribution`. |
| **plan** | What a planner decided, as plain data, before anything is applied: `ActorPlan`, `GeometryPlan`, `LightPlan`, `StackPlan`, `BindingPlan`. A plan holds no `RE::` pointer. |
| **binding** | A live attachment to an engine object that must be undone: `MaterialBinding`, `ShellBinding`, `LightBinding`. `MaterialBinding` and `ShellBinding` are `SlotTarget`s; `LightBinding` stands alone. The decision that produced a binding is a `BindingPlan`. |
| **lease** | A `shared_ptr` whose lifetime reserves a shared resource. The resource frees when the last lease drops (`TargetPool`, `TextureLeases`, `ConsumptionLeases`). |
| **target** | A render target the lab owns (`TextureLab::RenderTarget`), pooled by `RenderTargetPool`, referenced through a `TextureRef`. `SlotTarget` is the unrelated write interface a binding implements. |
| **region** | A named area of armor coverage, shown by overlays and legends (UI v2, section 4.7; no code uses the word yet). Never the paint vocabulary the 2026-09-09 decision retired. |
| **binders** | `recipe/Binders.h`, the one JSON boundary: `Reader`, `Writer`, `ParseObjectDocument`, `ParseSourceKind`/`SourceKindToJson`, `ReadRows`, `NamedRows`, `OneKey`. No other module names `nlohmann`. |
| **row level** | A `Diagnostic` whose `where` names a row (`signal`, `curve`, `source`, `mask`, `output`, `variant`) — `RowLevel`. `HasRecipeErrors` is its complement over a span: an error above row level holds the recipe out of the applied set. |
| **problem text** | The projection from `std::optional<Diagnostic>` to the string a row shows — `ProblemText` (`recipe/Recipe.h`). |
| **text file** | `engine/TextFile.h`: `ReadText` and `WriteText`, the only file reads and writes, each naming its failure. |
| **texture identity** | `planners/TextureIdentity.h`: `ImageCacheKey` (one cache entry per file, however the path is spelled) and `IsPlaceholderExtent`. |
| **texture handle** | An opaque `enum class` over `std::uintptr_t` the snapshot carries for a preview. `TextureHandleOf` (engine) and `TextureOf` (menu) are the only conversions. Lifetime belongs to the snapshot's `shared_ptr`, never to the handle. |
| **application record** | `studio/ApplicationRecord.h`, the application record types. Nothing under `studio/` includes `engine/`. |
| **visit** | `recipe/Visit.h`, the published recipe traversal, with the location vocabulary (`ResourceKind`, `ResourceRef`, the `*Owner` records, `PropertyLocation`). A new traversal extends it; it does not grow an anonymous namespace. |
| **shader constants** | `render/ShaderConstants.h`, the one copy of the GPU constant-buffer structs the shaders and the passes share. |
| **D3D result** | `render/D3DResult.h`: `Failed(hr)` and `DataOf(texture)`, the two D3D result checks. |
| **mesh cache** | `render/MeshCache.h`, the per-geometry cache of mesh data, facts, analysis, and bakes. `MeshReader` fills it from the engine. |
| **layers** | `ALLOWS` in `tools/gate.py`, the only copy of the include graph. A new directory, top-level file or widened edge is an edit to that table. |

## Gates

### Formatting

- `.clang-format` is `BasedOnStyle: LLVM` with `FixNamespaceComments:
  false`.
- The vendored and frozen trees (`src/extern`, `src/cs`, `src/_old`) opt out
  with their own `DisableFormat: true`.
- `tools/gate.sh fix` formats all first-party C++ in `src` and `tests`
  except those trees; the gate stages check it.

### The gate

`tools/gate.sh {commit|push|release|fix}` enters Nix and launches the Python
gate implementation in `tools/gate.py`. The git hooks
(`.githooks/pre-commit`, `pre-push`) and the Claude Code hook call it. It
re-execs into `nix develop` so the pinned clang tools always run.

- `commit` checks the staged content of staged first-party C++ files:
  formatting, layers, and comments.
  Run targeted tidy explicitly while developing; full tidy runs at release validation.
  Baseline checks include header diagnostics, ignore line movement, and
  block increased file/check counts.
- `push` runs the same checks over every first-party file, then the
  sanitized CTest suites. `release` adds the Windows build and full normal
  clang-tidy, allowing reductions in findings.
- Regenerate the baseline only after review with
  `python3 tools/tidy.py --update`.
- Analysis has no result cache. A failed clang-tidy invocation fails the
  run. Static analyzer checks run separately with
  `python3 tools/tidy.py --analyzer`.
- `docs/build.md` owns commands, presets, and recovery. Tidy
  reads the Windows CMake database directly; the clangd view is rewritten
  only when its contents change.

### No comments

- The gate scans first-party C++ in `src/` and `tests/` for `//` and `/*`
  comments outside string, character and raw-string literals. Any hit
  fails the commit or push. The exact license notice at the top of a file
  is exempt.
- `src/_old`, `src/extern`, and `src/cs` are frozen or vendored and keep
  their comments.
- A `NOLINT` marker is a tool directive, not text. The directives in
  `src/engine/RecipeStore.cpp` and the destructor directive in
  `src/engine/AnimationSubscriptions.cpp` have their reasons in `REFERENCE.md`.

### Layers

- `ALLOWS` in `tools/gate.py` holds the only copy of the include graph: one
  row per directory and top-level file under `src/`, listing what it may
  include.
- The gate reports a file whose directory or name has no row, every
  `#include "..."` outside its row (a root header name that is not listed is
  also outside it), and every `RE::`, `REL::` or `SKSE::` outside literals and
  comments in `Core.h` or an engine-free directory, each with file and line. The commit
  and push stages fail on any report.
- Adding a directory or widening an edge is an edit to the `ALLOWS` table.
  The graph changes in one place, and the change is visible in the diff.

### Native build and tests

- Configure with `cmake --preset native`, build with
  `cmake --build --preset native`, and run with `ctest --preset native`.
  Presets cap compilation and test concurrency at four.
- Use `native-sanitized` for ASan/UBSan in a separate build directory.
  CTest `-R <regex>` selects suites; CMake `--target <suite>` builds one.
- A module is done when four things hold (`REQUIREMENTS.md`): the build has
  zero warnings, the native suite passes, the tidy baseline shows only what
  the module meant to add, and the frozen counterpart is gone from the
  build.
- Build with `cmake --preset windows-release` then `cmake --build --preset windows-release`. More jobs exhaust WSL's memory and
  kill the instance. Do not build and run clang-tidy at the same time. Work
  inside `nix develop`.
