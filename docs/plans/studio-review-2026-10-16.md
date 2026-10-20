Status: open (2026-10-16) on branch `pure-tick`, at commit `a45265f`. A read-only review of the studio for correctness, intended behaviour and ease of use. Items that need the user's decision say so. Resolved: C6, C15 and G7 by the removal of the input wizard (its replacement is the wearer-value popup in `menu/WearerInputs`); G8 by the hit connection as one trigger with a fade curve; G10 by Fire on a "Driven by" row whose driver is a trigger. Fixed on 2026-10-17: C1 (the span holds for the whole gesture), C2 (see its corrected row), C3 (Save claims its destination and sets an unloaded file aside; ids collide without case), C5 (a term drag remembers once; Escape reverts it) and C13 (text buffers hold the expression limit; longer text is read-only). Resolved 2026-10-20: U1 and G6 by option A, a pulsing starter glow (`StarterEdits`) in every new recipe (0b8159f); G1 by holding a signal at a value, a constant swap in the applied catalog that leaves the document untouched and applies to every wearer, as mask peeks do (33c40d3). Fixed 2026-10-20: C4, C7, C8, C12 and G3 by one renumbering rule (39c5639); G2, G5, C9, C10, C11, U3, U11 and U12 (c4dd4d0); G9, G11, G12, G13, U2, U4, U6, U7, U8, U10, U14, U16, U17, U18, C14 and C16 (7b570ca); G4, U5, U9, U13 and U15 (5524d0b). Open: U19, because the studio draws no variants to go to, and trying another enchantment (G1). Not yet checked in game.

# Studio review

## How the review was done

Four reviews read the studio code in parallel. Each review traced one group of user flows:

| Area | Code read |
|---|---|
| Shell and navigation | `menu/Menu`, `Workspace`, `StudioPage`, `ContextRows`; `studio/Navigation`, `MenuState`, `Selection`, `View.h`, `Gesture`, `Isolation`; `engine/RecipeEditor`, `ManagerSnapshot.cpp` |
| Recipe lifecycle | `menu/RecipesPage`, `RecipeActions`, `SetupPage`; `studio/Create`, `NameEdit`, `DocumentRevisions`, `SelectorEdit`, `WornPieces`; `engine/RecipeStore`, `RecipeFiles`, `RecipeEditor` |
| Values, layers and expressions | `studio/Fields`, `FieldCheck`, `Forms`, `Edits`, `Rows`, `Presets`, `Board`; `menu/FormDraw`, `MenuWidgets`, `StackPanel`, `Tuning`, `ExpressionShelf`, `PatternChooser`, `BoardPage` |
| Inputs, sources and auditioning | `studio/InputCatalog`, `InputConnections`, `Relationships`, `ResponseGraph`, `Mask`, `PaintSession`, `PreviewWindows`; `menu/InputBrowser`, `RelationshipPanel`, `ResponsePanel`, `PaintPanel` |

The reviews measured the code against `REQUIREMENTS.md` item 8, `docs/ui-design-principles.md`, `docs/ui-api.md`, the component sheets and the UI v2 checkpoints. Tooltips, string wording, row thumbnails and render performance were out of scope by the user's earlier decisions.

The column **Check** gives the state of each finding:

| Check | Meaning |
|---|---|
| **verified** | A second reading of the code confirmed the finding. |
| **traced** | One review followed the full code path. |
| **plausible** | The code path suggests the defect. A test in game must confirm it. |

Line numbers refer to `a45265f`.

## Correctness defects

