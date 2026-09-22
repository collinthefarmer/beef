# BetterEnchantmentEffects release roadmap (2026-09-09)

Status: history. Superseded by `docs/history/release-roadmap-2026-09-20.md`,
which keeps the gates and decisions and refits their statuses to the tree
after the rebuild, the critique remediation and UI v2. This file holds the
reasoning behind the gates and the 2026-09-09 decisions; it previously
lived outside the repository under `plans/`.

The other plans say what to build next. This one says what has to be true
before any of it goes to another person, and in what order to make it true.

Written against main at 1383853. Nothing here changes the model; where an
item overlaps the development roadmap
(`worn-enchantment-pbr-roadmap-2026-09-08.md`) it says so.

## 0. Where the project stands

Working: recipe format 1 with its store, the expression language, sixteen
signal kinds and seven source kinds, nine material slots plus the shell and
light outputs, per-slot merge across recipes, the texture lab, the studio
menu (Compose, Paint, Recipes, Setup) with undo, and an importer that turns
vanilla enchantment shader records into recipes at load with no authoring.

Not working, or not built: Design mode is a placeholder; the hit-position
provider, term coverage, the atlas and texture scale are unbuilt; a crowd
freezes the game.

## 0.1 What the product is (decided 2026-09-09)

**The recipe format is the product.** The studio is a tool for developing
recipes, used by a smaller group: mostly mod authors, plus players who want
to tweak a recipe of their own.

That ranks everything below. A file someone writes has to keep working, so
the format's contract is a gate of its own (gate 5). The studio has to work
for the people who open it, which is a lower bar than polish, so Design mode
and the precision tools are not blocking. The importer stays what makes the
mod useful to a player who never opens either.

## 1. The five gates

These hold whoever the release is for. Nothing ships until all five pass.

### Gate 1: it does not freeze, and it does not exhaust the GPU

Decided 2026-09-09: both halves block the closed alpha, not just the public
release. A tester who freezes in a city, or who runs out of texture slots in
a crowd, tells us nothing we did not already know.

#### The freezes

Two known, both unexplained, both leaving no stack.

1. **Crowds.** With `PlayerOnly=false` in a city, three separate starts froze
   during the burst of NPC applies, each at a different actor, nothing in any
   log. `PlayerOnly=true` runs. The recorded suspicion is the animation-graph
   sink being removed and re-added on every apply and retire.
2. **Paint on a re-equipped cuirass**, 2026-09-07. The named suspects are the
   remaining synchronous readbacks: the flat-displacement measure, the
   material sample and a streamed mesh's GPU copy each hold the renderer lock
   across a map that waits on the GPU.

Neither can be fixed by reasoning. Both need a measurement first: a per-phase
timer around apply, the sink churn counted per refresh, and the readbacks
timed. The development roadmap already carries "runtime debts with a
measurement" as feature 2; this gate promotes it to first.

#### How textures are held

Every stack, mask, bake, ripple and preview holds a whole render target,
shown to the engine through one of exactly 512 placeholder presenter textures
shipped as DDS files. A crowd multiplies both the video memory and the slot
count quickly, and there are only 512 slots. Sizes are absolute as well: the
settings name 128 to 1024 pixels while modded armour ships 2K and 4K maps, so
a stack over a large base either wastes memory or loses detail.

The atlas was the sketched answer, and it may still be, but the requirement
is the problem rather than that solution: a crowd must not run the mod out of
slots or video memory, and a stack's size must follow the map it edits rather
than a fixed number. Whatever replaces the slot textures has to be chosen
against a measurement of what a crowd actually consumes.

Done when: a city with `PlayerOnly=false` runs for an hour without a freeze;
the apply burst for twenty actors is measured and bounded; and the slot and
video memory a crowd consumes is measured and bounded well under the limit.

### Gate 2: it is legal to distribute — SETTLED 2026-09-09

Decided: **GPL-3.0 or later**, added as `LICENSE` (the verbatim text from
gnu.org) with a Licence section in the README naming each vendored file.

It was the only clean answer, because both non-MIT headers the project
carries are themselves GPL-3.0: the SKSE Menu Framework header, and the
Community Shaders material header. Re-deriving the menu API would not have
helped while the CS header is also present. nlohmann's JSON header is MIT,
which GPL-3.0 permits.

