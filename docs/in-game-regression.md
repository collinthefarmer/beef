# Better Enchantment Effects: in-game regression flow

A manual integration run for the active source tree, checked against the
code on 2026-09-24.

- The order: startup → matching → actor state → GPU output → studio edits →
  persistence → teardown.
- The run uses the existing log and menu. It needs no debugger and no
  instrumented DLL.
- This is a test procedure, not a record of a completed run.

## Scripted lifecycle smoke test

The optional [console-driven quest runner](../tests/in-game/README.md) drives
apply, retire, and reapply with separate machine and visual verdicts. Its
compiled scripts and generated quest have offline checks; in-game startup
and operation are not yet accepted. Use it for the first lifecycle case, not
as evidence that the full procedure below has passed.

## Prepare a repeatable scene

Profile and records:

- Use a disposable save and a separate test profile.
- Record the DLL build/revision, the Skyrim runtime, SKSE, Community
  Shaders, SKSE Menu Framework, the armor/PBR replacer, the **recipe**
  files, and the INI.
- Keep copies of the initial INI and recipe directory outside the recipe
  root, because every JSON below that root is a candidate recipe.
- Installation alone supplies no regression recipe set.

Actors and items — prepare these with the test profile's actual forms.
Record their IDs; do not rely on load-order-dependent console commands:

- The player, wearing enchanted PBR body armor and visible PBR first-person
  gloves.
- A nearby NPC, wearing the same base armor without the enchantment. Later,
  give that NPC an enchanted copy too. Keep unrelated armor and default
  recipes out of this isolation comparison.
- A known non-PBR armor item, as the negative control.
- A quiet, consistently lit spot beside a wall for color and point-light
  checks; a brighter spot for material highlights; a door to another cell.

Settings — under **Better Enchantment Effects → Setup**:

- Enable **Enabled**, **Third person**, **First person**, **Unique material
  per clone**, and **Verbose logging**.
- Start with **Player only** enabled, **Texture scale = Full**, normal
  animation speed, and automatic re-apply enabled.
- Use **Save INI**, so startup logging is verbose on the next launch.
- Open the framework with the profile's configured hotkey.

Evidence comes from three places:

- `BetterEnchantmentEffects.log` in SKSE's log directory, normally
  `My Games/Skyrim Special Edition/SKSE` under Windows Documents. The file
  is truncated at each plugin launch: archive it before you restart.
- **Setup**'s filtered log view, which retains the latest 300 lines only.
- **Recipes**' loaded and merge-order tables and **Board**, plus
  **Studio**'s thumbnails, live values, and `Application: rendered` status.

Paths:

- INI: `Data/SKSE/Plugins/BetterEnchantmentEffects.ini`.
- Recipe root: `Data/SKSE/Plugins/BetterEnchantmentEffects/recipes/`,
  including `imported/` and `user/`.
- In MO2, inspect the winning virtual file and any generated Overwrite
  files, not only the original mod directory.

Discipline:

- Record a wall-clock start and end time for each numbered checkpoint.
- The log fragments below are literal searchable text; variable IDs and
  counts are omitted.
- Wait for application to settle, and close the menu for world checks.
  Allow up to 10 seconds as the practical timeout. A persistent
  queued/prepared state is a failure to investigate, not a successful
  application.
- A log line that says "applied" does not prove that pixels rendered.

### Animation subscription acceptance

Run these cases with one isolated actor and a recipe whose trigger responds to
an observed `anim.<tag>`. Record the action and tag; animation mods can change
the events available. Collect the JSONL trace's `metrics` / `heartbeat` fields
`sink_adds` and `sink_removes`, summed across each case. These count actual
registration operations, not events. Other participating actors can add noise.

| Case | Expected result |
| --- | --- |
| Repeated recipe edits, undo/redo and re-apply with the same actor graph | After initial attachment, zero additional sink adds/removes. Discovered tags remain available and triggering the action still drives the rebuilt recipe. |
| First/third-person transitions, equipment or race transformations that replace the graph | Events resume on the new graph; each observed replacement gives one removal and one addition. A transition that does not replace the selected graph needs neither. |
| Temporarily unavailable graph, then recovery | No crash or duplicate delivery. Observation recovers on refresh or within the one-second maintenance interval once the graph becomes available. |
| Remove the last matching effect, apply disabled rendering, retire, unload or distance-evict the actor | Its registration ends and discovery clears. Reapplying or returning creates one fresh registration. |
| Change settings with automatic re-apply disabled | Existing applied effects keep observation until the settings are applied. |
| Save-load/new-game teardown while animations are running | No old tags or triggers leak into the next session. Fresh events can populate discovery after effects reapply. |

