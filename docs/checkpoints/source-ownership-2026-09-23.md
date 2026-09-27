Status: record. Initial alpha source review: live instance indices, recipe
publication, queue cancellation, texture ownership, targeted checks, and
remaining work.

# Source ownership review, 2026-09-23

Initial pass against `6ed2f3a`, with the uncommitted live-instance correction
described below. This is source and targeted build/test evidence, not an
in-game checkpoint or completion of the alpha source review.

## Change

`ManagerApply.cpp` now sizes `LiveActor::instances` from `ActorPlan::instances`
and fills the exact `InstanceId` slot. Removed `ExistingInstance`, the second
recipe/enchantment deduplication after the planner already established
identity. `ResolvePlacement`, shell ownership, and light placement address
the live vector using planner indices. Previously an early preparation
return omitted a vector entry; a later entry could then occupy its index.
Likewise, distinct planned enchantments whose engine lookups both failed
could deduplicate as enchantment zero. The correction keeps empty failed
slots and never renumbers later instances. These are source-level failure
paths; neither was reproduced in a running game.

Review follow-up: status totals, apply logs/traces, and retirement logs/traces
use `LiveInstanceCount`, which counts only slots with a recipe. Empty slots
retain their planner indices without inflating the reported recipe count.

No new planner abstraction was needed. `ActorPlanning.cpp` already owns the
decision and native tests exercise its per-recipe/per-enchantment grain.
The changed engine adapter itself requires the Windows build and a runtime
checkpoint; the native tests do not execute `Manager::InstanceFor`.

## Boundaries traced

- `RecipeStore.cpp` has two stores: editable `g_loaded` and copied applied
  `g_recipes`. `RebuildApplied` invalidates every published recipe/layer
  pointer and recipe index, even for single-document changes and saves.
  `ManagerApplication.cpp::ChangeAndRebuildActors` includes all applied
  actors, retires them, calls the mutation synchronously, then queues their
  refreshes. Its report recipe does not narrow retirement. The inspected
  editor edit/history/save/revert/CRUD/paint/view routes use this boundary.
- `Manager::Clear` invalidates the session and retires actor effects before
  gesture rollback and transient paint removal can republish recipes.
  `SessionQueue` owns weak state captures and rejects stale generations;
  editor tasks capture IDs and edits by value. Pending file/edit/gesture
  objects keep a shared journal and report discarded work. Callback
  execution and teardown still depend on the game-thread ordering: a
  generation check is not cancellation of an already-running callback.
- `LiveInstance` borrows its recipe, and `RenderedStack::PreparedLayer`
  borrows recipe layers. Their destruction precedes store republication.
  The signal graph is shared-owned. Snapshot preview textures retain
  `TextureRef` leases; `TextureRef` checks presenter identity/generation.
  Preview work retains source/target ownership, and draw tickets retain
  targets until consumed. These establish CPU ownership, not GPU completion.
- `SlotWriter::Restore` uses the ownership journal and preserves an
  externally changed coupled slot group. `RetainPublishedTextures` and
  `SweepRetiredMaterialTextures` retain generated textures still referenced
  by materials after restoration/takeover. `RetireActorEffects` releases
  bindings before prepared stacks and instances. Actual external takeover,
  Community Shaders ABI behavior, and eventual resource recovery need game
  evidence.
- `RecipeEditResult` acknowledges document acceptance. File results record
  persistence; `ApplicationService` separately records preparation/render
  outcomes and rejects obsolete attempts. The engine component sheet now
  states this distinction instead of claiming actor rebuilds finished before
  an edit result. It also lists all four native engine-service units.

The existing `InstanceSpeed` helper is shared by carry-over and normal tick
timing. No additional timing change was made in this pass.

## Verification

All commands ran inside `nix develop`:

- `cmake --build --preset windows-release`: succeeded; changed adapter and
  build identity compiled and the DLL linked without reported warnings.
- Built and ran 11 `native-sanitized` suites: `planners_actorplan`,
  `planners_actorplanning`, `planners_placementlookup`,
  `planners_textureleases`, `planners_consumptionleases`,
  `planners_ownedstate`, `engine_sessionqueue`, `engine_applicationservice`,
  `engine_applicator`, `studio_gesture`, `studio_paintflow`. All passed.
