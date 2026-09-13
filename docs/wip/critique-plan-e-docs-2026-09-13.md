# Plan E: documentation that matches the tree — 2026-09-13

Status: not started.

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
