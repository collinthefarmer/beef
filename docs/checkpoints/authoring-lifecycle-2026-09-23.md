# Offline authoring lifecycle coverage — 2026-09-23

This pass adds cross-component regression sequences using production
`PrepareEdits`, `History<Recipe>`, `PreparePaintCommit`, `SessionQueue`,
`WriteText`, `ReadText`, serialization and parsing. It changes no runtime
behavior and does not replace the pending in-game authoring run.

## New sequences

`tests/engine/authoringlifecycle_tests.cpp` covers:

- Load an example, edit, undo, refuse a partially valid batch without losing
  the redo branch, redo, fail a save, reopen the previous file, retry, reopen
  the edited file, then undo and branch without changing the saved document.
- Refuse a stale paint layer assignment without partial mutation; retry at
  the current revision; apply the mask and assignment as one history entry;
  undo/redo the whole commit; serialize and reopen the complete document.
- Post a save, begin/resume a new game session, post and execute its save,
  then deliver the old queued task. The stale task must not execute its write
  or overwrite the new-session file.

`tests/engine/textfile_tests.cpp` additionally exercises failure at filesystem
replacement after a candidate has been staged. A nonempty destination
directory and its data survive, the staging candidate is removed, and a retry
works after the obstruction is resolved. Existing coverage already checks
failure to stage while preserving an ordinary destination file.

Existing gesture, paint-flow, suspended-paint, validation, history and queue
suites cover component refusal, coalescing and acknowledgement behavior;
this pass runs them alongside the new scenarios.

## Verification

Nine selected suites passed in both native and ASan/UBSan builds, including
the new lifecycle suite and extended text-file suite. No production changes
were needed for these scenarios. No Windows/game run was performed for this
test-only pass.

## Limits

The test explicitly composes production components. It does not execute
`RecipeEditor` or the global game-facing recipe store, so it does not prove
their dirty/saved flags, operation journals, imported-to-user save routing,
history/revision reset callbacks, or UI acknowledgements are wired correctly.
Reopening real temporary files establishes serialization/persistence behavior,
not a complete plugin restart. The filesystem checks run natively, not on
Windows/MO2, and do not simulate power loss or claim durable fsync semantics.

The next integration pass must exercise the actual editor/store orchestration,
including saving/reverting during a gesture, import promotion failure,
reload/load cancellation, and observed dirty/history state after refusals.
Keep the broad alpha lifecycle checkbox open until that evidence exists.

## Production save-boundary follow-up

Source inspection found two store inconsistencies hidden by the earlier
component-only coverage: imported metadata was cleared before writing, relying
on the editor caller to roll it back, and temporary paint masks were removed
only from the serialized copy while the uncleaned working recipe became the
saved baseline.

`RecipeStore::SaveRecipe` now delegates to engine-free `WriteRecipeFile` in
`RecipeFiles`. The helper prepares a copy, optionally clears import metadata,
removes scratch/peek masks and direct layer references, writes it with the
existing failure-safe text writer, and returns the exact written document.
Only after success does the store update its working document, path, saved
baseline and decoding diagnostics. The editor's existing before/after history
and revision handling now sees the successful cleanup as a document change.

`tests/engine/recipesave_tests.cpp` exercises this production boundary with
real temporary files: refused import promotion preserves input metadata,
imported source bytes and an existing user file; retry cleans only temporary
data; returned and reopened documents agree; ordinary saves retain import
metadata unless promotion is explicit; normalized saves are idempotent; and
paint drafts cannot create or overwrite files. History undo/redo of the returned
document leaves the saved file untouched.

Destination routing and global store/UI callbacks still require integration
coverage. The helper tests receive an explicit destination and promotion flag;
they do not claim to execute the full `RecipeEditor` or import loader.

Follow-up verification: all eight selected authoring/save suites passed in
native and ASan/UBSan builds; the new save suite contains 18 checks. The final
Windows Release build passed. Targeted analysis of RecipeFiles and RecipeStore
reported zero findings, and formatting, layer and diff checks passed. No
installation or in-game test was performed.

## Revision and revert preparation follow-up

The production editor now delegates its existing revision policy to
`studio/DocumentRevisions`: one monotonic clock, an epoch for untouched IDs,
and per-document revisions. Reset clears the entries and advances the epoch;
it never returns a reused recipe ID to an old revision. Existing editor
reload and game-load cancellation call sites use this same reset function.

The production store's revert path now calls `ReadRecipeFile` before publishing
any replacement. The helper reports missing/empty/undecodable files with their
path and recipe ID and returns the full `LoadResult` for decodable documents,
including input diagnostics. Form resolution remains in the game adapter.

