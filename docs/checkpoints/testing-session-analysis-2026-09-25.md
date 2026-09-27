Status: record. Paint rebuild fan-out, render-target pressure, authoring
errors and evidence limits from the preserved session.

# Long testing session: first analysis

Analyzed the preserved `build/evidence/session-20260925T022309Z` snapshot.
No installed recipes, settings, DLLs or captured evidence were changed.
Machine-readable counts are in
`build/evidence/analysis-session-20260925T022309Z/summary.json`.

## Scope and limits

The loaded build was `e67fc9b6d350-ef0f9d58560e1eca-Release-ce036c51`.
This predates the source-cleanup, material-offer consolidation and RGB-clustering
fixes. This session cannot validate those fixes.

The plugin log has 29,907 lines, from 20:26:44 through 22:20:42 on its local
clock. The trace contains 250,222 valid JSON records in segments 0, 13 and 14.
The retained windows are approximately 00:26:44–00:35:46 and
02:10:19–02:23:30 UTC on September 25. Segments 1–12 were already rotated away.
The final segment was being written during capture. No malformed JSON,
truncated event flags or queue-dropped events were observed in the copied
segments; that does not recover the events lost to rotation. The capture
contains no controlled end-of-session teardown measurement.

## 1. Paint edits rebuild far more actors than necessary

Confirmed in retained trace: 249 `editor.UpdatePaint` commands each generated
`execute_refresh` for 22 distinct actors, totaling 5,478 actor refreshes.
Across all retained operations, 18,394 application phase reports were
`unmatched`; 18,358 carried “no applicable loaded geometry, or rendering is
disabled.” These are phase reports, not counts of unique failed edits.
There were no `failed` phase reports in the retained trace.

The code explains the fan-out: `RecipeEditor::UpdatePaint` calls `ApplyEdits`,
which uses `Manager::ChangeAndRebuildActors`; that enumerates application actors,
retires effects, mutates the recipe, and refreshes them. The captured settings
have PlayerOnly=false. Most unmatched reports are expected under paint isolation,
but repeatedly visiting unrelated actors is avoidable work.

Recommendation: scope paint invalidation to its affected wearer(s), then preserve
unaffected geometry/output state where possible. Retain the broader path for
changes that genuinely alter matching across actors. This is a separate issue
from retaining historical source definitions.

## 2. Render-target pressure and churn deserve the next performance pass

| Measurement | Captured value |
|---|---:|
| Lifetime peak estimated target bytes | 3,933,951,202 bytes (3.66 GiB) |
| Lifetime peak target count | 347 |
| Final estimated target bytes | 2,408,710,057 bytes (2.24 GiB) |
| Final target count | 261 |
| Retained target acquire events | 18,489 |
| Retained target destruction events | 15,846 |
| Retained recycle events | 18,504 |

These are the plugin's counters for live RenderTarget objects, estimated as
mipped RGBA8 texture size. They are not measured total GPU VRAM or process RAM.
Acquire includes pooled reuse; recycle does not necessarily mean destruction.
The lifetime peak appears in heartbeat counters even though the precise peak
allocation event may be in a discarded segment.

The largest acquire categories were expression programs (12,288), stacks
(4,764), ripples (668), previews (503), and clusters (240). There were 8,353
2048-square acquisitions. A 2048-square mipped RGBA8 target is approximately
21.33 MiB, so many intermediates quickly become expensive.

Early retained samples fall from about 1 GiB to 247 MiB. Late samples rise to
about 3.25 GiB during work and then settle around 2.24 GiB. This is **not enough
to prove a leak**: recipes, active outputs and preview state changed, and there
is no final close-editor/unequip/idle baseline. It does establish a substantial
working/retained footprint. The idle pool limit is 16 targets / 64 MiB, so the
multi-GiB total cannot be explained by that pool alone; active or otherwise held
targets dominate. Missing segments prevent complete ownership reconstruction.