| # | Severity | Finding | Where | Check | Direction |
|---|---|---|---|---|---|
| C1 | high | A slider without a declared range recentres on every frame of a drag. The snapshot carries the dragged value, `ValueRelativeRange` centres the span on it, and ImGui maps the mouse onto the new span. A held mouse makes the value climb by half a span on each refresh. Signal constants, pulse and ramp fields are affected. | `menu/Tuning.cpp:242`, `studio/Fields.cpp:57` | verified | Keep the span in the gesture from the first frame to release. |
| C2 | medium | *Corrected 2026-10-17.* Discard on a "new mask" draft posted `RemoveMask{name, true}`, and the second field is `force`, not `cascade`, so no layer was removed (the review misread the field order). `force` clears layer masks only: a mask created from a layer's **source** field stayed bound as the source, the removal was refused, and Discard left it in place. | `menu/PaintPanel.cpp:434-443`, `studio/Edits.cpp:923-946` | verified | Fixed: the creation records the edit that restores the field, and Discard posts it with a plain removal. |
| C3 | high | Save writes `user/<id>.json` over any file at that path. A user file that failed to parse is never loaded, so a save of the shipped recipe with the same id replaces it. Id checks are case-sensitive, so on NTFS `Recipe` and `recipe` write one file. | `engine/RecipeStore.cpp:628-633`, `engine/RecipeFiles.cpp:82` | verified | On the first save to a path, refuse or set aside an occupied destination. Compare ids without case. |
| C4 | high | Mute, layer solo and output solo are keys by index (`LayerKey`, `Isolation`). No code remaps them after a layer or output is added, removed, moved, undone or reloaded. A different layer becomes muted or soloed, and a deleted muted layer still counts in "N muted layer(s)". Three reviews found this. | `studio/View.h:36-75`, `studio/Selection.cpp:254-285` | verified | Remap or drop the view keys where `DropShiftedPreviews` runs, and clear them for the recipe on undo, redo, revert and reload. |
| C5 | high | A mask-term slider commits on every frame of a drag, and each commit pushes one undo entry. A long drag also pushes the term additions out of the 100-entry history. Escape does not cancel the drag, although the tooltip says it does. | `menu/Tuning.cpp:266`, `menu/PaintPanel.cpp:790-797`, `studio/MenuState.cpp:387` | verified | Give the sink path a begin and commit pair: remember once at the start, preview during the drag. |
| C6 | high | "New input" in the value combo of a literal number field opens the wizard popup from inside the "typed-value" popup. The two popup ids differ, and closing the parent closes the child. The wizard does not open, so "glow when stamina is exhausted" cannot start from a number. | `menu/FormDraw.cpp:287-293`, `:461-471`, `menu/InputBrowser.cpp:475-478` | plausible | Set a pending flag and open the wizard at the parent window's level on the next frame. |
| C7 | medium | Ctrl+Z posts `Undo` without a check of the undo depth. The reducer runs before the editor answers, so an empty history still resets the inspector to the recipe page and removes the output and layer visits from Back and Forward. | `menu/StudioPage.cpp:391-396`, `studio/MenuState.cpp:613-617` | traced | Gate the key on `undoDepth`. Invalidate only when the edit result changes output or layer counts. |
| C8 | medium | `pendingSelection` is cleared only when its subject appears. After a refused "+ layer", a change to another recipe with the same output and layer index moves the inspector to it. | `studio/Navigation.cpp:193-195`, `menu/Workspace.cpp:137` | traced | Clear it on a recipe or piece change and on an error for its request. |
| C9 | medium | Solo, mute, freeze and speed survive menu close and a save load. A `soloPiece` whose form is absent from the new save hides every effect in gameplay, with nothing on screen. | `engine/Manager.cpp:99-150`, `studio/View.h:83` | traced | Reset the view on load. On menu close, reset it or show that a preview is still active. |
| C10 | medium | Re-import after a user override can load two recipes with one id. The user file replaces the imported one, the importer sees the shader uncovered, re-imports under the same id and appends it without deduplication. | `engine/RecipeStore.cpp:278`, `:392`, `:431` | traced | Treat a loaded recipe with the import's id as covering the shader, or append through `AppendDefinition`. |
| C11 | medium | Create and Duplicate are not tracked as editor changes. A refused create (the name `paint`, or the id of a held-back recipe) closes the popup and selects a recipe that does not exist, and the error has no row to show on. | `menu/Menu.cpp:135-151`, `studio/MenuState.cpp:585-590` | traced | Track them as Rename is tracked, and show the refusal where the request was made. |
| C12 | medium | A cascade removal (`RemoveMask`, `RemoveSource`, `RemoveSignal` with `cascade = true`) can remove layers, but `ShiftOf` and `ShouldInvalidateIndexedSubjects` treat it as no shift. Pinned layer windows, the layer selection and the paint assignment then target the next layer. | `studio/Navigation.cpp:284-292`, `studio/PreviewWindows.cpp:19-41` | traced | Treat every edit with `cascade = true` as an output shift. |
| C13 | medium | `TextBuffer` holds 1023 characters and expressions may hold 4096. An inline edit of a long mask commits truncated text, which fails to parse or saves a shorter mask. | `studio/MenuState.h:38`, `menu/MenuWidgets.cpp:505-509` | traced | Size the buffer to `kMaxExpressionLength + 1`, or refuse inline edits of longer text. |
| C14 | low | In the mask preview, the first shown term is always a plain set. Muting the first term of `[set body, not belt]` previews `belt`, not `1 - belt`. | `studio/Mask.cpp:81-82` | traced | Emit `1 - (x)` when a `not` term comes first. |
| C15 | low | The error of an Advanced connect button shows for one frame only. | `menu/InputBrowser.cpp:183-191` | traced | Keep the error in `WizardState::error`. |
| C16 | low | A two-component constant signal shows as `x, y`, but its value field has no two-component parse arm. The unchanged text may be refused. | `studio/Forms.cpp:828-864`, `studio/FieldCheck.cpp:220-239` | plausible | Add a two-component arm, or confirm that `Program::Parse` accepts the text. |

