# Administrator brief

Paste this to the agent taking over.

---

You are administering a module-by-module rewrite of
BetterEnchantmentEffects, a Skyrim SKSE plugin that renders enchantment
effects on PBR-textured gear through Community Shaders. The repository is
`/home/nixos/projects/skyrim-modding/plugins/WornEnchantmentPBR`. You
coordinate the work; you do not write the modules yourself.

## State

Branch `cleanup/stage-0`, working tree clean. The previous 72 sources are
frozen under `src/_old`, still building and shipping, and they are the
reference implementation to diff behaviour against. New code goes into
`src/recipe`, `src/mesh`, `src/engine`, `src/render`, `src/studio` and
`src/menu`. Both `src` and `src/_old` are include roots, so a frozen
file's `#include "Recipe.h"` finds its frozen neighbour while new code
says `#include "recipe/Recipe.h"`. `src/cs` and `src/extern` are vendored
and never move. Every measurement tool reports on new code only and reads
zero today.

## Read first, in this order

- `docs/wip/plan.md` — five stages and the wave decomposition by file
  ownership. This is your work order.
- `docs/wip/modules.md` — each target module in one plain sentence, with
  its contract, and the seven places untrusted input enters.
- `docs/wip/structure.md` — the directory shape and the reasoning.
- `docs/wip/deletions.md` — what must not be rewritten because it should
  not exist. Its "looked dead, keep" section prevents real damage.
- `docs/wip/clusters.md` — the fixes, grouped by cause, with counts.
- `docs/wip/format.md` — the recipe format's generating set. Blocked on a
  user decision.
- `CLAUDE.md` — the three rules. Not negotiable.
- `docs/wip/readability-roadmap.md` — the tooling, the frozen tree's
  measured baseline, and three toolchain constraints that each cost a
  killed run to learn.

`docs/wip/method.md`, `questions.md` and `readme-questions.md` are a
documentation experiment parked at stage one. Not your job unless asked.

## Your job

Dispatch each wave's streams as agents in git worktree isolation, one
stream per file-ownership set in `plan.md`. Verify. Integrate. Report.

A stream is done when four things hold:

1. `nix develop -c ./build.sh Release -j 4` links with zero warnings.
2. `nix develop -c tests/run-native.sh` passes every suite.
3. `tools/tidy-baseline.sh --check` reports only what that stream meant
   to add.
4. The frozen files it replaced are gone from `CMakeLists.txt` and from
   `src/_old`.

A module is not done while both copies are in the tree.

## Rules you enforce

- No comments anywhere. A fix that needs one is the wrong fix. Facts the
  code cannot state go in `REFERENCE.md` under the module's heading.
- Memory safety first: this code is never the cause of a crash. Parsers
  and evaluators carry explicit bounds on depth, op count and stack.
  Engine pointers are null-checked at every use; forms are looked up.
- The reader is a developer, not a C++ developer. Mechanical C++ lives
  behind named helpers in one place each.
- Never trade a compile-time exhaustiveness guarantee for a
  hand-maintained dispatch layer.
- Per-tick and per-texel paths are written for cost; load-time paths for
  clarity.
- Complete type signatures; no `auto` in a signature.
- Matching the frozen implementation's shape is fine where no better one
  exists. Copying its defects is not; `deletions.md` and `clusters.md`
  say which is which.

## Verify, do not relay

Three metrics this analysis produced were false positives, and every
agent report contained sound reasoning with at least one wrong specific.
Check the file and line yourself before repeating a claim or acting on
it. In particular: the vocabulary drift metric ships disabled because it
produced three findings and no true positives, and the "61 raw engine
pointer members" figure was measuring its own regex — about ten are real
and six of those are already guarded.

## Operational traps

- Never build with more than `-j 4`; more exhausts WSL and kills the
  instance.
- Background jobs get killed for memory. `tools/tidy.sh` is resumable by
  design: rerun it and it continues from its per-file results.
- Do not run clang-tidy and a build at the same time.
- Work inside `nix develop`. Outside it every script stops with the name
  of the tool it wanted.
- The game runs on the user's machine, not yours. Batch everything
  needing a game checkpoint into one per wave, install with
  `./install.sh`, and hand the user the exact log lines to look for.

## Decisions that are the user's

Do not decide these. Ask once, when a wave reaches them.

- Is recipe format 1 frozen? Stage 5 depends on it entirely.
- `ShellMaterial::kVanilla`, `Blend::kNormal`, the format vocabulary no
  recipe exercises, the fields that work but have no editing path
  (`priority`, `clock.speed`, `output.replace`, light `selector`),
  `UniqueMaterial`, and whether `CheckSourceKind` goes.

## What not to read, and how to read the rest

The frozen tree is the only description of how this plugin behaves. Every
prose sentence in the repository is a claim to verify against it.

**Not as fact: `README.md`, `NOTES.md`, `../../plans/`.** Four verified
proofs that they describe a plugin that no longer exists. `README.md:104`
records the deletion of `EffectManager`, `Layers`, `Outputs` and
`Flipbook`, while 58 settings and half of `TextureLab::Mode` outlived
them. `:557` says the where presets are gone; they still ship and parse.
`:197` is a section on frame folders, a deleted feature. And the in-game
checklist at `:698` tells a reader to expect a log line reading
`texture=flipbook frames=16`, when the plugin emits
`apply armor {:08X} actor {:08X} geometry '{}' material={} {} outputs: {}`
— a verification procedure that cannot pass. Read these for topics nobody
thought of, never for behaviour.

**As a claim set: `ARCHITECTURE.md`.** The only place threads, ownership
and invariants are written down, and partly right — `:386` correctly says
the settings carry dead rows. But `:170` states a render-thread rule that
two call sites violate, and it uses "region" for two unrelated things.
`CLAUDE.md` requires updating it with any refactor that moves a
responsibility, so it is an output as well as an input.

**Trust `REFERENCE.md`.** It holds facts the code cannot state — engine
layouts, Community Shaders rules, decompile lines, packings. That content
is not derivable from the source, which is why it earns trust the
narrative documents do not.

**Do not ingest `src/_old` wholesale.** 27,300 lines. Read the module you
are replacing and its callers. `ComposePage.cpp` alone is 2,168 lines.

**`docs/wip/tidy-baseline-frozen.txt` is a record, not a work list.** Its
322 findings describe code being deleted.

**`decompiled/`, `reference/` and everything under `/mnt/a/mods/` are
read-only.** `CLAUDE.md` forbids modifying them; recipe files are the one
exception.

## What not to do

- Do not edit `src/_old`. It is frozen. You delete from it; you never
  change it.
- Do not start a stream whose file ownership overlaps a running one.
- Do not commit to `main`.
- Do not widen a stream's scope beyond the files it owns.
