# BetterEnchantmentEffects release roadmap (2026-09-20)

Written against `ui-standardization` at 978a4c3. Supersedes the 2026-09-09
roadmap, now at `docs/history/release-roadmap-2026-09-09.md`; that document
holds the reasoning behind the gates and the decisions of 2026-09-09, and
this one does not restate it. The decisions stand: the recipe format is the
product, the studio is a tool, five gates hold before anything ships, and
the closed alpha needs gate 1, gate 4, and enough of gate 5 that a tester
can write a file.

## 0. What the drift changed

The 2026-09-09 roadmap was written against a tree that no longer exists.
Since then:

- The rebuild finished. Waves 0 to 4 replaced the frozen `src/_old` tree;
  the engine-free modules build natively and the native suite runs in
  `tests/run-native.sh`.
- The critique remediation closed. Plans F, A, B, C, D and E are done and
  checkpoint-passed. Plan G (the shell honours its whole pose) is
  implemented through the render (161f6b2; `render/Shell.cpp` applies
  offset, scale, scalePoint, spin and spinAxis to the shell's skin
  transforms), but its in-game checkpoint
  has not run and its plan document still says "not started".
- UI v2 landed whole: the thirteen wishlist slices, the fine-tuning round,
  and the standardization pass with every case closed. The mask editor,
  input wizard, terms editor, Board page and resource tabs exist. The
  game-object discovery service and the `studio/Create` seam landed.
- The documents were regenerated and fact-checked (a9890f5): the README
  describes the current runtime, `docs/README.md` owns completeness,
  `bugs.txt` is gone, the comments left the sources for `REFERENCE.md`.
- `LICENSE` (GPL-3.0-or-later) is in the tree. The repository has a remote.
- Format-1 record changes the old roadmap listed as blocking have landed:
  cluster settings live in the `materialClusters` record; a curve reference
  is one text that is a name or an inline expression (`CurveRef`);
  `regions.json` became `presets.json` and now holds only presets.
- The commit and push gates hardened (`tools/gate.sh`), the diagnostic
  trace rotates, and runtime textures are sized from the target geometry
  rather than a fixed table (899a339).

Two pieces of drift are unfinished business rather than progress:

- `ui-standardization` is 27 commits ahead of `main`. Nothing ships from a
  side branch.
- Strings stage 0 (an engine-free resolver and a ~637-message catalog with
  typed accessors) sits uncommitted in the `strings-stage0` worktree, and
  no call site is wired. It is either part of the release or it is dead
  weight in a worktree; deciding is cheap, wiring is not.

## 1. The five gates, refitted

### Gate 1: it does not freeze, and it does not exhaust the GPU — OPEN, and still the alpha's price of entry

Nothing here has been measured. The state of the three suspects:

- **Sink churn.** The animation-graph sink is still added at apply and
  removed at retire (`engine/ManagerApply.cpp`), the exact churn
  `REQUIREMENTS.md` defect 3 names as the crowd-freeze suspect. The rebuild
  did not change the pattern.
- **Texture slots.** The 512 placeholder presenter DDS files remain the
  only way the engine sees a generated texture (`REFERENCE.md`, presenter
  pool). Sizing now follows the target geometry, which answers the
  absolute-size half of the old gate; the slot-count and VRAM half stands.
- **Readbacks.** The synchronous readbacks named in 2026-09-09 (flat
  displacement, material sample, streamed-mesh copy) have not been re-timed
  in the new tree.

Done when (unchanged): a city with `PlayerOnly=false` runs an hour without
a freeze; the apply burst for twenty actors is measured and bounded; a
crowd's slot and VRAM consumption is measured and bounded well under the
limit. The measurement session comes first and ranks the fixes.

### Gate 2: legal to distribute — SETTLED

`LICENSE` is in the tree. Remaining, deferred until anything is public:
permissions stated on the mod page, and the README's position that a
recipe file is data, not a derived work.

### Gate 3: the documents say what the code does — LARGELY CLOSED

The regeneration closed the old complaints. What remains is residue:

- Verify every row of `BetterEnchantmentEffects.ini` (144 lines) is read;
  the importer-tuning rows are gone, the claim "only what is read" is
  unverified.
- Correct the Plan G status in `docs/README.md` and its plan document
  (found stale 2026-09-20).
- Plan-document hygiene: the UI v2 pair and the backlogs are complete or
  partially shipped; their index entries should say so.

This gate now also owns the strings decision's documentation half: if the
catalog lands, the wording pass and the deferred tooltip pass happen
against it, once, when the UI is final. The UI is now
standardization-complete, so "final" is no longer a moving target.

### Gate 4: played, not just checked — PARTIAL, and healthier than 2026-09-09

Per-change checkpoints have passed in game continuously through UI v2, the
standardization pass, the creation seam, and today's ripple fix. The
checkpoint discipline the old roadmap asked for exists. Missing:

- Plan G's in-game checkpoint.
- The two long sessions. The crowd session doubles as gate 1's
  measurement run; schedule them as one piece of work.

### Gate 5: the format is a contract someone can rely on — OPEN, and the critical path

The record changes that blocked the freeze have landed, so freezing
format 1 is now mostly writing, not building. The five parts:

1. **Schema-parser agreement test.** DONE. It already existed
   (`tests/recipe/schema_tests.cpp`, suite `recipe_schema`, run by
   `run-native.sh`): source and signal kinds both ways, every value enum.
   Extended 2026-09-20 with key kinds, selector terms, trigger origins,
   partition names and the output scalar fields.
