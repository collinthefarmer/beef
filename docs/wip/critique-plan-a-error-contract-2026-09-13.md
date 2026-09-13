# Plan A: one error contract and a published Reader — 2026-09-13

Status: not started.

Covers critique recommendations 1 (one `Diagnostic` everywhere) and 2
(publish `Reader` and `Reporter`, rebuild `Presets` on them). Depends on
nothing. Plan B depends on this plan's published source-kind parser.

## UI rework impact

- **Safe now:** A1 (publish `Reader`/`Writer`), A3 (presets on `Reader`),
  A5 (conventions text). UI slice 3F reads `MaskPresets` for the pattern
  chooser and may extend the presets format; the writer mirror and
  round-trip test from A3 make that extension safer, so land A3 first and
  tell the UI owner the format now has a writer.
- **Coordinate:** A2. The UI wave's slice 1A already defined
  `Studio::FileOperationResult{..., std::string error}` and
  `Studio::RecipeEditResult{..., std::optional<std::string> error}` in
  `src/studio/FileOperation.h` and `EditResult.h`, and routes save and
  revert results through them. Change the `RecipeStore` signatures as
  planned, but flatten the `Diagnostic` into those records' existing
  `error` string at the `RecipeEditor` boundary. Do not change the records'
  shape; the UI's reducers and native tests match on them.
- **Defer:** changing the two result records' `error` members to
  `std::optional<Diagnostic>`. Revisit after the UI complete-editor
  checkpoint, when the reducers are stable. Record it in the Deferred list.
- **Defer:** A4 step 1 (`FieldCheck` returning `Diagnostic`). UI slice 2A
  is extending `Forms`, `Fields`, `FieldParsing` and `FieldCheck` with units
  and ranges (its first wave already added the integral check at
  `FieldCheck.cpp`). A4 step 2 (`Binding::Problem`) is render-side and safe.

## Findings addressed

- `src/recipe/Recipe.h:820-848`: `Diagnostic{severity, where, message}` with
  `Reporter` is the model contract. `src/studio/Edits.h:287,294` follows it
  (`std::optional<Diagnostic>` and `std::expected<Recipe, Diagnostic>`).
- `src/engine/RecipeStore.h:46-54`: `SaveRecipe` returns
  `std::expected<path, std::string>`; `RevertRecipe`, `NewRecipe`,
  `RenameRecipe`, `AddTransientRecipe`, `DropTransientRecipe` return `bool`
  and send the reason only to `logger::warn` (`RecipeStore.cpp:547-560`).
  `CLAUDE.md` says errors carry a `where` so the menu can show them in place.
  A rename collision is currently unshowable.
- `src/studio/Presets.h:31`: `ParsePresets` returns `std::expected<MaskPresets,
  std::string>`, a bare message with no `where`, first-error bail.
- `src/studio/FieldCheck.h:12-20`: four checkers return
  `std::optional<std::string>` with no `where`.
- `src/render/Binding.h:30,49,116,150`: `Problem(Slot)` returns
  `std::string` with empty meaning success, a fifth representation.
- `src/studio/Presets.cpp:14-34`: `SourceFromJson` hand-reimplements the
  one-key source form and the source-kind table as an if-chain over
  `"material"`, `"bake"`, `"uv"` only. `Presets.cpp:47` onward uses raw
  nlohmann `contains`, `find`, `is_string`, `get`. No unknown-key pass, no
  writer mirror, so the round-trip invariant does not cover presets.
- `src/recipe/RecipeRead.cpp:58`: `Reader` is declared inside an anonymous
  namespace, so the surface `docs/conventions.md:44` advertises as the JSON
  boundary is unreachable from any other translation unit. This is the root
  cause of the Presets fork.
- `src/SettingsFile.cpp:15`: a hand-rolled INI parser with log-and-default.
  Left as is; INI is not JSON and the file has a different failure policy.
  Record that policy in `docs/conventions.md` instead.
- Logging: about 120 `logger::` sites and about 48 `Trace::Emit` sites, 13
  files using both, and `docs/conventions.md` never mentions `Trace`.

## The contract

One rule, stated in `docs/conventions.md` under the Diagnostics heading:

> A failure that a person may need to see is a `Diagnostic` with a `where`.
> A function that can fail that way returns `std::optional<Diagnostic>`
> (nullopt is success) or `std::expected<T, Diagnostic>`. Several failures
> are `std::vector<Diagnostic>` collected through a `Reporter`. No function
> returns `bool` or an empty string to mean "it failed, see the log". Snapshot
> rows carry `std::string problem` as the display projection of a
> `Diagnostic::message`; they are not a place a failure originates.