Remaining under this gate: state the permissions on the mod page when there
is one, and confirm the README's position that a recipe file is data rather
than a derived work, so authoring one imposes no licence on the author.

The reasoning that led here, kept for the record:

`SKSEMenuFramework.h` is vendored and is GPL-3.0. Publishing the plugin with
it makes the whole plugin GPL-3.0, or the small API surface has to be
re-derived from scratch. There is no licence file in the repository at all,
so today the source has no terms and a mod page could not state permissions.

Two ways out, and the choice is the author's:

- **Accept GPL-3.0.** Add the licence, state it on the mod page, publish the
  source. Costs nothing in work; it binds anything derived from the plugin.
- **Re-derive the menu API.** The framework is reached through
  `GetProcAddress` already and the menu is optional, so the surface is small.
  Then any licence is available.

Done when: a licence file is in the repository, its choice is deliberate, and
no file in the tree carries terms that contradict it.

### Gate 3: the documents say what the code does

The user-facing README opens by saying the runtime is still a proof of
concept and nothing reads the recipes yet. That has been false for weeks.
About eighty lines in the middle describe a deleted pipeline (runtime
textures, frame folders, the colour and extra-layer settings). The settings
file a player opens has four settings they care about followed by roughly
thirty rows of importer tuning that nothing reads any more. The tests section
claims five suites; there are twelve. Five checkpoints name example recipe
files that are not in the tree. `bugs.txt` still carries a rename-then-undo
bug that was fixed.

This is the cheapest gate and the one that most affects whether a stranger
can use the thing.

Done when: the README describes the current runtime from its first line, the
settings file holds only what is read, and every path a document names
exists.

### Gate 4: it has been played, not just checked

Twenty-seven numbered checkpoints exist. One carries a pass marker
(footfalls, 2026-09-05), three more passed on 2026-09-04, and one passed in
part. Everything since has not run in game, including today's five structural
commits: the actor-state tables, chained rendering, and the merge order.

Checkpoints catch what they were written for. They will not catch memory
growth over an hour, a crowd in Whiterun, an armour set nobody tested, or a
conflict with a mod that was not considered. Both kinds of testing are
needed, in this order: checkpoints to clear each change as it lands, then
long sessions before the gate.

Done when: the checkpoint list is current and passed, and two long sessions
run without a freeze, a leak, or stale state after reload.

### Gate 5: the format is a contract someone can rely on

This gate exists because the format is the product. It has five parts.

**The schema and the parser must agree.** `schema/recipe.schema.json` is what
an author validates against, and it is a separate file a human maintains. The
C++ word tables are pinned to their variants by static assertion, so the code
cannot drift from itself, but nothing stops the schema from drifting from the
code. An author whose valid file the plugin refuses, or whose refused file the
schema accepts, has been given a broken contract. The check is cheap: compare
the schema's kind lists against the word tables in `Vocabulary.h`, both ways,
in `run-native.sh` beside the existing schema validation.

**Decide what format 1 is, and freeze it.** The file already carries a
required `format` field set to 1, so a later reader can dispatch on it and a
format 2 is available. That makes the question narrow: which of the
development roadmap's remaining record changes are worth making before the
freeze, and which wait for format 2.

- Blocking, because they change what a file holds: step 8, cluster settings
  moving into the file's records; the part of step 9 where a curve reference
  becomes a variant of a reference and an inline expression; the part of step
  11 where `regions.json` loses `where` and `names.partitions`.
- Not blocking, because they are internal: step 10 entirely, since snapshot
  rows are the menu's read model and never reach a file; the rest of step 9
  and step 11; step 12, which adds a file rather than changing one.

**An author must be able to check a file without launching the game.** No
validator exists. The parser, the validator and the expression compiler are
engine-free and already build natively for the test suites, so a small
command-line tool that reads a recipe and prints its diagnostics with the
`where` that names the row is nearly free and is the single most useful thing
to hand an author.

**Worked examples are documentation now, not test fixtures.** The three that
were deleted on 2026-09-09 are replaced by a new set, designed rather than
accumulated. Two requirements, decided 2026-09-09: the set as a whole
exercises every deliverable, and each file teaches.