2. **Validator CLI.** DONE 2026-09-20. `tools/validate.sh <recipe.json>...`
   builds `build/validator/beef-validate` from `src/validator/Main.cpp`
   over the engine-free parser and prints each diagnostic with its `where`,
   then the store's verdict (ok / loads-with-inert-rows / held back /
   unreadable); exit 0 only when every file loads clean. Editor IDs still
   resolve only in game, and the tool says so. The Release build also
   links `beef-validate.exe` (CMake target `BeefValidate`), so packaging
   only has to include it.
3. **Freeze format 1, in writing.** The blocking record changes are in.
   Decide the per-row note field (still open; the examples and the studio
   both want it), then write the freeze: each remaining change made or
   deferred to format 2, named.
4. **The nine examples and their walkthroughs.** Not started; no
   `recipes/` directory exists. The lesson plan from 2026-09-09 stands
   (glow, breathe, where, the wearer, events, the surface, cloth and
   metal, shell and light, merging-as-a-pair). Still the long pole; still
   feeds back into the freeze; start in draft early.
5. **The second format.** `presets.json` slimmed to presets only, which
   settles the old step-11 half. The decision it still needs: are shipped
   presets part of the product (then it needs a schema, a freeze and
   documentation) or internal (then say so where an author will read it).

## 2. Outside the gates — unchanged, all six open

The 2026-09-09 list stands untouched: the ecosystem policy (where another
author's files live, key conflicts, override, load order), the imported
folder's lifecycle, the format-2 promise, the Community Shaders pin loop,
a channel to receive a bug, and the author cost model. The first two
belong inside gate 5's freeze decision; the cost model falls out of gate
1's measurement.

## 3. Packaging — unchanged, all open

Archive instead of a mod folder, debug symbols split out, a version scheme
and changelog, requirements stated on the page.

## 4. The list, re-ranked

Size: S is hours, M is a day or two, L is a week or more.

**Housekeeping, first because everything ships from `main`**

1. **Merge `ui-standardization` into `main`.** S.
2. **Decide strings stage 0.** DECIDED 2026-09-20: strings come later.
   The worktree stays as it is; nothing about the release waits on it.

**Gate 1, the alpha blocker**

3. **Measure.** M. One session: time the apply burst, count the sink churn
   per refresh, time the readbacks, count a crowd's presenter slots and
   VRAM. Doubles as the first long play session and produces the cost
   budget. Ranks 4 to 6.
4. **Fix the crowd freeze.** L, unknowable until 3.
5. **Replace how textures are held.** L. The slot count and VRAM half;
   sizing is already relative.
6. **Fix the readback stalls.** M, possibly the same work as 4.

**Gate 5's cheap half, in parallel with 3**

7. ~~**Schema-parser agreement test.**~~ DONE: pre-existing suite
   `recipe_schema`, extended 2026-09-20.
8. ~~**Validator CLI.**~~ DONE 2026-09-20: `tools/validate.sh` /
   `src/validator/Main.cpp`.

**Gate 5's decisions, then the freeze**

9. **Ecosystem policy.** M. Direction set 2026-09-20: the user-facing half
   lives on the Recipes page — a user edits a recipe's priority and its
   override mechanic there (replace, lerp, sampled one-of-n) instead of
   the current silent first-loaded-owner-wins. An override mechanic is a
   per-recipe record field, so this confirms the policy precedes the
   freeze. Still open: the author-side half — where another mod's files
   live, key ownership, whether load order means anything — and the
   mechanic list itself, to be settled on the Recipes-page visit.
10. **Per-row note field.** S decision, S to M to build.
11. **Freeze format 1, in writing.** S now that the record changes are in.
    Settle `presets.json`'s status in the same writing.
12. **Imported-folder lifecycle.** S to M, falls out of 9.

**The long pole**

13. **The nine examples and walkthroughs.** L. Start in draft before 11 is
    final; whatever is awkward to teach is a format defect found early.

**Finishing gates 3 and 4**

14. **Docs residue.** S: the INI audit, the Plan G status, the plan-doc
    statuses.
15. **Plan G's in-game checkpoint.** S, the author's time.
16. **Long sessions.** The second one, after the gate-1 fixes.
17. **Strings wiring and the single tooltip pass**, if 2 said yes. M. The
    studio-polish bar (a person can build a recipe and understand a
    refusal), not blocking, but cheapest done with the examples open.

**The alpha's furniture**

18. **Cost budget stated, recipe cost shown in the studio.** S then M,
    from 3's numbers.
19. **The format-2 promise.** S, a paragraph.
20. **A way to receive a bug, and what a report contains.** S.
21. **Closed alpha.**

**Before anything public**

22. **Mod-page permissions and the recipe-is-data statement.** S.
23. **The Community Shaders maintenance loop.** S.
24. **Packaging.** M.

## 5. Order

1. Merge to `main`; settle the strings worktree.
2. Measure (gate 1). The agreement test and the validator are already in
   (2026-09-20).
3. Decide: ecosystem, per-row note, presets.json. Freeze format 1 in
   writing.
4. Examples in draft alongside the gate-1 fixes the numbers rank.
5. Docs residue, Plan G checkpoint, the second long session.
6. Alpha, with the bug channel open and the cost budget stated.
7. Public only after the licence page work, the CS loop, and packaging.
