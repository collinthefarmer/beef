# Readability tooling roadmap

Six phases, ordered so that everything free runs before anything that
costs a refactor. Each phase says what it proves, because a check nobody
can act on is a check that gets muted.

## The tree is frozen

On 2026-09-09 the 72 sources moved to `src/_old` and stay there, building
and shipping, as the reference implementation. Each wave writes new code
into `src/recipe`, `src/mesh`, `src/engine`, `src/render`, `src/studio`
and `src/menu`, and deletes the frozen files it replaces.

The measurements below describe the frozen tree, reproducible with
`python3 tools/readability.py --frozen`, and recorded in
`docs/wip/tidy-baseline-frozen.txt`. Every tool now measures new code by
default and reports zero until there is some. That is the point: a wave
is assessed on what it wrote, not on what it inherited.

## Baseline of the frozen tree, taken 2026-09-09

72 files, 27,316 lines, 1,237 functions. Function length: median 8,
p95 48, max 256.

| check | today | target |
| --- | --- | --- |
| functions nesting over 4 | 32 | falling |
| functions over 60 lines | 33 | falling |
| functions over 4 parameters | 70 | falling |
| both long and deeply nested | 14 | 0 |
| bare literals in argument position | 223 | falling |
| vocabulary drift findings | 66 reported, unvalidated | see Phase 6 |
| comments | 0 | 0 |
| `auto` in a signature | 2 | 0 |
| raw `new` / `delete` | 5 | 0 |
| raw engine pointer members | 61 reported, ~10 real | see Phase 3 |
| `default:` labels in switches | 12 | 0 |

The single worst function is `BuildSnapshot` at `src/Manager.cpp:1376`,
234 lines at 10 levels of nesting. The tightest co-change pair is
`ComposePage.cpp` with `Studio.cpp`, 27 commits out of the last 400.

## Phase 0 — measure, change nothing

`tools/readability.py` for the per-file metrics, `tools/lint.sh` to run
everything and print one report, `.clang-tidy` for the check selection.
Run `tools/lint.sh` and keep the numbers above as the starting line.

## Phase 1 — build flags, no code changes

`CMakeLists.txt:108-114` applies `-Wno-overloaded-virtual`,
`-Wno-inconsistent-missing-override`,
`-Wno-delete-non-abstract-non-virtual-dtor` and
`-Wno-reinterpret-base-class` to both the plugin and CommonLibSSE. Those
four describe CommonLibSSE, so scope them to that target and let the
plugin's own code be warned about. The native test targets already build
at `/W4` (lines 140 and 147); the plugin target has no raised level.

Then add `-Werror=switch` with `enum class` and no `default:` label, so
an unhandled variant fails the build instead of falling through.

Then `-fsanitize=address,undefined` on the native test build only. The
game cannot run under a sanitizer; the 11,739 lines that compile without
it can.

Proves: nothing about design. Finds real defects for the price of a flag.

## Phase 1 result, 2026-09-09

Done. `CMakeLists.txt` now scopes the four suppressions to a
`COMMONLIBSSE_SUPPRESSIONS` list on that target alone and adds
`-Werror=switch` to the plugin. `tests/run-native.sh` takes
`BEEF_SANITIZE=1` and builds into a separate output directory.

Scoping the suppressions produced no new warnings in the plugin's own
code, so all four described CommonLibSSE and the plugin had been
under-warned for nothing. `-Werror=switch` built clean, so every switch
over an enum is exhaustive or carries a default.

**Two memory errors, found on the first sanitizer run.**

`src/RecipeJson.cpp`, in `SourceFromJson`, bound a structured binding to
`*a_value.items().begin()`. `items()` returns a proxy by value, so `key`
and `value` referred to a destroyed temporary. This runs at load time in
the game, because `ParsePresets` reads the shipped `regions.json`. Fixed
by taking the object iterator directly. The pattern appears nowhere else.

`tests/recipe_tests.cpp` held a pointer from `FindSignal` into a `Recipe`
temporary that died at the end of the statement. Fixed by naming the
value.

**Warnings went from 17 to 1.** Thirteen missing field initializers came
from three trailing members of the field record in `src/Forms.h` having
no default; one unused parameter in `src/Studio.cpp` is now marked
`[[maybe_unused]]`. Two functions were dead and are gone: `OutputAt` in
`src/Manager.cpp` and `NamesSignal` in `src/Edits.cpp`. That is Phase 5's
dead-surface pass, done early and for free by the compiler.

The one remaining warning is `-Wreturn-type` at
`src/extern/SKSEMenuFramework.h:7381`, a non-void function that can fall
off its end. It is vendored third-party code, so the choice is to patch
it in tree or suppress it for that header alone.

