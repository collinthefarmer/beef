# BetterEnchantmentEffects — code conventions

The patterns the pure core (`recipe/`, `mesh/`) already established. Wave-2+
modules (`studio/`, `planners/`, `engine/`, `render/`, `menu/`) follow these
so they reuse the helpers rather than reinvent them. Every rule below cites a
real helper by file and name; grep for it before writing a variant.

`CLAUDE.md` holds the three rules (memory safety, readability for a
non-C++ developer, lean performance) and the house rules. `REQUIREMENTS.md`
holds Architecture, Safety and Constraints. This document is the how, grounded
in the current source.

## Diagnostics: report through `Reporter`, never push a `Diagnostic`

`Reporter` (`recipe/Recipe.h`) is the shared diagnostic sink: it holds a
`std::vector<Diagnostic> &out` and a `std::string where`, and exposes
`Error`, `Warn`, and `At(where)` which returns a child `Reporter` naming a
narrower row. A row's `where` names it for the menu (`signal glowLevel`,
`output 2 layer 0`); `At` builds those (`ctx.At(std::format("output {}", i))`
in `ReadOutputs`, `RecipeRead.cpp`).

The rule: construct one `Reporter` for a scope and call `Error`/`Warn`; do not
build a `Diagnostic{Severity::…, where, msg}` and `push_back` it. `CheckSource`
and `CheckLayer` (`Signals.cpp`) show the form — `const Reporter report{out,
where};` then `report.Error(…)`.

For a function that returns one error through `std::expected` or `std::optional`,
construct it with `MakeDiagnostic(severity, where, message)` (`recipe/Recipe.h`).
`Reporter::Error` and `Warn` use the same factory. Do not create a temporary
diagnostic vector just to return its first element; use `Reporter` when collecting
diagnostics and the factory when returning one.

`SignalGraph` emits through `ReportSignal` (`Signals.cpp`), a static helper
that wraps a `Reporter{diagnostics_, "signal <name>"}`; graph phases call it,
they do not touch `diagnostics_` directly.

The contract, for every module:

> A failure that a person may need to see is a `Diagnostic` with a `where`.
> A function that can fail that way returns `std::optional<Diagnostic>`
> (nullopt is success) or `std::expected<T, Diagnostic>`. Several failures
> are `std::vector<Diagnostic>` collected through a `Reporter`. No function
> returns `bool` or an empty string to mean "it failed, see the log". Snapshot
> rows carry `std::string problem` as the display projection of a
> `Diagnostic::message` (`ProblemText` in `Recipe.h` makes that projection);
> they are not a place a failure originates.

The row `where` words are spelled once, by the helpers beside `Reporter` in
`Recipe.h` (`SignalWhere`, `CurveWhere`, `SourceWhere`, `MaskWhere`,
`OutputWhere`, `LayerWhere`, `VariantWhere`, `KeyWhere`); the parser, the
validator, the editor and the store all build their `where` through them.
`RowLevel` tells a row's diagnostic from a recipe-level one, which is how the
store decides whether a recipe with errors is applied or held back.
`RecipeStore`'s operations (`SaveRecipe`, `RevertRecipe`, `NewRecipe`,
`RenameRecipe`, `AddTransientRecipe`, `DropTransientRecipe`) return a
`Diagnostic` whose `where` is `recipe <id>`; `RecipeEditor` copies its
`message` into the `FileOperationResult` or `RecipeEditResult` that reaches
the menu. `SlotTarget::Problem` returns one whose `where` is the slot's name.

Logging, same rule:

> `Trace::Emit` records structured events for the diagnostic trace. `logger::`
> writes the human log. A failure is logged once, at the boundary that turns
> it into a `Diagnostic`, and never again downstream. A file that needs both
> uses `Trace` for state and `logger` for the human sentence.

