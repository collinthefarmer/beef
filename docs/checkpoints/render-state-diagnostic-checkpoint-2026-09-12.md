# First rendering-state diagnostic checkpoint

This is the first stage-0 build from the
[fix plan](../history/render-state-fix-plan-2026-09-12.md). Rendering algorithms, shell
construction, restoration policy and pool reuse policy are intentionally unchanged
so this build can capture the existing failures before their mechanisms change.
Diagnostic bookkeeping and queued context capture are new. The built-in sequencer
is the next implementation stage, after checking these events against a live run.

## What is recorded

The ordinary SKSE log reports `build:` and `diagnostic trace:` at startup. The
trace is `BetterEnchantmentEffects-trace-<run>.jsonl` alongside that log. The run
name uses the launch timestamp in microseconds. Existing trace files are refused
rather than intentionally truncated. Each file is capped at 32 MiB; the most
recent 512 events remain in memory, each limited to 16 KiB. Setup's status shows
file failures or the file limit. A new launch starts another trace file; old files
are retained. `DiagnosticLogging` defaults to true for this diagnostic build and
can be changed through Setup's **Diagnostic trace** setting without reapplication.

The first trace event identifies the build, full source SHA256, Skyrim runtime and
packed SKSE version. `build-identity.json` is generated at build time and staged as
`BetterEnchantmentEffects-build.json` beside the DLL. The fingerprint includes
active project source, vendored source under `src`, shader files, CMake/toolchain
inputs and the identity generator. It is not a hash of the DLL or every external
dependency/toolchain binary. Effective settings and recipes have separate stable,
noncryptographic FNV-1a fingerprints.

The JSONL envelope has schema, run, sequence, timestamp, thread, load session,
command, event kind and fields. Payload fields are strings, including decimal IDs;
pointers use hexadecimal. `truncated` and `dropped` expose capture limitations.
Command events name the originating editor/manager/engine operation. Queued posts
and refreshes retain that context, including delayed equipment finalization;
stale queued work logs its original session and the queue generation.

Coalesced refresh requests are logged under each requesting command. The existing
queue's single rerun retains the first scheduled callback's context: use the
coalesced events to see additional causes, rather than treating one command as the
exclusive cause of the refreshed state. No queue scheduling policy was replaced.

Other events connect:

- Actor/armor/geometry/property/shell identity to each installed geometry.
- Slot-writer identity to installed material and emissive storage.
- First texture/emissive writes and texture/emissive restoration comparisons.
- Whole-binding restore/skip decisions and original/current PBR flag words.
- Target IDs/generations, presenter/renderer/SRV identity, recycle/destruction,
  stack publication and preview rendering requests after invalidation.
- Source/clone transforms, skin roots, array/matrix addresses, up to 16 bind
  transforms per skin, skin data before/after copying, and the first effective pose.
- Observed page/selection changes, rebuild view state, and application attempts.

An application `rendered` event still means the existing runtime's completion
assessment, not GPU readback or correct visible appearance. No GPU readback was
added. Page observation detects a different page or selection when its render
callback runs; closing and reopening the same page/selection is not independently
recorded. Preview events do not claim that their source has a valid target lease.

This checkpoint does not yet provide complete per-field scalar journals, selected-
actor filtering, full palette/world-matrix validation, CS module-version discovery,
a separate run manifest, or renderer/UI completion tracking. Those remain in the
plan. The current trace describes resource lifetimes; it does not repair them.

## Run these comparisons

1. Launch this DLL and verify the new `build:` and `diagnostic trace:` lines.
   Record the displayed build ID. Leave Diagnostic trace enabled.
2. Reproduce the empty/default shell with diffuse selected and no layers. Record
   the armor, recipe, camera and whether stretching appears. The trace should have
   `shell` events with `action=cloned`, skin roles before/after copying, and
   `action=first_pose` with the actual inflation vector.
3. From a red-emission starting state, toggle the same Solo control on/off several
   times. Record each visible body/helmet state. Look for `editor.ChangeView`
   commands, queued refreshes and retirements, target generations, and binding
   first writes/restoration events.
4. Reload to red, enter Recipes without clicking controls, and record the result.
   This should record a `page` observation. If a rebuild occurs, its command trace
   should distinguish it from preview activity alone.
5. End the capture and preserve the new JSONL file plus the ordinary SKSE log.
   If the trace-limit notice appears, report it and use a fresh launch for the
   remaining comparison. Do not infer missing events after the limit as inactivity.

Use the actual observed controls and armor; do not change the test fixture to make
the trace look cleaner. Visual outcomes remain user observations alongside the
events. Repeated Quickload crash reproduction is not needed for this first capture.