Native tests cover controlled graph replacement and callbacks crossing teardown;
these cases remain required to validate Skyrim's real graph lifetime and timing.
Record PASS/FAIL/BLOCKED and the candidate identity for each case.

### Keyword and enchantment-effect matching acceptance

- Give a demo recipe two keyword entries. Armor with either one alone must not
  match; armor with both must match. Add an explicit armor selector: even that
  exact armor must retain both keywords, and the keyword pair alone must not
  select another armor. A keyword-only recipe needs no additional selector.
- Give an enchantment two distinct effects and key a recipe to the secondary,
  lower-cost effect. It must match and appear among the piece's offered keys.
  Reversing effect order or changing which effect is costliest must not remove
  that magic-effect match. Repeat using the secondary effect's shader key.
  Casting the same magic effect as a spell is not a matching input.
- Drive visible outputs with enchantment magnitude and cost. A winning secondary
  magic-effect or shader key must read that effect's values. Multiple matching
  entries use the highest matching cost (first entry on ties); removing the
  selected effect must not supply unrelated values. Armor-, keyword-, and
  enchantment-selected recipes still use the generic costliest effect.
- Select different effect keys for placements sharing a recipe and enchantment.
  Their signal values and phases must stay independent through an edit/reapply;
  placements with the same selected effect should share evaluation state.
- Combine the secondary-effect key with the two required keywords. Missing one
  keyword must exclude the recipe without suppressing an eligible fallback;
  adding the missing keyword must allow the specific effect recipe to select.
- Save/reload the recipes and repeat. Verify the reported selection key and
  default priority come from the non-keyword selector (or priority 20 for a
  keyword-only recipe), and explicit priority still takes precedence.

## Fixture contract

- Use small, saved test recipes, authored in Studio or as JSON that follows
  [`recipe.schema.json`](../schema/recipe.schema.json).
- Freeze the fixture files with the run evidence, so the next build can
  replay the same inputs.
- These are fixture specifications, not names of bundled files:

| Fixture | Configuration and unmistakable result |
| --- | --- |
| A: base | An enchantment key for the player item; a low-strength red emissive **output** on the material. No **shell** or **light** at first. |
| B: overlay | A different matching key, such as that armor's key, with an explicitly higher priority; blue emissive added over A. Use unequal priorities and different keys, so both recipes survive key ownership. |
| C: animation/events | A slow wave driving emissive strength; an actor-state **signal** for sneaking; a trigger/counter for `hit.received`; a **ripple** using that trigger. Route each signal to a visible output in turn. |
| D: sources | A small asymmetric image, a material channel, a UV expression, a **curve**, a mesh **bake**, a distance, a ripple, and a material-cluster **source**. Feed each separately into an emissive diagnostic output. |
| E: shell/light | A visibly offset, translucent shell, and a modest colored point light on a known valid wearer bone. |

- Start with only A affecting the isolation scene. Add the other fixtures
  at their checkpoints, then disable or remove their test keys before you
  return to A.
- An armor key affects unenchanted copies too. That is intended.
- A missing fixture makes its cases **blocked**, never implicitly passed.

## Ordered run

### 1. Boot, import, and first application

1. Launch through SKSE and load the test save. Confirm
   `loading on runtime`, `kDataLoaded`, `settings loaded from`, `recipes:`,
   `SKSE Menu Framework pages registered`, `event sinks registered`, and
   `hooked PlayerCharacter::Update` in the file log.
2. For the import branch, use an armor enchantment with a vanilla effect
   shader whose effect-shader key is not in the test recipe set. Reload
   recipes after you prepare that condition. Confirm `imported recipe` with
   `reads back identical`, the generated JSON, and the imported row in
   Recipes. Reload again: the existing imported recipe loads without a
   second import for the same effect-shader key.