`diagnostics/Trace.h` is the trace recorder: `Trace::Emit(Event, fields)`
appends one structured line per event (`kStartup`, `kSettings`, `kRecipe`,
`kCommand`, `kPage`, `kQueue`, `kLoad`, `kApplication`, `kRetire`,
`kBinding`, `kRestore`, `kTexture`, `kShell`, `kPreview`,
`kCaptureFailure`) to the run's trace file under a byte cap; `Trace::Scope`
tags the events of one command with a command id, and `Trace::Safely` wraps
a capture so a throwing capture becomes a `kCaptureFailure` event instead of
a crash. `tools/trace-report.py` reads the file.

The one stated exception is `SettingsFile.cpp`: the INI reader logs a bad
line and keeps the default. Settings are not recipe data, no menu row shows
them, and a wrong value must never stop the plugin from starting, so that
file has no diagnostics and no `Reporter`.

## The JSON boundary: `Reader`/`Writer` binders, read/write symmetric

Recipe JSON is parsed and serialized by two mirrored vocabularies, both over
`nlohmann::ordered_json` (ordered, so a round-trip preserves key order). Both
are published by `recipe/Binders.h`, the one place a module includes to read
or write a JSON document: `RecipeRead.cpp`, `RecipeWrite.cpp` and
`studio/Presets.cpp` all build on it and none names `nlohmann` for itself.
`ParseObjectDocument(text, Reporter)` is the entry for a whole file: it
refuses text nested past `kMaxRecipeDepth` before parsing, parses with a
duplicate-key detector, and reports "not a JSON object" for anything that is
not one. `ParseSourceKind(Reader &)` and `SourceKindToJson` publish the
source-kind pair so a second format (presets) reads and writes sources
through the recipe's own table rather than an if-chain of its own.

`Reader` (`Binders.h`) is the parse binder. It tracks which keys it
consumed and `Finish()` reports every unconsumed one as `unknown key`, so an
object is fully specified by the `Read`/`Child`/typed-accessor calls made
against it. Vocabulary:
- typed getters returning `std::optional`: `Number`, `Integer`, `Boolean`,
  `String`, `Reference`, `Parameter`, `Vector2`, `Vector3`, `Point`,
  `Literal`; `Required(key)` for a mandatory string.
- `Read(key, out)` overloads that assign into a field only when present
  (`Param`, `Ref`, `Vec2Param`, `Vec3Param`, `bool`, and enum-table form).
- `Enum(key, table)` / `Read(key, table, out)` resolve a word against a spec
  table via `FromName`, reporting `Choices(table)` on a miss.
- `IntRange(key, lo, hi, out)` for a bounded integer.

`Writer` (`Binders.h`) is the mirror: `Set`, `Write` (Param/Vec3Param),
`WriteText`/`WriteTextIf`, `WriteRef`/`WriteRefIf`, `WriteEnum`/`WriteEnumIf`,
`WriteIf` (value-vs-default overloads), `WriteNumberIf`, `WritePointIf`. The
`*If` forms omit a field equal to its default, which is how "defaults are
omitted on write" (`REQUIREMENTS.md`) is enforced field by field.

Read/write symmetry is the anti-drift property: `ParseRecipe` then
`SerializeRecipe` reproduces the file byte-for-byte (the round-trip invariant),
so a field added to the reader without its writer mirror (or vice versa) fails
the round-trip test. Add both together.

Free helpers layered over `Reader`:
- `ReadObject(v, word, ctx, fill)` — assert `v` is an object, run `fill` over
  an inner `Reader`, `Finish()`. The body of most per-kind parsers.
- `ReadRows(array, word, ctx, out, parse)` — parse an array into a vector,
  capping at `kMaxRecipeRows` via `RowCapReached`.
- `NamedRows(root, section, rowWord, out, parse)` — the same over a
  name-keyed object (`signals`, `curves`, `sources`, `masks`).
- `OneKey(j, ctx, what, common)` — a variant object carrying exactly one kind
  key plus optional `common` keys; returns the kind key and its value, or
  reports "no kind key" / "two kind keys".
- `EnumShorthand(v, table, what, ctx)` — a bare string resolving to an enum
  (a source/signal that is just its word, e.g. `"material": "roughness"`).