## Gaps against intended behaviour

| # | Severity | Finding | Where | Check | Direction |
|---|---|---|---|---|---|
| G1 | high | Auditioning has freeze, scrub, speed, step, fire, solo and mute only. There is no input override, no peak hold and no other enchantment context. Principle 6 and four rows of the workflow table need them. `ui-assessment-2026-09-13.md` recorded the gap, and it is still open. | `studio/Intent.h:215-233` | traced | **Decision:** a view-level override map from signal to value, cleared by Return to live and shown as an audition badge. |
| G2 | high | A recipe that fails to load, or is held back by errors, does not appear in the studio. The studio lists the applied catalog only. The user sees "N with errors" and must read the log for the file and the `where`. The "Held back" branch in `RecipeActions.cpp:110-114` is unreachable. | `engine/RecipeStore.cpp:76-79`, `:237-243`, `:482-484` | verified | List every file the loader saw, with its state and diagnostics. In-game repair stays unsupported. |
| G3 | medium | A layer move, remove, clear or reset sends the inspector from the output to the recipe page and prunes Back. The layer remap in `ReduceRemoveLayer` and `ReduceMoveLayer` never takes effect, because the invalidation resets the layer after it. | `studio/Navigation.cpp:97-101`, `:284-291`, `studio/MenuState.cpp:75-86`, `:567-569` | traced | Keep the output subject for layer-only edits and remap the layer subject. |
| G4 | medium | When the inspected piece leaves the snapshot (unload, unequip, no recipe match), the studio moves to the player's piece and recipe with no message, and does not return when the piece comes back. | `studio/Selection.cpp:362-370`, `:475-477`, `engine/ManagerSnapshot.cpp:640-642` | traced | Keep the chosen piece, show why it is unavailable, and adopt it again when it returns. |
| G5 | medium | Duplicate exists in the editor and the store, but no menu control posts it. | `engine/RecipeEditor.*`, `src/menu` | traced | Add Duplicate beside Rename. |
| G6 | medium | Every create path passes no geometry, so the emissive starter output in `NewRecipe` never runs. The import templates are not offered as starting points (principle 8). | `menu/ContextRows.cpp:342`, `:382`, `:450`, `engine/RecipeStore.cpp:779-786` | traced | Pass the selected geometry, or offer a template in the New recipe popup. See U1. |
| G7 | medium | The input wizard needs a field, so it cannot start from an input. No view answers "what can stamina control?" (principle 2). | `menu/InputBrowser.cpp:80-87`, `:480-487` | traced | Add the standalone entry point from the wishlist, and go to the new signal after a connect. |
| G8 | medium | The hit wizard builds `hit_glow = 1 - @hit`, an expression signal, and the response graph refuses expression signals. The raw trigger's graph plots the age, the inverse of the glow. | `studio/InputConnections.cpp:44-51`, `studio/ResponseGraph.cpp:69-98` | traced | Build the glow as the trigger with the curve `1 - x`, or let the graph show an expression over one trigger. |
| G9 | medium | An edit that only the engine refuses (a reference cycle, a new field error) reports in the recipe status line, and the field returns to the old text with no message. | `menu/FormDraw.cpp:61-63`, `:272-285`, `menu/RecipeActions.cpp:50-64` | traced | Return the edit's `where` and draw the problem on that field. |
| G10 | low | Fire is only on the trigger's inspector. After the hit wizard, the property is bound to `hit_glow`, and the user must find `hit` to fire a test. | `menu/Workspace.cpp:860-879` | traced | Show Fire in the Driven-by row when the chain reaches a trigger. |
| G11 | low | The value-relative range differs from the plan: the code uses `round(sqrt|v|)` clamped to [1, 100] and integer bounds. A value of 0.02 gets the span [-1, 1]. | `studio/Fields.cpp:57-63` | verified | Follow the plan's `span = max(|v|, eps)`, or record the chosen rule. |
| G12 | low | A mask-term parameter is a slider only when it declares a range, and the terms table uses plain inputs for the same parameter. | `menu/PaintPanel.cpp:144-167`, `:787-788` | verified | Use the value-relative slider for every scalar term parameter. |
| G13 | low | The layer field is named `colour` and the property location is `color`, so following a layer colour relationship does not reveal the field. | `studio/Forms.cpp:729`, `menu/FormDraw.cpp:47-49` | traced | Use one name. |