3. Equip the enchanted PBR armor and select it in Studio. Confirm
   `TextureLab: ready (runtime layer textures)`,
   `PBR material layout check passed`, `apply armor`, `material=private`,
   and `recipe(s) applied`. The layout check runs once per process, not
   once per equip.
4. Confirm an effect on the intended **geometry**, a populated Board, and
   no failed application. Retire it with **Recipes → Retire all
   (baseline)**, capture the original appearance, then **Re-apply all** and
   capture the effect.

Pass: initialization and first rendering succeed; imported files read back;
the baseline and applied states are visibly distinct. Recipe totals depend
on the profile: record them, do not expect a fixed count.

### 2. Matching, selection, and per-wearer isolation

1. Apply A. In **Resolved for the selection (merge order)**, verify its
   recipe ID, key, priority, geometry, and private material. The player
   glows red.
2. Disable **Player only**. The NPC's unenchanted copy stays at baseline.
   Equip the enchanted copy on the NPC: both actors now show the effect.
   Unequip it from one actor: only that actor loses the effect.
3. Re-enable **Player only**: the NPC returns to baseline; the player stays
   affected. Restore NPC processing for the later crowd tests.
4. Equip the non-PBR control. Expect `has no PBR geometry; left alone`
   under verbose logging, and no plugin effect. Re-equip the PBR item
   successfully.
5. On a multi-geometry item, restrict A's output selector to one actual
   geometry: only that geometry changes. Use a nonmatching selector and
   confirm no output on the excluded geometry. Restore the selector.
6. For matching coverage, repeat A with one key kind at a time:
   `magicEffect`, `enchantment`, `effectShader`, `keyword`, `armor`,
   `material`, `default`. Record the matching key Recipes shows, and one
   positive and negative item where applicable. A default key has no
   key-level negative control. Remove broad keys before you continue.

Pass: the affected actors and geometries agree with the keys and selectors;
no effect leaks to the control wearer. Application counts may include
first-person **pieces**.

### 3. Shared binding and layer composition

1. Enable A and B together. Confirm both appear in merge order. Red plus
   blue produces a combined result. The result stays stable for 30 seconds
   and after closing and reopening the menu; neither contribution
   disappears on the next tick.
2. Set the higher-priority output's `replace` flag: its result cuts out the
   lower contribution. Clear the flag: the combined result returns.
3. Reorder two visibly different **layers** inside one output, vary
   opacity, and mute or solo layers and outputs. Check that the thumbnail
   and the armor agree. Clear every solo and mute override.
4. Animate A; keep B static. B's composite must follow the changing
   underlying result. Verify Recipes labels the dependent output animated.
5. Remove B's output, then A's output. The first removal leaves A visible.
   The second restores the original material **slot**. Undo both removals.

Pass: one stable combined appearance, a correct replace cut, and no
material fight. An unexpected `dropping` line in this controlled scene
requires investigation.

### 4. Signals, clocks, and actual game events

1. Apply C's wave. Watch at least two full periods in the live signal
   value and on the armor. Freeze the clock footer, step with `>|`, scrub
   `t (s)` to two distinct times, then unfreeze. Hold is stable; step and
   scrub change it; motion resumes.
2. Compare the clock footer's speed at 0.5x and 2x. Restore 1x. Separately,
   vary Setup's **Animation FPS** and **Animation speed**; check continued
   motion; restore the original values.
3. Route sneaking to the visible output. Sneak and stand twice: the value
   and the effect follow the wearer. Route a current actor value such as
   magicka, spend some with a spell, and let it recover: the live value and
   the effect follow it.
4. With `hit.received` wired to the counter and ripple, take one controlled
   hit. Record the counter delta and the visible front. Repeat after a
   re-apply: one hit must not multiply its response after repeated
   re-applies. Exercise `hit.dealt` separately by striking a target.
5. When the fixture uses an animation event, trigger its documented game
   action and verify the signal. Record the actual `anim.<tag>` the profile
   uses. Do not substitute a manually fired trigger when the test target is
   the engine event sink.

Pass: values, clocks, and pixels agree. The existing logging does not
acknowledge every event; the counter and the live signal are the
observables for event delivery.

### 5. GPU sources, previews, and all output families

For each row, show only that diagnostic output, compare its thumbnail to
the armor, change one parameter, then restore or remove it. Use moderate
strengths.