One field, one source of truth: a field is read in one place and written in
one place. `ReadMetadata`, `ReadKeys`, `ReadOutputs`, `ReadVariants` in
`RecipeRead.cpp` are the per-section readers; `SerializeRecipe`'s `named`
lambda is the writer counterpart for the name-keyed sections.

## Variants and closed sets: one spec table, dispatch by `Match`

Every enum carries exactly one `constexpr` spec table in `Words.h`, one row per
value in enum order, with `static_assert(Complete(table, kFooCount))`
(e.g. `kSignalKinds`/`kSignalKindCount`, `kSlots`/`kSlotCount`,
`kScalarFields`, `kMaterialChannels`). `Complete` (`Core.h`) checks that the
table has `kFooCount` rows and that every value below the count appears in
exactly one row, so `NameOf`'s `"?"` branch is unreachable for a table that
passes. Every enum in `Recipe.h` has a `kFooCount` beside it for that assert.
For variant closed sets, the table is either a `Named<Id>` table over an id
enum whose enumerators follow alternative order (`kSourceKindWords` over
`SourceKindId`, with `SourceKindIdOf(kind)` reading the id from
`variant::index()`) or a word array asserted against `std::variant_size_v`
(`kBakeKindWords`, `kTriggerOriginWords`); both carry a run of
`std::is_same_v<std::variant_alternative_t<I, V>, …>` asserts pinning table
order to alternative order. That pin is load-bearing: `DefaultSourceKind`
builds the alternative from the id, and `ParseSourceKind` indexes
`kSourceParsers`, a table built over `std::variant_alternative_t<I,
SourceKind>` from one `ParseSourceAlternative<T>` specialisation per
alternative, so an alternative without a parser is an undefined symbol at
link time rather than a wrong entry at runtime.

The generic table operators live in `Core.h`: `NameOf(table, value)`,
`FromName(table, word)`, `RowOf(table, value)`, `Choices(table)`,
`WordsOf(table)`, and `AlternativeAt<Variant>(index)` to build a default-
constructed alternative by index. A spec accessor is `RowOf(table, v)` then a
field read (`DefaultPriority`, `BlendShaderMode`, `MaterialMapOf` in
`Vocabulary.cpp`), never a hand-written switch.

Dispatch over a variant is `Match` (`Core.h`), index dispatch over
`std::get_if` — not `std::visit`. It is non-throwing: the
`kAlternativesNothrowMovable` `static_assert` proves the variant can never be
valueless, so `Match` inside a `noexcept` path cannot become `std::terminate`.
`Get<T>` and `Is<T>` are the get-if / holds-alternative shorthands.

No catch-all arm in a `Match` over a recipe variant (`SignalKind`,
`SourceKind`, `BakeKind`, `Output`, `Bones`, `VariantKey`): each alternative
gets its own lambda, empty-bodied where nothing applies (`CheckSource` in
`Signals.cpp`, `VisitSourceParams` in `Edits.cpp`). A `[](const auto &)` arm
would silently absorb a new alternative; without one, `Match`'s overload
resolution fails at every site the new kind must be handled.

Adding a source kind — the sites the compiler reports when `SourceKind` gains
an alternative (recorded from adding a fake eighth alternative, critique Plan
B, 2026-09-14): `Recipe.h` (`kSourceKindCount` against `variant_size_v`, and
`SourceKindId` needs its enumerator), `Words.h` (`Complete(kSourceKindWords)`
and the alternative-order asserts), `RecipeRead.cpp` (undefined
`ParseSourceAlternative<T>`), `Vocabulary.cpp` `SourceType`, `Signals.cpp`
`CheckSource`, `Recipe.cpp` `AnimationQuery::Source`, `RecipeWrite.cpp`
`DescribeSource` and `SourceKindToJson`, `studio/Edits.cpp`
`VisitSourceParams`, `studio/SourceRows.cpp` `BuildSourceRow`, and
`render/CompositorSource.cpp` `SourcePreparer` and `SourceInspector`. Still
discipline-only until Plan B3 lands: `studio/SourceRows.cpp` `SourceKindOf`,
`studio/Forms.cpp` `SourceForm` (string if-chains) and the `SourceRow` field
union in `studio/Snapshot.h`. `tests/recipe/schema_tests.cpp` then fails
until `schema/recipe.schema.json` lists the new word.