Teaching means each example adds one idea, reuses what came before, and holds
nothing that is not needed for its lesson. A file that shows six features at
once teaches none of them. The order is the lesson plan:

| # | Name | Teaches | Covers |
|---|------|---------|--------|
| 1 | glow | the smallest file that does something | file shape, keys, `constant`, a shell emissive output, one layer, slot scalars |
| 2 | breathe | a signal drives a parameter | `pulse`, `ramp`, curves, clock, a parameter as a signal reference |
| 3 | where | putting the effect somewhere | `material` source, masks, the expression language, channels, blends, opacity |
| 4 | the wearer | reading the actor | `av`, `actorState`, `enchantment`, `delta`, `smooth`, `expr`, and what makes a signal inert |
| 5 | events | reacting to what happens | `trigger`, `payload`, `counter`, `accumulate`, the `ripple` source |
| 6 | the surface | modulating the material, not adding light | normal, height and rmaos slots; `bake`, `uv`, `distance` sources; `noise`, `gradient` |
| 7 | cloth and metal | the response slots | fuzz, glint, coat, subsurface, diffuse; `materialClusters` and `image` sources |
| 8 | shell and light | the outputs that are not textures | shell settings and pose, the light output, `efsh` |
| 9 | merging | how two recipes combine, which one file cannot show | a keyed pair at two priorities, `replace`, selectors, variants, key specificity |

Nine lessons, ten files, since 9 is a pair. Between them that is all sixteen
signal kinds, all seven source kinds, all nine material slots, both non-
texture outputs, and the merge rules. Number 7 carries the most and may split.

Open question for the format freeze: a recipe can carry a name, author,
description and version at the top, but no row can explain itself. Teaching
examples can live with a companion walkthrough per file, which is arguably
better teaching anyway. An author reading someone else's recipe in the studio
would still be better served by a note per signal, source and layer. If that
field is wanted it is a format-1 change and has to land before the freeze.

**There is a second format, and it has no schema at all.**
`regions.json` is Paint's preset and vocabulary file: plain names for biped
partitions and bones, thirteen `where` presets naming body areas by partition
and bones, and eight `what` presets naming material regions by an expression
over the armour's own maps (`leather`, `polishedMetal`, `engravings`, `dark`
and so on). It carries its own `format: 1`, its own parser (`ParsePresets`)
and its own caps, and nothing validates it.

The question to settle is whether it is part of the product. A `what` preset
is reusable material knowledge that does not depend on any particular armour,
which is exactly the kind of thing an author would want to write and share. If
authors can ship presets, this is a second contract needing the same schema,
the same freeze and the same documentation as recipes. If it stays internal,
say so, and the gate only asks that it be labelled.

Either way it moves with format 1's freeze, because the development roadmap's
step 11 removes `where` and `names.partitions` from it. Decide both at once.

Two smaller things to fix here: the README says the `where` presets were
removed while thirteen remain in the shipped file, and the plugin folder now
holds two file kinds with different rules, which the documents should state
plainly.

Done when: a test fails if the schema and the parser disagree; the format-1
record changes are made or explicitly deferred to format 2 in writing; a
validator ships; the examples an author reads exist; and `regions.json` is
either a documented format with a schema or labelled internal.

## 1.6 Six things the gates do not yet cover

Raised 2026-09-09. None is a gate on its own; the first two probably belong
inside gate 5, and the rest need a decision before the alpha rather than a
body of work.

**The ecosystem, which is unsketched.** If the format is the product then
other people ship recipe files, and nothing describes how. The store loads
every JSON under the plugin's folder in sorted path order, shipped first and
the user's folder last, and the first loaded owner of a key wins with the
others warned and left unresolved. That is a mechanism, not a policy. An
author needs to know where their mod puts files, what happens when two mods
key the same effect shader, how a user overrides a recipe they did not write,
and whether mod-manager load order affects any of it. Sorted path order means
it does not, which is probably wrong. This is the largest unwritten piece and
it is the one that decides whether recipes from different authors can
coexist.