| Case | Action and confirmation | Existing log evidence |
| --- | --- | --- |
| Image/copy | Use an asymmetric image; tile, offset, mirror, and select a channel. Orientation and channel change as expected. | Application logs; inspect pixels. |
| Material/curve | Display a material channel, then apply a threshold or curve. Recognizable armor detail becomes a sharply different **mask**. | Material sampling logs, when sampling is requested. |
| UV/expression | Display `u`/`v`-based bands and invert the expression. The bands move and invert on the same surface. | Application status plus thumbnail. |
| Mesh bake/distance | Display position, partition, or component data; move a distance origin. Regions stay attached to the posed armor. Repeat the same request. | `mesh '`, `bake '`; `cached` may appear on reuse. |
| Material clusters | Select a visibly different cluster on an armor with distinct materials. The highlight moves between those regions. | `sampling`, then `sampled` with dimensions and cluster count. |
| Ripple | Fire the configured event, watch a traveling front, then let it decay. The idle output returns to black without a stale ring. | The live trigger/counter and pixels; no dedicated success log. |

Slot sweep — test all nine Board slots separately: **diffuse**,
**emissive**, **rmaos**, **normal**, **height**, **fuzz**, **glint**,
**coat**, **subsurface**.

- Use a tint for diffuse, strength for emissive, roughness for rmaos, a
  patterned normal and height, and an obvious scalar change for the
  remaining slots.
- Rotate the camera in the brighter spot for highlights.
- For every slot, check the written/original information in its Board
  tooltip, and check baseline restoration when the output is removed.
- Glint has scalars only: do not require a texture thumbnail.
- Coat and subsurface share a map. Test them separately, and check that an
  incompatible combination displays an exclusion or refusal instead of
  silently corrupting the other output.

Resolution sweep:

- Repeat a detailed bake at Full, Half, Quarter, then Full texture scale.
- Expect corresponding `bake '…' … at … px` sizes, subject to the 64–4096
  clamp, and the same spatial pattern at different sharpness.
- Use an unclamped input to prove the ratios. Record the actual dimensions.
- Ordinary output success alone proves neither resolution scaling nor cache
  efficiency.

Pass: every available source and output path renders and restores. An
unexpected `TextureLab:` failure, a persistent blank preview, unrelated
world or UI corruption, or a visually unchanged diagnostic is a failure. An
unsupported fixture/material combination is blocked, with its displayed
reason.

### 6. Shells and point lights

1. Apply E beside the wall. Confirm the shell details in the geometry and
   application output, and one visible translucent offset layer. Walk,
   turn, crouch, and draw a weapon: the shell follows the pose, with no
   detached or duplicate geometry.
2. Change the shell alpha and offset, then restore them. Toggle the shell
   output off and on, and repeat equip/re-apply five times: shell thickness
   must not accumulate.
3. Change the light color and range, and walk toward and away from the
   wall. The wall illumination changes, not only the armor's emissive
   color. Remove the light: the illumination disappears. Re-add it and
   unequip: it disappears again.
4. Test a deliberately nonexistent bone on the disposable light fixture.
   Expect `light: bone '` with `not found on the wearer`, no crash, and a
   successful recovery after you restore the valid bone.

### 7. Studio edit, paint, validation, and persistence

1. On a test recipe, change the emissive color, Undo, then Redo. Confirm
   each visible state and the dirty indicator. Repeat with Ctrl+Z/Ctrl+Y
   and no text field active. Rename a referenced signal and confirm the
   references still resolve.
2. Enter malformed text in a typed field, and separately try an expression
   with an unknown reference or a cycle. Confirm inline diagnostics or
   `not applied:` / `edit refused:` / recipe row diagnostics, as
   appropriate. Do not assume every invalid input takes the same path. No
   crash, no unrelated-output corruption, and no falsely successful
   application is acceptable. Restore the valid expression.
3. Enter **Paint** from the selected recipe. Wait for the temporary paint
   preview (`paint: previewing`). Add a conspicuous region term, change its
   threshold, mute and solo it, Undo and Redo, and compare the mask
   thumbnail to the armor preview.
4. Leave Paint without Keep: the temporary preview disappears, and the
   authored recipe is unchanged. Re-enter, build the same mask, and choose
   **Keep → Keep as regression_mask**. Expect
   `keep: mask regression_mask written into` the intended recipe. Use the
   mask in an emissive layer and confirm the same region.