`tests/engine/editorstate_tests.cpp` covers revision isolation, successive
resets with reused/untouched IDs, refusal of captured edits and gestures after
reset, stale paint assignment and reselection, revert read failures and retry,
retention of input diagnostics, and gesture commit/save/undo/redo with an
unchanged saved file. These tests call the same revision and file helpers as
the editor/store and combine them with production gesture/history functions.

This extracts existing policy without changing the intended command semantics.
It still does not execute the game-facing callback queue and operation journal
as a whole. Failed-revert dirty/history publication, save-versus-gesture task
ordering, reload history clearing, and terminal UI cancellation acknowledgements
remain integration acceptance items; do not close the broad lifecycle gate.

Revision/revert verification: all nine selected authoring, file, gesture,
history and menu-state suites passed in native and ASan/UBSan builds. The
Windows Release build passed, with existing Menu.cpp initializer warnings.
Targeted analysis of the four changed production translation units passed the
tidy baseline gate with no new findings. Formatting and layer checks passed.
No installation or in-game test was performed.

## Operation journal and queue follow-up

`RecipeOperations.h` extracts the editor's production journal, snapshot readers,
and pending file/edit/gesture task guards. `RecipeEditor` uses these same types;
the native suite does not substitute a second journal implementation.

The new `engine_recipeoperations` suite combines those guards with SessionQueue
and a controlled task submitter. It covers rejected submissions, posts during
load, immediate load cancellation before callback release, late completion,
same-ID replacement followed by old-session delivery, callbacks outliving the
queue, guard destruction after success, and bounded result retention. It also
checks the result snapshots consumed by the editor/UI.

The first run exposed that `PublishGesture` could revive a terminal gesture
with a late active publication, unlike the file/edit completion paths. It now
requires the matching journal gesture to still be active. The regression also
checks that a fresh gesture can begin afterward and old task destruction cannot
affect it. This establishes the journal invariant; it does not establish an
observed in-game race under the game-thread serialization contract.

Actual RecipeEditor/store callback execution, failed-revert dirty/history state,
and visual UI acknowledgement remain open. These tests invoke load cancellation
explicitly alongside the production queue; they do not execute Manager teardown
or SKSE scheduling.

Verification: all six selected queue/journal, authoring, revision, gesture and
menu-state suites passed natively and under ASan/UBSan; the new journal suite
contains 19 checks. Windows Release linked successfully, with the existing
Menu.cpp initializer warnings. Targeted RecipeEditor analysis, including the
extracted header, passed the tidy baseline gate with no new findings. Formatting,
include-layer and diff checks passed. No installation or in-game check was run.

## Actual editor/store callback integration

`engine_editorintegration` compiles the production RecipeEditor and RecipeStore
translation units into the native test executable. Only that target shadows
platform headers with `tests/engine/platform`: form lookup/catalog access yields
no forms, logging is inert, and a small manager double posts to the real
SessionQueue and executes mutations synchronously. No editor/store command body
is copied into the harness. Real files live in a disposable working directory
using the normal Identity paths. Production source code is unchanged by this pass.

The callback scenarios cover:

- Missing, empty and malformed revert files with both undo and redo available:
  working document, dirty state, history depths, revision and cached graph remain
  unchanged. Results expose the failed operation and attempted path. Subsequent
  undo still reaches the original saved baseline; redo restores the dirty edit.
- Repair and revert retry: the replacement becomes the saved baseline, clears
  redo, creates an undo step and advances revision. Undo/redo updates dirty state
  against that new baseline without rewriting the saved file.
- Revert during tuning: the active gesture commits before file reading. Read
  refusal preserves the applied values and leaves that gesture as one undo step.
  A queued gesture update before save is persisted and committed once; undo and
  redo compute dirty state against the actual saved gesture document.
- Imported save failure/retry: refusal preserves the imported origin, metadata,
  revision and history; successful retry routes to the user directory, normalizes
  metadata as an undoable change and leaves the imported source untouched. Reload
  selects the promoted user definition over the imported file.
- Reload clears history, advances revisions and refuses an edit captured before
  reload. Load cancellation restores an active gesture and immediately fails a
  pending save; delivery after queue resume cannot execute that old save.

This closes the previously identified native callback/state gaps for these cases.
Manager actor retirement/rebuild, Skyrim form discovery/import generation, real
SKSE scheduling, visual acknowledgements and restart acceptance remain outside
the harness. Keep the broad in-game lifecycle gate open.