Big arm vs tiny arm — the recorded judgment:
- When each per-kind arm is a substantial body, extract per-kind functions and
  dispatch through an enum-ordered function-pointer table with a size assert:
  `kSignalParsers` and `kSourceParsers` (`RecipeRead.cpp`), each
  `static_assert`-ed against the kind count / `variant_size_v`. `SignalFrom`
  and `SourceFrom` then index the table instead of enumerating kinds inline.
  The serializers likewise route through per-kind `*ToJson` helpers behind one
  `Match` (`SignalToJson`, `SourceToJson`, `RecipeWrite.cpp`).
- When arms are one-liners, keep them as a single exhaustive `Match` — the
  compiler then demands an arm when a kind is added. `SignalState::Evaluate`
  and the per-texel expression `Evaluate`/`Reduce`-style switches are kept flat
  deliberately: turning them into function-pointer tables is **rejected**
  because the per-texel `Evaluate` switch compiles to an inlined jump table and
  an indirection would cost more than it cleans. Make illegal states
  unrepresentable at load and edit time; on per-tick/per-texel paths keep the
  check cheap, explicit and tested.

## Multi-phase algorithms: named phases, bounded recursion, capped rows

A multi-step algorithm is a sequence of named private static phases over the
object, not one long function. `SignalGraph::Compile` (`Signals.cpp`) is the
model: `ParseCurves`, `RegisterNodes`, `ResolveRefs`, `OrderNodes`,
`InferTypes`, `CheckReferenceTypes`, `PropagateInert`, each taking
`SignalGraph &`. The topological sort in `OrderNodes` is a C++23 deducing-this
recursive lambda (`[&](this auto &&a_self, …)`).

Bound every recursion and cap every list:
- `kMaxRecipeDepth` (`Recipe.h`) caps recursion. `OrderNodes` passes a depth
  and marks a node inert past it; `TexelTypeOf`/`MaskTypeOf` (`Signals.cpp`)
  thread `a_depth` and stop at the limit; expression parsing has its own
  `kMaxExpressionDepth` (`Expression.h`).
- `kMaxRecipeRows` (`Recipe.h`) caps every row list, checked by
  `RowCapReached` inside `ReadRows`/`NamedRows` and inside the ad-hoc loops
  (`selector`, `stops`, `boneWeight`, `overrides`).
- `MaxNestingDepth` (`RecipeRead.cpp`) rejects JSON nested past
  `kMaxRecipeDepth` before parsing, and a `DuplicateFinder` parse callback
  reports duplicate keys.
- Expression bounds: `kMaxExpressionLength`, `kMaxExpressionDepth`,
  `kMaxExpressionOps` (`Expression.h`). Mesh/cluster bounds:
  `kMaxMaterialClusters`, `kMaxClusterIterations`, `kMaxChannelWeight`
  (`Recipe.h`), `kMaxIslands` (`Islands.h`).

Parse, don't validate: untrusted input is turned into a typed record once, at
the boundary. `FormKey::Parse`/`FormRef::From`, `ChannelSet::Parse`,
`ParseParam`/`ParseVec3Param` (`Vocabulary.cpp`) are the atomic parsers; past
them the rest of the code trusts the type.

## Structure: shape before fill, one `.cpp` home per function, no catch-all

`docs/buildup-plan.md`: a module's header (data types then complete function
signatures) lands first and is frozen; fill agents then own disjoint `.cpp`
files. A header states its data types first, then the free functions over them
(`Recipe.h`, `Signals.h`, `Mesh.h`, `Merge.h` all follow this).

Every declared function has one assigned `.cpp` home; there is no catch-all
translation unit. `recipe/` splits by concern:
- `Recipe.cpp` — record accessors (`Recipe::Find*`), variants (`ApplyVariant`,
  `VariantApplies`), `IsAnimated` (the `AnimationQuery` walker).