5. Start another paint preview, and switch actor or piece, or reload
   recipes, while it prepares. The old result must not appear on the new
   selection, and must not commit into another recipe. Return to Compose
   and confirm normal rendering recovers.
6. **Save** the test recipe. Expect `recipe … saved to …`; record that
   exact file. Change the color without saving, then **Revert to file**:
   expect `reverted to its file` and the saved color. **Reload recipes**
   retains the saved mask and color. An edited imported recipe saves under
   `user/`.
7. Change a Setup preference, **Save INI**, change it again, then **Reload
   INI**. Confirm `settings: saved`, `settings loaded from`, and the saved
   value. Disable automatic re-apply, change an apply-time setting, verify
   `re-apply needed`, press **Re-apply**, and restore automatic mode.
8. Archive the log, exit completely, relaunch, and load the save. Saved
   recipe and INI changes survive. Unsaved edits and temporary paint/solo
   state do not become saved recipe changes.

### 8. Lifecycle, camera, and repeated cleanup

1. Toggle third and first-person view ten times, with the enchanted gloves
   visible. Disable each corresponding Setup switch in turn: only the
   intended model is affected. Restore both. Do not expect body armor in
   first person.
2. Equip and unequip five times, then rapidly swap two test armors five
   times. Wait for the queue to settle: only the final equipment is
   affected, with no stale shell, light, or material. Correlate
   `recipe(s) applied` and `retired` by actor.
3. Cross the cell door and return, three times, with NPC processing
   enabled. NPCs recover their correct effects on return. Visit a populated
   area for two minutes, then return: no growing stutter, no duplicated
   effects, no warning flood.
4. Save while an effect is active. Start a visible edit or paint preview
   and load the earlier save. Expect `kPreLoadGame`,
   `cleared … actor states`, and `kPostLoadGame`; the loaded actor and
   equipment win over old queued work. Repeat three times, including one
   load with the armor unequipped.
5. Use **Retire all (baseline)** and inspect immediately, without changing
   equipment: every plugin surface change, shell, and light disappears.
   Re-apply restores them. Disable **Enabled** for a persistent baseline,
   move and swap equipment, then re-enable it. No residual effect survives
   while disabled.
6. On a separate disposable new-game run, verify `kNewGame` and a
   successful first equip. Archive the prior session's log first.
7. In a separate configuration with distance eviction enabled, move an NPC
   wearer beyond the threshold between preparation and its first render.
   Capture the application and `evict_far` trace ordering; if this timing cannot
   be reproduced, record this case as blocked. Its pending application should
   become unmatched with `the actor was retired before rendering completed`,
   rather than remain prepared. Return inside the restore threshold and verify
   a fresh attempt renders. Repeat a recipe edit/reapply to confirm retirement
   of old state does not terminate the replacement. Restore packaged defaults
   afterward; this case does not establish default-configuration performance.
8. Create mesh bakes and material-cluster analyses, then retire every applied
   actor and stop requesting previews. Wait at least 40 seconds without applying
   another actor. Confirm mesh-cache cleanup still runs and target destruction
   follows lease release; a zero allocation count is not required because idle
   targets, scratch buffers, and external consumers can remain. Repeat with
   varied armor and recipe edits: expired shared-cache keys and unused material
   analyses should not accumulate across passes. The idle pool allowance is 16
   targets / 64 MiB; active allocations are separate.
9. During genuine material/shell takeover, keep another geometry on the same
   actor active. Confirm the lost geometry stops rendering and its obsolete
   stacks release, while the sibling effect and shared instance lights continue.
   Keep a preview or external texture consumer alive through retirement, then
   release it; resource destruction must wait for that consumer. Use the takeover
   fixture requirements below rather than treating unequip as equivalent evidence.

Pass: no crash, no hang, no stale application, no duplicate response, no
visible resource accumulation. A quiet log alone is not sufficient evidence
of cleanup.

## Additional negative and coexistence runs

- Hook startup failure: with a controlled zero-relocation test fixture, expect
  `Player update hook could not resolve its vtable` in the log and an
  **Effects could not start.** menu warning with matching-target/restart guidance.
  No hook or event sinks should be installed and effects must stay disabled.
  Without that fixture, mark the visible failure case BLOCKED; native tests
  cover the refusal and do not establish in-game UI behavior. A normal boot
  must still emit `hooked PlayerCharacter::Update` once.