Verification: all 46 editor integration checks passed natively and under
ASan/UBSan. The native CMake build regression passed, as did formatting,
include-layer and diff checks. No additional production fix was needed for
these scenarios. This pass changes only the native test build, harness and
documentation; no Windows rebuild, installation or in-game check was performed.

## Creation, identity and file-ownership integration

The actual editor/store harness now also covers creation and duplication through
save/reload, loaded-ID collisions, path-like ID refusal, independent duplicate
content and import metadata, and rename/delete of unsaved documents.

Owned-file rename coverage starts with both undo and redo available and active
isolation, mute and pin references. Destination collision preserves the document,
history, view, revisions and destination bytes. Successful rename moves the file,
transfers history/view references and invalidates both IDs. Delete refusal retains
state; retry removes the owned file, document, history and view references. An old
captured edit cannot mutate a recreated document with the deleted ID.

The first run exposed that owned-file rename changed the working/history IDs but
left the saved baseline under the old ID. Undo to the moved file's exact contents
therefore remained dirty. Accepted owned rename now updates the saved baseline ID
as well. Both clean rename and dirty rename followed by undo/redo are covered.
This does not make an unsaved draft or renamed shipped copy clean.

Shipped rename/delete tests preserve source bytes. Saving the renamed copy and
reloading restores the shipped identity alongside the user copy. A same-ID user
definition wins on reload; deleting that override removes only the user file, and
the next reload reveals the shipped definition. This coverage concerns shipped
rename/delete and user overrides; it does not change ordinary save routing.

Verification: all 84 editor integration checks passed natively and under
ASan/UBSan. Windows Release linked successfully. Targeted RecipeStore analysis
reported zero findings; formatting, include-layer and diff checks passed.
No installation or in-game check was performed.

## Paint callback integration

The actual editor/store suite now exercises begin/update/Keep/discard and their
history, revision, persistence and isolation effects. Successful Keep combines
source reuse/addition, expression reference rewriting, mask creation and layer
assignment into one undo step. Duplicate Keep delivery has no second effect.
Undo/redo and save/reload retain the authored result without transient recipes or
scratch/peek masks; saving the transient preview itself is refused.

Refused preview updates preserve both documents; newer valid updates recover and
older deliveries cannot overwrite them. Old-session update/Keep/discard cannot
affect a replacement session. Discard removes preview history and restores the
previous isolation without editing the destination. An absent begin destination
publishes a refusal.

Destination revision changes refuse positional Keep atomically and retain the
draft. Reselecting the layer permits retry without losing unrelated edits. Rename
invalidates Keep addressed to the old ID; a request to the new ID can finish and
restore renamed isolation. Deletion refuses Keep without recreating the target;
discard then clears the obsolete return isolation.

Load cancellation drops preview/history and publishes the reset before old queued
work is released. Old callbacks and old reset tokens cannot revive painting;
a fresh token permits a new session. Recipe reload also ends the draft.

The reserved-name case exposed a data-loss path: Keep accepted `peek`, although
save preparation strips it as temporary preview data. `PreparePaintCommit` now
rejects both `scratch` and `peek` with a diagnostic. Integration and helper tests
cover refusal; the integration case also retries successfully with another name.
This prevents new paint commits under the reserved name; it does not migrate
existing documents containing authored masks named `peek`.

These are editor/store callback and result checks. Projection onto actual armor,
rendering, and displayed menu acknowledgement remain in-game acceptance cases.

Verification: all four selected editor integration, paint-session, paint-flow and
suspended-paint suites passed natively and under ASan/UBSan. The integration suite
now contains 129 checks. Windows Release linked successfully with existing
initializer warnings. Targeted PaintSession analysis reported zero findings;
formatting, include-layer and diff checks passed. No installation or in-game
check was performed.

## Validation and diagnostic callback integration

The actual editor/store suite now checks atomic refusal of invalid indexed and
nonfinite edits after an earlier valid edit in the same batch. Refusal publishes
a located error while preserving document, dirty state, undo/redo, revision,
cached graph and document diagnostics. Empty, identical and net-zero batches
acknowledge success without changing those states or consuming redo; a subsequent
real change correctly starts a new history branch.

A decodable input-invalid file remains editable but is excluded from the applied
set. Ordinary edits retain the original decoding diagnostics. Failed save retains
the diagnostics and original file bytes; successful normalized save clears them
and republishes the recipe without manufacturing a document-history change.
Undo does not resurrect errors from file bytes that have been replaced. Revert to
a decodable input-invalid file succeeds while publishing its errors and excluding
it from application; reload retains errors until the underlying file is repaired.

