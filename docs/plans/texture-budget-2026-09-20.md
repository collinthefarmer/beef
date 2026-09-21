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

Target: the stress scene (28 concurrent actors) runs under 1 GiB of
target VRAM and under half the slots at full texture scale, and the
census decays as the crowd disperses. A plain unmodded-armor crowd
should sit far below that.

## Stage 0: ownership attribution — DONE 2026-09-21, and it ruled out a leak

Every `TextureLab::Acquire` caller now passes an owner word (stack,
bake, program, clusters, scratch, preview, ripple, normalSlope, sample,
mean, neutralHeight), carried on the acquire trace event;
`trace-report.py` prints live targets by owner at each save-load
boundary and at the trace end (5115a44).

The tagged stress-scene trace
(`...-trace-1789951768121688.jsonl`) settles the question: **there is
no leak.** At quit, 504 of 512 targets were checked out by live caches
and only 8 sat idle in the free pool; the census, once it reached 512,
never dropped despite dozens of logged actor retirements, and stack
targets recycled 702 times, so retire-and-recycle works. The 504 is a
legitimate working set: 28 concurrent actors, ~110 recipe-instances,
about 18 targets per actor (per-recipe output stacks plus shared bake,
cluster and normal-slope maps). The earlier "~487 survived a save-load"
reading conflated repeated acquires of one pooled object; the
distinct-object count shows demand, not retention.

Consequence: the binding problem is working-set **size**, not a leak or
missing trim. The size levers below come first; the free-pool trim
drops to a dispersal aid.

## Stage 1 (now first substantive fix): per-slot resolution factor and formats — the big lever

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

Landed and measured 2026-09-21 (4689920). On a 30-actor crowd
(`...-trace-1789953808540559.jsonl`) VRAM peak fell from 5.4 GiB to
2.4 GiB, a 55% cut on a slightly larger scene; the acquire-size
histogram shifted down as designed (512 and 256 now dominate, with
1024/2048 only where normal and height stay full). Still open: the
studio authoring control for the override field, and the in-game visual
check that a 4K piece's normal and height detail is unchanged.

## Stage 2: re-measure — DONE 2026-09-21, and it re-ranks the rest

The stage-1 trace answers the ranking question and exposes a split the
byte census hid: **VRAM and slot count are separate limits, and
resolution only touches VRAM.** VRAM more than halved (5.4 to 2.4 GiB),
but the pool still reached all 512 slots with 509 distinct targets
live — the same ~500-target working set, now cheaper each. Reducing a
target's size does not reduce the number of targets or slots.

Consequence: the budget has two numbers, not one. VRAM at 2.4 GiB is
above the 1 GiB target but within reach of stage 3; the 512-slot count
is now the harder wall and resolution cannot move it. Both point to
static demotion (stage 3), which is the only lever that removes a
target and its slot outright, not just its bytes. Cluster maps
(~0.66 GiB, native size, unchanged by stage 1) are now a larger share
of the VRAM and a candidate for their own reduction.

## Stage 3: static demotion — CONFIRMED PRIMARY by the stage-2 measurement

The working set is dominated by stacks, and in a town crowd most stacks
are static (no animated signal). A static stack renders once; demote
its result to a BC7 immutable texture behind the same presenter (about
5.3x smaller), release the render-target view **and its presenter
slot**, and let scalar single-channel stacks use R8. This is the only
lever that reduces the target and slot COUNT, which stage 2 showed is
the binding wall (512 slots exhausted regardless of size); it also
takes the next VRAM bite (2.4 GiB toward the 1 GiB budget). Engine work
under the renderer lock; M to L. Now the next stage.

Design note from stage 2: demotion must return the presenter slot, not
only free the render-target view — the slot count, not the byte count,
is what the 512 cap enforces. A demoted static texture needs its own
lighter presenter path or must relinquish the slot to the pool.

## Stage 4: eviction by distance — for the crowd tail

The stress scene held 28 actors at once; the ones not near the player
need not hold working targets. Retire the stacks of actors beyond a
distance, restore the original material, and reapply on approach; the
actor-state tables make reapply cheap. Size L; taken if stages 1 and 3
leave a packed crowd over budget.

## Stage 5: trim the free pool in play — dispersal aid

The pool records a recycle time per free target; a periodic tick sweep
destroys free targets idle past a threshold (proposed 30 s), returning
slot and VRAM. In a packed scene this reclaims little (8 targets were
free at quit); its value is returning memory when a crowd disperses.
Size S.

## Packaging, independent: presenters into a BSA

The 512 one-pixel presenter DDS files move into an archive so no loose
files ship; `LoadPresenter`'s name validation must accept BSA-loaded
names. The pool's count can shrink to what the post-stage-3 census
justifies. Belongs to the roadmap's packaging item. Size S.

## Decisions this plan asks for

Settled 2026-09-21:

1. **Budget: 1 GiB** of target VRAM for the stress scene (28 actors) at
   full texture scale, on 8-12 GiB cards beside a 2K pack.
2. **Default per-slot factors:** normal and height full; diffuse and
   rmaos half; emissive and the response slots (fuzz, glint, coat,
   subsurface) quarter. Masks and intermediates follow their consumer.
3. **Per-output resolution field: yes**, added to format 1 now as
   `resolution: full|half|quarter` on an output, overriding the slot
   default. Ships with stage 1 so the schema-agreement test and the
   freeze see it together.

Open, minor:

4. The idle-trim threshold (proposed 30 s) — settled with stage 5.

## Order

0 (attribution, done; no leak) → 1 (resolution + formats, the lever) →
2 (measure) → 3 (static demotion, expected primary) → 4 (distance
eviction) and 5 (free-pool trim) as the numbers demand. Packaging rides
with the roadmap's packaging item. Every stage ends with the stress
scene and `tools/trace-report.py` against the baseline.