- Bad file and recovery: add one malformed JSON in the isolated test recipe
  directory, reload, and record its filename and diagnostic. A separate
  valid recipe must still render. Repair or remove the bad fixture and
  reload successfully. Also try an unresolved editor ID and confirm
  `matched no loaded form`.
- Key ownership: supply two differently named files with the same key and
  distinguishable colors, with the intended winner in `user/`. Expect
  `key … is owned by recipe … (loaded later)` and only the winner for that
  key. This differs from checkpoint 3, where two different keys merge.
- Vanilla effect-shader coexistence: with an installed flesh-spell, blood,
  or scripted effect-shader fixture, show its effect on the same armor
  while A is active. Retire A: the other effect continues. Let the other
  effect expire: A continues when re-applied.
- Ownership takeover: run this only when an available test mod can actually
  replace a bound material or shell. Trigger it while A is active, then
  retire A. Expect `dropping '…': its material or shell was replaced by
  another system`; the other owner's result survives. An ordinary unequip
  is not evidence of this branch. Otherwise mark the case blocked.
- Missing dependencies: in separate launches of the disposable profile,
  disable Community Shaders and expect `CommunityShaders.dll is not loaded;
  emissive path disabled, plugin idle`. Disable only SKSE Menu Framework
  and expect `SKSE Menu Framework is not loaded; editor unavailable`, while a
  saved recipe still renders. A DLL present on disk but rejected by SKSE must
  follow the same branch and must not log successful page registration. A
  loaded framework missing a required export must name it and log `editor
  disabled`, with no page registration. Verify the missing-CS message in the
  editor header when the framework remains available. Restore dependencies;
  archive each log. Export availability does not prove ABI compatibility.

## Record and acceptance

Copy this row for each numbered checkpoint and each additional run:

| Case | Build/profile | Time interval | Actor/armor/recipe IDs | Expected vs actual visual | Log excerpt / screenshot | Result |
| --- | --- | --- | --- | --- | --- | --- |
| | | | | | | PASS / FAIL / BLOCKED |

A full pass requires:

- every numbered checkpoint, with its fixtures, no unexpected errors, and a
  successful return to baseline;
- blocked and optional branches reported explicitly;
- the complete session logs, the starting and final recipe and INI files,
  and baseline/applied/retired screenshots with the same camera and
  lighting;
- the initial profile files restored, and generated test overrides removed.

What this run does not prove:

- every signal kind and expression operator, exact GPU/CPU numerical
  agreement, per-field ownership under every competing mod, the absence of
  COM leaks, or the 60 Hz / twelve-geometry performance budget. Native
  tests cover the pure logic.
- Allocation-failure and GPU live-object checks require the separate
  [GPU ownership validation](checkpoints/gpu-ownership-validation-2026-09-12.md).
- There are no per-pass timing, event-acknowledgment, or resource-count
  logs today. Do not infer those guarantees from `ready`, `applied`, or an
  absence of warnings.

The log vocabulary and controls above are grounded in `src/main.cpp`,
`src/Settings.cpp`, `src/engine/{RecipeStore,RecipeEditor,ManagerApply,
ManagerTick,Events}.cpp`, `src/render/{CompositorBake,Binding}.cpp`, and
`src/menu/`.

## Readback stall capture

Capture a fresh trace with the candidate build identity and effective settings.
Exercise first application with several armor meshes, material clustering,
source normalization, and curves both with and without `mean`. Repeat the
same operations with warm caches, then exercise source inspection and paint
preview/commit. Keep the fixture recipes and note visible frame spikes.

Run `python3 tools/trace-report.py <trace.jsonl>` for total readback counts
and timings. Inspect `metrics` events with `action=readback` in the JSONL
for `op`, `us`, `lock_wait_us`, `lock_held_us`, `map_us`, `success`, and
`bytes`. Compare successful reads separately from failures and retain the
slowest events with their session/command identifiers. Map time overlaps
lock-held time; it is CPU time in Map, not a GPU timestamp. A curve without
`mean` should not cause a mean read solely for its curve lookup; source
normalization can independently require one.

