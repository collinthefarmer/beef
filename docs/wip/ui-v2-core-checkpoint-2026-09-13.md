# UI v2: core editor checkpoint

Status: implementation integrated; automated validation in progress. No new
in-game pass claimed. The user accepted the earlier framework checkpoint.

## Scope

This integration covers stages 1-3 of the [implementation plan](../ui-v2-implementation-plan.md).
Three agents implemented preview/relationship/pattern work, gestures/input/graphs,
and document/navigation/mask work. The coordinator integrated field controls,
expression editing, status reporting, and validation.

The prior dirty tree was preserved. A second-wave source/test baseline is under
`/tmp/ui-v2-wave2-baseline`. The proposal remains frozen.

## What changed

- Browse and edit a recipe without applying it to armor. Unmatched documents keep
  editable definitions and show unavailable live values honestly.
- Follow drivers and consumers to their properties; search the navigator and pin
  a preview subject. Pins resolve textures from current snapshots.
- Tune scalar properties with sliders and exact input. A drag creates one undo
  step; Escape cancels. Unknown ranges require user-chosen slider limits.
- Tune or promote one expression number. The parser identifies the occurrence;
  revision checks refuse stale edits instead of changing a different operand.
- Connect supported actor values, current/max fractions, exhaustion conditions,
  or a received-hit response in one validated edit batch.
- Inspect response graphs for supported pulse, ramp, and trigger definitions.
  Axes show elapsed seconds and output; referenced inputs are held at displayed
  values. Unsupported formulas retain their text controls.
- Leave a mask draft, edit elsewhere, then Resume. Keep can assign the mask to
  its layer atomically. Save excludes the draft. Changed destinations are refused
  or visibly invalidated; pending Keep blocks competing mutations.
- Choose supported mask patterns using existing terms, controls, and previews.
- Scrolling image-source thumbnails now apply source sampling transforms. They
  show the sampled source before layer color, opacity, normalization, and blend.

## Deliberate boundaries

Clock controls are still global. Scoped value holds, material-region overlays,
new flame/lightning generators, and coordinated peaks belong to stages 4-5.
The pattern chooser presents capabilities that already exist.

This is a texture preview, not an embedded armor renderer. The worn armor remains
the live result. Preview ownership continues through the existing retained
texture and draw-ticket path.

## Validation

Recorded 2026-09-14 on the merged tree `9e8247c` (this wave as `8f7b8b5`,
plus critique Plans F to E and the segment-rotating trace):

- Build identity `9e8247c88ca0-3f5e6004ba107e76-Release`, source SHA256
  `3f5e6004ba107e76...`; `./build.sh Release -j 4` links with no warnings.
- `tools/gate.sh push` green: formatting, the sanitized native suite
  (66 suites), full clang-tidy matching `docs/wip/tidy-baseline.txt`,
  `tools/layers.sh`, and the no-comment check.
- One test of this wave was updated during integration:
  `tests/studio/paintsession_tests.cpp` now expects `PreparePaintCommit` to
  refuse a commit whose target lacks a signal at preparation, which is what
  this wave's preflight does.
- Installed to the MO2 mod folder by `./install.sh` on 2026-09-14. The
  in-game pass below has not been run; it is batched with the critique
  checkpoints.

## Game check findings, 2026-09-14

The user ran the complete-editor check on build
`9e8247c88ca0-3f5e6004ba107e76-Release`. The log carries no warning or error;
the trace rotated as designed. Steps 3, 4, 6 and 8 are evidenced by the
command trace (undo, edits, expression promotion, a full paint session, view
changes); the rest passed on the user's report. Layout issues are noted for
later and not listed here. Three findings for the UI owner:

1. **Custom slider ranges are keyed by layout.** `menu/Tuning.cpp` stores a
   user-set range in `MenuState::tuningRanges` under `ImGui::GetID("tune")`,
   which hashes the ImGui id stack. The wide workspace draws the inspector
   inside `Split("workspace", ...)` and the narrow one does not, so the same
   field has two ids and a range set in one layout is absent in the other
   (observed on `glossBoost`, which showed "Set slider range" again after a
   resize and kept two independent ranges). Key the range, and the number
   and text buffers in the same record, by the field's own identity from the
   form rather than the widget id.