Incomplete source drafts are accepted with semantic row diagnostics. Saving and
reloading does not erase those diagnostics. Repair clears them immediately, and
undo/redo restores or clears them along with the corresponding document and
dirty state.

Invalid gesture updates publish an error without changing document or redo;
valid updates clear the error. Switching the edit target mid-gesture is refused
without losing the last accepted value. Recovery then commits one undo step;
explicit cancellation restores the original document while preserving redo.

These tests inspect production editor results, store origin diagnostics and the
applied recipe list. They do not execute Manager snapshot assembly or displayed
menu acknowledgements. No production fix was needed for these cases.

Verification: all 176 editor integration checks passed natively and under
ASan/UBSan. Formatting, include-layer and diff checks passed. This pass changes
tests and documentation only; no Windows rebuild, installation or in-game check
was performed.

## Editor results and UI acknowledgement integration

The integration suite now feeds actual RecipeEditor operation and paint results
into the production MenuState acknowledgement handlers. Test snapshots copy the
editor's result fields; pending-request tracking uses the same records/functions
as menu dispatch. This does not execute ImGui, menu dispatch itself, Manager
snapshot assembly, recipe-row selection resolution or the renderer.

Recipe rename refusal/retry retains inspector context and blocks competing edits
while allowing navigation. Old results cannot acknowledge newer rename/delete
requests. Accepted rename follows the new identity; accepted delete releases its
gate and resets the removed document's inspector. Resource rename refusal keeps
selection, success schedules the new subject, and later navigation is respected.
Refused indexed edits release their pending gate while retaining displayable
operation error text.

Actual file results retain the pending gate while queued, release it on refusal
or success, ignore an old refusal during retry, and preserve later recipe
navigation. Load cancellation releases pending file state before callback release;
an edit dropped during load also releases its tracked/indexed gates without
following the unexecuted rename.

Paint begin makes the UI draft ready only after acknowledgement. Preview updates
clear their matching pending revision; Keep keeps the draft pending and blocks
conflicting actions. Refusal exposes the error without losing mask terms, a new
request can retry, and the old refusal cannot clear that retry. Successful Keep
closes the UI draft and restores destination inspector/scroll. A load reset clears
even a pending Keep, and old successful results cannot restore it.

The first run exposed that recipe rename acknowledgement reset navigation even
after the user selected another document. The reducer now resets navigation only
when the renamed document is still selected. The regression preserves the other
document's subject, back/forward histories and scroll through late completion.

Verification: all five selected integration, rename-flow, menu-state, navigation
and edit-result suites passed natively and under ASan/UBSan. The final integration
suite contains 215 checks and was rerun in both builds after the last harness
adjustment. Windows Release linked successfully with an existing initializer
warning. Targeted MenuState analysis passed the tidy baseline gate with no new
findings; formatting, include-layer and diff checks passed. No installation or
in-game check was performed.

## Save ownership and shipped overrides

Save now treats `recipes/user/` as the writable ownership boundary. Existing
user files keep their paths; all other loaded sources save to
`recipes/user/<id>.json`. Imported-folder sources still clear their import
metadata; shipped source metadata is otherwise preserved. Successful writes
publish the new origin and saved baseline together. Refusal retains the original
origin, document, dirty/history/revision state and both existing files.

Integration cases cover recipe-root sources, nested vendor folders, the sibling
folder `user-assets` (which must not count as user-owned), and nested user files
in both ordinary and dot-prefixed folders. Ownership now checks the relative
path's `..` component rather than rejecting every leading dot; `.personal`
inside `user/` remains user-owned.
Each shipped case starts with an existing destination and a blocked staging path,
checks failure preservation, then retries and verifies the saved destination,
undo/redo dirty state, revert from the adopted user origin, reload precedence,
and deleting the override to reveal the unchanged shipped definition. Routing
alone adds no content-history step or document revision. The first run failed
the shipped-routing assertions under the old source-overwrite behavior.

Save continues to replace an existing user destination when explicitly requested;
this change does not add external-edit conflict detection or change filesystem
replacement semantics. Windows file locks and MO2 behavior remain in-game/platform
acceptance items.

Verification: all 267 editor integration checks passed natively and under
ASan/UBSan after the final path-component fix. Windows Release linked
successfully. Targeted RecipeStore analysis reported zero findings; formatting,
include-layer and diff checks passed. The alpha package instructions document
the new save ownership policy. No package regeneration, installation or in-game
check was performed.