Summarize a capture with:

```sh
nix develop -c python3 tools/trace-report.py /path/to/capture.jsonl
```

The report counts events, groups retirements by command, lists page observations,
and reports partial/malformed lines and trace limits. Preserve the original trace
for identity analysis; summary counts alone cannot attribute a rendering defect.

## Build validation and installation

Installed build: `a98378789c00-12cfe533792723da-Release`. The installed DLL and build manifest were
compared byte-for-byte with the staged artifacts and matched.

The complete sanitized native run passed 45 suites plus schema validation. Final
focused sanitized reruns passed 528 recorder checks and 24 queue checks; application
service checks also passed after context propagation was added. Release compilation,
format checks and focused static analysis completed. The recorder and queue have no
findings in that focused non-analyzer check; existing large binding/shell functions
remain flagged for size/complexity. The trace report handles a partial final JSONL
line, and identity generation preserves its output timestamp for unchanged source.

## Startup capture: run 1789246943220302

The user loaded the game with the installed build. Preserved events 1–297 in
[the startup trace](regression-evidence/2026-09-12/diagnostic-startup-1789246943220302.jsonl).
Build identity matches. These records contain no malformed lines, dropped events,
page observations or retirements; visual reproduction remains pending.

The strongest new lead is a source/clone skin discrepancy on both 35-bone torso
skins (player and Faendal). Sampled bone entries 14 and 15 have null bone nodes
on both source and clone, but the source has non-null world-transform pointers
that become null in the clone. This is already present at `clone_before_copy`
and remains at `clone_after_copy`; copying skin data does not repair it.
Player evidence: source sequences 76–77, clone 93–94 and 111–112. Faendal:
202–203, 219–220 and 237–238. The sampled bind transforms match. All five
helmet entries match, as do the 15 entries on the other player geometry.
Only the first 16 of 35 torso entries were sampled. This narrows investigation
to preserving the effective skin palette through cloning, but does not prove
that the pointers remain missing at draw time or cause the observed stretching.
Their external owner and lifetime are not established; blindly copying pointers
is not yet a justified fix.

All four shells first pose with inflation `0,0.02,0.02` and alpha `0.5`.
This starting state is not the zero-inflation empty/default-shell reproduction.
Each clone has independent skin data after copying and separate pointer arrays,
while retaining its source root and skin partition. Installed material pointers
are distinct across the four geometries.

Two interpretation limits matter: source/clone top-level renderer-data pointers
are both null, so their equality does not establish GPU buffer sharing. Clone
world transforms are captured before attachment/update; identity there is not
evidence of an incorrect world transform during rendering.

Application/cloning executes on thread 393028; first pose executes on thread
15304. Queue callbacks also execute on multiple thread IDs. This merits checking
execution ownership, but thread differences alone do not establish overlap or a
race. Next capture: cleared/default shell, diffuse selected, no layers, with
user confirmation of visible stretching and recorded zero inflation.


## Empty/default-shell reproduction

The user reported completion of the requested visible-stretch reproduction.
[Preserved capture through event 755](regression-evidence/2026-09-12/diagnostic-empty-shell-1789246943220302.jsonl).
Studio selection records Faendal (`FF00095A`), armor `000FE300`, recipe
`Skyrim-92DEC`; its camera selector says `1st` (this is UI state, not proof of
which actor geometry was visible). Editor command 37 rebuilds that recipe at
revision 6. First-pose events 737, 751 and 753 confirm inflation `0,0,0`, alpha
`1`, and emissive `0` for Faendal's torso and both player body geometries.
The helmet uses another recipe and retains `0,0.02,0.02`; it is not part of the
reset. Empty layers and diffuse selection are user-provided reproduction
conditions; this trace records recipe fingerprints rather than full recipe bodies.

Both 35-bone torso clones again lose world-transform entries 14 and 15 while
retaining matching sampled bind transforms. Player source 585–586 versus clone
602–603 and 620–621; Faendal source 695–696 versus clone 712–713 and 730–731.
This discrepancy exists before the skin-data copy and before the first pose,
including the confirmed zero-inflation case. Nonzero inflation is therefore not
necessary for the reported visible defect. The trace still does not observe
these palette entries after attachment or at draw time, so causation remains
unproven. The next shell diagnostic must cover all entries and check their state
after attachment and on the first pose, plus establish the external transforms'
owner/lifetime before choosing a repair.

No whole-binding restore skips, malformed records, capture failures, dropped
records or trace-limit events occur in the preserved capture. No Solo cycles or
Recipes-page observations are present yet. Those remain separate reproductions.

