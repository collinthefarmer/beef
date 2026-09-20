# Texture budget plan (2026-09-20)

Gate 1's texture half, measured on 18e9b9a with the stress scene, fails
three ways: the pool reaches all 512 presenter slots and 8,016 MiB of
render-target VRAM within about 50 seconds of active play; nothing
returns during play because the only trim runs on the save-load
teardown; and about 487 of the 512 targets survive that teardown, held
by strong references outside the pool, dying only gradually through the
next session. The measurement, and the trace it came from, are the
baseline every stage below compares against
(`BetterEnchantmentEffects-trace-1789941114749860.jsonl`, session 1:
census 0 to 512/8,016 MiB; acquires 1,583 at 1024, 948 at 2048, 58 at
4096, 283 at 64, 24 at 512).

The cost of one target is `Metrics::MippedRgbaBytes(side)`: 5.6 MiB at
1024, 22.4 MiB at 2048, 89.4 MiB at 4096.

Target: the stress scene runs under 1 GiB of target VRAM and under half
the slots at full texture scale, and the census decays to an idle level
when the crowd disperses and collapses on a save load. A plain
unmodded-armor crowd should sit far below that.

## Stage 0: root-cause the retention — first, because it is a defect

`TextureLeases` holds weak references (`planners/TextureLeases.h`), so
the survivors are pinned by strong `shared_ptr`/`TextureRef` holders.
Candidates: `TextureRef`s in retired `LiveGeometry`/material state that
the sweep misses, preview tickets (`ConsumptionLeases`), compositor
chained-rendering state, the scratch map, studio/menu-held previews.

1. Attribute ownership: give `TextureLab::Acquire` an owner tag from
   the caller, carried on the `acquire` trace event and into a
   per-owner live count on the metrics heartbeat.
2. Reproduce: fill the pool in the stress scene, save-load, read the
   per-owner counts that remain.
3. Fix each holder so retirement releases it; the sweep order in
   `Manager::Clear` may need the same look.

Done when: after a save load the census returns to near zero within
seconds. Size S to M; the fix is unknown until the owner is named.

## Stage 1: trim the free pool in play

The pool records a recycle time per free target; a periodic sweep
destroys free targets idle longer than a threshold (default 30
seconds), returning slot and VRAM while keeping burst reuse. Runs from
the tick, bounded work per pass.

Done when: leaving the crowd, the census visibly decays without a save
load. Size S.

## Stage 2: per-slot resolution factor — the big lever

Today one size serves every output of a piece: the native maximum of
the material's maps scaled by the global `TextureScale`
(`ManagerApply.cpp`, `StackSize`). Most slots do not carry
map-frequency content. Apply a per-slot factor where each output's
target size is chosen: normal and height full; diffuse and rmaos half;
emissive and the response slots quarter. Masks and intermediates follow
their consumer's size. Defaults live in settings, adjustable.

Expected: roughly 4x to 16x off the common case (a 4K piece's emissive
falls from 89 MiB to 5.6 MiB).

Open for the format freeze (gate 5): whether an author may override the
factor per output (`resolution: full|half|quarter`) as a format-1
field. Engine policy ships first; the field is decided at the freeze.

Done when: the stress scene census drops accordingly and a 4K armor's
normal and height detail is visually unchanged at the in-game
checkpoint. Size M.

## Stage 3: re-measure, then rank what remains

Re-run the stress scene, compare the Measurement section against the
baseline, restate the budget numbers, and feed the author cost model
(the studio knows each stack's size and whether it animates). If the
target is met, stages 4 and 5 wait; if not, the numbers rank them.
Size S.

## Stage 4: static demotion and formats — evidence-gated

A stack with no animated signal renders once; compress its result to a
BC7 immutable texture behind the same presenter (about 5.3x smaller),
release the render-target view, and let scalar single-channel stacks
use R8. Engine work under the renderer lock; sized honestly at M to L,
taken only if stage 3 says static stacks still dominate.

## Stage 5: eviction by distance — contingency

If a large crowd still exceeds the budget: retire the stacks of actors
beyond a distance, restoring the original material, and reapply on
approach; the actor-state tables make reapply cheap. Size L, only on
evidence.

## Packaging, independent: presenters into a BSA

The 512 one-pixel presenter DDS files move into an archive so no loose
files ship; `LoadPresenter`'s name validation must accept BSA-loaded
names. The pool's count can shrink to what the post-stage-2 census
justifies. Belongs to the roadmap's packaging item. Size S.

## Decisions this plan asks for

1. The budget number the gate holds (proposed: 1 GiB at full scale for
   the stress scene).
2. Default per-slot factors (proposed above).
3. The idle-trim threshold (proposed 30 s).
4. The per-output resolution override as a format-1 field — needed
   before the gate-5 freeze either way, as a yes or a written no.

## Order

0 (defect) → 1 (cheap relief) → 2 (the lever) → 3 (measure) → then 4
and 5 only as the numbers demand. Packaging rides with the roadmap's
packaging item. Every stage ends with the stress scene and
`tools/trace-report.py` against the baseline.
