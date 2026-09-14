# Plan E: documentation that matches the tree — 2026-09-13

## Status

Implemented 2026-09-14 on `critique/e-docs`, branched from `cleanup/stage-0`
at `c357f26`. Five commits: `ea88b12` (E1), `4e69da4` (E2), `314bb99` (E3),
`1c0c3da` (E4), `3ab6c3f` (the tidy baseline E4's line shifts moved), and
`ca035bc` (this block, the handoff's row and log).

### What was done

**E1.** `README.md` (93 lines) states what the plugin does for a player and for
a recipe author, the reading order as a table with one line per document saying
what question it answers, the three commands and `./install.sh`, where recipes
and presets live at runtime, and the source layout. `docs/README.md` indexes
every file under `docs/` and `docs/wip/` exactly once, grouped as canon, active
plans, checkpoints and evidence, and history. Fourteen history documents gained
a one-line status header naming what superseded them, following
`docs/wip/render-ownership.md`'s pattern. `CLAUDE.md`'s reading order became a
pointer to the README, so there is one copy.

**E2.** `REQUIREMENTS.md`: the directory heading counts eight, the artifact list
counts four (the same class of error, found while checking the first), the
signal kind is `av` with the sentence naming it the community's abbreviation,
and a module is done when its frozen counterpart leaves the build rather than
`src/_old`, which stays until the roadmap's release gate. `docs/wip/deletions.md`
gained a status header: the settings citation carries its `src/_old/` path, the
two citations into the deleted root readme and architecture document are
dropped, and the entries a grep of the active tree found already resolved are
listed. The `ShellPose` entry says the fields are implemented by Plan G; the
`where` presets entry says Plan A resolved it. `REFERENCE.md`: the preamble
names the four cited sources that are not in the repository (`NOTES.md`,
`ARCHITECTURE.md`, `reference/`, `decompiled/`), the lab heading names its four
files, the compositor heading its three, the menu-mechanics heading the files
that exist; `Vocabulary.h` reads `Words.h` and `IsAnimated` reads
`recipe/Vocabulary.cpp`. Paint's term templates moved under a new `## History`
heading, since `Paint.cpp`, `Region.cpp` and `TermKind.h` live only in
`src/_old`. `docs/conventions.md`'s list of `recipe/` `.cpp` homes matches Plan
C's split. `presets/regions.json` became `presets/presets.json` by `git mv`,
with `CMakeLists.txt`, `src/Identity.h`, `tests/studio/presets_tests.cpp` and
`REFERENCE.md` following; the two ownership docs named the vocabulary, not the
file, so neither needed an edit. Plan A's conventions edits (the `Reporter`
rule, the `Trace` paragraph, the `SettingsFile.cpp` exception) and Plan B's
source-kind checklist under the variants heading were both verified present.

**E3.** Each value was searched in the order the plan gives, stopping at the
first hit: `src/_old`, then `git log -S`, then the pre-strip tree (`667bb88`,
the commit before `adac71d` moved every comment into `REFERENCE.md`). The
`reference/` and `decompiled/` trees the plan names are not in the repository
and never were, so that step could not run; the pre-strip tree carried the
citation that step was meant to find.

| value | file | reason | citation |
|---|---|---|---|
| `kIslDefaultCutoff` 0.05, `kIslShadowCutoff` 0.022 | `render/Light.cpp` | ISL's own default cutoff, and its cutoff for a shadow-casting light | `667bb88:src/Binding.cpp:42` "ISL's radius formula constants (CS InverseSquareLighting.cpp)"; frozen at `src/_old/Binding.cpp:38-39` |
| the 0.5 in `0.5f / std::max(mean, 0.05f)` | `render/CompositorSource.cpp` | normalise a colour field to mid grey | `667bb88:src/Compositor.cpp:300`; frozen at `src/_old/Compositor.cpp:273` |
| `kMaxExpressionOps` 256, as a coupling | `recipe/Expression.h` | pinned to the interpreter shader's bare literal `float4 code[256]` | `render/ShaderSource.cpp:123` |
| the two `format` failures | `recipe/RecipeRead.cpp` | missing: report and continue, so the author sees every problem at once. newer: report and stop, because reading it would produce noise about this loader | the code; the frozen `RecipeJson.cpp` had no comment either |
| `kMaxExpressionOps` 256, as a magnitude | `recipe/Expression.h` | **no trace** | carried unchanged from `src/_old/Expression.h:17` |
| `kMaxMaskDepth` 8 | `render/CompositorSource.cpp` | **no trace** | carried unchanged from `src/_old/Compositor.cpp:675` |
| flatness margins 0.02 and 0.98 | `render/CompositorSource.cpp` | **no trace** | carried unchanged from `src/_old/Compositor.cpp:92` |
| the 0.05 floor under the normalisation mean | `render/CompositorSource.cpp` | **no trace** (it caps the factor at ten, which the code shows; why ten is not recorded) | carried unchanged from `src/_old/Compositor.cpp:273` |
| the freeze epsilon `a_time + 0.001f` | `engine/ManagerTick.cpp` | **no trace** for the magnitude. 0.001 is one millisecond in the seconds base the same function builds from `(nowMS - startMS) * 0.001f` | carried unchanged from `src/_old/Manager.cpp:1139`; `667bb88:src/Manager.cpp:1135-1138` explains the rebuild, not the epsilon |
| `kMaxTextFileBytes` 4 MiB | `engine/TextFile.h` | already recorded by Plan F | `REFERENCE.md`, engine heading |
| the biped slot range 30..61 | `recipe/Recipe.h` | already recorded by Plan A | `REFERENCE.md`, mesh heading |
| `kPlaceholderTextureExtent` 4 | `planners/TextureIdentity.h` | already recorded by Plan A | `REFERENCE.md`, planners heading |

Each of the five untraced values has a `REFERENCE.md` line that gives the value,
the file, what it does and where it came from, and says the reason is not
recorded. The batched question to the user is in the implementer's report; no
derivation was invented and the lines are written so the answer replaces the
"not recorded" sentence without touching anything else.

**E4.** The count before this pass was 40 line-start comments, not the plan's
fifty: Plan D had already moved three, and the tree has no trailing comments
(`grep -rn ' // '`) and no block comments (`grep -rn '/\*'`) outside the frozen
and vendored trees. Thirty-eight lines are gone. Thirteen comments stated a fact
the code cannot and moved to `REFERENCE.md` under their module: the intrusive
refcount sample in `SweepRetiredMaterialTextures`, the allocation-free `splice`
at retirement, why `SlotWriter::Restore` writes only through an attached
material and why `~MaterialBinding` leaves the private material in place,
`CopySkinData`'s ownership split, `SkinPaletteLease`'s teardown order, the
address-library resolve before the light attaches, `RenderTarget`'s member
order, `TextureRef`'s two constructor kinds, `OwnedState`'s equality test and
its blind spot, `ConsumptionLeases`' acknowledgement rule, the two expression
stacks, and `CollectPieceGeometries`' all-or-nothing piece. Three said what
`REFERENCE.md` already said (the write parity in `Compositor.cpp`,
`ChangeAndRebuildActors` in `Manager.h`, the `Evaluator`'s shared context in
`Signals.cpp`) and are simply gone.

The `NOLINTNEXTLINE(cppcoreguidelines-owning-memory)` in `render/Binding.cpp`
was removed by restructuring: `SlotWriter::RetiredTextures` no longer leaks a
raw `new` but holds a never-destroyed local union named `ImmortalList`, which
says through its name why the list outlives the plugin's statics and satisfies
`CLAUDE.md`'s no-raw-`new` rule at the same time. clang-tidy reports zero
findings for that file afterwards.

The one exception: `src/engine/RecipeStore.cpp:21` keeps
`NOLINTNEXTLINE(bugprone-exception-escape)` on `struct LoadedRecipe`. MSVC's map
move can allocate, so the aggregate's implicit move is not `noexcept`;
clang-tidy reads it as `noexcept` and then reports that it can throw. The only
restructurings that would silence it are making `Studio::ReferenceCounts`'
move `noexcept` or changing its container, both of which are studio design
changes outside this plan and neither of which the finding actually argues for.
The `static_assert` below the struct is the real check and its reason is now in
`REFERENCE.md` under the engine heading.

The push stage of `tools/gate.sh` now greps `src/` for a `//` that begins a line
or follows whitespace after a `;` or `}`, and fails on a hit. It excludes
`src/_old`, `src/extern` and `src/cs` (frozen or vendored), `NOLINT` directives,
and `src/studio` and `src/menu`, the last with a dated note to delete them from
the pattern after the UI complete-editor checkpoint. `docs/conventions.md` gained
a Gates bullet for it.

### Deferred

To the UI v2 plan's complete-editor checkpoint, per this plan's UI rework
impact section:

- `REFERENCE.md`'s "Menu mechanics" section content. Its heading now names the
  files that exist (`menu/MenuWidgets.cpp`, `studio/Intent.h`, since `MenuState`
  is still in `Intent.h` pending UI slice 1B) and carries a note saying the body
  is rewritten after that checkpoint. The body still describes the compose/paint
  mode bar and the field-key scheme the rework is replacing.
- The two comment lines in `src/menu/MenuWidgets.cpp:89-90` (the DX11 backend's
  consumption acknowledgement). `src/studio` carries none, so the studio half of
  the exclusion is precautionary.
- Deleting `studio` and `menu` from the gate's exclusion pattern
  (`tools/gate.sh`) and from the Gates bullet in `docs/conventions.md`.
- The `REFERENCE.md` studio heading's header list, which is inside a UI seam and
  was left alone; every header it names exists, but the rework has added others.

### Acceptance

| check | command | result |
|---|---|---|
| the README and the index exist | `ls README.md docs/README.md` | both |
| every docs file indexed once | a loop over `git ls-files docs` grepping `docs/README.md` for `(<path>)` | no misses; `.obsidian/` editor config and the `regression-evidence/` leaves are covered by their directory line, not one line each |
| REQUIREMENTS claims | `grep -n 'Seven directories\|actorValue' REQUIREMENTS.md` | nothing |
| deletions.md citations | `grep -n 'SettingsCore\|ARCHITECTURE.md' docs/wip/deletions.md` | one line, `29:... \`src/_old/SettingsCore.h:19-89\`` — the repoint step 4 offers, which the literal grep cannot distinguish from the broken citation it replaced |
| REFERENCE headings | `grep -n 'RuntimeTextures.cpp\|Paint.cpp\|Region.cpp\|TermKind.h' REFERENCE.md` | one line, 1166, under `## History` |
| every constant has a line | grep for each value in `REFERENCE.md` | `0.022`, `kMaxMaskDepth`, `0.98`, `0.02`, `0.05`, `0.001`, `kMaxExpressionOps`, `'format' is required`, `4 MiB`, `kPlaceholderTextureExtent` all present |
| the comment grep | the gate's own pattern | nothing |
| the presets rename | `grep -rn 'regions.json' . --exclude-dir=_old --exclude-dir=.git --exclude-dir=build --exclude-dir=dist` | eleven lines, all in this plan file, Plan A's finding and the handoff's decision record, where the old name is the subject. `presets/presets.json` exists |
| native suite | `tests/run-native.sh` | 66 suites green, exit 0 |
| sanitized suite | `BEEF_SANITIZE=1 tests/run-native.sh` | green, exit 0 |
| layers | `tools/layers.sh` | "every include stays inside the graph", exit 0 |
| DLL | `./build.sh Release -j 4` | links, 106 targets |
| commit gate | the hook on every commit | green; "staged files: no new clang-tidy findings" |
| tidy baseline | `tools/tidy.sh && tools/tidy-baseline.sh` | 57 findings before and after; seven rows moved by deleted lines, no count changed |

`tools/gate.sh push` was not run; it is optional per plan and mandatory once
after the last plan. `./install.sh` and the in-game checkpoint are batched into
that final pass.

### In-game checkpoint (batched)

The presets rename is the only behaviour change. After `./install.sh`:

1. Delete the stale
   `/mnt/a/mods/SkyrimSE/mods/BetterEnchantmentEffects/SKSE/Plugins/BetterEnchantmentEffects/regions.json`
   by hand. The installer no longer stages it and there is no fallback, so it
   would otherwise sit there unread. (`dist/` was cleaned of it already.)
2. Load a save and look for the `presets:` line the plugin logs, which reads
   `presets: <n> mask presets from Data\SKSE\Plugins\BetterEnchantmentEffects\presets.json`.
   `<n>` must match the count the previous build reported from `regions.json`.
   A `presets: ... does not exist; no mask presets` line means the new file did
   not install.

Covers critique recommendation 8: a README, an index for `docs/`, and the
correction of every stale claim the critique found, plus moving the fifty
remaining source comments into `REFERENCE.md`. Runs last so names and paths
are final. Three items need the user's decision; they are marked.

## UI rework impact

- **Safe now:** E1 (README and index), E2 steps 1 to 4 and 7
  (REQUIREMENTS fixes, `deletions.md`, REFERENCE headings), E3 (constants
  into REFERENCE), E4 for files outside `src/studio/` and `src/menu/`.
- **Index the UI documents as active work:** `docs/ui-v2-proposal.md`
  (frozen baseline), `docs/ui-v2-implementation-plan.md` (implementing),
  `docs/ui-assessment-2026-09-13.md`, `docs/ui-design-principles.md`,
  `docs/wip/ui-v2-framework-checkpoint-2026-09-13.md`, and
  `docs/wip/ui-primitives-ownership.md`. Do not add status headers to them;
  they carry their own and belong to the UI owner.
- **Safe now:** the `presets.json` rename (E2 step 6). The UI proposal
  reintroduces "region" for armor coverage areas (section 4.7, slice 4C),
  so the rename removes a collision before 4C creates it. Tell the UI owner
  the file's new name; slice 3F reads it for the pattern chooser.
- **Safe now:** E2 step 5 is a doc edit only, since `ShellPose` is
  implemented by Plan G rather than removed. The REFERENCE section at `:685`
  ("Menu mechanics") describes code the UI rework is replacing; correct
  the file names now, rewrite the content after the complete-editor
  checkpoint. E4 comment removal inside `src/studio/` and `src/menu/` waits
  for the same checkpoint; the gate check in E4 step 3 should therefore
  start with those two directories excluded and a dated note to remove the
  exclusion.

## Findings addressed

- No README. The reading order lives in `CLAUDE.md:3-12`, an agent
  configuration file.
- About twenty-five documents under `docs/wip/` and eight under `docs/` are
  unindexed. Two are referenced from `CLAUDE.md`, three from
  `REQUIREMENTS.md`.
- `REQUIREMENTS.md:126`: "Seven directories" above a list of eight.
- `REQUIREMENTS.md:98`: names the signal kind `actorValue`; the schema and
  `src/recipe/Words.h:142` say `av`.
- `REQUIREMENTS.md:241-242`: says the frozen files a module replaced are
  gone from `src/_old`. `src/_old` is intact.
- `docs/wip/deletions.md:13-15`: cites `src/SettingsCore.h:19-89` and
  `ARCHITECTURE.md:386`. Neither exists.
- `docs/wip/deletions.md:37-40`: flagged five dead `ShellPose` fields.
  `src/recipe/Recipe.h:647-655` carries `offset`, `scale`, `scalePoint`,
  `spin`, `spinAxis`, all parsed, and `src/render/Shell.cpp:558` poses on
  `inflate` alone. `REQUIREMENTS.md:46-47` says dead code is never written.
- `REFERENCE.md:170`: heading names `RuntimeTextures.cpp`, now four files
  (and renamed by Plan D). `:662`: names `Paint.cpp`, `Region.cpp`,
  `TermKind.h`, none in the active tree. `:685`: names `MenuState.h`, which
  Plan D creates; verify the section content matches it.
- Constants with no recorded reason: `kIslDefaultCutoff = 0.05f` and
  `kIslShadowCutoff = 0.022f` (`src/render/Light.cpp:37-38`);
  `kMaxMaskDepth = 8` (`src/render/CompositorSource.cpp:737`, `REFERENCE.md:618`
  says only "bounded at preparation"); the flatness thresholds `0.02f` and
  `0.98f` (`CompositorSource.cpp:54`); the normalisation `0.5f /
  std::max(mean, 0.05f)` (`CompositorSource.cpp:329`); `a_time + 0.001f`
  (`src/engine/ManagerTick.cpp:316`); `kMaxExpressionOps = 256`
  (`src/recipe/Expression.h:16`); the format-version policy at
  `src/recipe/RecipeRead.cpp:1555-1563` (missing: error and continue; newer:
  bail); the `ReadText` cap Plan F adds.
- `src/Identity.h:46`: `PresetsPath` returns `regions.json` while
  `docs/wip/menu-ownership.md:105` and `docs/wip/engine-ownership.md:239` say
  the paint-era "region" vocabulary is gone; `REFERENCE.md:824` documents the
  filename. The UI v2 proposal gives "region" a new meaning (armor coverage
  areas), so the filename will soon name the wrong thing.
- Fifty `//` comment lines in `src/` outside `_old`, `extern` and `cs`:
  about nineteen in `render/`, ten in `engine/`, five in `planners/`, four
  in `recipe/`. `src/render/Binding.cpp:72-75,94-95,413-415,484-485` is the
  densest; its content (the engine's intrusive refcount API) is exactly what
  `CLAUDE.md` routes to `REFERENCE.md`.
- `docs/conventions.md` still documents the `push_back({Severity::…})`
  exception (resolved; Plan A removes the paragraph) and never mentions
  `Trace` (Plan A adds it). Verify both landed.
- The repository directory is still `WornEnchantmentPBR`. Out of scope; the
  plugin name is spelled only in `src/Identity.h` and the folder name is the
  user's.

## Steps

### E1: README and index

1. Write `README.md` at the root, under 120 lines: one paragraph on what the
   plugin does for a player and for a recipe author; the reading order copied
   from `CLAUDE.md` with one sentence per document saying what question it
   answers; the three commands (`nix develop`, `./build.sh Release -j 4`,
   `tests/run-native.sh`) and `./install.sh`; where recipes live on disk;
   a link to `docs/README.md`. Do not duplicate `REQUIREMENTS.md`; point to
   it.
2. Write `docs/README.md`: one line per file under `docs/` and `docs/wip/`,
   grouped as **canon** (conventions, in-game regression, ui design
   principles), **active plans** (the six critique plans, any ownership doc
   whose Status is open), **checkpoints and evidence** (dated cleanup and
   checkpoint notes, `regression-evidence/`), and **history** (superseded
   proposals and seams). Each superseded document gets a one-line status
   header at its top naming what superseded it, following the pattern
   `docs/wip/render-ownership.md:4-9` already uses.
3. Change `CLAUDE.md:3-12` to point at `README.md` for the reading order
   rather than restating it, so there is one copy.

### E2: correct the claims

1. `REQUIREMENTS.md:126`: change the heading to match the list (eight,
   including `diagnostics/`), or fold `diagnostics/` into the count sentence.
2. `REQUIREMENTS.md:98`: `actorValue` becomes `av`, with the sentence "the
   wire word is the modding community's abbreviation for actor value".
3. `REQUIREMENTS.md:241-242`: rewrite to the true state: `src/_old` is the
   frozen oracle and stays until the release gate in the roadmap removes it.
4. `docs/wip/deletions.md`: remove the two citations to nonexistent files
   and either repoint them at the current file or delete the entries. Add a
   status header saying which entries are resolved.
5. `ShellPose` (decided 2026-09-13: implement the five fields). Nothing to
   remove. Update `docs/wip/deletions.md:37-40` to say the fields are
   implemented by Plan G, and leave the schema, reader, writer and form as
   they are.
6. `regions.json` (decided 2026-09-13: rename, no fallback). `git mv
   presets/regions.json presets/presets.json`; change `CMakeLists.txt:162`
   and `src/Identity.h:46` to `presets.json`; correct `REFERENCE.md:824` and
   the two ownership docs. Confirm nothing else names the old file:
   `grep -rn 'regions.json' . --exclude-dir=_old --exclude-dir=.git`. In the
   plan's report, tell the user to delete the stale `regions.json` from
   `/mnt/a/mods/SkyrimSE/mods/BetterEnchantmentEffects/SKSE/Plugins/BetterEnchantmentEffects/`
   after `./install.sh`, and give them the log line `LoadPresets` writes so
   they can confirm the new file loaded.
7. `REFERENCE.md` headings at `:170`, `:662`, `:685`: replace the file
   names with the current ones. Read each section and confirm the body still
   describes code that exists; where it describes deleted code, move the
   section under a `## History` heading at the end rather than deleting it,
   since the frozen tree still exists.
8. Verify Plan A's conventions edits landed (Diagnostics rule, Trace
   paragraph, INI exception) and Plan B's extension checklist is under the
   variants heading.

### E3: constants into REFERENCE

Decided 2026-09-13: search history first, then ask. For each constant in
the findings list:

1. Search, in this order, and stop at the first hit: the same value in
   `src/_old/` (the frozen implementation often carries the original
   context); `git log -S'<value>' --oneline -- src` and the commit message
   of the hit; the ISL and Community Shaders sources under `reference/` for
   the light cutoffs and the flatness thresholds (the PBR material layout
   sections of `REFERENCE.md` already cite these sources; follow the same
   links); `decompiled/` for anything engine-facing.
2. Add a line under the owning module's heading in `REFERENCE.md` with the
   value, the file, the reason and the citation.
3. Collect the values with no trace into one batched question to the user,
   listing each with what was searched, and record their answer verbatim.
   Do not invent a derivation. If they say a value was tuned by eye, write
   "tuned by eye on <date>; no derivation" so a future reader knows it is
   safe to retune.

### E4: comments out of the source

1. List them: `grep -rn '^\s*//' src --include='*.cpp' --include='*.h' |
   grep -v _old | grep -v extern | grep -v src/cs`. Also `grep -rn ' // '` for
   trailing comments and `grep -rn '/\*'` for block comments.
2. For each: if it states a fact the code cannot (an engine layout, a
   refcount rule, a packing), move the sentence to `REFERENCE.md` under the
   module heading and delete the comment. If it narrates the next line,
   delete it. If it names a constraint that a name or a small helper could
   state, write the helper and delete the comment. Do not leave a comment
   because it is helpful; that is the rule's whole point, and the reference
   is where helpfulness goes.
3. Add the no-comment check to the push stage of `tools/gate.sh`: the grep
   above must print nothing. Exclude URLs in string literals by requiring
   the `//` to begin the line or follow whitespace after a `;` or `}`.
4. `src/cs/` is vendored under a different licence and keeps its comments.

## Acceptance

- `README.md` and `docs/README.md` exist. Every file under `docs/` and
  `docs/wip/` appears in the index exactly once.
- `grep -n 'Seven directories\|actorValue' REQUIREMENTS.md` prints nothing.
- `grep -n 'SettingsCore\|ARCHITECTURE.md' docs/wip/deletions.md` prints
  nothing.
- `grep -n 'RuntimeTextures.cpp\|Paint.cpp\|Region.cpp\|TermKind.h'
  REFERENCE.md` prints only lines under `## History`.
- Every constant in the findings list has a `REFERENCE.md` line; grep for
  each value.
- The gate's comment grep prints nothing and the gate is green.
- `grep -rn 'regions.json' . --exclude-dir=_old --exclude-dir=.git` prints
  nothing; `presets/presets.json` exists.
- The constants with no trace, and the user's answer for each, are
  recorded in the Status block.

## In-game checkpoint

The presets file rename is the only behaviour change. After `./install.sh`,
ask the user to delete the stale `regions.json` from the plugin folder,
load a save, and confirm the `LoadPresets` log line reports the same preset
count as the previous build from `presets.json`.
