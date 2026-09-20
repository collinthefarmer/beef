# Shared recipe application service

This follows the in-game failure of the first Paint implementation wave.
Scratch and saved recipes already used the same matcher and compositor. The
missing boundary was coordination of accepted changes, actor rebuilding,
view selection, cache ownership, and the first successful render.

## Ownership and behavior

`RecipeEditor` remains the owning-thread command entry point. Model edits,
Paint startup/update/Keep/exit, undo/redo, recipe reload, pinning, Solo and
layer mute use Manager's shared rebuild path. Clock-only view changes remain
tick inputs and do not require actor reconstruction.

`ApplicationService` is engine-free and owns application revisions, affected
actors, results, and the SessionQueue scheduler. Manager supplies scene effects
only through the service's actor-application callback. Load/new-game refreshes,
object-load events, equipment changes and delayed finalization, node updates,
reapply, and recipe edits all enter this service. Coalesced reruns also pass
through it; Manager has no independent queue or public direct-apply entry.
Application targets are
retained across revisions, including the interval when the previous revision
has retired the actor. Refresh captures current tokens into `LiveActor`;
preparation and rendering report those captured tokens. Stale revisions and
load-cancelled work cannot complete a newer application. Lifecycle refreshes
have per-actor tokens, and pending recipe results carry an actor-attempt stamp.
A later refresh invalidates earlier draw acknowledgments even if the recipe
revision itself did not change. Actor results do not replace other actors'
records, and the Studio filters lifecycle records by the selected actor.
All pending lifecycle records are retained. Terminal actor results are limited
to the newest 256 revisions; resuming after load releases cancelled actor
records after the cancellation snapshot has been copied. This bounds historical
actor-result retention without discarding active applications.

Submission failures, including internal equipment/follow-up scheduling, are
collected in a thread-safe mailbox and reported on the owning thread. Load
cancellation invalidates queued work before clearing the mailbox. Key edits
do not issue a second loaded-actor refresh after the shared applicator has
already scheduled those targets.

Each actor has queued, prepared, rendered, failed, unmatched, or cancelled
status. Snapshot records exist independently of projected geometry rows. The
Studio displays the selected actor's latest relevant result. Paint edit
acknowledgment and Keep success still mean model acceptance; the separate
application result describes scene application. A render success means the
plugin's preparation and draw calls succeeded, not a GPU fence or proof of
on-screen visibility.

Rebuilds conservatively include currently applied actors and loaded actor
candidates within the player-only setting. This prioritizes correct recovery
and newly matching recipes over targeted refresh performance. Actor removal
before the first render terminates pending application results.

Solo commands carry their target and desired on/off state. They execute
against the authoritative view, then rebuild on scope changes, including
changes within one recipe. Isolated recipes are resolved before competing
recipes can claim their keys; they still require a matching key or an explicit
pin. Output visibility filters geometry and light
contributions before replacement planning. Layer visibility remains a shared
compositor filter. Light selection chooses the first visible light, allowing
Solo of later light outputs. Unsolo restores the appropriate recipe/output scope.

Mask and ripple cache keys own recipe ID, local name, and texture size.
Inspection, dependency lookup, and rendering use the same identity. Recipe
mutations continue to retire geometry inputs before changing recipe storage;
cache contents are not retained across recipe revisions.

Compositor success propagates through mask and ripple dependencies. Failed
sources remain represented so an active failure fails the draw, while Solo
or mute can hide that layer. Failed draws do not establish successful cache
markers. Missing masks cannot silently produce a successful unmasked layer.

## Verification

The lifecycle routing follow-up passed 1,401 ASan/UBSan checks across 44 suites
plus schema validation, without warnings or sanitizer errors. This includes
45 service-owned scheduler checks, 86 application-service checks including
retention, and 21 queue checks.

The same run includes 18 cache checks, duplicate-key recipe isolation, replaced-output Solo,
second-light Solo, and saved/scratch matching and placement parity. The
application harness exercises production ApplicationService and SessionQueue
with controlled scheduling and rendering results; it does not execute D3D.

Targeted clang-tidy completed for the eight production sources changed by the
lifecycle follow-up; its baseline gate passed with no new findings. Existing
baseline findings and pre-existing working-tree changes were preserved. The final
Windows Release build passed without warnings or errors, all 13 changed C++ files
passed formatting verification, and `git diff --check` passed.

The matching DLL/PDB pair is staged in
`dist/BetterEnchantmentEffects/SKSE/Plugins` and its hashes match the final
build outputs. Nothing was installed or committed.

## In-game checkpoint

1. Apply a saved recipe, then author the same source/mask in Paint. Check
   application status and compare the resulting surface, not only thumbnails.
2. Select masks rapidly, Solo alternating terms, undo, and Keep before an
   update finishes. Confirm the settled result follows the latest command.
3. In Compose, Solo an output displaced by another output's `replace`, switch
   Solo within that recipe, then unsolo. Confirm the selected output appears
   and the ordinary replacement chain returns afterward.
4. Use two recipes with the same mask/source names but different definitions.
   Check combined rendering, Solo, and resource inspection.
5. Exercise missing source/mask resources, then repair and reapply. Failure
   should be visible and retry should reach rendered. A retained texture must
   not conceal a failure status.
6. Unequip or load/reload while an application is queued/prepared. Confirm it
   becomes unmatched/cancelled and a new application succeeds.
7. Re-equip an item, wait for delayed equipment finalization, and trigger a
   geometry/node update. Verify each refresh reports the selected actor's
   application result. Switch between actors; another actor's later refresh
   must not replace the displayed status for the selected actor.

With verbose logging enabled, refreshes log `actor ... recipe(s) applied`.
That line describes prepared actor state; the Studio's `Application: rendered`
status confirms the subsequent draw result. `no SKSE task interface; dropping
work` indicates submission failure, which the service reports as a failed
actor application when its owning thread drains the rejection mailbox.

UV transforms, geometry-local offer identity, external renderer ABI/lifetime,
and deferred UI draw-resource ownership remain separate work. No install or
in-game success is implied by native or Windows build validation.