- `Vocabulary.cpp` — the static vocabulary and atomic text forms over the
  `Words.h` tables (every `…Name`/`Parse…`/spec accessor).
- `Resolve.cpp` — piece matching and resolution (`Resolve`, `KeyMatches`,
  `Matches`, `GlobMatch`, `KeyChoicesOf`).
- `RecipeRead.cpp` / `RecipeWrite.cpp` — the JSON boundary, verified by
  round-trip against the canonical files.
- `Signals.cpp` — graph compile, per-tick evaluation, and validation.

Functional core, thin adapter (`REQUIREMENTS.md`): the pure decision is native
and tested; the engine adapter only calls existing APIs and null-checks
pointers each frame. `Signals.h`'s `SignalEnvironment` is the seam — an
abstract interface the pure `SignalState::Tick` reads through, with
`NullEnvironment` for tests; the engine supplies the live implementation.
`Merge.h`'s `Plan*` functions are pure planners over records that the engine
adapter executes.

A planner decides over value records and opaque handles, never over engine
(`RE::`) pointers. It takes plain records or `enum class` handle types (as
`Merge`'s `Plan*` take `PlacedRecipe`/`SlotContributor`, and an actor
planner takes an `ActorPlan` over opaque ids) and returns a plan the adapter carries out. If
a decision needs a live pointer to make up its mind, the split is wrong: that
logic belongs in the adapter, not the planner. This is what keeps the pure core
testable without the engine and the adapter thin.

Illegal states unrepresentable where a type can carry it: `Merge.h` gives the
two index spaces distinct types (`enum class
SlotContributor`/`LightContributor` and
`SlotContribution`/`LightContribution`) so a light index cannot be used as a
slot index — already built into the new tree.

## Reuse across modules: call the core, do not re-implement it

A downstream module calls recipe/'s published surface; it does not write its
own copy. The surface: `ParseRecipe`/`SerializeRecipe`, `Validate`, `Resolve`,
`Recipe::Find*`, `Reporter`, and (behind the JSON boundary) `Reader`/`Writer`.
Validation is composed once — `Validate` (`Signals.cpp`) builds the
`SignalGraph`, wraps it and the recipe in a `RowTypes`, and runs the public
`Check*` (`CheckCurve`, `CheckSource`, `CheckMask`, `CheckLayer`,
`CheckOutput`) over `RowTypes`. A consumer that validates an edit calls these
`Check*` over `RowTypes`; it does not re-derive "does this name a known row of
the right type."

The anti-pattern this avoids is on record: the frozen tree wrote one row-check
three times (`Validator`, `CheckSourceKind`, `EditCheck`) and they diverged, so
the editor accepted values the loader rejected. One check over `RowTypes`,
reused, is why the new tree cannot drift that way.

Name lookup is a `Find*`, never a re-scan: `Recipe::FindSignal`/`FindCurve`/
`FindSource`/`FindMask` (`Recipe.cpp`) are the single lookups; call them rather
than re-iterating `recipe.signals`.

## Runtime identities and editor commits

`SourcePlanBuilder` (`studio/SourcePlan.h`) is shared by paint transfers and term
templates. It prefers an existing source with the requested name and definition,
then an equivalent source under another name, then reserves a unique name and
stages an `AddSource` edit. Newly staged sources immediately become available to
later requests in that operation. The builder owns its working copy; it does not
change its caller's input. `ReuseOrAdd` returns the selected name; `TakeEdits`
consumes the staged additions. `SourceCatalogOf` adapts both recipe records and studio
snapshots to a `SourceCatalog` of reusable source records and reserved names.
Mask names are reserved too, even though masks are not reusable sources.

The board, inspector, and selection code share `WritesCell` (`studio/Rows.h`)
to match surface outputs to slots. `SelectedOutput` only returns surface outputs.
Isolation indicators use `Isolation::TargetsOutput` and `TargetsLayer`; visibility
and muting remain the responsibility of `View`.