## Red-emissive baseline after reload

The user reports red emission visible on their armor after reloading. A newer
process trace exists: `BetterEnchantmentEffects-trace-1789247531051909.jsonl`.
[Preserved baseline through event 2931](regression-evidence/2026-09-12/diagnostic-red-baseline-1789247531051909.jsonl).
Continue subsequent comparisons against this run, not the previous process.
Load session 2 resumes at event 2855; the player installs four pieces across
three recipes at 2904 and reaches the runtime's rendered phase at 2928.
The red appearance is the user's visual observation; scalar emissive writes
alone cannot establish the composited texture color. Solo comparison is pending.

## Eight Studio Recipe Solo toggles

The user used the Solo button in Studio's Recipe section, on/off four times.
[Preserved capture through event 3685](regression-evidence/2026-09-12/diagnostic-recipe-solo-1789247531051909.jsonl).
Commands 179/182, 187/190, 195/198 and 203/206 alternate isolation `recipe`
and empty isolation. The selected recipe ID is literally `recipe`; the empty
`recipe` field in rebuild events is the affected-recipe argument, not the selected
recipe. The user reports that the body stayed red and the helmet shifted from red+white
to baseline. They did not assign each helmet appearance to a specific On/Off
step, so that mapping remains unspecified.

There is direct evidence of presenter aliasing between distinct render targets.
At event 3092, target 175 has renderer `00000171B1C01C00` but its presenter
`00000171C464C7C0` currently points to renderer `00000171B1C01B40`, belonging
to target 178 (3097). Both are acquired before either is recycled. At 3166–3169,
four distinct 1024 targets also share this presenter and point to the 2048 target's
renderer. First texture writes at 3192–3205 bind that same renderer for helmet
emissive/fuzz/height/rmaos and both body emissive outputs. This violates target
presentation ownership independently of a visual interpretation. Restore checks
pass because they compare the aliased presenter pointer, not its backing resource.

`RenderTargetPool::LoadPresenter` accepts any non-null loaded source texture;
`CreateTarget` then overwrites its renderer pointer without enforcing uniqueness.
Possible asset fallback/cache aliasing must be investigated; the trace does not
record the requested presenter paths or identify why loading aliases. This is a
more immediate demonstrated defect than the separate stale-target-lease concern.
The repair must enforce exclusive presenter identity and valid assets, followed
by lifetime ownership checks; merely refreshing renderer pointers on acquisition
would redirect every other target sharing the object.

## Presenter ownership repair checkpoint

First targeted fix: stage the missing presenter assets and enforce presenter
identity before replacing renderer data. The generator emits exactly the 512
paths the current loader requests, with valid uncompressed RGBA8 DDS headers.
The loader rejects missing resources, unexpected texture names (fallbacks), and
objects already claimed by this pool. Acquiring a target with replaced renderer
data fails; publication through `Texture()` also checks renderer ownership.
The pool retains claimed presenter identities across target destruction, within
its existing 512-name lifetime allocation limit. Full target leases and shell
palette fixes are not included in this checkpoint.

New JSONL events: `texture/presenter_loaded` with requested path, loaded name and
presenter identity; `texture/presenter_rejected` with a reason. The report now
counts acquisitions aliasing another live target and acquisitions with mismatched
renderer data. These assertions should be zero after a full game restart.

Repeat the red-armor baseline and four Recipe Solo on/off pairs. Record body and
helmet appearance separately; Solo intentionally suppresses other recipes.
Compare resource identity counts in addition to visible colors. A separate
Recipes-page reproduction can follow if this comparison passes.

Installed presenter repair build: `a98378789c00-0b2e500e8bb442ff-Release`. DLL, build manifest and
all 512 presenter files match staged bytes. Release compilation and sanitized
native suites plus schema validation and two Python packaging/report regressions
passed. Focused static analysis reports no findings in RenderTargetPool;
RuntimeTexturesLab retains existing function-size and shader-name string-view
findings. Formatting and whitespace checks pass. In-game validation is pending.

## Presenter repair live verification

The user reports helmet and armor now appear to show separate effects.
[Run 1789248773552860, events 1–440](regression-evidence/2026-09-12/presenter-fix-startup-1789248773552860.jsonl)
confirms the repair build is loaded. Eleven requested presenter assets load as
eleven distinct engine texture objects. All acquisitions point to their own
renderer data; the report finds zero concurrent presenter aliases, zero renderer
mismatches and zero presenter rejections. No malformed lines or restore skips.
Four ChangeView commands (21, 23, 29, 31) have already exercised retirement and
reacquisition in this capture. This supports the presenter repair across initial
application and those transitions, not completion of all rendering-state fixes.
Recipes-page no-click and shell stretching checks remain outstanding.

