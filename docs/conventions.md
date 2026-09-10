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

`SignalGraph` emits through `ReportSignal` (`Signals.cpp`), a static helper
that wraps a `Reporter{diagnostics_, "signal <name>"}`; graph phases call it,
they do not touch `diagnostics_` directly.

Exception, documented: several `Check*` functions in `Signals.cpp`
(`CheckCurve`, `CheckMask`, `CheckUniqueNames`, `CheckVariants`,
`CheckSlotExclusions`) still `push_back({Severity::kError, where, …})`
directly, mostly to forward a parser's `program.error()` string. This is a
residual inconsistency, not the target pattern — see Cleanup below. New code
uses `Reporter`.

## The JSON boundary: `Reader`/`Writer` binders, read/write symmetric

Recipe JSON is parsed and serialized by two mirrored vocabularies, both over
`nlohmann::ordered_json` (ordered, so a round-trip preserves key order).

`Reader` (`RecipeRead.cpp`) is the parse binder. It tracks which keys it
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

`Writer` (`RecipeWrite.cpp`) is the mirror: `Set`, `Write` (Param/Vec3Param),
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
value in enum order, with `static_assert(std::size(table) == kFooCount)`
(e.g. `kSignalKinds`/`kSignalKindCount`, `kSlots`/`kSlotCount`,
`kScalarFields`, `kMaterialChannels`). For variant closed sets, the table is a
word array with a `static_assert` against `std::variant_size_v` plus a run of
`std::is_same_v<std::variant_alternative_t<I, V>, …>` asserts pinning word
order to alternative order (`kSourceKindWords`, `kBakeKindWords`,
`kTriggerOriginWords`). That pin is load-bearing: `SourceFrom` and
`DefaultSourceKind` index the parser table by `variant::index()`.

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
`Merge`'s `Plan*` take `PlacedRecipe`/`SlotSource`, and an actor planner takes
an `ActorState` over opaque ids) and returns a plan the adapter carries out. If
a decision needs a live pointer to make up its mind, the split is wrong: that
logic belongs in the adapter, not the planner. This is what keeps the pure core
testable without the engine and the adapter thin.

Illegal states unrepresentable where a type can carry it: `Merge.h` gives the
two index spaces distinct types (`enum class SlotSource`/`LightSource` and
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
  `tools/format.sh` formats or `--check`s all of `src` except those trees;
  `.githooks/pre-commit` blocks a commit whose staged sources are unformatted.
- **Lint baseline** — `.githooks/pre-push` runs `tools/format.sh --check`, then
  `tools/tidy.sh --force`, then `tools/tidy-baseline.sh --check`, which fails
  if the current clang-tidy findings differ from `docs/wip/tidy-baseline.txt`.
  A new module should add only the findings it meant to. Regenerate the
  baseline after an intended change with `tools/tidy.sh --force &&
  tools/tidy-baseline.sh` (no args writes the file). `tidy.sh` reads
  `build/clangd/compile_commands.json`; `tools/compile-db.sh` rewrites that
  after a source file is added or removed.
- **Native build and tests** — engine-free modules compile natively and run
  through `tests/run-native.sh`. A module is done (`REQUIREMENTS.md`) when the
  build is zero-warning, the native suite passes, the tidy baseline shows only
  what the module added, and the frozen counterpart is gone from the build.
- Build with `./build.sh Release -j 4` (more jobs OOM-kill WSL); never build
  and run clang-tidy at once; work inside `nix develop`.