- `python3 tools/tidy.py src/engine/ManagerApply.cpp`: succeeded, four
  `readability-function-size` findings. The targeted baseline gate passed.
  These are the placement preparation, replacement marking, light placement,
  and actor refresh routines; this patch does not split their domain steps
  merely to reduce line counts. Baseline was not regenerated.
- Targeted formatting, layer checks, and `git diff --check` passed.

No full release gate, staging, installation, or in-game run was performed.

The recipe-count follow-up passed the Windows release build, targeted
formatting, layer checks, and `git diff --check`. Rebuilding header consumers
reported four missing-field initializer warnings in unchanged `menu/Menu.cpp`.
The targeted sanitizer and tidy runs above preceded this follow-up.

## Remaining review and next runtime checkpoint

The active plan's broad review checkboxes remain open. Specific follow-ups
observed in this pass:

- Recipe rename/delete refusal and resource rename controls were addressed
  in the follow-up below. Their visible in-game behavior remains to check.
- Carry-over identity, retention, and conversion were addressed in the narrow
  timing follow-up below. Multi-enchantment phase retention still needs its
  in-game checkpoint.
- `TextureLabReadback.cpp::ReadMapping` calls `Map` with flags zero after a
  copy under `RendererLock`. Readbacks remain synchronous in this code,
  contrary to the async direction in requirements. Measure apply/inspection
  stalls and settle the implementation before claiming the crowd budget.
- Stack-warning suppression now has session clearing and fixed key/text
  budgets, described in the warning-history follow-up below. Long-session
  runtime validation remains open.

Next game checkpoint: apply one recipe to pieces with two enchantments,
including first-/third-person geometries; edit, save, undo/redo, and reload
while watching the same pieces and previews. Then load a save during a
gesture and paint update. Confirm the correct recipe/signal stays on each
piece, canceled old-session work cannot reappear, and every application
reaches a terminal outcome. Capture candidate identity, application/queue
traces and visual results. Separately exercise real material/shell takeover
and confirm `preserve_external_group` plus visible preservation; ordinary
unequip is not equivalent evidence.

## Authoring follow-up

Recipe rename/delete now runs the checked, engine-free `RecipeFiles`
operation before changing or unpublishing the document. Filesystem inspection,
move, and removal errors return a recipe diagnostic naming the path. Rename
refuses an existing destination even when that file is not loaded. Missing
files still permit operations on unsaved documents; shipped files remain
on disk. These operations rely on the existing serialized editor flow;
they do not provide a cross-process filesystem transaction.

All four named resource inspectors now expose the existing rename edits:
signals, sources, masks, and curves. A shared popup checks syntax and names
across resource kinds; the edit rewrites references through the normal
undo/redo path. Resource rename is disabled during a mask draft.

`pendingEditorChange` matches asynchronous edit results by request and recipe.
Recipe rename/delete no longer changes selection optimistically. Resource
rename follows the accepted name unless the user has navigated elsewhere;
refusals retain the previous selection. Both pages acknowledge results before
resolving selection against the new snapshot.

Native coverage includes actual move/remove/inspection failures, destination
collision preservation, shipped and unsaved files, result matching, refused
operations retaining selection, and navigation during queued renames. All
four resource kinds have reference rewrite, invalid/duplicate-name refusal,
undo/redo, and serialized round-trip checks.

Verification: Windows release build passed. The four targeted ASan/UBSan
suites (`engine_recipefiles`, `studio_renameflow`, `studio_edits`, and
`studio_navigationintegration`) passed after the final changes. Targeted tidy
covered eight changed translation units; its two new findings in result
handling were resolved by separating acknowledgment from following selection.
Reanalysis of `MenuState.cpp` passed the targeted baseline gate, with no
baseline changes. Formatting, layer checks, and `git diff --check` passed.
Existing missing-field initializer compiler warnings remain in menu/state
code. Native tests exercise the file service and studio state/edit paths,
not the engine-facing recipe store or rendered ImGui controls.