Logging rule, same section:

> `Trace::Emit` records structured events for the diagnostic trace. `logger::`
> writes the human log. A failure is logged once, at the boundary that turns
> it into a `Diagnostic`, and never again downstream. A file that needs both
> uses `Trace` for state and `logger` for the human sentence.

## Steps

### A1: publish the binders

1. Create `src/recipe/Binders.h` and `src/recipe/Binders.cpp`. Move `Reader`
   (and `Finish()`, the unknown-key pass) out of the anonymous namespace in
   `RecipeRead.cpp` into the header. Move the matching `Writer` from
   `RecipeWrite.cpp` alongside it. The header includes
   `<nlohmann/json.hpp>` and uses `nlohmann::ordered_json` as `RecipeRead.cpp`
   does. Keep every method signature as it is; this is a move, not a
   redesign. Types first, then functions, per convention.
2. Publish a source-kind parser from `RecipeRead.cpp`: declare
   `std::optional<SourceKind> ParseSourceKind(Reader &a_reader)` in
   `src/recipe/Recipe.h` next to `ParseRecipe`, implemented over the existing
   `kSourceParsers` table. Plan B will make that table index-checked; do not
   do that here.
3. Run `tools/compile-db.sh` after adding the files. Update
   `docs/conventions.md:44` so it names `recipe/Binders.h` as the place.

### A2: RecipeStore

1. Change the five `bool` operations in `src/engine/RecipeStore.h` to
   `std::optional<Diagnostic>` and `SaveRecipe` to `std::expected<
   std::filesystem::path, Diagnostic>`. The `where` is `recipe <id>`; the
   message is the sentence that currently goes to `logger::warn`. Keep the
   single `logger::warn` at the point of failure, since the human log is
   still the place a modder without the menu looks.
4. Follow each caller. `src/engine/RecipeEditor.cpp` and the menu pages
   consume these. Read `src/studio/FileOperation.h` and
   `src/studio/EditResult.h`, which already carry results into the snapshot;
   route the `Diagnostic` through them so the recipes page shows it beside
   the row. Do not add a new result type if one of those fits.
5. **Hold back recipes with recipe-level errors** (decided by the user
   2026-09-13). Today `ParseRecipe` at `RecipeRead.cpp:1530-1570` reports
   file-level problems with `where == "file"`, recipe-level problems with
   `where == "recipe"` and sub-scopes such as `clock`, and row problems
   with `signal <name>`, `curve <name>`, `source <name>`, `mask <name>`,
   `output <n>`, `variant <name>`. `Edits.cpp:28-46` spells the same row
   prefixes a second time in its `*Where` helpers.
   - Move the `*Where` helpers (`SignalWhere`, `CurveWhere`, `MaskWhere`,
     `SourceWhere`, `OutputWhere`, `LayerWhere`, `KeyWhere`) from
     `Edits.cpp` into `src/recipe/Recipe.h` beside `Reporter`, and make
     `RecipeRead.cpp` build its `At()` arguments with them so there is one
     spelling.
   - Add `[[nodiscard]] bool RowLevel(const Diagnostic &) noexcept` next to
     them: true when `where` starts with one of the row prefixes. Add
     `LoadResult::HasRecipeErrors()`: any error diagnostic that is not
     row-level.
   - In `RecipeStore::LoadFile` and the publish loop at
     `RecipeStore.cpp:389-394`, keep every parsed recipe in `g_loaded` (the
     menu's set) but push into `g_recipes` (the applied set,
     `LoadedRecipes()`) only when `HasRecipeErrors()` is false. Apply the
     same rule in `Republish` and wherever an edited recipe re-enters
     `g_recipes`, so fixing the error in the menu applies it and
     introducing one withdraws it.
   - Add `bool heldBack` to `Studio::RecipeRow` (`Snapshot.h:181`) set from
     the same predicate, so the recipes page can label the row. The label
     itself is menu work; add the field and tell the UI owner.
   - Test in `tests/recipe/recipe_tests.cpp`: a recipe with a bad `format`
     has `HasRecipeErrors()`; a recipe with one bad signal does not.
   - Record the policy in `REFERENCE.md` under the engine heading: row
     errors make the row inert; recipe-level errors keep the recipe loaded
     but unapplied.

### A3: Presets