## Ease of use and flow

| # | Severity | Finding | Where | Check | Direction |
|---|---|---|---|---|---|
| U1 | high | The path from an equipped armor to a saved visible effect is New recipe, Create for selected armor, an empty recipe, add output, add layer, configure, Save. Nothing shows until the inspector steps are done. | `menu/ContextRows.cpp`, `engine/RecipeStore.cpp:779-786` | traced | **Decision:** one "Create for selected armor" that produces a visible starter from a template. |
| U2 | medium | The cascade confirmation says "rows that depend on it are removed too" without naming them. `PlanCascade` already computes the layers and resources (principle 3). Two reviews found this. | `menu/Workspace.cpp:767-773` | traced | List `plan.layers` and `plan.resources` in the modal. |
| U3 | medium | Reload recipes discards every unsaved edit, new recipe and undo history without a confirmation. Revert and Delete ask. Two reviews found this. | `menu/RecipesPage.cpp:140-146`, `engine/RecipeEditor.cpp:464-469` | traced | Confirm when a recipe is dirty, and name the recipes. |
| U4 | medium | "Add match" adds an empty geometry clause, which matches no geometry, so the output stops at once. Validation counts clauses only. | `menu/FormDraw.cpp:739-744`, `studio/SelectorEdit.cpp:15-20` | traced | Seed the clause with the selected geometry, or ignore an empty clause as a draft. |
| U5 | medium | Preview state is partly hidden. A solo on another piece does not show on the selected piece, and the muted count does not name the recipe or layer. The only exit is Return to live, which also clears freeze and speed. | `menu/StudioPage.cpp:263-270` | traced | List each active preview with its own clear control. |
| U6 | medium | A text field commits only on Enter. Valid text is lost on a click elsewhere or Tab, with no message. | `menu/MenuWidgets.cpp:505-521` | traced | Commit valid text when the field loses focus, or mark it "not applied". |
| U7 | medium | A tunable field shows only the slider at `%.3f`, so 0.0004 reads 0.000. A Ctrl+click value outside a hard range is refused without a message and snaps back on release. | `menu/FormDraw.cpp:474-479`, `menu/Tuning.cpp:128`, `:173-175` | traced | Show the authored text beside the slider and the refusal beside the field. |
| U8 | low | The combo mode of a value widget is set the first time a field key is seen. After an undo turns `@tint` into a literal, the field still shows "Choose a signal". | `menu/MenuWidgets.cpp:277-284` | traced | Derive the mode again when the model changes outside the field. |
| U9 | low | Binding a number to a signal takes three steps: "=", the badge, the signal. | `menu/FormDraw.cpp:457-479` | traced | Put a signal picker beside the slider. |
| U10 | low | The Board "+" posts a raw `AddOutput` and does not focus the new output. The navigator's add does. | `menu/BoardPage.cpp:139`, `menu/Workspace.cpp:122-135` | traced | Use `Create(NewOutput)`. |
| U11 | low | Revert is enabled for a recipe that was never saved and fails with "cannot open user/x.json". | `menu/RecipeActions.cpp:209`, `engine/RecipeStore.cpp:850` | traced | Disable it, or say "never saved". |
| U12 | low | Delete removes the user file with no set-aside copy. The dialog says a shipped recipe returns "at the next start", but it also returns at the next Reload. | `engine/RecipeFiles.cpp:135`, `menu/ContextRows.cpp:398-401` | traced | Set the file aside as the importer does, and correct the text. |
| U13 | low | Save does not detect an edit made to the file outside the game. `authoring-lifecycle-2026-09-23.md` records this as not done. | `engine/RecipeStore.cpp:620-640` | traced | Compare the file's modification time with the time at load. |
| U14 | low | Index labels differ: the solo tooltip counts from 0 and shows the output index, while rows count from 1 and show the slot name. | `menu/ContextRows.cpp:505-510`, `menu/Workspace.cpp:195`, `:721` | traced | Use the tree's label. |
| U15 | low | A piece or recipe change resets the whole selection and Back and Forward, also when the same recipe applies to the new piece. | `studio/MenuState.cpp:284`, `studio/Selection.cpp:382` | traced | Keep the recipe and subject when the new piece carries the recipe. |
| U16 | low | The X on a surface line removes every output on the surface in one click. Undo restores it, but C7 and G3 also move the inspector. | `menu/Workspace.cpp:495-520` | traced | Confirm, or keep the place after undo once C7 and G3 are fixed. |
| U17 | low | A mask made with "New mask" shows "(destination changed)", because its assignment holds the revision from before the create edit. | `menu/PaintPanel.cpp:632-646`, `studio/MenuState.cpp:1109-1124` | traced | Skip the assignment when `createdMask` is set. |
| U18 | low | The Keep popup accepts the name `peek`, which the engine refuses. | `menu/PaintPanel.cpp:409-411` | traced | Refuse it in the popup. |
| U19 | low | A "Used by" row for a use inside a variant goes to the recipe, not the variant. | `menu/RelationshipPanel.cpp:46-48` | traced | Navigate to the variant. |

