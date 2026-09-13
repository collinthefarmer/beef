# Plan B: source kinds by the compiler, and an engine-free snapshot — 2026-09-13

Status: not started.

Covers critique recommendations 3 (a typed `SourceRow`, no engine include in
`studio/`) and 4 (exhaustive dispatch over source kinds, count asserts,
`NameOf` failing loudly). Depends on Plan A's published `ParseSourceKind`.
Runs before Plan C because both touch `src/studio/Edits.cpp`.

## UI rework impact

This plan has the heaviest overlap. The UI plan's reuse audit names
`Forms.cpp` source forms, `BuildRecipeRow`, the `Edits.cpp` reference
traversal, `Selection`/`MenuState`, and retained texture previews as the
seams it extends in slices 1B, 1C, 2A, 2B and 2D.

- **Safe now:** B1 entirely (`SourceKindId`, count asserts, `Complete`
  check, index-checked `kSourceParsers`, `schema_tests`). B2 steps 1 and 2
  (`Vocabulary.cpp`, `Signals.cpp`). B4 step 1 (`ApplicationRecord` to
  studio) and B4 step 3 (the purity gate); the UI wave's new studio files
  contain no `RE::` names, so the gate passes on them.
- **Coordinate:** B2 step 3 (catch-alls in `VisitSourceParams` and
  `VisitSignalParams`). UI slice 2B extends the same visitors with owning
  property locations. Do this step before 2B starts, or hand the two
  lambdas to the 2B agent as part of their change. Plan C3 moves these
  templates to `recipe/Visit.h`; see that plan for the ordering.
- **Coordinate:** B4 step 2 (`TextureHandle` opaque). The UI checkpoint
  lists preview pinning as not yet wired and warns that a pinned preview
  must not keep an unowned raw texture pointer past its snapshot. An opaque
  handle with a generation is the right shape for that contract. Do it
  before pinning is wired, and tell the UI owner the conversion helpers'
  names.
- **Defer:** B3 entirely (`SourceRow` variant of per-kind field records,
  `SourceForm` rewrite, `LayerRow::blend`, `RecipeRow::key`). Slice 2A is
  adding metadata to the same forms and 2D is drawing sliders from them.
  Rewriting `SourceRow`'s shape under that work would force the UI agents
  to rebase twice. Land B3 after the complete-editor checkpoint, or offer
  it to the 2A agent as the shape they build on if they have not started.
  The acceptance check "adding a fake eighth alternative" then reports
  fewer enforced sites until B3 lands; record the shorter list.
- **Note for Plan D:** the `State()` singleton at `Intent.h:70` is inside
  UI slice 1B's seam and is deferred there.

## Findings addressed

- Adding a source kind touches about thirteen files. Compiler-enforced:
  `src/recipe/Recipe.h:446` (variant), `src/recipe/Words.h:96`
  (`kSourceKindWords` with per-index alternative asserts),
  `src/render/CompositorSource.cpp:316,492` (`SourcePreparer` and
  `SourceInspector`, exhaustive by overload resolution). Discipline only:
  - `src/recipe/RecipeRead.cpp:1094`: `kSourceParsers` asserts size, not
    that entry `i` parses alternative `i`.
  - `src/recipe/Vocabulary.cpp:264`: `SourceType` ends in `[](const auto &)
    { return ValueType::kScalar; }`, silently typing any new kind as scalar.
  - `src/recipe/Signals.cpp:796`: `CheckSource` covers five of seven kinds
    behind a catch-all; a new kind gets no validation.
  - `src/studio/Edits.cpp:1282`: `VisitSourceParams` ends in `[](auto &)
    {}`; a new kind's `Param`s escape rename and reference rewriting.
    `VisitSignalParams` at `:1279` has the same catch-all.
  - `src/studio/SourceRows.cpp:195`: `SourceKindOf` is a string-literal
    if-chain on `a_row.kind`.
  - `src/studio/Forms.cpp:1320`: `SourceForm` repeats the same if-chain.
  - `src/studio/Snapshot.h:116-144`: `SourceRow` is 25 consecutive
    `std::string` fields, the union of every kind's parameters, with `kind`
    as a string.
  - `schema/recipe.schema.json:154`: the kind list is duplicated by hand.
- `src/recipe/Words.h:48`: `kActorStates` has no count assert, unlike
  `kSlots`, `kSignalKinds`, `kScalarFields`. `src/Core.h:179`: `NameOf`
  returns `"?"` on a missing row.
- `src/studio/Snapshot.h:4`: includes `engine/ApplicationService.h` for one
  field, `std::vector<ApplicationRecord> applications` at `:247`. This is a
  directory-level cycle that the native library guard misses only because
  that header is SDK-free.