`ActorPlan::geometries` is flat: each planner `Geometry` describes one
renderable geometry. Its `GeometryId` also identifies the corresponding
geometry in the runtime's nested traversal order. `LiveActor::pieces` groups
runtime geometries by worn armor part; `LivePieceId` indexes those groups.
These handles are distinct even when their numeric values happen to match.
`LocateGeometry` returns a runtime geometry and its owning piece together,
using one definition of their traversal order.
`MatchesForPiece` combines placements over a contiguous range of geometry IDs
to build the studio's armor-piece view.

`planners/ActorPlanning` matches recipes and builds geometry/light plans through
`PlanGeometryPlacement` and `PlanActorLights`. It does not apply them to the
engine. `RecipesOfInactiveInstances` is an `ActorPlan` query: it reports one
recipe ID per instance without live geometry, so results can repeat a recipe ID
or name a recipe that still has another active instance.

`BuildRecipeRow` in `studio/RecipeSnapshot` projects a recipe and optional live
signal data into a snapshot row. Individual row builders, including light and
shell projections, live in `studio/Rows`; panel assembly stays in `Panels`.

Live instances and prepared layers borrow recipe storage.
`PrepareEdits` applies a batch to a candidate recipe without modifying the original;
the native batch `Apply` and runtime editor both use it. The runtime returns before
retiring actors when preparation fails or the candidate equals the current recipe.
`RebuildRecipeWearersAfterChange` and `RebuildAllActorsAfterChange` use one
application path. It conservatively includes applied and loaded actor candidates,
retains targets across pending revisions, retires them before mutation, then
queues refreshes. Keep recipe-store mutations inside these lifetimes;
the callbacks run synchronously, while the refresh is queued.
For prepared edits, retirement still happens before replacing the recipe and
republishing it through `RefreshRecipeDerivedState`. This operation validates the
recipe, resolves forms, invalidates the signal graph, recomputes dirty state, and
republishes derived data. Retirement itself does not mutate recipe storage, so
the target remains valid through the commit callback.

`engine/ApplicationService` owns engine-free application tokens, actor targets,
and queued/prepared/rendered/failed/unmatched/cancelled results. A model commit
does not establish render success. Refresh stamps live state with captured tokens;
the compositor reports success explicitly, and snapshots publish outcomes even
when no geometry row exists. Old-token reports cannot complete newer work.
Solo commands reduce against the owning thread's view. Recipe isolation precedes
duplicate-key resolution; output filters run before replacement planning and
first-light selection. Layer filters run in the shared compositor. Geometry-local
mask and ripple caches use owning recipe ID, local name, and texture size, and
are discarded by recipe retirement before mutation.

Paint commits carry the requested expression, target, and mask name in one
`PaintCommitRequest`. Preparation checks a copy of the paint recipe; applying
the resulting batch commits the target atomically. Only a successful apply
ends the runtime preview. The menu keeps its session until a snapshot carries
the matching `PaintCommitResult`; an older result cannot close a newer session.

## Component ownership

`Manager` owns actor application, retirement, event delivery, task scheduling,
and snapshot publication. `engine/LiveActor.h` holds the runtime records;
helpers over those records include it without depending on the manager.

`RecipeEditor`, owned by `Manager`, owns recipe history, paint commits, and the
editor view. Menu commands go through `Manager::Editor()`. The editor queues work
through the manager and uses its retirement callbacks before changing recipe
storage. These callbacks remain synchronous; actor refresh remains queued.
`RecipeEditor` has friend access for these scheduling and lifetime operations.
Snapshots and rendering read editor state through const queries on the game
thread. The editor cannot be copied or moved while queued tasks refer to it.
Runtime events live in `ManagerEvents.cpp`, mesh inspection in
`ManagerInspection.cpp`, and recipe editing in `RecipeEditor.cpp`.