## Found sound

The reviews found these parts correct:

- Back and Forward: a 64-entry history, reachability checks, and skipping of visits that no longer exist.
- Bounds: subject, output and layer indices are checked before drawing and in every edit arm. Batches apply to a copy, so a batch is one atomic undo step.
- Recipe slider gestures: one drag is one undo step, Escape cancels, and a revision mismatch refuses a stale gesture.
- Rename: it moves files, history, view state, paint return and saved baseline, and refuses an occupied destination.
- Document revisions only increase, so a captured edit cannot reach a recreated document.
- While an indexed edit is pending, edits, undo, solo and mute are blocked.
- `FieldKey` scoping: no cross-talk in the inspector, the expression shelf or the term pane.
- The colour picker and the expression shelf open as anchored non-modal popups.
- Cascade removal itself: it removes layers in descending order, resets references, refuses while a variant references the resource, and is checked again.
- Connection building: unique names, a zero guard in current over maximum, and an idle trigger that reads 1.
- `RelationshipsOf` covers signals, curves, mask expressions, images and overrides.
- Paint commits refuse a stale assignment and a removed or renamed original mask.
- Pinned previews: an 8-window cap, a waiting message for an unviewed piece, and closing on a vanished subject.
- The cluster readout clamps all eight axes.

## Order of work

1. The defects that lose or corrupt work: C3, C2, C5, C1 and C13.
2. The index-keyed state that drifts after edits: C4, C12, G3, C7 and C8. One remap rule in the editor covers the view keys, preview windows, selection and paint assignment.
3. Load and lifecycle visibility: G2, C9, C10, C11, U3, U11 and U12.
4. The first-effect path and the decisions: U1 with G6, and G1. Both need the user's decision before design.
5. Feedback in place: G9, U2, U6, U7, C6, C15 and G8.
6. The remaining low items.
