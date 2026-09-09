# Clusters worth one fix each

Three agents read the 322 clang-tidy findings in `tidy-baseline.txt`
against the code, one per layer, and returned causes rather than check
names. Counts are what each change retires from that baseline.

Ordered here by value against risk across all three territories.

## What the twelve have in common

**The pattern is already in the file, applied to some cases and not the
rest.** Seven of the twelve are siblings to finish rather than designs to
invent:

- `Post(a_out, recipe, edit)` exists at `src/ComposePage.cpp:180`; the
  intent collector never got its counterpart.
- `LightRowOf`, `ShellRowOf` and `SourceRowOf` sit at
  `src/Studio.cpp:615,649,700`; the other eight row projections stayed
  inline in `BuildSnapshot`.
- Ten `Bind*` factories exist; fifteen inline lambdas beside them did not
  become the eleventh through twenty-fifth.
- `OutputFrom` walks `kScalarFields` with a member-pointer `Match` at
  `src/RecipeJson.cpp:1264`; `SignalFrom` enumerates its kinds by hand
  for 256 lines.
- `RendererLock` (`src/RuntimeTextures.cpp:536`) is RAII; `SavedState`
  ninety lines above restores through 24 hand-maintained `Release` calls.
- `ParsePresets` caps rows, bones and sources; `NamedRows` in the same
  file caps nothing.
- `Depth` bounds the expression parser's recursion; three other recursive
  walks bound only termination.

A reviewer can therefore check most of this work by comparison against
the sibling that already exists, rather than by argument.

Three shapes recur underneath.

**Behaviour sealed in an anonymous closure where a named record or
function belongs.** The rule line's `std::function`, the form bindings,
the `PostTask` wrappers. This one produces the nesting findings, the
const-capture copies and most of the exception-escape cluster together.

**A closed set enumerated once per pass.** Sixteen signal kinds across
six passes, thirteen vocabulary tables hand-copied into the schema, four
resource tables that are one table, five full-screen passes, one
row-check written three times.

**An invariant kept by discipline rather than by type.** The store's
index parity, `Contribution`'s two index spaces, `SavedState`'s release
list, `Depth`'s implicit copy, the three unbounded recursions.

Mechanically they share a shape: add one named thing, then delete N
variations of it, so the compiler finds every site.

The limit on all of this: converting an invariant into a type costs an
indirection, and both rejections were that mistake — a function-pointer
table on the per-texel `Evaluate`, and per-frame key lookups replacing
pointers `StillOwned` already validates with two comparisons. Make
illegal states unrepresentable at load time and edit time; on per-tick
and per-texel paths keep the check cheap, explicit and tested.

## Land first: small, checkable, and they delete a hazard

**The render thread reads live game-thread state.** `RenderStatus`
(`src/Menu.cpp:351`) runs on the render thread and calls `GetStatus()`,
which iterates `applied_` (`src/Manager.h:189`), an `unordered_map` the
game thread inserts into (`src/Manager.cpp:471`) and erases from
(`:1180`) with no lock. `src/Menu.cpp:245` iterates the recipe store the
same way. `Status` is nine scalars; move it into `Snapshot`, fill it
where `BuildSnapshot` already runs on the game thread, and delete the
public `GetStatus`. `RenderStatus` already receives the snapshot and
ignores it. Costs a one-tick lag in the status line, which belongs in
`REFERENCE.md`. Retires 1 finding and one class of crash.

**`Match` hides a throw inside every `noexcept` function.** `Match`
(`src/Core.h:118`) is `std::visit`, which throws on a valueless variant;
seventeen callers are `noexcept`, so the throw becomes `std::terminate`.
The first rule forbids unchecked `std::get`, and this is the same hazard
wearing a domain name. Replace the body with index dispatch over
`std::get_if` and give the valueless case a defined answer. No signature
changes; none of the call sites move. Retires 5, all on per-tick or
per-texel paths (`src/Signals.cpp:466,474,482`, `src/Expression.cpp:33`,
`src/Recipe.cpp:1466`).

**Every queued command is a lambda inside a lambda.** Each public
`Manager` method wraps its body in `PostTask`, and the editing ones wrap
that again in `WithRecipeRetired`, so the body starts at nesting level 3.
Give each command a private method taking arguments by value
(`DoRevertRecipe`, `DoReloadRecipes`, `DoBeginPaint`, `DoRequestMesh`,
`DoFireAt` with a `FireRequest` record). No engine call moves. Retires 6
(`src/Manager.cpp:901,925,956,980,1057,1086`).