**The imported folder has no invalidation.** The importer skips a shader that
any loaded recipe already keys, so an author's file does prevent a new
import. But a file imported earlier stays on disk and keeps loading, so
installing a recipe mod leaves the generated recipe it supersedes in place,
both claiming the key, one of them warned as unresolved. A user has to know
to delete the file. The generated folder is a cache derived from the game's
records and needs the lifecycle of one.

**The promise that makes format 1 worth writing for.** The file carries a
required `format` field, so a format 2 is technically available. Nothing says
what a format-2 reader does with a format-1 file, whether the studio rewrites
a file it opens, or how long format 1 is read. An author deciding whether to
invest a weekend in a recipe pack is deciding whether that promise exists.

**The Community Shaders pin is a standing commitment, not a one-off.** The
material layout is a raw struct that moves between CS releases, pinned here to
one commit. The runtime layout check disables the emissive path instead of
crashing, which is the right failure, but it is silent to a user who only
notices the mod stopped working. Needed: how a CS release is noticed, how
quickly the layout is re-verified and re-pinned, and what the user is told in
the meantime.

**There is no way to receive a bug.** No remote, no tracker, no channel.
Diagnosing an author's problem needs their recipe file and their log
together. Worth deciding what a report contains before handing builds out,
because the alpha's whole purpose is receiving them.

**Authors have no cost model.** Nothing tells an author whether their recipe
is expensive, and the format makes it easy to write one that is: several
animated stacks at 1024 pixels across a crowd. Gate 1's measurement produces
the budget as a by-product. Once it exists, state it, and show a recipe's
cost in the studio, which already knows each stack's size and whether it
animates. Without that, authors will ship recipes that cost too much and the
mod will be blamed.

## 2. Packaging, once the gates pass

- **An archive, not a mod folder.** `install.sh` writes into the MO2 mods
  directory, which suits development and nothing else. A release needs a zip
  with the folder structure a mod manager expects.
- **Split the debug symbols.** The package is 54 MB, of which 47 MB is the
  PDB. It belongs in a separate optional download, kept per released version
  so a user's crash log can be read.
- **A version scheme and a changelog.** The project is 0.1.0 with 72 commits
  and no changelog. Pick the number the first release carries and start the
  file.
- **State the requirements on the page.** Skyrim SE/AE 1.6.1170, no VR build;
  SKSE and Address Library; Community Shaders, tested against 1.8.3, with the
  material layout pinned to a commit that moves between CS releases; po3's
  Tweaks for editor-ID resolution; SKSE Menu Framework optional. The CS pin is
  the fragile one: the layout check disables the emissive path rather than
  crashing, which is the right behaviour, but a CS update can silently turn
  the mod off. Say so on the page.
- **Back the repository up.** It exists on one machine with no remote.

## 3. What the decision leaves out

Not blocking, on the ground that the studio is a tool rather than the
product:

- **Design mode.** It is the mode built for authoring, but it is a studio
  mode, and the studio already authors recipes through Compose and Paint. It
  ships after.
- **The precision tools**, the atlas and presenter spike, the raycast, and
  the world eyedropper and brush.
- **Studio polish.** The bar is that a person who opens it can build a recipe
  and understand what refused their input. It is not that every tooltip has
  had its pass.

Still blocking despite the studio being secondary, because a broken tool
teaches an author the wrong thing about the format: validation must explain
what it refused, and undo, save and revert must not lose work.

## 4. The closed alpha

A private build to a handful of authors. It needs gates 1 and 4, and enough
of gate 5 that a tester can write a file: the validator, the examples, and
the format documented. No licence decision while distribution is private, no
mod page, no packaging.

Gate 1 in full is the price of entry, by the 2026-09-09 decision. A tester
who freezes in Whiterun or runs out of texture slots spends the evening
reporting what is already known instead of finding what is not.

It also tests the real question the decision raises. If the format is the
product, the thing to learn early is whether someone who did not write it can
author a recipe with the documents provided. That is not knowable from here.

## 5. The list, in priority order

Size is rough: S is hours, M is a day or two, L is a week or more. "Decision"
means the work is choosing, not building.

**Before anything else**

1. ~~**Back up the repository.**~~ DONE 2026-09-09: pushed to
   `git@github.com:collinthefarmer/beef.git`, main tracking origin/main.