2. **Row renaming is unreachable.** The old resource tables
   (`menu/ResourcePanels.cpp`, `DrawResources`) drew a name field on every
   signal, curve, source and mask row through `Studio::RowNameField`, which
   binds the `Rename*` edits and rewrites references. The workspace's
   per-subject inspectors in `menu/Workspace.cpp` draw the form but not the
   name field, and `DrawResources` has no caller left. The edits, the form
   builder and their native tests are intact; each row inspector needs the
   name field at its top.
3. **Row removal is unreachable for the same reason.** The remove button
   the tables drew (`RemoveButton`, guarded by the row's reference count)
   is drawn by no inspector; the `Remove*` intents are still posted only by
   `ResourcePanels.cpp`. The add menu survived (`DrawResourceAddMenu` is
   called from the navigator).

4. **Adding a layer does not select it.** "Add layer" in
   `menu/StackPanel.cpp` posts `AddLayer{output, DefaultLayer(), index}`
   and leaves the selection where it was, so the user must find the new
   row by hand. The add is asynchronous: the editor applies it on the game
   thread and the menu learns the outcome from a `RecipeEditResult` in a
   later snapshot, while `MenuState::pendingIndexedEdit` blocks output and
   layer selection until then. The fix is a follow-up subject recorded
   beside the pending edit when the add is posted (`AddLayer` already
   names the index the layer will occupy, so `LayerSubject{output, index}`
   is known up front) and applied by `AcknowledgeEditorOperations` in
   `studio/MenuState.cpp` through `Navigate` when the result arrives
   without an error. The same slot serves `AddOutput` and the resource
   adds. A native test in `tests/studio/menustate_tests.cpp` can cover it
   without the menu: post, acknowledge, assert the selection.

5. **A source cannot change kind to image, ripple or distance.** The kind
   field posts `SetSource{name, DefaultSourceKind(word)}`, a blank record
   of the new kind, and `Edit(SetSource)` in `studio/Edits.cpp` runs the
   full `CheckSource` on it before storing it. A blank image has an empty
   `path`, a blank ripple an empty `trigger`, a blank distance neither node
   nor point, so each is refused with a validation error at the top of the
   inspector, the kind never changes, and the form that would fill the
   missing field never opens. Material, uv, bake and material-clusters
   switch because their defaults are complete. The edit path is identical
   in the pre-wave tree (`a983787`), so this predates the wave. Fix: when
   the incoming kind's alternative differs from the current one, store it
   without the check and let the blank required field surface as the row's
   `problem` (a row error keeps the row inert; the validator still runs at
   save and in the snapshot), so the new kind's form opens with its empty
   field marked; add a scenario in `tests/studio/edits_tests.cpp` that
   switches material to image, then sets the path, and expects the row to
   validate. `Edits.cpp` is slice 2B's seam.

6. **The rename popup keeps stale text and hides the refusal.** The store
   is correct: the log shows `rename Skyrim-92DEC -> abc: a recipe named
   'abc' already exists` and the same for `defg -> abc`, and no recipe was
   renamed onto an existing id. Two UI defects sit on top of it. First,
   `RenameRecipeButton` in `menu/ContextRows.cpp` draws its name through
   `LiveTextField`, whose buffer lives in `MenuState::textBuffers` keyed by
   the widget and is never reset, so the popup opens showing whatever was
   typed last time rather than the recipe's current id; the Rename button
   is then disabled when that stale text equals the current id, which
   reads as "the button will not enable for an existing name". The field
   should be seeded with the recipe's id each time the popup opens.
   Second, the refusal reaches the snapshot as a `RecipeEditResult` with
   the store's message, but the popup closes on submit and only the file
   actions strip (`menu/RecipeActions.cpp`, `DrawRecipeResults`) draws
   that result, so the user sees no reason. The popup should stay open on
   a refusal and draw the result's error under the field. This is the
   case Plan A's checkpoint was written to catch; Plan A's routing works
   and the presentation does not. Observed too: the refusal appeared in
   the file-actions strip only after changing recipes and changing back.
   The strip looks up the result by the resolved recipe id, and after the
   preceding successful rename (`Skyrim-92DEC` to `defg`) the selection
   still named the old id, so `ResolveSelection` re-picked a row through
   the piece until `defg` was reselected. A successful rename must move
   `MenuState::selection.recipeID` to the new id (the `RecipeEditResult`
   for the rename can carry it), and the popup should draw its own result
   rather than depend on the page's resolved row.

7. **The overview is blank for a recipe bound to no geometry.** Selecting
   "Recipe / overview" on `test-bad-row` (whose key resolves to no form,
   so it matches no piece) draws nothing: `DrawBoardPage` in
   `menu/BoardPage.cpp` returns silently when the frame has no piece, and
   dims "no geometry bound" when it has a piece but no geometry. An
   unmatched document should still show its authored outputs; Plan B's
   `BuildStackView(recipe, selection, view)` and `BuildInspector(recipe,
   selection)` already project a recipe without geometry, and the board
   needs the same authored overload. Until then the overview should at
   least say why it is empty rather than draw nothing.
8. **A row that failed to parse is invisible in Studio.** The parser drops
   a row it cannot read (`NamedRows` skips it), so `test-bad-row`'s
   `brokenSignal` is not in `recipe.signals`; its diagnostic (`signal
   brokenSignal: unknown signal kind 'bogus'`) is in `RecipeRow::problems`,
   which only the Recipes page draws ("Rows with problems"). The workspace
   draws `problems` nowhere, so in Studio the recipe looks healthy and the
   user cannot tell which row is bad or that the fix is in the file (a row
   with no readable kind has no record to edit). The navigator or the
   recipe overview should list `problems`, and a diagnostic whose row is
   absent from the recipe should say "not loaded; fix the file".

Recipe-level rename and the add-resource menu work. Findings 2 and 3 are
regressions from slice 1C's replacement of the resource tables and belong
to its owner; `ResourcePanels.cpp` can be deleted once its two remaining
behaviours are moved. Finding 4 is a usability gap in slice 1B's selection
model rather than a regression.

## Complete-editor game check

Use a disposable copy of a working recipe for destructive edits.

1. **Documents:** create a recipe with an explicit key, select an unmatched file,
   edit an output/layer/source, Save, and reload. Browsing alone must not change
   the worn armor. Check no-geometry labels and two window sizes/UI scales.
2. **Navigation:** search for a source, follow its driver, follow a Used-by link,
   and use Back. Check the property/component, scroll, and exact output. Pin a
   preview, tune another property, then change recipe and reload a save.
3. **Tuning:** drag a scalar for several seconds, release, and Undo once. Redo,
   then cancel another drag with Escape. Try exact input, an invalid value,
   and a custom slider range. Close Studio during a drag and reopen it.
4. **Expressions:** edit one of two identical numbers, then promote that number
   to a signal. The other occurrence must stay unchanged; promotion must preserve
   the effect. Check driver/consumer links and Undo. Confirm refusals are visible.
5. **Gameplay inputs:** connect stamina exhaustion to glow opacity; check that it
   remains on while depleted. Try current/max and the received-hit helper. Fire
   the hit response and inspect a supported response graph.
6. **Masks:** open a layer mask task, add a supported pattern, change preview
   geometry, and edit another inspector. Save excludes the draft. Resume, Keep
   into the original layer, Save, and reload. Change/reorder the destination on a
   disposable recipe and verify stale assignment cannot edit a neighbor.
7. **Animation:** inspect a scrolling imported fill such as `VaporTile01`.
   Compare scroll, tiling, mirror/transpose, and Freeze/Step with its output.
   Compare two sources using the same image with different sampling settings.
8. **Restoration:** solo/mute, freeze/scrub, then Return to live. Repeat preview
   browsing, mask Keep/Discard, Studio close, and save-load while textures are
   visible. Check for stale images, stuck pending controls, or changed armor.

For failures, report the recipe, geometry, action, and visible result. Check the
startup build identity and `edit refused`/`save failed` records when present.
Absence of log errors does not establish visual correctness.