- `src/studio/Snapshot.h:19-23`: forward-declares `RE::NiSourceTexture` and
  aliases it as `TextureHandle`, used at eight sites. Engine vocabulary in the
  pure UI model.
- `src/studio/Intent.h:70`: `MenuState` exposed as a function-local static
  singleton in the pure model. Left for Plan D, which moves `MenuState` to
  its own header; note it there.
- `docs/conventions.md:163` already states "no catch-all". The rule exists
  and is broken in three places.

## Decisions

- **Source kinds get an id enum.** Introduce `enum class SourceKindId` in
  `src/recipe/Recipe.h` next to `SignalKindId`, one enumerator per variant
  alternative in variant order, with `kCount`. `kSourceKindWords` becomes a
  `Named<SourceKindId>` table with the existing per-index alternative asserts
  plus `static_assert(std::size(...) == kSourceKindCount)`. Add
  `SourceKindIdOf(const SourceKind &)` returning the id from `index()`.
- **No catch-all lambdas in `Match` over a recipe variant.** Each alternative
  gets its own lambda, empty-bodied where nothing applies. `-Werror=switch`
  and `Match`'s overload resolution then flag a new alternative at every
  site.
- **`SourceRow` becomes a variant of per-kind field records.** Replace the 25
  strings with `std::string name; SourceKindId kind; SourceFields fields;
  std::size_t references;` where `SourceFields = std::variant<ImageFields,
  MaterialFields, BakeFields, UvFields, DistanceFields, RippleFields,
  MaterialClustersFields>`. Each `*Fields` record holds only that kind's
  strings (the form still edits text, so the strings stay; they are the
  editing buffers). A `static_assert` pins `SourceFields` alternative `i` to
  `SourceKind` alternative `i` the same way `kSourceKindWords` does.
- **The snapshot's application records move to studio.** Studio owns the
  model the snapshot shows. Engine already includes studio in nineteen
  places; studio must include engine in none.
- **The texture handle becomes opaque.** `enum class TextureHandle :
  std::uintptr_t {}` in studio. Exactly one engine helper converts from
  `RE::NiSourceTexture *` and exactly one menu helper converts to
  `ImTextureID`. No `RE::` name appears under `src/studio/`.

## Steps

### B1: tables and asserts

1. Add `SourceKindId` and `kSourceKindCount` to `Recipe.h`; convert
   `kSourceKindWords` in `Words.h` to `Named<SourceKindId>`; keep the
   alternative-type asserts. Add `SourceKindIdOf`.
2. Add `kActorStateCount` to `ActorStateKind` and the count assert to
   `kActorStates`. Do the same for any other `Named` table in `Words.h`
   without one (`kMeasures`, `kEnchantmentFields`, `kPayloadFields`,
   `kUvAxes`, `kRippleShapes`): list them in the Status block.
3. Add a `constexpr bool Complete(const Row (&)[N], E kCount)` check in
   `Core.h` that every enumerator below `kCount` has a row and no row is
   duplicated, and `static_assert` it for every table. With that, `NameOf`'s
   `"?"` branch is unreachable for tables that pass; keep the branch, since
   `NameOf` is `noexcept`, but it is no longer a silent failure mode.
4. Make `kSourceParsers` index-checked: a function template
   `template <class T> std::optional<T> ParseSourceAlternative(Reader &)`
   with one explicit specialisation per alternative, and the table built by
   `[]<std::size_t... I>(std::index_sequence<I...>)` over
   `std::variant_alternative_t<I, SourceKind>`. A missing specialisation is
   a link error, which is loud enough.
5. Add `tests/recipe/schema_tests.cpp`: parse `schema/recipe.schema.json`
   with nlohmann and assert that its source-kind enum, actor-state enum,
   signal-kind enum and the operator names in the expression description
   equal the tables in `Words.h` and the `Op` names in `Expression.h`. Read
   the schema first to find the JSON paths; they are cited in the critique as
   `:35` (operators), `:93` (actor states), `:124` (signal kinds), `:154`
   (source kinds).

### B2: remove the catch-alls

1. `Vocabulary.cpp` `SourceType`: seven lambdas. `UvSource`,
   `DistanceSource`, `RippleSource`, `MaterialClustersSource` return
   `kScalar` explicitly.
2. `Signals.cpp` `CheckSource`: seven lambdas. `MaterialSource` and
   `UvSource` get empty bodies.
3. `Edits.cpp` `VisitSourceParams` and `VisitSignalParams`: one lambda per
   alternative. Read each alternative's members to decide whether it holds a
   `Param`, `Vec2Param`, `Ref` or `CurveRef` that the visitor must reach; the
   critique found the catch-all was hiding at least the possibility. Write a
   test in `tests/studio/edits_tests.cpp` that renames a signal referenced
   from every source kind that can reference one and asserts the rename
   landed.