In-game checks still needed: rename each resource with active references,
undo/redo, save/reload, duplicate and invalid names, and file-operation refusal
messages. Confirm that the inspector follows only successful renames and that
mask drafts retain their original resources. No installation was performed.

## Timing follow-up

`CarriedTimes` is an engine-free store of phase and retirement time, keyed by
actor, recipe name, and enchantment form ID. The engine records the collected
enchantment ID before resolving its pointer. A failed lookup therefore does
not alias an enchanted instance to enchantment zero. Plan indices and geometry
handles never enter the continuation store.

The existing two-second window remains. Taking a record consumes it; the
manager's one-second heartbeat expires unused records even with no applied
actors; session clearing discards everything. A 4096-entry cap bounds bursts.
At capacity, new identities restart while retained identities can still update.

`ClockOffsetMS` is shared by carry-over and scrub resumption. It checks finite,
nonnegative seconds, finite positive speed, and the integer range before
converting. Invalid or unrepresentable continuation uses a fresh clock origin.
The calculation includes all three existing speed factors.

Tests cover independent actors/recipes/enchantments, unenchanted instances,
reordered rebuilds, consume-once behavior, replacement, exact expiry boundaries,
expiry without reapplication, clock wrap, capacity/recovery, session clearing,
and invalid/extreme clock offsets. This does not consolidate broader lifecycle
management or change instance sharing, variant selection, or GPU ownership.

Verification: the Windows release build and three targeted ASan/UBSan suites
(`engine_instancetime`, `engine_sessionqueue`, `planners_actorplanning`) passed.
Fresh tidy on `InstanceTime.cpp`, `Manager.cpp`, `ManagerApply.cpp`, and
`ManagerTick.cpp` passed the existing targeted baseline with no new findings.
Formatting, include layers, and `git diff --check` passed. The Windows build
still reports the existing four missing-field initializer warnings in
`menu/Menu.cpp`. No staging, installation, or game run was performed.

Next timing checkpoint: one actor wearing pieces with two enchantments matched
by the same recipe; reapply and edit within the carry window, freeze/scrub and
resume, then unequip beyond the window and load a save. Observe that phases
stay independent, short rebuilds retain time, and late/new-session applies
start fresh. Capture startup build identity and the apply/retire traces with
the visual result; logs alone cannot establish phase continuity.

## Warning-history follow-up

The process-lifetime static `FirstOccurrenceThisSession` set is gone.
`Manager` owns an engine-free `WarningHistory` and clears it during session
teardown alongside the existing non-PBR armor log set. Stack preparation
passes that history explicitly to the warning logger. A mutex preserves
safe observation/reset behavior.

The history retains at most 1024 keys and 128 KiB of key text. Repeats use
transparent lookup without copying the key. First observations log normally;
the first observation that cannot fit requests one budget notice; subsequent
over-budget observations are omitted without storing their text. Smaller
warnings can still fit after an oversized warning. The stack diagnostics
themselves and their editor presentation are unaffected.

Tests cover session reset, duplicate accounting, independent contexts,
key-count saturation, byte saturation, oversized input, post-limit recovery
after clearing, and concurrent duplicate observations. The runtime checkpoint
is to trigger the same stack warning repeatedly, load a save, and trigger it
again: the first occurrence should log once in each session, with repeats
suppressed. In an intentional warning flood, expect one `stack warning
history reached its budget` notice and continued in-editor diagnostics.

Verification: Windows release build passed, with the existing four
`menu/Menu.cpp` missing-field initializer warnings. The ASan/UBSan
`diagnostics_warninghistory` and `engine_sessionqueue` suites passed.
Targeted tidy for `WarningHistory.cpp`, `Manager.cpp`, and `ManagerApply.cpp`
passed the existing baseline; the final warning-decision refinement was
reanalyzed with zero findings. Formatting, layer checks, and
`git diff --check` passed. No staging, installation, or in-game run occurred.

## Retirement and republication review

This focused follow-up traced store publication through editor commands,
manager rebuilds, queued callbacks, live actor destruction, and snapshot
construction. It is source evidence, not an in-game lifetime acceptance.