## Recipes-page comparison after repair

The user completed Recipes → Studio without reporting the visual result yet.
[Preserved run through event 1240](regression-evidence/2026-09-12/presenter-fix-pages-1789248773552860.jsonl).
Recipes opens at 1210, allocates an independent 128px preview target at 1212,
and requests preview rendering at 1213; Studio resumes at 1214. No application
rebuild, retirement or material write is recorded from 1210 through 1240.
The preview target's presenter and renderer differ from its scene source.
The entire capture still reports zero presenter aliases, renderer mismatches,
presenter rejections and whole-binding restore skips. Earlier additional
ChangeView commands precede this page transition and are separate from it.
Visual stability of this page comparison remains to be confirmed by the user.

## Shell added after presenter repair

The user confirmed Recipes → Studio leaves appearance unchanged, and separately
reported mask painting now works correctly. Adding a shell still produces
visible stretching. [Preserved events 1–2333](regression-evidence/2026-09-12/shell-after-presenter-fix-1789248773552860-2333.jsonl)
include painting and the subsequent shell addition at editor event 2111.
The body shell at clone `0000016FECFA5300` first poses with `0,0,0` inflation
and alpha 1 at sequence 2324; the other body shell also has zero inflation at
2329. The helmet retains its separate inflated settings.

The 35-bone source still has non-null world-transform entries 14 and 15 at
2199–2200; the clone loses them at 2216–2217 and remains missing them after
skin-data copying at 2234–2235. The other sampled skins have no null-node entries.
Thus the reported stretching persists with zero inflation after presenter
independence was repaired. The existing before-attachment capture still leaves
post-attachment/first-pose palette state and transform ownership unresolved.

## Shell palette diagnostic checkpoint

This build adds source/clone snapshots immediately after attachment and update,
then clone snapshots before/after the first pose and after pose calls 2 and 30.
The new `shell/state` event connects geometry, parent, skin and local/world
transforms. Skin/bone records carry explicit stage roles. The palette sample
limit rises from 16 to 256 and reports total/sample counts and whether limited.
This covers the observed 35-bone torso in full. External world-transform pointers
are recorded as addresses only; no external pointer is followed and no extra
engine object is retained. Construction and pose behavior are unchanged.

Capture roles to inspect: `source_after_attach`, `clone_after_attach`,
`clone_before_first_pose`, `clone_after_first_pose`, `clone_after_second_pose`,
`clone_after_thirtieth_pose`. Pose calls are not proof of rendered frames;
compare skin frame IDs, matrix counts and matrix addresses as well. The captures
are bounded per shell instance and still subject to the 32 MiB run limit.

After a full restart, add the same default shell to the affected body armor,
close the menu and let the game animate for a few seconds, then report whether
stretching persists. This checkpoint is for diagnosis, not an expected visual
fix. The existing presenter repair remains included.

Installed shell diagnostic build: `a98378789c00-4b30bbfe7b09cba1-Release`. Installed DLL, manifest
and 512 presenter assets match staged bytes. Release compilation, 528 sanitized
recorder checks, formatting and whitespace checks pass. Focused shell static
analysis reports only the existing Create/Pose/WriteGlint complexity or size
findings. In-game shell-stage capture is pending.

## Shell palette results across pose calls

The user loaded the diagnostic build and added the shell.
[Run 1789249474196324 through event 1077](regression-evidence/2026-09-12/shell-pose-1789249474196324-1077.jsonl)
confirms the expected build, with no presenter aliases/mismatches/rejections.
The complete 35-bone torso capture reveals eight lost world-transform entries:
14, 15, 21, 22, 28, 29, 31 and 32. All source world-transform entries are
non-null. The eight entries are null immediately after cloning, after skin-data
copying, after attachment/update, before/after the first pose, and after calls
2 and 30. All 35 bind transforms continue to match the source at zero inflation.
Source skin: `000002427EC3ED40`; clone skin: `00000242ED2DC7C0`.

Source and clone local/world geometry transforms match after attachment. The
clone's matrix cache progresses from uninitialized to 35 matrices at frame 1196
(second pose), then frame 1224 (thirtieth pose), with the same eight entries still
absent. This establishes that they are not simply waiting for initial attachment
or cache initialization. The source frame at creation was 1195. Relevant skin
snapshot starts: source 251, clone-before-copy 287, after-copy 324,
source-after-attach 361, clone-after-attach 398, before-first-pose 571,
after-first-pose 609, after-second-pose 694, after-thirtieth-pose 755.
The first effective body inflation is zero at event 607.