Record coverage of `buffer`, `mean`, and `pixels`; a missing kind is untested,
not zero-cost. Establish supported-workload budgets before calling spikes
acceptable. These measurements do not fulfill the outstanding requirement
to copy now and poll readbacks later.

## Long-session follow-up

Track completion in the [active alpha plan](plans/alpha-preparation-2026-09-22.md#long-session-follow-up-2026-09-25).
Use copies of the user recipes from `build/evidence/session-20260925T022309Z`,
preserving the original capture. Record DLL/build identity, package checksum,
settings, recipe changes and PASS/FAIL/BLOCKED for each case. The captured run
predates the material-editor fixes; do not use it to accept the newer bundle.

1. With multiple loaded actors and PlayerOnly=false, edit one wearer's paint
   terms repeatedly. Compare affected actor/geometry refreshes before and after
   the invalidation fix. Exercise Keep, cancel, undo/redo and a matching-key edit;
   unrelated effects must remain intact, while matching changes still propagate.
2. Repeat cluster seed/weight edits, peek changes, mute/unmute and undo/redo;
   Keep over an existing mask, save and reload. Live source rows should follow
   current dependencies, shared sources must survive, and old unrelated orphans
   must not be mistaken for newly generated accumulation. Check one material
   offer per operation across geometries and compare RGB clustering at color
   weights 0 and 1 on similarly bright, differently colored material regions.
3. Record target ownership/count/byte summaries at a fixed equipped baseline,
   while editing, after closing the editor, after unequip and after cache aging.
   Repeat the same cycle and compare recovery. Distinguish active/held targets
   from idle-pool targets and estimated texture bytes from measured GPU memory.
   If ownership diagnostics are unavailable, mark that measurement BLOCKED.
4. Try an absent partition, a world anchor with scalar hit payload, a curve
   referencing a recipe row and an empty image path. Verify clear field guidance,
   safe refusal/inert output and recovery after correction; save/reload the
   corrected recipe. Do not overwrite the captured reproduction fixtures.
5. Retain startup through final teardown in the next long run. Check trace
   rotation/drop counters and that repeated shell detail does not crowd out
   lifecycle evidence. Compare timings with diagnostic logging and packaged
   defaults separately; operation timing alone is not frame-time acceptance.

## Recipe resolution acceptance (pending runtime access)

Use the examples in [the resolution contract](recipe-resolution.md) with
distinct filename identities and visible, distinguishable layers. Preserve
the original authored files and record the candidate build identity.

1. Load two same-key stack recipes and verify both appear in merge order.
   Add a later user definition of the first identity and verify its new
   precedence, including a held-back replacement and its load diagnostics.
2. Use two same-key sampled alternatives plus an ordinary recipe. Record the
   choice for two actors and two pieces sharing one pool. Edit priorities and
   output values, reapply, save and reload; an unchanged actor ID and identity
   set must retain its sampled choice. Isolation/pinning may override it only
   while the preview is active. A selector-excluded winner must not reroll.
3. Match one shared recipe at material priority on gloves and magic-effect
   priority on boots, with another recipe at priority 30. Check opposite
   local composition orders and the priorities shown for each piece.
4. Replace one target using two sibling outputs. Verify both layer sequences
   survive, including when only the second output has replace enabled. Other
   targets survive, and an excluded replacing selector clears nothing.
5. Give equal-priority recipes different shell slots. The later definition
   owns shell settings regardless of slot ordering; retained layers from the
   earlier definition remain visible.
6. Replace actor-wide lights across two pieces, including two enchantment
   instances of the replacing recipe. Both replacing instances survive. A
   first-person-only, selector-excluded, muted, or absent light cannot clear
   other groups. Check named and skinned bones against the selected third-
   person geometry set, and verify the winning recipe in light diagnostics.
   Include an addon-only selector with matching, different, and absent addon
   identity; use two addons with identical mesh names/textures to ensure the
   actual biped addon determines eligibility. A nonmatching replacing light
   must leave the eligible base light intact.
7. Trigger a controlled preparation failure and verify diagnostic separation
   from matching and replacement; neither reroll nor revival occurs. Restore,
   retire and reapply to verify the existing ownership and lifetime guarantees.

These steps are not established by native plan tests or a successful DLL
link. Visual acceptance remains pending until Skyrim can run.