2. **Run the checkpoint for the five unverified commits.** S, and it is the
   author's time. The actor-state tables, chained rendering and the merge
   order are all unrun. Everything later compounds whatever is wrong in them.

**Gate 1, the alpha blockers**

3. **Measure.** M. One session: time the apply burst, count the animation
   sink churn per refresh, time the three synchronous readbacks, count the
   presenter slots and the video memory a crowd consumes. Ranks items 4 to 6
   and produces the budget for item 15.
4. **Fix the crowd freeze.** L, and unknowable until 3.
5. **Replace how textures are held.** L. 512 placeholder slots and absolute
   128-to-1024 sizes against 2K and 4K armour.
6. **Fix the synchronous readback stalls.** M, and possibly the same work
   as 4.

**The format contract. 7 and 8 are cheap and unblock the long pole, so start
them while 3 measures.**

7. **Schema-parser agreement test.** S. Protects the author's contract from
   silent drift, and is worth having before the format moves.
8. **Validator CLI.** S. The parser is already engine-free. Makes every later
   format decision and every example checkable without launching the game.
9. **Decide the ecosystem policy.** M, mostly decision. Where another
   author's files live, what happens when two mods key one effect shader, how
   a user overrides, whether load order means anything. May impose format
   requirements, so it comes before the freeze.
10. **Decide the per-row note field.** S decision, S to M to build. A format-1
    change, so it precedes both the freeze and the examples that would use it.
11. **Freeze format 1.** M. Make or defer each remaining record change, in
    writing.
12. **Give the imported folder a lifecycle.** S to M. Falls out of 9: a
    generated recipe that an author's file supersedes must stop loading.

**The long pole**

13. **Write the nine examples and their walkthroughs.** L. Also the sharpest
    test the format will get; start in draft early, since it feeds back
    into 11.

**Making it presentable**

14. **Reorganise the README, clean the settings file, refresh the checkpoint
    list, clear the stale bug note.** M. The examples fill most of it.
15. **State the cost budget, then show a recipe's cost in the studio.** S then
    M. The budget is a by-product of 3; the studio already knows each stack's
    size and whether it animates.
16. **Write the format-2 promise.** S. A paragraph, and it is what makes 13
    worth an author's weekend.
17. **Set up a way to receive a bug, and define what a report contains.** S.
    Receiving reports is the alpha's whole purpose.

**Then**

18. **Closed alpha, with the long play sessions alongside it.**

**Before anything public**

19. ~~**Decide the licence.**~~ DONE 2026-09-09: GPL-3.0 or later, forced by
    the two vendored headers. Remaining: state permissions on the mod page.
20. **Write the Community Shaders maintenance loop.** S. How a CS release is
    noticed, how fast the layout is re-verified, what a user sees meanwhile.
21. **Packaging.** M. Archive, the 47 MB debug file split out, a version
    number and changelog, the requirements stated.

**Not blocking, by the 2026-09-09 decision:** Design mode, the precision
tools, the raycast and world tools, and step 5 of the actor-state plan
(an edit re-placing one instance rather than retiring the actor).

## 6. Order

1. Run the checkpoint for today's five commits. Nothing else starts until the
   actor-state tables are known good.
2. Gate 1, measured first: time the apply burst, count the sink churn, time
   the readbacks, and count the slots and video memory a crowd consumes. Fix
   what the numbers name, in the order the numbers rank.
3. Gate 5's cheap half: the schema-parser agreement test and the validator.
   Both are small, and the validator makes every later format decision easier
   to check, including the examples.
4. Gate 5's format freeze: make or defer each record change, in writing.
   Settle the per-row note question here, since the examples depend on it.
5. The example set, in order, each with its walkthrough. Writing them is the
   best test of the format there is: whatever is awkward to teach is a design
   problem found before anyone else finds it.
6. Gate 3, the documents, with the README reorganised so the format leads and
   the deleted pipeline's sections go. The examples fill most of it.
7. Closed alpha. Gate 4's long sessions run alongside it.
8. Gate 2, the licence decision, before anything public.
9. Packaging.

Steps 3 and 4 can start while step 2 is measuring. Step 5 is the long pole
and is worth starting early in draft, since it feeds back into step 4.