1. Rewrite `src/studio/Presets.cpp` on `Reader` and `Reporter`. `where` is
   `preset <name>`. Collect every diagnostic instead of bailing on the first.
   Return `PresetsLoadResult{std::optional<MaskPresets> presets;
   std::vector<Diagnostic> diagnostics; bool HasErrors() const;}` shaped like
   `LoadResult`. Call `Finish()` so unknown keys are errors.
2. Replace `SourceFromJson` with `ParseSourceKind` from A1. Presets then
   accept every source kind the recipe format accepts, which removes a
   silent restriction. If a source kind should not be allowed in a preset,
   reject it with a diagnostic naming the kind rather than by omission.
3. Add `SerializePresets(const MaskPresets &)` in `Presets.cpp` on `Writer`,
   mirroring the reader field for field.
4. Add `tests/studio/presets_tests.cpp` if absent, or extend the existing
   one: round-trip the presets file the plugin ships (find it under
   `presets/`), assert byte-equality after parse and serialize, and feed it
   the malformed-input table from `tests/recipe/recipe_tests.cpp:184-197`
   adapted to presets (unknown key, wrong type, missing name, over
   `kMaxPresets`).
5. Update the `LoadedPresets` path in `RecipeStore.cpp` to log the collected
   diagnostics the way `LogDiagnostics` does for recipes.

### A4: FieldCheck and Binding

1. Change the four checkers in `src/studio/FieldCheck.h` to return
   `std::optional<Diagnostic>`. The `where` is the field's label, which
   `FormField` carries; read `src/studio/Forms.h` to confirm the member.
   Update the callers in `src/studio/Fields.cpp` and the menu form drawer,
   which display `message` and can now also display `where`.
2. Change `Problem(Slot)` in `src/render/Binding.h` and its two overrides to
   return `std::optional<Diagnostic>` with `where` set to the slot's name
   through `SlotName`. Callers that copy it into a snapshot row take
   `->message`.

### A5: conventions

1. Write the two rules above into `docs/conventions.md` under the
   Diagnostics heading. Remove the paragraph that documents the
   `push_back({Severity::…})` exception in `Signals.cpp`; the critique found
   zero remaining instances.
2. Add a paragraph on `Trace` naming `src/diagnostics/Trace.h` and the
   event kinds it records, so the newest module is in the canon document.
3. Record the INI policy (log and default, no diagnostics) in the same
   section so it is a stated exception rather than a fifth idiom.

## Acceptance

- `grep -rn 'std::expected<[^,]*, *std::string>' src --include='*.h' | grep
  -v _old | grep -v extern` prints only `src/recipe/Expression.h` (the
  expression parser's message is consumed and rewrapped by `Reporter`; leave
  it) and nothing in `engine/` or `studio/`.
- `grep -rn 'std::optional<std::string>' src/studio/FieldCheck.h` prints
  nothing.
- No function in `src/engine/RecipeStore.h` returns `bool` to signal failure.
  `IsDirty` and `IsTransient` are queries and stay `bool`.
- `Reader` is declared in `src/recipe/Binders.h` and `RecipeRead.cpp` and
  `Presets.cpp` both include it. No `nlohmann` call appears in `Presets.cpp`
  except through `Reader` and `Writer`.
- Presets round-trip test passes; the recipe round-trip test still passes.
- The row `where` prefixes are spelled once, in `Recipe.h`; `grep -rn
  'std::format("signal {}\|std::format("output {}' src/recipe src/studio`
  prints only the helpers' definitions.
- `HasRecipeErrors()` tests pass: a bad `format` holds a recipe back, a bad
  signal does not.
- Native suite and sanitized suite green; gate push green.

## In-game checkpoint

RecipeStore and the menu's file operations changed. Ask the user to: rename a
recipe to an id that already exists and confirm the refusal appears in the
recipes page beside the row, not only in the log; save a recipe and confirm
the path appears; revert; create a new recipe; and load the game with the
shipped presets and confirm the log reports the same preset count as before.
Then the hold-back rule: put a recipe file with a bad `format` value in the
recipes folder and confirm it appears in the recipes page with an error and
is not applied to the armour, fix the value in place, reload, and confirm
it applies; and confirm a recipe with one bad signal still applies with that
row inert. Give them the log lines to look for: `recipe <id>:` prefixed
lines for the operations, the presets load line from `LoadedPresets`, and
the `recipes: N loaded, M with errors` summary line, which should now also
report how many are held back.

## Out of scope

`Diagnostic` does not gain a `file` member. `RecipeOrigin` in
`RecipeStore.h:33-36` already pairs a path with its diagnostics, which is
the right place for that fact.