`TextureLab` owns shader setup, render passes, and readback. It delegates texture
allocation, presenter slots, scratch targets, and reuse to `RenderTargetPool`.
Each drawing path uses `TextureLab::RenderPass`, a noncopyable, nonmovable scope
that owns the renderer lock and captured D3D state together. Context commands go
through the pass; its destructor restores state before its lock member releases
the renderer. Capture includes bindings D3D can unbind implicitly, as well as
the fields the pass explicitly sets. Staging resources have COM owners and
successful read mappings have a scoped unmap.
Outstanding targets return through a weak reference to the pool's shared return
cache, never through a captured pool address. Returning a target after that cache
expires destroys the target directly. Cache access is locked, and target destruction
and shared-pointer construction happen outside the cache lock.
Within the pool, scratch targets are destroyed before the return cache.
Recycling from a shared-pointer deleter cannot throw. If caching a returned
target fails, local unique ownership releases it instead.
Each render target owns its replacement presenter metadata with `unique_ptr`;
teardown restores the original presenter pointer while it still points to that
target's replacement, before releasing resources.
`TexturePreviews` owns preview requests, generations, locking, and delayed
release. It uses the renderer's public API and holds no shader/device state.
The renderer destroys previews before the pool, so preview targets can still
return to the pool during destruction. Clearing preserves the order: scratch
release, preview retirement, unused-target release, then readback-cache reset.
Preview retirement still retains targets for the existing eight-tick interval.
Preview entries and work batches retain their source textures. `Manager::Snapshot`
extends the studio's value snapshot with owning texture references; menu code keeps
the shared snapshot alive while using its borrowed texture handles. These references
preserve texture object lifetime, not an immutable copy of rendered pixels.

`PbrMaterial::Bind` checks a shader property before constructing an owning PBR
record. `SlotWriter` requires that record and cannot be default-constructed or
copied; `MaterialInputs::From` also requires it. The record keeps both the material
and property alive and exposes no raw layout access to callers. Replacing a
property's material can release the previous material. Writes check geometry and
material attachment identity before touching them. Temporary engine materials and scene clones gain
an owning reference immediately, including paths that fail before installation.
Private skin copies own each copied bone-weight array, and remain destructible
at every allocation failure point.

Runtime starts loading and resumes on successful post-load or new-game notification. `BeginLoad`
pauses ticks and task submission, invalidates queued tasks through the generation,
unwatches actors, clears bindings and caches, and publishes an empty snapshot.
`FinishLoad` resumes work and requests loaded actors through ApplicationService.
ApplicationService owns the only runtime SessionQueue. Its actor callback creates
lifecycle tokens and captures current recipe-attempt tokens before invoking the
Manager adapter. Equipment finalization and coalesced reruns use that callback too.
`SessionQueue` owns the loading
state, generation, pending refreshes, follow-ups, and equip deadlines behind one
mutex. The manager submits actor IDs and owned command values rather than handling
epochs. Failed submissions release pending markers and notify the service's
rejection mailbox while holding the queue generation lock. That notification
only appends actor IDs; it must not re-enter the queue or call engine APIs.
The owning thread drains failures before starting newer attempts, and loading
cancels the queue before clearing the mailbox. Queued callbacks hold weak
queue-state references and cannot run after the queue is destroyed. The native
runner tests this engine-free queue adapter directly. Queue callbacks and load
transitions run on the game thread; the mutex protects submission bookkeeping and
is released before engine calls, which can re-enter the queue.
Live actors retain actor handles and resolve an owning reference during each tick;
unloaded or deleted actors retire through the same cleanup path as explicit retires.
Worn-piece enchantments are form IDs, resolved when read. Lights retain the shadow
scene that registered them and remove themselves from that scene during teardown.

`studio/Panels.cpp` builds stack, inspector, and signal-list views.
`studio/Forms.cpp` builds form fields and their edit bindings.
`studio/Rows.cpp` and `studio/SourceRows.cpp` project recipe records into rows;
source reconstruction lives beside its projection. `studio/FieldParsing` holds
the parsers shared by source reconstruction and forms. Row conversion and source
planning do not depend on panel assembly or form construction.

## Performance discipline: clarity at load time, measured per tick/texel