This narrows the next repair to preserving the effective skin transform palette
for entries without bone nodes. It still does not establish who owns the external
source transform storage or how to track its lifetime. A repair must resolve that
ownership before borrowing these pointers; copying bind transforms or adjusting
shell geometry transforms would not address this demonstrated loss.

## Shell palette repair checkpoint

`SkinPalette` validates and restores only missing flattened-bone transform
references, retaining their recognized owning tree, source geometry/skin and
root. The shell keeps separate pointer arrays, bind transforms and renderer
matrix caches. Unresolved ownership, unexpected non-null mismatches, different
nodes/roots/counts or shared arrays reject the shell before mutation. Runtime
checks detect changed source skin, arrays, roots, owner storage and repaired
entries. Teardown clears still-owned repaired entries before releasing owners.

The flattened-tree layout and SE/AE offset inference are documented in
REFERENCE. The live test must validate that those offsets identify the actual
owners on this runtime. A rejected shell is a failed checkpoint, not a visual
pass. Existing render/update synchronization assumptions are unchanged.

Expected trace: `palette_preserved` with eight restored entries for the affected
35-bone torso, `palette_owner` with the retained tree/storage identity, and
matching source/clone world-transform pointers after attachment and through pose
30. On failure: `palette_rejected` with a reason. The helmet and other geometry
should need zero repairs. Repeat default zero-inflation shell first, then walk
or rotate to check that it follows animation; test inflation only after that.

Installed palette repair: `a98378789c00-0836646830bc092f-Release`. DLL, manifest and presenter
assets match staged bytes. Release build and sanitized native suite passed;
the final ownership-helper cleanup passed all 13 focused sanitized checks.
Focused static analysis of SkinPalette and TransformStorage is clean. Formatting
and whitespace checks pass. Source/clone owner matching and visible shell
behavior remain pending in-game checks.

## Shell repair live result

The user reports shell added with no stretching.
[Run 1789250514170747 through event 1771](regression-evidence/2026-09-12/shell-palette-repair-1789250514170747-1771.jsonl)
confirms the repair build. The torso repairs eight entries at events 412 and
1242 across two applications. Both resolve to the same retained flattened tree
`000001F5FFAB8400`, with 94 entries at `000001F67ACD22C0`. This validates the
layout/owner membership check on this runtime and fixture.

The first torso shell has zero inflation (event 662). All 35 world-transform
entries are non-null after attachment and through pose 30; its matrix cache
progresses to 35 matrices at frames 1467 and 1495. A later edit uses inflation
`1,1,1` (event 1492); the second torso also retains all 35 entries through frames
2441 and 2469. The user did not separately describe the inflated visual result.
Helmet and the other body geometry require zero repaired entries. No palette
rejections, presenter aliases, renderer mismatches or restore skips are recorded.

The zero-inflation stretching checkpoint passes for the reported armor.
Longer animation, inflation appearance, repeated shell removal/re-addition and
load/equipment lifetime regressions remain distinct acceptance checks.

## Repeated shell recreation result

The user reports repeated shell use and movement appear correct.
[Run 1789250514170747 through event 36500](regression-evidence/2026-09-12/shell-repeat-1789250514170747-36500.jsonl)
adds 157 clone/detach events of each kind beyond the initial checkpoint and 52
additional eight-entry torso repairs. No palette rejections are recorded.
The entire capture has zero presenter aliases, renderer mismatches, presenter
rejections, whole-binding restore skips or malformed lines. These recreations
include recipe/view edits and painting, not just explicit shell add/remove.
This passes the reported repeated-recreation check on the current armor/session;
load/equipment transitions and broader fixtures remain unverified.

## Save/load and equipment result

The user reports save/load and don/doff working as expected.
[Run 1789250514170747 through event 41131](regression-evidence/2026-09-12/load-equipment-1789250514170747-41131.jsonl)
adds 4631 events, including 30 equip-event commands, a SaveRecipe command and a
completed load transition into session 2. Clear begins at 39701, finishes at
39756, and resumes at 39784. Subsequent player application reaches rendered at
40918–40919. Equip event count includes load-generated events and is not a count
of distinct manual don/doff operations.

The capture reports no palette/presenter rejections, renderer mismatches,
concurrent presenter aliases, restore skips, capture failures or trace limits.
Combined with the user's visual result, this passes the exercised load/equipment
checkpoint for the two targeted repairs. It is not completion of the broader
state-management plan or an exhaustive Quickload/crash stress test.