4. Grep for any remaining `(const auto &)` or `(auto &)` lambda inside a
   `Match(` over `SignalKind`, `SourceKind`, `BakeKind`, `Output`, `Bones`
   or `VariantKey` and remove it the same way. List the sites in the Status
   block.

### B3: `SourceRow`

1. Define the seven `*Fields` records and `SourceFields` in `Snapshot.h`,
   with the alternative-order assert.
2. Rewrite `BuildSourceRow` (find it in `src/studio/RecipeSnapshot.cpp` or
   `SourceRows.cpp`) to `Match` over the source's `kind` and fill the
   matching record.
3. Replace `SourceKindOf(const SourceRow &)` in `SourceRows.cpp` with a
   `Match` over `fields` that calls the existing per-kind builders
   (`ImageKindOf` and siblings) with the typed record instead of the row.
4. Replace the if-chain in `Forms.cpp` `SourceForm` with a `Match` over
   `fields` calling the existing `ImageFields` and siblings. The `kind`
   choice field uses `NameOf(kSourceKindWords, row.kind)`.
5. Follow the compiler through `Fields.cpp` (`BindSourceMember`),
   `ResourcePanels.cpp`, `ContextRows.cpp` and the tests under
   `tests/studio/`. `BindSourceMember` is generic per the critique and
   should need only the member path changed.
6. `LayerRow::blend` at `Snapshot.h:49` is a string although `Blend` exists;
   change it to `Blend` and use `BlendName` at the display site. `RecipeRow`
   at `:180` carries both a typed `keys` vector and a stringly `key`; keep
   the vector, derive the display string where it is shown, and drop the
   member.

### B4: purity

1. Read `src/engine/ApplicationService.h`. Move `ApplicationRecord` and
   whatever it depends on that is engine-free into
   `src/studio/ApplicationRecord.h`. If it depends on `SessionQueue` types
   that are engine-free, move those declarations with it; if it depends on
   anything under `RE::`, split the record so the snapshot half is pure and
   the engine half stays. `ApplicationService.h` then includes the studio
   header. Run `tools/compile-db.sh`.
2. Replace the `RE::NiSourceTexture` forward declaration and alias with the
   opaque enum. Add `TextureHandleOf(RE::NiSourceTexture *)` in
   `src/engine/ManagerSnapshot.cpp` (or wherever the eight sites populate
   it) and `ImTextureIdOf(TextureHandle)` in `src/menu/MenuWidgets.cpp`.
   Remove every other conversion.
3. Add the purity check to the push gate in `tools/gate.sh`: fail if
   `grep -rln '"engine/\|"render/\|"menu/\|#include "PCH.h"\|\bRE::' src/recipe
   src/mesh src/planners src/studio src/diagnostics` prints anything. Plan C
   generalises this into a layer check; here it only needs to hold for the
   pure directories.

## Acceptance

- The gate's purity grep prints nothing.
- `tests/run-native.sh` compiles studio without `engine/` on the include
  path having been touched: verify by temporarily deleting the studio deps
  from `MODULE_DEPS` and confirming only `recipe mesh` are needed (Plan C
  removes that table; here it is a check).
- Adding a fake eighth alternative to `SourceKind` locally (do not commit
  it) produces compile errors at: `Words.h`, `Recipe.h` (`SourceKindId`),
  `RecipeRead.cpp` (missing specialisation, link error), `Vocabulary.cpp`,
  `Signals.cpp`, `Edits.cpp`, `Snapshot.h` (`SourceFields` assert),
  `SourceRows.cpp`, `Forms.cpp`, `CompositorSource.cpp` twice. Record the
  list of sites the compiler reported in the Status block; that list is the
  new extension checklist and goes into `docs/conventions.md` under the
  variants heading.
- `schema_tests` passes and fails when a word is removed from the schema.
- Native and sanitized suites green; gate push green.

## In-game checkpoint

The snapshot and the menu's source rows changed. Ask the user to open the
studio, select a recipe with at least an image source and a ripple source,
open each source's form, edit one field of each and confirm the value
persists after save and reload, and confirm texture previews still draw
(this exercises the opaque handle). Log lines: the snapshot publication line
from `ManagerSnapshot` and any `source <name>:` diagnostics.

## Out of scope

`PreparedSource` in `src/render/Compositor.h:46` is texture-shaped with one
`shared_ptr` per non-texture kind (`rendered`, `ripple`). A procedural source
would add a third. Reshaping it is render work with its own checkpoint and is
deferred; record it in `REFERENCE.md` under the compositor heading as known
debt.