Next measurement: bytes/counts by recipe, geometry and owner; active versus idle
pool counts; then compare editor closed, armor unequipped and caches aged out.
Inspect expression intermediate reuse and rebuild invalidation before simply
raising the pool budget. The source-cleanup fix may help, but these logs do not
establish how much of the footprint came from unused sources.

## 3. Refresh timings show occasional expensive work, not uniform slowness

For 9,652 retained individual refresh timings: median 0.163 ms, p95 1.098 ms,
p99 18.440 ms. There were 124 refreshes above 16 ms and one above 100 ms.
The maximum was 433.094 ms on the initial player refresh. The maximum observed
readback was 17.469 ms. These are instrumented operation timings, not GPU timings
or frame-time measurements; summing actor refreshes into a frame requires more
scheduling evidence. Verbose and diagnostic logging were both enabled.

## 4. Logged errors are authoring states, with room for better field guidance

| Local log time | Evidence | Interpretation |
|---|---|---|
| 21:02:06–08 | Lines 17783–17809: head/hands partition absent on four cuirass geometries | Unsupported partition previews; no missing asset diagnosis. Disable or explain unavailable combinations before rendering. |
| 21:29:07 | Line 28437: curve using `@charge` refused | Correct rejection: curves read x, not recipe rows. Explain this in the curve field. |
| 21:37:16 | Lines 29768–29772: world-space anchor needs vec3; dependent signals inert | Temporary invalid Ward trigger configuration. Restrict/explain anchor choices for scalar hit payloads. |
| 22:02:55 | Line 29893: image source path empty | Incomplete image-source authoring state. Show a clear draft/inert state or require a path before activation. |

The saved Ward recipe returns to its named spine-node anchor and has no empty
image source, so neither logged invalid state persists in that saved snapshot.
The snapshot's CommunityShaders log has no [E]/[C] entries; its warnings include
devbench delivery and UI/localization notices. The plugin log contains no
presenter-missing or D3D-allocation failure messages. This supports recovery from
the earlier packaging failure, not a claim of complete stability or crash-free
shutdown.

## 5. Saved edits are valuable evidence, but differ from the shipped demos

Arcane Circuit has 8 sources rather than 2. `charts`, `components`, `metallic`
and `occlusion` are unreferenced by the saved recipe. No cluster sources remain
in these final saved examples, so this snapshot does not independently reproduce
the full cluster-tuning accumulation history.

Arcane now also writes height (scale 1) and fuzz, with changed masks and magicka
response. Winterglass no longer writes height, and its diffuse/roughness amounts
changed. Ward has four outputs, stronger glow/light, alpha shell compositing,
added shell height and bone/luma masking. These are user-authored variations,
not defects by themselves; their rendering cost and appearance cannot be
attributed to the original three-example bundle.

The retained trace contains successful installs, 4,624 restore-group events,
344 shell clones and 344 detaches. This supports exercised application and
retirement paths, but matching counts over disconnected windows do not prove
absence of leaked objects. Visual quality, actual hit response, selected-effect
magnitude correctness and complete teardown still need direct runtime evidence.

## Recommended order

Implementation and acceptance tasks are tracked in the sole active plan's
[long-session follow-up](../plans/alpha-preparation-2026-09-22.md#long-session-follow-up-2026-09-25).
This checkpoint remains the evidence record; it does not mark those fixes complete.

1. Narrow paint-update actor/geometry invalidation.
2. Instrument target ownership and measure the idle/unequip baseline; reduce
   repeated expression/stack allocations based on that evidence.
3. Tighten partition, trigger-anchor and curve authoring guidance.
4. Recheck source cleanup and color clustering using the rebuilt bundle while
   preserving the captured user recipes as the reproduction fixture.

For the next long run, retain more useful lifecycle evidence per byte: repeated
shell bone dumps dominate this retained trace (71,034 events), while twelve
middle segments disappeared. Prefer change-based detail plus periodic ownership
summaries; no diagnostic settings were changed during this analysis.