**Toolchain notes for the sanitizers.** `g++` cannot build this code
under AddressSanitizer; instrumentation breaks a constexpr evaluation at
`src/Analysis.h:31`. Inside the dev shell `BEEF_SANITIZE=1
tests/run-native.sh` builds with the `NATIVE_CXX` clang, and the run stops
with a message if it would land on `g++`. That clang does not link the
UndefinedBehaviorSanitizer vptr runtime, so the flags carry
`-fno-sanitize=vptr`.

## Phase 2 — clang-tidy

`tools/compile-db.sh` writes `build/clangd/compile_commands.json`, which
is the only input needed. Regenerate it after any change to
`CMakeLists.txt`, because clang-tidy reads the flags from there and a
stale database lints with the old ones.

Run `tools/tidy.sh`, or `tools/lint.sh --tidy` which delegates to it.
Raw results land one file per source under `build/tidy/`, so a run that
dies resumes where it stopped. That directory is ignored by git, so
`tools/tidy-baseline.sh` folds it into `docs/wip/tidy-baseline.txt`, one
sorted `path:line: [check]` per finding. That file is what makes "no new
findings" checkable: `tools/tidy-baseline.sh --check` fails when the tree
and the baseline disagree, and its diff names what changed. Flags: `--force` redoes them, `--changed` limits the
run to the working diff, and `--summary` reports what is already on disk
without running anything. Triage once, fix or silence per site, then keep
it at zero new findings with `tools/lint.sh --tidy --changed` before a
commit.

Three constraints on this machine, each learned by hitting it:

- **Use the unwrapped toolchain.** A wrapped clang injects Linux glibc
  and libstdc++ include paths that collide with the Windows
  cross-compile database and produce about 85,000 spurious errors. The
  dev shell puts the unwrapped `clang-tidy` on `PATH` and names it in
  `$CLANG_TIDY`, which `tools/tidy.sh` honours.
- **Resolve the binary once.** Four runs were killed for memory before
  the cause turned out to be the script re-resolving its toolchain for
  every file; that resolution is what consumed the memory, not linting.
  clang-tidy itself peaks at 710 MB on `src/Timing.cpp` and 1.37 GB on
  `src/ComposePage.cpp`, the largest file in the tree, against about 14 GB
  free. `tools/tidy.sh` takes `--jobs=N`; three at a time fits comfortably.
- **The analyzer runs separately** into `build/tidy-analyzer/` via
  `tools/tidy.sh --analyzer`, on the assumption that a path-sensitive
  family costs more. That assumption is untested; the earlier kill blamed
  on it was the toolchain resolution.

`.clang-tidy` enables `bugprone-*` and `clang-analyzer-*` in full, the
five `cppcoreguidelines` checks that bear on ownership and construction,
`performance-*`, and the `readability-function-size` and
`readability-function-cognitive-complexity` checks with thresholds
matching `tools/readability.py`. The analyzer checks are the ones worth
the runtime: they follow branches, so they reach leaks and null
dereferences that only happen on an error path.

Once those two `readability` checks are green, delete the `nesting`,
`length` and `params` metrics from the script. They approximate on a
brace scanner what clang measures on a real AST. The script keeps what
clang will never know: vocabulary drift, abbreviations, mechanics
density, and later the glossary and citation metrics.

## Phase 2 result, 2026-09-09

All 32 sources linted, 322 findings.

| check | count |
| --- | --- |
| readability-function-size | 121 |
| modernize-use-emplace | 78 |
| readability-function-cognitive-complexity | 48 |
| bugprone-exception-escape | 34 |
| performance-inefficient-vector-operation | 11 |
| readability-simplify-boolean-expr | 6 |
| modernize-use-nodiscard | 6 |
| performance-* (four other checks) | 11 |
| cppcoreguidelines-special-member-functions | 2 |
| everything else, one each | 5 |

`ComposePage.cpp` holds 118 of the 322, `Studio.cpp` 48, `RecipeJson.cpp`
23. The same three lead on nesting, parameter counts and co-change.

**One real defect, fixed.** `Depth` at `src/Expression.cpp:190` holds a
`std::size_t&`, increments it in its constructor and decrements it in its
destructor, with an implicitly defaulted copy constructor. A copy
decrements twice against one increment and underflows the counter that
enforces the parser's nesting limit, which the first rule requires.
Nothing copies it today, so this was latent. Its copy operations are now
deleted.

The rule-of-five check found exactly the two RAII types predicted before
it ran. The other one, `RendererLock` at `src/RuntimeTextures.cpp:536`,
already deletes its copy operations, which suppresses moves as well, so
that finding is completeness rather than hazard.

**Four singletons examined and left alone.** `TexelBand` at
`src/Analysis.cpp:386` is aggregate-initialized at every use, so the
uninitialized `axis` cannot occur. `Identity::kName.data()` at
`src/RuntimeTextures.cpp:645` is a view over a macro-defined literal and
is NUL-terminated. The widening multiplication at
`src/RuntimeTextures.cpp:1090` is inside a `static_assert`. The
`gsl::owner` finding at `src/RuntimeTextures.cpp:762` is one of the five
raw allocations already tracked for Phase 4.