`REQUIREMENTS.md` Constraints: load-time paths (parse, validate, resolve) are
written for clarity — a recipe is read once. Per-tick and per-texel paths
(signal evaluation, mask interpretation, the binding's writes) are written for
cost and measured. Do not optimise a load-time path on speculation.

The concrete consequences are the flat switches kept in `Evaluate`
(`Signals.cpp`) and the expression evaluator, and the `Match` index dispatch
that stays branch-cheap. `IsAnimated` (`Recipe.cpp`) is the static-vs-animated
classification a stack's caching turns on (a static stack bakes once, an
animated stack re-renders each tick).

## House rules

- No comments anywhere in the C++ source — not a banner, section rule, member
  note, or trailing aside. The code says it through a name, a type, or a small
  named helper. A fact the code cannot state (an engine layout, a CS rule, a
  decompile line, a packing, the reason for a constant) goes in `REFERENCE.md`
  under the module's heading. (This markdown doc is prose; the rule is for the
  source.)
- Complete type signatures: no `auto` in a signature, no `Any`-like escape
  hatch. `[[nodiscard]]` on every pure query.
- One name per concept for the whole codebase. `Identity.h` is the only place
  the plugin name is spelled. Rename with `tools/rename.py Old New --apply`.
- Names describe the code as it stands, never the change that produced it.

## Gates

- **Formatting** — `.clang-format` is `BasedOnStyle: LLVM` with
  `FixNamespaceComments: false`. Vendored and frozen trees opt out with their
  own `DisableFormat: true` (`src/extern`, `src/cs`, `src/_old`).
  `tools/format.sh` formats or `--check`s all of `src` except those trees.
- **The gate** — `tools/gate.sh {commit|push}` is one script that the git hooks
  (`.githooks/pre-commit`, `pre-push`) and the Claude Code hook all call; it
  re-execs into `nix develop` so the pinned clang tools are always used.
  `commit` blocks a commit whose staged sources are unformatted or that adds a
  clang-tidy finding above the baseline for a touched `.cpp`, compared per
  `(file, check)` count so line shifts from an edit do not false-trigger
  (`tools/tidy-baseline.sh --gate`). `push` runs `tools/format.sh --check`, the
  sanitized native tests (`BEEF_SANITIZE=1 tests/run-native.sh`), a full
  incremental `tools/tidy.sh`, then `tools/tidy-baseline.sh --check`, which
  fails if the findings differ from `docs/wip/tidy-baseline.txt`. A new module
  should add only the findings it meant to. Regenerate the baseline after an
  intended change with `tools/tidy.sh --force && tools/tidy-baseline.sh` (no
  args writes the file). `tidy.sh` caches per-file results under `build/tidy`
  (named by source path, `src_engine_X.txt`) and re-lints a file only when
  it, the compile database, `.clang-tidy`, or one of the headers its object
  included at the last build (from ninja's dependency log in
  `build/Release`) is newer than the result; a file that has never been
  built falls back to "any header under `src` is newer". Run the build
  after adding an include so the dependency log knows about it. It reads
  `build/clangd/compile_commands.json`, which `tools/compile-db.sh` rewrites
  after a source file is added or removed.
- **Layers** — `tools/layers.sh` holds the only copy of the include graph:
  one row per directory under `src/`, listing what that directory may
  include. It reports every `#include "..."` outside its row, every include
  that names no directory (`src` is the only include root), and every `RE::`
  symbol in an engine-free directory, each with file and line, and the push
  gate fails on any. Adding a directory or widening an edge means editing
  that table, which is the point: the graph changes in one place and the
  change is visible in the diff.
- **Native build and tests** — engine-free modules compile natively and run
  through `tests/run-native.sh`, which compiles the union of every selected
  suite's sources in parallel (`NATIVE_JOBS`, default 4) and then links and
  runs the suites in order; `SUITE=<substring>` selects suites and
  `BEEF_SANITIZE=1` builds them under ASan/UBSan into a separate object
  directory. A module is done (`REQUIREMENTS.md`) when the
  build is zero-warning, the native suite passes, the tidy baseline shows only
  what the module added, and the frozen counterpart is gone from the build.
- Build with `./build.sh Release -j 4` (more jobs OOM-kill WSL); never build
  and run clang-tidy at once; work inside `nix develop`.
