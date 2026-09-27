Status: record. The user's in-game findings from the 2026-09-12 run, with the
follow-up analysis.

# In-game regression feedback — 2026-09-12

## Investigation follow-up — empty default shell stretches

User reports: **“an empty shell (cleared, defaults applied) with diffuse selected
and no layers exhibits stretching.”** This establishes stretching without authored
layers in this reproduction. Exact armor, recipe, camera, and UI reset control
were not restated.

Preserved [live log snapshot](regression-evidence/2026-09-12/empty-default-shell-stretch.log).
At `15:49:50.296`, player armor `000FE2FF` has two geometries logged as
`material=untouched shell (PBR copy, additive, skin cloned, buffers shared,
emissive storage cloned, inflation own skin data (copied))`. This is consistent
with shell creation without a body-material binding. The log does not record
layer counts or resolved inflation values, and the exact action timestamp has
not been confirmed.

In the current source, `ResetShell` assigns `ShellSettings{}`, whose inflation
defaults to `(0,0,0)`. `ClearOutputs` also calls `ResetShell`. This moves cloned
skin attachment, skin-data copying, and matrix/partition handling ahead of
nonzero inflation or layer compositing as stretching suspects. It does not yet
prove the running DLL or effective recipe uses those exact defaults.

## Session scope

Information gathering for a later developer handoff. Record user observations,
reproduction details, relevant log evidence, and follow-up questions. Do not infer
test success from logs alone. No implementation changes requested.

Test procedure: [in-game-regression.md](../in-game-regression.md).

User-provided log: `A:\home\documents\My Games\Skyrim Special Edition\SKSE\BetterEnchantmentEffects.log`.
Local access: `/mnt/a/home/documents/My Games/Skyrim Special Edition/SKSE/BetterEnchantmentEffects.log`.

Current log copy saved alongside this report at the user's request:
[regression-feedback-2026-09-12-current.log](regression-feedback-2026-09-12-current.log).
This is a snapshot taken when requested, not a live link. Earlier snapshots remain
in `regression-evidence/2026-09-12/`.

## Initial evidence

- Preserved [initial log](regression-evidence/2026-09-12/initial.log) before any further reports; the live log is truncated on plugin launch.
- Log startup: `11:35:16.950`, plugin `0-1-0-0`, Skyrim runtime `1-6-1170-0`. Log timezone unconfirmed; filesystem modification time was `2026-09-12 15:45:27 UTC`.
- Workspace revision: `cc2e135`; correspondence to the running DLL is unconfirmed.
- Startup settings include PlayerOnly=false, shaders enabled, both camera modes enabled, UniqueMaterial=true, verbose logging, 60 animation FPS, speed 1, Full texture scale.
- At `11:45:27.474`: `9 loaded, 0 with errors, 0 unresolved editor IDs, 0 imported this session` after reload. Earlier startup entries show successful imports with `reads back identical`.
- At `11:45:27.475`: player actor `00000014`, armor `040291BA`, recipe `Skyrim-92DEC`, private shell application and lights reported. These are log observations only; visual correctness is unconfirmed.

## Feedback

### F01 — Checkpoint 1.4: shell stretching at arms

User report:

> 1.4: enchanted armor appears on body with effects applied. shell mesh appears to stretch towards level origin(?) at the arms. light appears to work.