The inspected boundaries preserve the existing ownership contract:

- Store publication replaces the entire applied recipe vector. Editor mutation
  callbacks retire applied actors first, including save normalization, CRUD,
  history, gesture updates, and transient paint insertion/removal. Initial
  loading precedes hook installation; save-load teardown retires effects before
  gesture restoration and paint removal.
- Deferred editor/event/inspection callbacks own IDs, names, edits, and request
  records, then resolve live objects on execution. Synchronous mutation callbacks
  can borrow mutable store documents because retirement does not mutate the
  store. Published-recipe spans and planner indices remain within an applied
  lifetime; prepared layer pointers are destroyed before republication.
- Signal graphs are shared-owned; actor environments retain handles and form
  IDs. Snapshot builders copy document/diagnostic values and retain preview
  texture leases. Bindings retire before prepared texture producers. Material
  restoration continues to preserve externally changed groups and retain texture
  leases still referenced by materials.

A concrete gap was found in distance eviction: `SweepEviction` called
`Manager::Retire` without completing pending application results. Eviction can
run after preparation but before the first render, leaving actor and recipe
records prepared indefinitely after their live tokens have been destroyed.

`Manager::Retire` now passes the retiring live state's captured tokens to
`ApplicationService::Retire` before destroying effects. Pending matching attempts
become unmatched with the existing retirement message. It deliberately uses
captured tokens: rebuilds begin a replacement before retiring old state, so
finishing all currently pending work here would cancel the replacement. Revision
and attempt checks reject stale retirement; completed outcomes remain unchanged.
Explicit unload still finishes all pending tokens, even without live state.

Native applicator scenarios use the production application service and session
queue to cover retirement before first render, independent wearers, aggregate
outcomes, immutable held snapshots, duplicate/late completion, return after
eviction, overlapping recipe rebuilds, and reused actor/recipe IDs after load.
They do not execute Skyrim's distance calculation or destruction of GPU bindings.

Verification: all 63 applicator checks passed; `engine_applicator`,
`engine_applicationservice`, and `engine_sessionqueue` passed natively and under
ASan/UBSan. Windows Release compiled and linked, with the existing four
missing-field initializer warnings in `menu/Menu.cpp`. Targeted tidy covered
`ApplicationService.cpp`, `ManagerApplication.cpp`, and `ManagerApply.cpp`,
including the changed service header: four existing findings, no new baseline
findings. Formatting, include layers, and `git diff --check` passed. No staging,
installation, or game run was performed.

The review also identified a separate retention follow-up, now in the active
plan: recipe-scoped `ApplicationService::records_` and its per-recipe actor lists
have no pruning policy. Distinct recipe IDs remain through rename/delete and
load cancellation, while the existing cap covers terminal actor-scoped records
only. Any fix must preserve pending attempts and retry targets.

Runtime checks remain: evict a prepared wearer before its first render, confirm
an unmatched result, return it to range and confirm a fresh rendered attempt;
also repeat edits/reload while effects and previews exist, and exercise genuine
material/shell takeover. Engine-thread serialization, GPU completion, and
resource recovery still need game evidence.

## Readback assessment

The retained 2026-09-22 startup trace contains three mean readbacks (maximum
0.488 ms, average 0.427 ms) and no buffer or pixel readbacks. Its older build
identity and narrow coverage cannot establish candidate crowd or authoring
stall behavior.

Curve preparation now skips source-mean acquisition when `UsesMean()` is
false. ParseCurve forbids references and calls to other curves, so a hidden
mean dependency cannot bypass this flag. Readback events now separate lock
wait, lock-held time, and Map time, and record map attempts, successful maps,
completed reads, and payload bytes. Existing aggregate totals remain intact.
The in-game regression checklist describes cold/warm and authoring captures.
Synchronous mapping is unchanged; the asynchronous-readback requirement and
candidate runtime measurements remain open.

Validation: Windows Release build passed; `recipe_expression` passed under
ASan/UBSan; targeted clang-tidy for both render translation units passed the
existing baseline with no new findings. Formatting, include-layer checks,
and `git diff --check` passed. No in-game readback capture was performed.