**Intents are posted by building the alternative inside `push_back`.**
`Intent` is a 48-alternative variant and sixty sites construct a
temporary through `a_out.push_back(SoloOutput{...})`. The file already
owns `Post(a_out, recipe, edit)` for the other collector. Add
`void Post(Intents&, Intent)`, one `push_back` in the file. Retires 67 —
40% of the interface layer's findings — for one overload.

## Land next: real structure, native tests cover most of it

**One row-check, three implementations, already diverged.** "Does this
field name a known row of the right type" is written in `Validator`
(`src/Recipe.cpp:733,856`), in `CheckSourceKind` (`src/Edits.cpp:1004`)
and in `EditCheck.cpp:49-105`. They disagree today: the edit layer has no
`MaterialClustersSource` arm, so the editor accepts a cluster count of 0
or a weight of 50 that `src/Recipe.cpp:768` rejects, and the recipe is
refused only on reload. Promote the per-row checks to free functions over
`RowTypes`, which is already the only thing they need. Retires 4 and
fixes a live divergence. `tests/edits_tests.cpp` and
`tests/studio_tests.cpp` assert on refusal wording and will fail where it
moves; read each failure rather than re-baselining.

**Every drawing function re-threads the same ambient context.**
Thirty-four functions in `src/ComposePage.cpp` each carry the same five to
seven arguments — piece, recipe, geometry, actor id, layout, names,
`Intents&` — and several recompute `SignalNamesOf` from the recipe they
were handed. One `Page` record built per frame in `DrawBody`, passed by
const reference, with `ActorOf`, `BonesOf` and `ScaleOf` as free
accessors. Retires 34. Three sites deliberately draw something other than
the ambient selection and keep an explicit parameter: `AddTermOfKind`
(`:1597`), `DrawBody` (`:1979`), `DrawSignalDetail`.

**Sixteen kinds spelled out once per pass.** Parse, serialize, type,
evaluate, editor rows and schema each enumerate the signal and source
kinds by hand; four of the six longest functions in the repository are
that enumeration. `OutputFrom` (`src/RecipeJson.cpp:1264`) already shows
the answer one level down, walking `kScalarFields` with a member-pointer
`Match`. One dispatch table per closed set carrying the per-kind
functions, in `RecipeJson.cpp`'s anonymous namespace so `Vocabulary.h`
stays free of json. Retires 10 there, 15 if extended. Confirm every kind
appears in a fixture before starting.

**A rule line's buttons are a closure.** `Widgets::RuleLine`
(`src/MenuWidgets.h:110`) holds a `std::function<void()>` plus a
hand-computed width, so each builder sums `ButtonWidth` terms by hand and
writes its buttons inside a returned lambda, three levels deep before its
first `if`. Make the right-hand side data: `RuleButton` records, a span,
and `Rule` returning the index clicked. Retires 5 alone, 7 with the `Page`
record.

**Bindings capture const entities, so no closure can be moved.**
Twenty-three lambdas in the form layer capture a `const std::string&` or
`const Selection&` by copy, which makes the closure member const and
forces `std::function` to copy every captured string and vector. Route
them through named `Bind*` factories taking captures by value with
init-capture — ten such factories already exist — and finish each
`FormField` with designated initializers instead of positional init plus
`form.back()` patching. Retires 26 and removes the reason the trailing
defaults were added to `src/Forms.h:128`. Highest verification cost here:
no native test covers `src/Studio.cpp`.

## Land when the game is available

**The actor state has no reader.** `ActorState` is five levels deep with
levels cross-linked by bare `std::size_t`, and one type carries two index
spaces: `Contribution::placed` (`src/Merge.h:20`) means an index into
`bound.placements` from `PlanGeometry` and into `state.instances` from
`PlanLights`. `BuildSnapshot` (`src/Manager.cpp:1365`) both walks all five
levels and inlines eight of the eleven row projections; the other three
were already extracted to `src/Studio.cpp:615,649,700`. Half A moves the
remaining projections out as pure functions — no engine risk. Half B
flattens the geometry level and splits `Contribution` into
`SlotContribution` and `LightContribution` over `enum class` index types.
Retires 6. Half B needs two actors wearing recipes at once to exercise.