**Two clusters left as decisions.** The 34 `bugprone-exception-escape`
findings are mostly `noexcept` lambdas that allocate, where an escape
calls `std::terminate`; whether allocation failure inside a game process
is worth defending against is a policy call, not a fix. The 78
`modernize-use-emplace` and 22 `performance-*` findings are cleanup with
no correctness content, and mass-applying them would churn a lot of
readable code.

The 169 size and complexity findings are the same signal
`tools/readability.py` already reports, measured on a real AST. They stay
duplicated until Phase 4 brings them down, at which point the script's
`nesting`, `length` and `params` metrics can go.

## Phase 3 — hold the preconditions at zero

The precondition counts are the surface where a bug class can exist, not
the bugs. They are worth tracking because they can be driven to zero and
then re-verified on every commit, which a missing restore cannot.

Replace the greps in `tools/lint.sh` with `clang-query` against the
compile database. This matters more than it looked. The 61 raw engine
pointer members are five populations, not one: two are regex false
positives matching `return *v;`, nine are `const char*` to string
literals in settings and interface tables, eighteen are D3D COM handles
owned for the process lifetime in `TextureLab`, seven are per-call
parameter structs that live for one call on the game thread, and about
ten are the population the metric was aimed at. Six of those ten sit in
`src/Binding.h` and are already guarded: `SlotWriter::StillOwned`
(`src/Binding.cpp:341`) re-checks that the property still holds the
material and that each written texture is still the one it wrote,
`MaterialBinding::~MaterialBinding` refuses to restore when that fails,
and `Manager::DropLostGeometries` retires the binding and logs
`dropping '{}': its material or shell was replaced by another system`.

So the number was measuring the regex, not the hazard. The matcher takes
non-static data members of pointer-to-class type, excluding `const char*`
and the `REX::W32::` namespace, and the target is what remains.

Add three more: a destructor with a body on a type that is still
copyable, an error or `expected` return without `[[nodiscard]]`, and an
`enum class` switch carrying a `default`.

## Phase 4 — structural changes, each retiring a check

Ordered by what each one buys. A phase is done when its precondition
count reads zero.

1. **Store keys, not pointers.** A placement holds a form ID and a node
   name; lookup returns `std::optional`. Retires the 61 raw engine
   pointer members and makes a stale pointer unrepresentable.
2. **Pair every write with its undo in one object.** Applying returns an
   owner whose destructor restores, so a failure part way through unwinds
   itself. The restorer stores a token of what it wrote and restores only
   on a match, because `dropping '{}': its material or shell was replaced
   by another system` says the state can be taken.
3. **Thread affinity through `-Wthread-safety`.** Model the game thread
   as a `CAPABILITY` and mark the state it owns `GUARDED_BY`. These are
   attributes, not comments. Turns an ownership rule the reader cannot
   check into one the compiler does.
4. **A variant per output slot.** `EmissiveOutput{strength}`,
   `HeightOutput{scale}`, `FuzzOutput{color, weight}` in place of a slot
   enum beside optional fields, mirroring what the schema already
   enforces with `if`/`then`.
5. **A parse boundary with access control.** `Recipe` gets a private
   constructor and a static `Parse` returning `expected`, so no code
   downstream can hold an unvalidated recipe.
6. **One named factory per engine object.** Retires the five raw
   allocations and states the ownership transfer in a return type.

## Phase 5 — the passes no tool runs

Scheduled reading, each producing a written result.

- **Symmetry.** Read the handler for one signal kind, predict the next.
  Where the prediction holds, a reader learns seventeen kinds by reading
  one.
- **The shear test.** Add a signal kind and count the files touched. This
  is documentation question 33, and the answer is the extension contract.
- **Dead surface.** Exported names with no caller, INI settings nothing
  reads, enum variants nothing constructs.
- **Constants.** Every numeric literal outside 0, 1 and 2 is named,
  derived, or explained under its module's heading in `REFERENCE.md`.
- **One-sentence responsibility.** For each module, write the sentence.
  The ones that resist are the confused ones.

## Phase 6 — the metrics that need the documentation

These wait on Stage 2 and Stage 3 of `method.md`.

- **Citation spread per question.** Group the fact base's citations by
  the question they answer. One file behind a question means the code
  carries the answer; six means it does not.
- **Glossary coverage.** The share of identifiers in the engine-free
  modules that are a glossary term, ordinary English, or a standard
  library name. The remainder is the entry cost of the codebase.
- **Vocabulary drift against the real glossary.** The current drift
  groups in `tools/readability.py` are guesses. Once Stage 3 orders the
  vocabulary, replace them with the actual terms.