- Result: visual defect reported; checkpoint is not a clean pass. Armor and effects are visible, and light appears functional. Board population and retire/re-apply baseline comparison have not yet been reported.
- Expected: shell follows the armor and arm pose without stretched geometry.
- Actual: shell stretches at the arms. Direction toward the level origin is the user's tentative interpretation, not a confirmed destination or cause.
- Log checked after report: latest entries remain `11:45:27.475–476`, already preserved in the initial snapshot. Player armor `040291BA` has `material=private shell (PBR copy, additive, skin cloned, buffers shared, emissive storage cloned, inflation own skin data (copied))`; recipe `Skyrim-92DEC` reports lights at `NPC Spine2 [Spn2]` and `NPC Pelvis [Pelv]`. Correlation to the observed item and exact observation time remains unconfirmed. These entries do not establish the cause of stretching.
- Follow-up answers:
  - Equipped item: **Nordic Armor of Eminent Health**.
  - Seen in **third person**; first person does not appear to have the same issue (user's qualified observation).
  - Present consistently on **both arms**. The stretched endpoint appears fixed in the world while the arm-side origin moves with the arms. Actual world coordinates are unknown.
  - **Retire all (baseline)** removes the stretch; **Re-apply all** brings it back. This confirms repeatability across retirement/reapplication, not a root cause.
- Additional scope observation: **Daedric Helmet of Recovery** shows no stretching. This checks another item but does not establish whether other body armors are affected.
- Second affected body armor: user equipped **Daedric Armor of Revival** and reported the same sleeve stretching behavior as the Nordic armor. The defect is therefore observed on at least two body armors; it is not limited to Nordic armor. Camera mode, fixed-endpoint behavior, and retire/re-apply recovery were not separately reconfirmed for the Daedric armor.
- User confirmed **Daedric Helmet of Recovery was still equipped** when testing Daedric Armor of Revival.
- Expanded affected regions: user also reports that the **pelvis piece of both Nordic Armor of Eminent Health and Daedric Armor of Revival appears to stretch**. Fixed-endpoint behavior was described earlier for the arms; it has not been separately confirmed for the pelvis pieces.
- Later scope refinement: user reports stretching **appears to occur only with vanilla-imported recipes**. The authored **red emissive effect does not stretch**, while switching to a recipe such as **`Skyrim~5D606`** causes the same stretching to appear again.
- Preserve recipe spelling as reported: `Skyrim~5D606`; earlier logs list imported recipe `Skyrim-5D606`, a likely correspondence whose exact UI identifier has not been reconfirmed.
- This narrows the observed comparison to the authored material-emissive effect versus an imported effect. It does **not** establish that all imported recipes stretch, that all authored recipes are unaffected, or that import itself is the cause: shell presence/output configuration also differs in the reported examples.

### F02 — Equipping helmet changes chest armor appearance

User report:

> equipped a "Daedric Helmet of Recovery". no stretching issues, but it did completely change the visuals of the Nordic armor when equipped. The helmet has a deep purple in some areas and a cyan in others, and this was applied to the chest armor as well.

- Context: follow-up during checkpoint 1.4; also relevant to matching and composition checks.
- Trigger: equip **Daedric Helmet of Recovery** while **Nordic Armor of Eminent Health** is equipped.
- Actual: Nordic chest armor appearance changes completely to the helmet's deep-purple/cyan appearance. Helmet itself has no reported stretching.
- Expected to verify with developer: each item's enchantment appearance remains appropriately scoped when another enchanted item is equipped. Recipe scope and cause have not been established.
- Result: unexpected cross-item appearance change reported; reproducibility and recovery pending.
- Evidence: [log snapshot](regression-evidence/2026-09-12/helmet-appearance-change.log). At `11:52:34.632–635` and `11:52:35.185–186`, player `00000014` has private shell applications for armor IDs `000FE303` and `040291BA`, with `3 piece(s), 2 recipe(s) applied`. Recipe `Skyrim-92DED` reports a head light and `Skyrim-92DEC` reports spine/pelvis lights. At `11:52:59.549`, only `000FE303` is logged with one recipe. Exact report timing is unconfirmed; logs alone do not identify why colors change.
- Follow-up questions pending:
  1. Does removing the helmet restore the Nordic armor's previous appearance, and does re-equipping the helmet repeat the change?
  2. What colors/effect did the Nordic armor have before the helmet was equipped?
  3. With both items equipped, does retiring/re-applying preserve the same purple/cyan result on the chest?

Follow-up with **Daedric Armor of Revival** (distinct from the original Nordic armor report):

- Helmet was equipped when this body armor was tested.
- User reports the Daedric armor has the same coloring as the helmet.
- Unequipping the helmet and retiring/re-applying does not change the effect. In context this concerns the appearance/color; whether sleeve stretching was also being described is not separately confirmed.
- This does not yet establish persistent cross-item contamination: the Daedric armor's independent appearance before any helmet application has not been observed, and whether the two items resolve to the same recipe is unconfirmed.
- Next question: with the helmet off, does switching back to **Nordic Armor of Eminent Health** restore its original appearance, or does it retain the purple/cyan appearance too?
- Earlier Nordic-specific recovery and original-color questions remain unanswered; do not treat the Daedric result as their answer.

Further Nordic follow-up:

- Asked whether switching back to Nordic Armor of Eminent Health with the helmet off restores its earlier appearance. User answered: **"Still purple/cyan."** Thus helmet removal followed by switching back does not restore the earlier Nordic appearance in this session.
- The previously reported retire/re-apply check was on Daedric Armor of Revival. A separate retire/re-apply check after switching back to Nordic armor has not been reported.
- Preserved another [log snapshot](regression-evidence/2026-09-12/persistent-color-and-pelvis-stretch.log) on receipt of this report. Exact action timestamps are not confirmed.
- Remaining clarification: what colors/effect did the Nordic armor show before the helmet was first equipped? This establishes the visual before/after independently of recipe assumptions.

Original appearance clarified:

- Before first equipping the helmet, Nordic Armor of Eminent Health had **a sheen and a red light**.
- The **red light is still present** after the purple/cyan appearance change. User has not specified the original sheen's color. This documents that the visual change did not remove the reported red light.

### F03 — Apparent effect texture stretching or misalignment

User report:

> Also, the effects do not appear to be aligned with any pieces of the armor. Seems like the texture is being stretched or misaligned.

- Actual: effect patterns appear unaligned with armor pieces; user describes apparent texture stretching or misalignment.
- Track separately from F01's stretched shell geometry until their relationship is established. UV mapping, texture source, and root cause are unconfirmed.
- Exact affected items and whether this is visible on intact torso surfaces versus only the stretched sleeves/pelvis have not been established.
- Follow-up questions: is the misalignment visible on the main chest surface as well as the stretched sleeves/pelvis, and does the helmet show it too? Does the pattern stay attached to the armor when moving/turning, or slide across it?
- Follow-up answer: **helmet does not appear misaligned**. Main chest involvement and whether the pattern slides during movement remain unanswered.

### F04 — Viewing recipe layers changes the visible effect without edits

User report:

> opening the recipe editor (didn't make any changes, only selected specific layers to view in the editor) removes the purple/cyan and replaces it with a generic (white, transparent) sheen

- Trigger: open recipe editor and select specific layers for viewing. User explicitly reports **no edits**.
- Actual: purple/cyan appearance disappears and is replaced by a generic white, transparent sheen.
- Context: follows F02's persistent purple/cyan armor appearance. Exact selected recipe/layers and affected equipped item(s) remain unconfirmed.
- Expected to clarify: whether this is a temporary selection preview or an unintended lasting appearance change. Do not infer mute/solo actions, recipe edits, or a cause from layer selection alone.
- Preserved [log snapshot](regression-evidence/2026-09-12/editor-selection-appearance-change.log) on receipt of this report; exact timing of editor interactions is unconfirmed.
- Follow-up questions: does the white sheen remain after closing the editor? Which recipe and layer(s) were selected? Did the change occur on opening the editor or only after selecting a layer?

Follow-up answers:

- **Yes, the white transparent sheen persisted after closing the editor.**
- User viewed every layer applied by default to vanilla effects, identifying **material height**, **material rmaos**, **shell emissive**, and **shell fuzz**. These are the user's names for the entries viewed; exact recipe ID and selection order remain unconfirmed.
- While exploring which layers had been selected, the armor **returned to its initial appearance from before the purple/cyan change**. User describes the restoration as unexpected; the exact interaction responsible is unknown.
- Observed sequence: initial Nordic sheen/red light → purple/cyan after helmet equip → purple/cyan persists with helmet removed and Nordic re-equipped → white transparent sheen after viewing editor layers, persisting on editor close → original appearance restored during further layer exploration.
- This is a reported recovery of appearance, not a confirmed fix or evidence that shell stretching recovered.
- Remaining focused question: after the original appearance returned, did sleeve/pelvis stretching remain, and was any particular layer selection immediately associated with the restoration?
- User confirmed **sleeve and pelvis stretching remains after the original appearance returned**. Appearance recovery did not resolve the geometry defect. The specific interaction that restored appearance remains unknown.

### F05 — Unenchanted Ebony helmet equip

User report:

> equipped an unenchanted ebony helmet and the effect is retained. no purple/cyan.

- After the preceding appearance recovery, user equipped an **unenchanted Ebony helmet**.
- User reports the effect is retained and **purple/cyan does not return**.
- "Effect is retained" has not yet been localized: confirm whether this means the chest keeps its restored sheen/red light, or whether the unenchanted helmet itself also shows an effect. Do not mark the helmet as an unaffected negative control until clarified.
- Clarification: user meant **the chest keeps its restored sheen/red light**, not that the unenchanted helmet shows an effect.
- User then re-equipped the **enchanted Daedric Helmet of Recovery** and reports the chest effect is still retained. The earlier purple/cyan change did **not recur on this re-equip after editor exploration restored the original appearance**.
- F02 remains an observed issue with inconsistent reproduction across session states; the initial helmet-equip trigger alone is insufficient to reproduce it in the current state. No cause or fix has been established.

### F06 — Checkpoint 2.1: new material emissive recipe has no visible effect

User report:

> 2.1: created new recipe for daedric armor of revival with 1 emissive layer on material. source of 1,1,1; color 1,0,0. no change in visuals.

- Item: **Daedric Armor of Revival**.
- Authored setup: new recipe, one material emissive layer, source `1,1,1`, color `1,0,0` (values as reported; exact field/source type not yet confirmed).
- Expected: visible red emissive contribution on the armor.
- Actual: no visual change. Checkpoint 2.1 is not passed; matching, output generation, and rendering have not yet been distinguished.
- Preserved [log snapshot](regression-evidence/2026-09-12/material-emissive-no-visible-change.log). At `12:11:45.789–790`, player `00000014` receives private material/no-shell applications on armor `000FE300`, geometries `TorsoLow:0` and ` (00098BB3)[0]/ (000FE300) [75%]`; actor summary reports `3 piece(s), 1 recipe(s) applied`. The excerpt does not name the applied recipe, so it does not independently confirm that the newly authored recipe is the one applied or that it rendered.
- Follow-up questions: does the new recipe appear under **Resolved for the selection (merge order)**, and what recipe ID/key is shown? Is the emissive thumbnail red, and what application status is displayed? What emissive strength and layer opacity are set?

Follow-up answers:

- Recipe **appears in Resolved**.
- Reported key: **`armor:0xC5D11~skyrim.esm`**.
- Thumbnail is **entirely black in both Board and Stack**.
- Strength: **100**. Layer opacity: **1**.
- Evidence discrepancy requiring clarification: the preserved log shows player Daedric armor as `000FE300`, while `000C5D11` is applied to geometry `robes` on actor `000DCFF3` (Novice Storm Mage). The reported recipe key therefore differs from the logged player armor ID. Do not assume which actor/item the editor has selected or that this discrepancy explains the black previews.
- Next question: which actor and armor are selected in the editor/Resolved view—player with Daedric Armor of Revival, or Novice Storm Mage's robes? Application status and exact recipe ID remain unreported.

Selection clarified:

- User reports **selection: `123/Daedric...`; recipe: `Skyrim-92DEC`**. This identifies the player/Daedric selection rather than the mage's robes.
- Earlier startup/reload evidence identifies `Skyrim-92DEC` as an imported recipe with key `effectShader:0x92DEC~Skyrim.esm`. User previously reported creating a new recipe and seeing `armor:0xC5D11~skyrim.esm`; the relationship between these reported UI entries is unresolved. Do not assume the selection is wrong, the recipe key was changed, or a new recipe was successfully selected.
- Next clarification: is `armor:0xC5D11~skyrim.esm` shown on the same Resolved row as `Skyrim-92DEC`, and were the emissive edits made in `Skyrim-92DEC` or in a separately named new recipe?

Recipe attribution corrected:

- User confirms the emissive layer is on **`Skyrim-92DEC`**.
- **No**, the mage robe key was not on that recipe's Resolved row. User is unsure why that ID appeared. Treat `armor:0xC5D11~skyrim.esm` as an unexplained earlier UI observation, not the established key for the edited recipe.
- Current confirmed reproduction context: selection `123/Daedric...`, edited recipe `Skyrim-92DEC`, material emissive layer with reported source `1,1,1`, red color `1,0,0`, strength `100`, opacity `1`; Board and Stack previews entirely black and no visible armor change.
- The initial "created new recipe" description is superseded for layer attribution by confirmation that the layer is on `Skyrim-92DEC`. Whether a separate recipe was also created is not established.
- Next focused question: what application status or diagnostic does the editor show for this recipe (for example, rendered, prepared/queued, or an error)?
- User renamed the edited recipe to **`abc`** and sees that name reflected in the **Resolved row**. This confirms the rename is reflected in the resolved recipe UI; it does not establish successful output rendering. Application status remains unreported, and no change to the black previews or armor visuals has been reported following the rename.
- Application status confirmed by user: **`Application: rendered`**.
- Current observed mismatch: recipe `abc` (renamed from `Skyrim-92DEC`) appears in Resolved and reports rendered, while the reported Board/Stack thumbnails are entirely black and the expected red material emission is absent. Status alone does not demonstrate correct pixels or establish the cause.
- Preserved [log snapshot at status confirmation](regression-evidence/2026-09-12/emissive-reported-rendered.log).
- Remaining reproduction detail: exact source field/type used for `1,1,1` (literal RGB/constant versus expression text or other source), including any inline diagnostic. This has not yet been confirmed.
- Source entry method confirmed: user **clicked the Source text input and typed `1,1,1`**. This was text entry, not an RGB picker/constant control. Accepted syntax, commit behavior, and any inline validation message remain unconfirmed; do not infer validity or a parser defect solely from `Application: rendered`.
- Follow-up pending: after leaving the Source field, does it retain `1,1,1`, and is any warning or validation message shown beside it?
- User confirms the Source field **retains `1,1,1` after leaving it**, with **no validation message**.

Layer-removal comparison:

- User removed the emissive layer **without clearing the slot**, retaining its settings, including **strength 100**.
- Result: the armor's **already-emissive parts appear to glow brighter**.
- This contrasts with the earlier black Board/Stack previews and absent expected red emission while the layer was present. It establishes a visible response with the layer removed and slot settings retained; it does not establish why the authored layer produces black previews.
- Preserved [log snapshot](regression-evidence/2026-09-12/emissive-layer-removal.log).
- Follow-up pending: if Undo restores the removed layer, do the existing emissive areas dim again, or does their brightness stay the same while the expected red effect remains absent?
- User confirms **the existing emissive areas dim again when Undo restores the layer**. Observed comparison with slot settings retained: layer removed → existing emissive regions brighten; layer restored → those regions dim again. This is a reversible visual change associated with layer presence; the underlying cause remains unconfirmed.
- User also confirms **muting the layer restores the existing emission**. Both removing and muting the authored layer restore emission; restoring the removed layer dims it again. Unmuting has not been separately reported.

Later recovery observation:

> Upon closing and reentering Skyrim, it appears that the red emissive layer IS applied to the whole armor.

- User now observes **red emission applied across the whole armor** after closing and reentering Skyrim.
- Meaning of closing/reentering remains to be clarified: full process exit/relaunch, menu close/reopen, or switching away/back. Do not yet label this restart-dependent recovery.
- Current recipe, source, selector, mute state, preview colors, and whether settings changed since the earlier failure are unconfirmed. Whole-armor coverage does not establish that the previously tested `TorsoLow:0` selector was active or defective.
- F06 remains a recorded earlier failure with later visible recovery, not a confirmed fix.
- Preserved [log snapshot after reopening](regression-evidence/2026-09-12/red-emissive-after-reopening.log); prior evidence snapshots remain intact.
- Follow-up: was this a full quit to desktop and relaunch, and were the recipe settings unchanged?
- New log starts with plugin initialization at `15:10:52.496`, supporting that a fresh game/plugin launch occurred since the earlier snapshots. At `15:11:18.567`, player armor `000FE2FF` receives private materials on both logged geometries, and Faendal's `000FE300` receives a private material on `TorsoLow:0`. User confirmation of unchanged recipe settings remains needed for a controlled before/after comparison.

Cross-item observation after relaunch:

> though it was also being applied to an "Orcish Helmet of Eminent Magicka", which should've had a different enchantment effect.

- User reports the red emissive effect **also appears on Orcish Helmet of Eminent Magicka**; expected a different enchantment effect on that item.
- Relevant to F02's cross-item appearance changes, but a shared cause is unconfirmed. Current recipe keys and output scope have not been verified.
- The post-reopening snapshot logs another player armor `000CF80F` with a private shell and recipe `Skyrim-92DED` with a head light at `15:11:18.566–567`; this is a candidate correspondence to the reported helmet, not independent confirmation of its item name or the recipe responsible for its red pixels.
- Follow-up: does muting the red material-emissive layer remove the red from both chest and helmet? This would distinguish whether the helmet's observed red tracks that layer.

### F07 — Checkpoint 2.2–2.3: Player only toggle on Faendal

User report:

> player only appears to work. gave faendal a similarly enchanted piece of deaedric armor. I can see the emission level change when the toggle occurs.

- Setup: **Faendal** given a similarly enchanted piece of **Daedric armor**. Exact item/enchantment ID has not been supplied.
- Observed: visible emission-level change when **Player only** is toggled; user assesses the setting as appearing to work.
- Result: positive partial evidence for Player only filtering. Do not mark all of checkpoints 2.2–2.3 passed: toggle direction, player remaining affected, unenchanted control, and per-wearer unequip behavior have not all been confirmed.
- Current emissive-layer mute state and recipe setup during this comparison are unconfirmed; the preceding report described muting the problematic layer.
- Preserved [log snapshot](regression-evidence/2026-09-12/player-only-faendal.log).
- Follow-up: does Player only ON return Faendal to baseline while leaving the player's effect unchanged, and OFF restore Faendal's modified emission?
- User confirms **ON returns Faendal to baseline while the player's effect stays unchanged; OFF restores Faendal's modified emission**.
- Result update: **PASS for the Player only toggle behavior in checkpoint 2.3** under this setup. Unenchanted-control and per-wearer unequip coverage in 2.2 remain unconfirmed.
- Supporting snapshot entries: at `12:27:10.211`, actor `FF00095A` (Faendal) receives armor `000FE300` with a private material; at `12:27:10.662`, Faendal's recipe is retired and the player is reapplied. The log excerpt does not itself label toggle direction; direction is confirmed by the user's visual observation.

### F08 — Checkpoint 2.5: geometry-selector coverage blocked

User report:

> I don't have any multi-geometry items available to test those steps, I think.

- Result: **BLOCKED for now — no confirmed suitable multi-geometry fixture**. User is uncertain about availability; do not assert that no such items exist in the profile.
- Scope: checkpoint 2.5's single-geometry restriction, nonmatching selector, and restoration checks are untested. Other checks requiring separately selectable geometry may also need a suitable fixture.
- Existing logs list `TorsoLow:0` and ` (00098BB3)[0]/ (000FE300) [75%]` under player Daedric armor `000FE300`. These are possible fixture leads only; logs do not establish that they represent separately visible armor pieces suitable for this test.
- No additional fixture preparation requested during this information-gathering session.

Selector trial:

> TorsoLow:0 appears to work as a selector, but I can't identify which geometry that excludes so I can't tell if it's actually working properly. Using it as a selector does appear to work (increases the emissivity)

- User tried selector **`TorsoLow:0`** and observed an **increase in emissivity**.
- User cannot identify which geometry is excluded, so the observation supports a visible effect with this selector but does not confirm geometry isolation.
- Result update: **partial observation; checkpoint 2.5 remains blocked for full verification** because excluded surfaces cannot be identified. A nonmatching-selector negative check and selector restoration are not yet reported.
- Follow-up: with a deliberately nonexistent selector such as `NoSuchGeometry_Test`, does the extra emission disappear, and return when `TorsoLow:0` is restored?
- User confirms **the nonexistent selector removes the extra emission, and restoring `TorsoLow:0` brings it back**, with validation message **`selector did not match`** for the nonmatching selector.
- Result update: **PASS for the nonmatching-selector negative check and recovery**. Full checkpoint 2.5 remains only partially verified: selective exclusion of another visible geometry is still unconfirmed.

### F09 — New recipe creation: unclear selection and list placement

User report:

> creating a new recipe feels a little buggy. it isn't clear if the new recipe is automatically selected or where it appears in the list.

- Usability observation: after creating a recipe, user cannot readily determine whether it becomes the active selection or where it appears in the recipe list.
- Desired clarity: visible confirmation of the created recipe's identity, current selection, and location so subsequent edits have an obvious target.
- Actual automatic-selection behavior, list placement, and whether creation succeeds consistently have not been established. Do not classify this as a confirmed creation failure.
- Potentially relevant earlier context: F06 began with a report of creating a new recipe, but the edited layer was later identified on existing imported recipe `Skyrim-92DEC`. A causal connection to this selection ambiguity is unconfirmed.
- Follow-up: which create control is being used, and immediately after pressing it does the editor's recipe name change or remain on the previous recipe?
- Control identified by user: **the `New` button in the recipe rule**. Whether the editor switches to the created recipe remains unconfirmed.

### F10 — After relaunch: Studio armor selection removes effect from all armor

User report:

> opening the Studio and selecting a recipe from the dropdown removes the effect from all armor.

- Context: after red emission became visible on the body armor and unexpectedly on Orcish Helmet of Eminent Magicka following relaunch (F06 follow-up).
- Corrected trigger: open **Studio**, then use the **Selection dropdown to select the Daedric armor on the player's character**. User explicitly clarified that the **Recipe dropdown was not used**; the original report's wording above is superseded on this point.
- Actual: user reports the effect disappears from **all armor**. In context this concerns the red emissive effect; whether other effects/lights also disappear and whether "all armor" includes NPCs are unconfirmed.
- No layer edit or mute action was reported for this trigger. Do not treat this as an answer to the preceding proposed mute comparison.
- Related observation: F04 also describes appearance changing while viewing recipe/layer selections; a common cause is unconfirmed.
- Preserved [log snapshot](regression-evidence/2026-09-12/studio-dropdown-removes-effect.log).
- Selection target confirmed: **player's Daedric armor**. Remaining follow-up: does the effect remain absent after closing Studio?
- Recovery reported: user **reloaded the save, which reapplies the red emissive effect**. A full game restart is therefore not the only reported recovery action. Whether merely closing Studio would have restored it remains unanswered.
- Current observed sequence: relaunch → red visible (including unexpected helmet coverage) → select player's Daedric armor in Studio's Selection dropdown → effect disappears → reload save → red emission returns.
- Preserved [log snapshot after save reload](regression-evidence/2026-09-12/save-reload-restores-red.log).
- Follow-up: after this save reload, does selecting the same Daedric armor in Studio remove the red again? This would establish repeatability of the selection/reload sequence.

Further reproduction clarification:

- After reloading, **opening Studio does not appear to change anything**. Do not treat merely opening Studio as a consistently reproduced trigger. The earlier Selection-dropdown observation remains recorded, but its repeatability has not been established.

### F11 — Solo toggling and Paint transitions change effect state

User-provided sequence:

1. Reload save.
2. Red emissive appears on **helmet and body armor**.
3. **Solo armor** appears to work: red emission disappears from the helmet.
4. Toggle Solo again: helmet shows **a light white overlay**, not the red emission.
5. Toggle Solo **twice more**: red emissive returns.

- User also sees a **similar state change when toggling back and forth from the Paint menu**. Exact Paint transition sequence has not been separately specified.
- Observed issue: exiting the initial Solo state does not immediately restore the preceding helmet appearance; further toggles restore red emission. Whether the initial red helmet appearance is itself correct remains disputed by the earlier cross-item report.
- Preserve the user's action count: two additional Solo toggles after the white-overlay state, not one.
- Exact Solo control label, intermediate state after the third toggle, and body armor appearance during the helmet's white-overlay state remain unconfirmed.
- Preserved [log snapshot](regression-evidence/2026-09-12/solo-paint-state-cycle.log).
- Follow-up: while the helmet has the white overlay at step 4, does the body armor keep its red emission? Does this same cycle recur after another save reload?
- User adds that **continuing to toggle eventually applies the purple/cyan effect**. The sequence is therefore not established as a fixed red/white cycle; it can reach another appearance with further toggles. Exact toggle count, Solo state, and affected item(s) at the purple/cyan transition remain unconfirmed.
- Preserved [additional log snapshot](regression-evidence/2026-09-12/solo-eventually-purple-cyan.log).
- Next clarification: does purple/cyan appear on the helmet, the body armor, or both?
- White-overlay state clarified: **when the helmet is white, the body armor has no visible effect applied**, according to the user. It does not retain red emission in that state. This is a visual observation, not confirmation that runtime bindings have been removed.
- Purple/cyan scope clarified: **both helmet and body armor display purple/cyan** after continued toggling.
- Confirmed appearance states during this exploration include red on both items, white overlay on helmet with no visible body effect, and purple/cyan on both items. Transition counts beyond the initial reported sequence remain unconfirmed; do not present these as a deterministic repeating cycle.

Detailed sequence supplied afterward (supersedes the earlier lack of exact toggle counts):

- User reports there **does appear to be a repeating order**, starting from a save reload.
- Selection: **`123/Daedric Armor of Regeneration`**. Recipe: **`abc`**. This names the body item for this sequence specifically; earlier Revival observations remain separate.
- Prior report establishes the initial post-reload appearance as red emission on helmet and armor.

| Toggle from reload | Solo state | Helmet appearance | Body armor appearance |
| --- | --- | --- | --- |
| 1 | On | Baseline | Red |
| 2 | Off | White | Baseline |
| 3 | On | Baseline | Baseline |
| 4 | Off | Red | Red |
| 5 | On | Baseline | Baseline |
| 6 | Off | White | White |
| 7 | On | Baseline | White |
| 8 | Off | Red | Red |
| 9 | On | White | Red |
| 10 | Off | Purple/cyan | Purple/cyan |

- Step 10 was reported simply as "purple/cyan"; both-item scope follows the user's immediately preceding clarification.
- Preserve this as the user's apparent repeating order. Whether all ten states repeat identically after another reload or continued toggling has not yet been explicitly confirmed.
- Preserved [log snapshot for ten-toggle sequence](regression-evidence/2026-09-12/solo-ten-toggle-sequence.log).
- Follow-up: does another save reload reproduce the same ten states in order?
- Follow-up outcome: reloading instead produced **red body armor and baseline helmet**. This differs from the earlier red-on-both starting appearance. User then immediately reloaded again and reported a crash (F12); no second full ten-toggle sequence was completed.

Later pinned vanilla-recipe comparison:

> solo-toggling cycling the vanilla recipe (pinned here) walks through the red/white/purple+cyan steps described earlier

- User reports the **vanilla recipe is pinned**, and **Solo toggling cycles through the previously described red, white, and purple/cyan states**.
- Immediate preceding context named imported recipe `Skyrim~5D606` (likely logged as `Skyrim-5D606`), but the exact pinned recipe identifier was not restated in this report.
- This extends the observed color cycling to a pinned vanilla-recipe context, beyond the earlier `abc` sequence. Exact toggle counts/order and per-item appearances were not separately enumerated for this run; do not copy the ten-step table as independently reconfirmed.
- Preserved [log snapshot](regression-evidence/2026-09-12/pinned-vanilla-solo-color-cycle.log).

### F12 — Crash on immediate second save reload

User report:

> reloading produced red armor, baseline helm. immediately reloading again crashed.

- Context: following the Solo-toggle exploration in F11.
- Sequence: reload save → red body armor / baseline helmet → immediately reload again → crash.
- Result: **FAIL — reported crash during repeated save loading**, relevant to checkpoint 8.4. Cause and repeatability are unconfirmed.
- Preserved [plugin log at crash](regression-evidence/2026-09-12/second-reload-crash.log) before another launch can truncate it.
- Log evidence: first load has `kPreLoadGame` at `15:25:49.911`, `cleared 2 actor states`, then `kPostLoadGame` at `15:25:50.650` and applications. Next load has `kPreLoadGame` and `cleared 2 actor states` at `15:25:54.426`, where the captured log ends. There is no subsequent `kPostLoadGame` in this snapshot. This locates the last recorded lifecycle event, not the crash cause.
- No crash report matching this event was visible in the SKSE directory listing at capture time; listed crash reports predate this session's crash.
- Follow-up: was the same save loaded both times, and was Solo still enabled when reloading?
- User confirms **the same save both times, using Quickload**. Reproduction action is two consecutive Quickloads after Solo exploration; the first completes with red armor/baseline helmet, and the immediate second crashes. Solo state at Quickload remains unconfirmed.
- User confirms **no changes between the first Quickload and the second**. No intervening setting, recipe, or selection change is reported; do not infer the Solo state before the first Quickload from this answer.

### F13 — New Solo run from reload with Solo initially disabled

- User explicitly starts this run **from reload with Solo disabled**. Whether this follows a full process restart as well is not specified.
- Reported sequence:

| Toggle | Solo state | Observation |
| --- | --- | --- |
| 1 | On | Solo works as intended. |
| 2 | Off | Red helmet and red body armor; helmet appears to have a white overlay stacked over its red emission. |
| 3 | On | Solo works as intended. |
| 4 | Off | Same as step 2. |
| Further toggles | Alternating | Same behavior repeats, per user. |

- This run differs from F11's changing red/white/purple-cyan sequence. Preserve both observations; the cause of the difference is unknown. Initial Solo state is explicitly known for this run.
- User clarifies that **previously described red helmets did not exactly match the armor's red emission**: they appeared to have a white overlay on top of the red. Earlier "red helmet" shorthand should not be interpreted as identical to the body armor's effect.
- No recurrence of the purple/cyan state is reported in this run. This does not resolve the earlier issue or establish correct cross-item recipe scope.
- Preserved [log snapshot](regression-evidence/2026-09-12/solo-disabled-start-repeat.log).

### F14 — Visiting Recipes page disrupts red emission

User report:

> selecting the recipes page appears to break the red emissive. returning to studio and toggling solo *does* toggle a white overlay on the helm, but armor does not change.

- Context: follows F13's consistent Solo behavior with red emission present.
- Trigger: **select the Recipes page**, then return to **Studio**. No recipe edit or other action on the Recipes page was reported.
- Actual: red emissive appears to break. Subsequent Solo toggles change a **white overlay on the helmet**, while **body armor appearance does not change**.
- Distinguish this page-navigation trigger from the previously reported Studio Selection dropdown and Paint transitions. Whether they share a cause is unconfirmed.
- Exact body appearance after visiting Recipes (baseline versus retained red or another appearance), and whether the red disappears immediately upon entering the page, remain to be clarified.
- Preserved [log snapshot](regression-evidence/2026-09-12/recipes-page-breaks-red.log).
- Follow-up: after visiting Recipes, is the body armor at baseline with no red emission, and did merely opening the page cause the change without clicking any controls there?
- User confirms **both**: body armor is at **baseline with no red emission**, and **merely opening the Recipes page, without clicking any controls there, causes the change**. Subsequent Solo toggles leave the body at baseline while toggling the helmet's white overlay.

Fresh reproduction from reload:

> from reload: armor and helm are red. open menu, select Recipes, armor returned to baseline and helm has white diffuse overlay(? - not the same white as seen before).

- Steps: **reload save → armor and helmet red → open menu → select Recipes**.
- Result: **body armor returns to baseline; helmet shows a white overlay visibly different from the earlier white effect**.
- User tentatively describes the new overlay as "diffuse"; output family and cause are unconfirmed. Do not merge this with the previously reported white-overlay appearance as if visually identical.
- This provides another reported reproduction of the Recipes-page trigger from a red-emission starting state.
- Preserved [log snapshot](regression-evidence/2026-09-12/recipes-page-repro-from-reload.log).