**Five copies of the full-screen pass.** `TextureLab::Render`,
`RenderClusters`, `RenderProgram`, `BakeMesh` and `RenderRipple` repeat
the same 28-line sequence, and `SavedState` (`src/RuntimeTextures.cpp:439`)
is a hand-written capture whose `Restore` ends in 24 hand-maintained
`Release` calls, beside a `RendererLock` that is properly RAII. Make
`SavedState` RAII with a `Com<T>` holder, then a `FullScreenPass` record
and one `Draw`. `BakeMesh` differs — input layout, vertex and index
buffers, `DrawIndexed` — and forcing it in is how this breaks. Retires 3
and about 110 lines. A leak shows as frame rate decaying over minutes,
not as a log line.

**The store keeps two copies and hands out raw pointers into one.**
`g_loaded` and `g_recipes` are kept in step by index, and `Republish`
already carries a `logger::error` for them disagreeing. Everything
downstream holds raw pointers into the second vector, kept valid by
discipline that ten call sites currently observe correctly.
`shared_ptr<const Recipe>` as the published form deletes `g_recipes`,
`Republish`, `Publish` and `Unpublish`, and makes `PreparedLayer::layer`
valid by construction. Retires 0 findings and 3 raw pointer members. The
failure mode is quiet: an instance renders the old recipe forever.

## Bounds that look present and are not

Only the expression parser bounds its recursion. `src/Signals.cpp:255`
and the two mutual recursions in `src/Recipe.cpp` (`:556`/`:572`,
`:1206`) bound termination with a visited set and never bound depth, and
`NamedRows` (`src/RecipeJson.cpp:1452`) caps no row count while
`ParsePresets` does cap. Same shape as the `Depth` copy defect already
fixed. No adversarial fixture exists; that absence is part of the
finding.

## Two smaller finds

`src/Signals.cpp:319` is an empty `if (n.curve && n.type !=
ValueType::kScalar) { }` — a validation deleted or never written.

`ParsePresets` (`src/RecipeJson.cpp:1761`) has the highest cognitive
complexity in the repository and is written in a second JSON dialect,
raw `find`/`contains`/`operator[]` instead of the `Reader`/`Ctx`
vocabulary the other 1700 lines use, with `SourceFromJson` at `:1737` a
third partial copy of source-kind parsing.

## Rejected, with the reasons

**An opcode table for `Expression::Check` and `Evaluate`.** Worth 4
findings. `Evaluate` is the per-texel path; the switch compiles to a jump
table with inlined kernels and a function-pointer table forbids both.
Both switches are long but flat. The metric is complaining about a shape
that is correct.

**Splitting `Perform` (`src/ComposePage.cpp:59`) and `Reduce`
(`src/MenuState.cpp:89`).** Worth 4 findings, and they are the fattest
single targets. They are long because `Intent` has 48 alternatives and
each gets a one-line arm: the length is the intent list. The exhaustive
`Match` is what makes the compiler demand an arm when someone adds an
intent, and splitting trades that for a hand-maintained dispatch layer.

**Driving the 61 raw engine pointer members to zero.** They are five
populations: 2 regex false positives matching `return *v;`, 9 `const
char*` to string literals, 18 D3D COM handles owned for the process
lifetime, 7 per-call parameter structs, and about 10 real ones — 6 of
which are already guarded by `SlotWriter::StillOwned`
(`src/Binding.cpp:341`), a destructor that refuses to restore when the
check fails, and `DropLostGeometries` logging `dropping '{}': its
material or shell was replaced by another system`. Replacing those with
keys would mean a per-frame lookup to learn what two comparisons already
know. Fix the check, not the code.

**The 34 `bugprone-exception-escape` findings.** `~LightBinding`
(`src/Binding.cpp:877`) fires because it calls the engine through
`REL::Relocation` function pointers that carry no exception
specification, which no annotation short of lying removes. The rest are
`noexcept` interface lambdas that allocate, where allocation failure
inside a Skyrim process is already fatal.

**The 78 `modernize-use-emplace` findings** outside cluster 1's sixty:
mostly menu-draw row building, no measurable frame cost, and applying
them churns readable code.
