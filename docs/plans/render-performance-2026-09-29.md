Status: in progress. Stages 1 to 6 are implemented and measured; review
findings for stages 3 and 4 are in
[the review record](../checkpoints/render-performance-review-2026-09-30.md).
Stage 5 has one open finding, the memory left after a crowd leaves. Stage 7,
the render target budget, is open (2026-10-06). Later stages are open. This plan follows the
[render graph correctness plan](render-graph-correctness-2026-09-29.md), whose
checkpoint findings are its starting point.

# Render performance

## Goal

The demo recipes cost a bounded, measured amount of frame time and memory, and
each optimization is chosen from a measurement of where the cost is.

## Measured facts

All figures come from the stress load order on 2026-09-29. Frame rates are
worst-case numbers relative to the same run's baseline.

| Measurement | Value |
|---|---|
| Frame rate, nothing equipped | 50 to 56 fps |
| Frame rate, three demo recipes | 15 to 19 fps, about 108 step executions per frame |
| Plugin CPU per frame, three demo recipes | 1.7 to 3.7 ms |
| Live targets after release, three demo recipes | 194 targets, 2300 MiB |
| CPU apply at equip | up to 462 ms refresh, up to 225 ms first tick |

The plugin's CPU time explains about 3 ms of the roughly 40 ms that the effects
add to a frame. The rest is GPU time or a GPU stall. Two sources are possible:
the draws the plugin issues, and the game's own rendering of the published
materials and shells. The harness must separate them.

## Stage 1: the timing harness

The harness answers three questions in order.

1. How much of the added frame time comes from the plugin's draws, and how
   much from the game rendering what the plugin publishes?
2. Which step kinds, sizes and passes cost the most GPU time?
3. Does a change reduce that cost without changing the image?

### Controls

- **Clock freeze.** The studio's freeze holds the effect clock, so animated
  chains stop re-executing while the published textures stay bound. The
  frame rate with the clock frozen against the frame rate with it running
  separates the draw cost from the presentation cost. This control exists
  today and needs no code.
- **Publication off.** A setting that keeps the render plan running but
  restores the original material textures. It measures the draws without
  the presentation cost.

### GPU spans

A **span** is a pair of `D3D11_QUERY_TIMESTAMP` queries around a sequence of
commands on the immediate context, inside one `D3D11_QUERY_TIMESTAMP_DISJOINT`
query per render tick. A span has a key: the step kind, the target side and
the format, or a pass name.

| Span | Placement |
|---|---|
| Render tick | Around `Manager::Tick` |
| Step | Around each `ExecuteStep` call in `RenderInstance` |
| Mip generation | Around each `GenerateMips` call in `TextureLabPass.cpp` |

Readback submissions are steps, so the step spans cover them. Spans nest: a
step's time includes its mip generation, and the tick span includes every step.

Every full-screen draw generates the mip chain of its target, including
intermediates that are only sampled at their base level. The mip span measures
that cost separately from the draw.

### Collection without stalls

- A ring of four frame slots holds the queries of recent ticks. A tick records
  into a free slot. When no slot is free, the tick is not timed and a
  dropped-frame counter advances.
- Each frame, the plugin polls finished slots with
  `D3D11_ASYNC_GETDATA_DONOTFLUSH`. A slot whose disjoint query reports a
  frequency change is discarded and counted.
- A tick records at most 256 spans. Spans beyond that are counted as untimed.
- Measuring never waits for the GPU, because a wait would change the frame
  time being measured.

### Engine-free core

The slot ring, the span keys and the aggregation are plain records and pure
functions in an engine-free module, tested natively like the readback ring.
The tests cover slot reuse, dropped ticks, discarded disjoint frames, the span
limit and out-of-order completion. The engine adapter in `TextureLab` owns the
query objects and calls the core.

### Reporting

The heartbeat carries, per span key, the count, the total GPU time and the
maximum, plus the timed, dropped and discarded tick counts. `trace-report.py`
prints a table sorted by GPU time per frame. A consistency check compares the
sum of step spans with the render tick span.

### Setting

`gpuTiming` in the INI turns the spans on. It is off by default, so a normal
session issues no queries.

### Stage 1 result, 2026-09-30

Three demo recipes, stress load order, one run. The user attributes the
larger frame-rate dips in the frozen phase to gameplay noise, so the table
uses the steady values.

| Phase | fps | Frame time | Plugin GPU per tick | Plugin CPU per frame |
|---|---|---|---|---|
| Nothing equipped | 50–51 | about 20 ms | none | 0.1 ms |
| Clock frozen, effects published | 43–47 | about 22 ms | 0.00 ms | 0.6–0.7 ms |
| Clock running | 27–31 | about 34 ms | 9.2–9.8 ms | 2.7–3.9 ms |

- Presentation, the game rendering the published materials and shells, costs
  about 2 ms per frame.
- The plugin's draws cost about 12 ms per frame, which matches their measured
  GPU time plus the plugin's CPU time.
- Per render tick, `CompositeStack 2048` takes 3.5 to 4.5 ms over 28
  composites, and `EvaluateProgram 2048` about 2.5 ms. `GenerateMips` takes
  about 3 ms over about 144 calls, nested inside those steps.
- A stack draws each layer with a full-screen draw, and every full-screen draw
  regenerates its target's mips, so a stack generates mips after every layer
  write, not only for its result.
- Every tick was timed; none were dropped or discarded. Apply ticks exceed
  256 spans and leave the excess untimed.

## Stage 2: mips only where they are read

- `MipPolicy` has `kGenerate` and `kNone`. The lowering sets `kNone` on every
  value it builds; stack steps keep `kGenerate`. The sharing suite checks that
  only stack results generate mips.
- A render target carries the policy it was acquired with, and the full-screen
  draw and the mesh bake generate mips only under `kGenerate`.
- A stack draws its layers without mips and calls `GenerateMipsFor` once on
  its final result. The bake's dilation gutter no longer gets mips.
- Inspection generates mips on an inspected intermediate, because a studio
  thumbnail minifies it.
- The image does not change: intermediates are read at their own size, where
  sampling uses mip 0, and the published results keep their mips.
- Waits for its measurement run.

## Stage 3: inlined fields and one-pass stacks

Goal: draw as few passes as possible without changing any result, provably.

### Why an inlined result can equal the separate draws

- A chain of separate draws evaluates a field F at the texel centres of its target,
  stores `Q(F)` in an RGBA8 target, and the consumer reads it at the same
  texel centres. `Q` is the float-to-UNORM8 conversion.
- D3D11 snaps sampling coordinates to at least 8 bits of sub-texel precision,
  so a texel-centre read returns the stored texel exactly.
- An inlined pass evaluates F at the same texel centre and applies `Q` in the
  shader. Under a reference model with exact rounding, the inlined result
  equals the result of the separate draws.
- D3D11 allows its own float-to-UNORM conversion a tolerance of 0.6 ULP. On
  values within that tolerance of a rounding tie, hardware and exact rounding
  can differ by 1/255. The separate draws already carry that tolerance.

### Inlining rules

A producer step is inlined into its consumer's pass when all hold:

1. It is pointwise: program fields, mapped fields and composed vectors.
   Bakes, dilation, reductions, lookups, cluster draws, readback submissions
   and ripples stay materialized. Image samples with animated scroll are
   deferred, because their sampling is computed during execution.
2. It has exactly one consumer. A field read by several steps or geometries
   is cheaper drawn once.
3. It animates. A static field is cached, so inlining would re-evaluate it
   every tick.
4. The inlined program fits the interpreter limits. Otherwise inlining stops
   greedily in operand order and the rest stays materialized.

Stacks draw their layers in one pass. The original steps stay in the
plan for inspection and are released when idle.

### Sub-stages

- 3a. A CPU reference interpreter for `FieldProgram` that mirrors
  `PSProgram`, with a quantize operation. Tested against the recipe
  expression evaluator.
- 3b. Program inlining: splice a producer into a consumer at one texture input,
  followed by quantize. Property test: the inlined program equals the
  consumer evaluated with that input set to the quantized producer.
- 3c. Plan inlining: a pure pass over the lowered plan that applies 3b under the
  rules above.
- 3d. One-pass stacks: one pass per stack, quantizing after each layer, proven
  against a CPU model of the layer pass.
- 3e. In-game validation: a setting that renders a one-pass stack and its
  per-layer chain and reduces their largest difference.

### Progress

- 3a done: `planners/ProgramReference` evaluates interpreter programs on
  the CPU, mirroring `PSProgram`, including `pow` as `exp2(log2(a) * b)` and
  HLSL `clamp`. It matches the recipe expression evaluator on about 3,400
  random evaluations of 17 expressions. `kQuantize` rounds to an RGBA8 step
  (ties to even, NaN to 0) and `kSplat` broadcasts `.x`.
- 3b done: `FieldProgram::Inline` splices a producer into a consumer's
  texture input, followed by `kSplat` for a scalar producer and `kQuantize`.
  Over 221 random program pairs and 5,525 evaluations the inlined program
  equals the consumer reading the stored value bit for bit; without the
  quantize step 4,482 evaluations differ.
- 3c done: `planners/FieldInlining` inlines eligible producers to a fixpoint;
  `BuildRenderPlan` lowers with `LowerRenderPlan` and then inlines. Over 150
  random animated chains (167 producers inlined) and the demo recipes on two
  geometries, every stack operand equals the lowered plan's bit for bit;
  without quantize 109 operands differ. The demo plan inlines 2 of 86 steps,
  because most animated fields feed stack layers directly, which is 3d.
- 3d, layers: a stack of up to 8 layers without a legacy curve draws in one
  `PSStack` pass. The per-layer pass and `PSStack` share `ComposeLayer`;
  `PSStack` rounds to RGBA8 between layers and leaves the last layer to the
  hardware store. Other stacks keep the per-layer path.
- 3d, layer fields: an animated program field whose every live consumer is a
  stack layer is evaluated inside `PSStack` in each of those stacks, and its
  own draw stops. This relaxes rule 2 for stack consumers, because a demo
  field feeds two or three stacks and its program is 13 to 20 instructions.
  Over 150 random chains (209 layer fields) and the demo plan (10 layer
  fields: frost into two stacks and tracePacket into three, on two
  geometries), every layer operand equals the separately drawn stored value bit for
  bit. In game, `FusionCheck` compared 18,908 stacks with layer fields, with
  a largest difference of 1/255 and none over one step. With the check off,
  a run with all three recipes held a median of 44 fps at 64 stack
  evaluations per frame. Plugin GPU time per timed tick was 10.9 ms and
  plugin CPU tick time was 2.0 ms per frame. Program draws fell to about
  0.1 per tick and the stack passes took on their work, so GPU time per tick
  did not fall measurably against the 10.5 ms of the stacked-pass run.
- 3e done: `FusionCheck` renders the per-layer chain after each stacked draw
  and reduces the largest RGB and alpha difference. In game, 13,028 stacks
  compared with a largest difference of 1/255 and none over one step, the
  bound predicted from the hardware conversion tolerance.
- After 3d, a run with all three recipes held 39 to 45 fps (median 43),
  against 24 to 31 in the previous run, with plugin GPU time per tick
  unchanged at about 10.5 ms and plugin CPU at about 3 ms. The run has no
  nothing-equipped baseline and evaluates 72 stacks per frame against 64, so
  the gain is not yet attributed; a same-scene A/B would settle it.
- Fixed after 3d: `pow` now follows `std::pow` on the GPU and in the CPU
  reference (`pow(-2, 2)` is 4, `pow(0, 0)` is 1), so a signal and a field
  agree. The same pass made lookups exact (two `Load`s and a shader lerp
  instead of 8-bit filter weights), guarded the normal blend against a zero
  vector, packed the pop count into the instruction constants, and raised
  the shader optimization level to 3.

## Stage 4: generated shaders

The interpreter runs a bytecode loop per pixel: a constant read per
instruction, a switch over the opcodes, and a stack held in a local array
that the shader indexes at run time, which drivers place in scratch memory.
Every pixel of a draw runs the same program, and the planner knows the
stack depth at each instruction, so all of this can be decided once.

### Design

- One table gives each opcode its HLSL statement. The interpreter's switch
  and the generated code are both built from it, so an opcode has one
  textual definition.
- The generator turns an `FieldProgram` into straight-line HLSL: each
  stack position is a named local, each instruction is one block using its
  table statement, and an input reads its texture or value directly.
  Numbers stay in the instruction constants, so programs that differ only
  in numbers share one shader.
- The engine compiles a generated shader once per distinct text, on a
  worker thread, and draws with the interpreter until the shader is ready
  or when its compile fails.
- A check mode draws each program both ways and reduces the largest
  difference; the bound is one 8-bit step from compiler reordering.

### Sub-stages

- 4a. The opcode table and the generator, engine-free, with golden texts.
- 4b. The shader cache and worker compile; single programs draw with
  generated shaders.
- 4c. In-game check and timing.
- 4d. Generated stacks: one shader per stack with its layers and fields
  compiled in, proven against `PSStack` by `FusionCheck`.

### Progress

- 4a done: `planners/ProgramShader` holds the opcode table and generates a
  program's HLSL. The interpreter switch built from the table matches the
  previous hand-written switch line for line. Goldens pin the switch and
  four programs; generated programs declare one local per stack slot and
  share text across numbers.
- 4b done: `GeneratedShaders` (on by default) draws single programs
  with generated shaders compiled on a worker thread; `FusionCheck` also
  compares each generated draw with the interpreter.
- 4c, check: in game, with studio edits, 4,364 generated program draws
  matched the interpreter with a largest difference of 0 (RGBA8 and f32
  targets), no generated compile failed, and the stack check still held at
  1/255 over 15,798 stacks. Single program passes are about 0.1 per tick, so
  this stage has no measurable frame cost to change; 4d carries the timing.
- 4d implemented: `planners/StackShader` generates a stack shader per stack
  shape, keyed without numbers; goldens pin a texture stack and a field
  stack. `RenderStack` draws with it once compiled.
- 4d, check: `FusionCheck` compared 5,664 stacks with a largest difference
  of 1/255 and none over one step; the program check held at 1/255 over 743
  draws; no generated shader failed to compile.
- 4d, timing: in one scene at the same per-tick workload (30 stacks at 2048,
  20 at 512 and 8 at 1024 per timed tick), `GeneratedShaders` on against off
  measured plugin GPU time per tick of 3.3 ms against 10.4 ms and a median
  of 69 fps against 43. Per stack draw: 2048, 52 us against 254 us; 512,
  41 us against 106 us; 1024, 82 us against 108 us. The off half is short
  (12 heartbeats against 41).
- After the review fixes and the function splits passed it in game, the
  `FusionCheck` setting and its program check were removed; the progress
  lines above record what it measured.

## Stage 5: crowd bursts and memory

The soak in the [unattended regression plan](unattended-regression-2026-10-01.md)
spawns twelve mannequins in the demo cuirass. Its first run, on 2026-10-01,
froze the game for about 13 seconds at the burst. The peak was 893 targets
(14 GB), and the crowd settled at 301 targets (4.2 GB).

Three changes, each measured with the same soak:

| Change | Where | Result |
|---|---|---|
| A tick admits one actor's first render | `AdmitRender`, `kFirstRendersPerTick` in `engine/ManagerTick.cpp` | The peak stayed high |
| A render step's idle output is released 500 ms after its last use, not after 30 instance ticks | `kReleaseAfterIdleMS` in `render/RenderInstance.cpp` | Burst 2 s at 80 fps, worst frame 105 ms, peak 8.6 GB |
| Static render steps are shared across actors by content | `MarkContentKeyedSteps`, `CacheKeyFor`, the Compositor's step output cache | Peak 2.2 GB, steady 136 targets and 1.4 GB, worst settle frame 24 ms |

A review of the graph invariants followed the sharing change. It made the
geometry identity cover the mesh content, partitions, bones and bound, and it
made state and signal identities carry the actor. The cache key carries the
exact bits of the numeric operands. `REFERENCE.md` (*Compositor*) records the
key parts and why each is there.

Run 20261001T160233-3 repeated the figures: burst 1.2 s, worst frame 93 ms,
steady fps 63 % of baseline at 2.4 ms of plugin time per frame.

Open: after the crowd leaves, 12 targets and 95 MB stay above baseline,
over the recovery budget of 64 MiB. The likely cause is the pool's scratch
targets, which stay until the next load; the
[regression plan](unattended-regression-2026-10-01.md) records the
arithmetic and the measurement that decides it.

## Stage 6: work that re-runs without changing

`soak-stack` solos nothing, so it measures every recipe the profile loads.
On the test profile that is the three demo recipes and an imported
effect-shader recipe (`EnchArmorHealthFXS`) whose four image sources scroll
every tick. The trace now counts executions per step kind (`step_kinds`),
times up to 2048 GPU spans per tick (the 256 cap left 849 spans per tick
untimed), and samples which input made a step run again (`step_stale`).

| Change | Where | `soak-stack` steady |
| --- | --- | --- |
| Before | | 13 fps (9 % of baseline), 18.7 ms plugin per frame, 862 targets (11.6 GB), VRAM over budget |
| An empty ripple keeps its draw | `ExecuteDrawRippleStep`, `TextureView::drawStamp` | 44 fps, 16.1 ms, 850 targets |
| Image samples read only by sampling draw nothing | `MarkDirectSamples` | 47 fps, 14.2 ms, 754 targets (10.3 GB), peak 11.8 GB |

`REFERENCE.md` (*Compositor*) records both proofs and why direct samples sit
outside the stage 3 equality proof.

Measured on 2026-10-02, run 20261002T032405-1 (the machine ran slower in that
run, so figures are relative to the same run's baseline): `soak-stack` steady
63 % of baseline fps (from 47 %), 5.5 ms plugin per frame, 8.0 ms GPU per tick,
576 targets (7.1 GB), peak 7.7 GB; `soak-fight` 75 % of baseline, 3.75 ms
plugin. The first measurement left two animated programs outside a merged stack,
because field inlining did not count merged results as live consumers; the fix
and a check that merging never adds an animated pass are in `BuildRenderPlan`.
The changes:

| Change | Where | Effect |
| --- | --- | --- |
| Material samples fold like image samples | `MarkDirectSamples` | Fewer static targets; a test checks that every image and material read in the demo and imported recipes is direct unless a reduction measures it |
| Static fields inline into static consumers only | `InlineFields`, `AnimatedSteps` | Static recipes become one texture in, one texture out apart from bakes, reductions and ripples. Inlining into animated stacks was measured and rejected; `REFERENCE.md` records the figures |
| A slot chain draws as one pass | `MergeSlotChains`, `RenderMergedChain` | One target and one pass per slot instead of one per recipe on that slot |
| State is captured once per geometry | `TextureLab::PassBatch` | Removes most of the per-pass capture and restore, the largest CPU cost in the profile |

Open: 360 stack executions per frame remain, all from signals that change
every tick (`travel` and the imported scrolls). GPU per tick is 13.5 ms, of
which 2048 stacks take 9.8 ms and their mips 4.0 ms. Plugin CPU is about
27 microseconds per step execution. `soak-ward` and `soak-frost` show that
Resonant Ward and Winterglass alone re-run no stack.

## Stage 7: render target budget

Opened 2026-10-06 by `docs/bug-reports/2026-10-06-vram-overrun.md`. In combat
with `PlayerOnly=false`, the plugin's render targets pushed the process past
the DXGI budget, and the game ran at 2 to 5 fps until it was killed.

Measured in the session of 2026-10-06 17:31 (trace
`BetterEnchantmentEffects-trace-1791322260999346.jsonl`):

| Measurement | Value |
|---|---|
| Player only, one recipe, 51 steps | about 34 targets, 70 to 95 MiB |
| One NPC, two imported recipes, 6 to 7 geometries, 232 to 270 steps | about 100 targets, about 30 of them 2048², about 1 GiB |
| Three NPCs and the player | peak 315 targets, 3.1 GiB |
| Acquires by owner | 1485 of 1616 are `render step` |
| `soak-enchanted`, 12 mannequins in six vanilla cuirass and enchantment pairs (2026-10-07, run 20261007T032055-1) | steady 3.53 GB live targets, peak 4.20 GB; a second run (20261007T032811-1) peaked at 4.69 GB, stalled 3.5 s in its steady window and hung after the crowd despawned (frames 6.8 s apart, then no trace) |

The imported recipes came from the uncommitted fill template (85 signals,
8 sources, 12 masks). `TargetsOf` (`planners/TargetCost.h`) gives the targets
a plan holds. At a 2048² base the fill template holds 69.3 MiB on its first
render (13 targets of 1024²) and 58.7 MiB while rendering (11 targets). The
template at 48938f0 holds 164.0 MiB and 52.0 MiB. Each doubling of the base
side multiplies the bytes by 4. The heartbeat reports the estimate over every
live instance (`estimated_bytes`, `estimated_first_render_bytes`). In
`soak-enchanted` run 20261007T155955-1 the steady estimate was 3365 MiB, equal
to the measured holdings without the pool and scratch targets. The first-render
estimate and the peak differed by 1 MiB. In that run 141 of 323 created targets
were 2048², so some worn geometries had a 4096² base. 7a can therefore admit an
actor by its plans' estimate. Before 2026-10-07 the estimate counted every
drawing step as held at once and left out the stacks (80 MiB per wearer and
geometry, W3 and W13 in `architecture-fit-2026-10-06.md`).

An actor out of view gets no render tick, so its instances release nothing and
keep the rendering set. Releasing an out-of-view instance down to its stacks
would cut the fill template at a 4096² base from 234.7 MiB to 85.3 MiB per
plan (item 7d).

Work:

| Item | Change | Done when |
|---|---|---|
| 7a | A byte budget for live render targets. Admission is nearest-first: the player always, then NPCs by distance. What an actor that does not fit receives is an open decision: a lower resolution, its cheapest outputs, or no effect. | A crowd soak stays under the budget, and the over-budget actors are named in the log. |
| 7b | The idle pool keeps released targets for a time (about 2 s) inside the budget, not up to `kMaxIdleTargets` 16 and `kMaxIdleBytes` 64 MiB. Today three released 2K targets fill the idle limit, so a rebuild destroys and creates most of an actor's targets (about 880 MiB created and 480 MiB destroyed per burst in the report). | A rebuild of one NPC creates no new targets in a soak. |
| 7c | A log line, written with diagnostics off, when live target bytes cross a threshold: bytes per actor and per recipe. | The bug report's question "which actors?" can be answered from the plain log. |
| 7d | An out-of-view actor's instances release their texture outputs except the stacks, as `BeginFrame` does for an instance that renders. Today `ReleaseIdle` runs only inside `BeginFrame`, so a hidden actor keeps every target of its rendering set. | A soak with actors out of view shows `estimated_bytes` falling to the stacks of the hidden actors, and the measured holdings follow. |

The soak for 7a to 7c is W10 of `architecture-fit-2026-10-06.md`: a crowd in
vanilla armor under vanilla enchantments, so the imported recipes apply.

Actor-core stage 4b reduces the churn that 7b absorbs: until it lands, an
equip event that changes an actor's plan rebuilds the whole actor.

## Later stages

Stage 1 decides the order. The candidates known now:

- Lower the animation rate for chains whose output changes slowly.
- Draw animated intermediates at a lower resolution. This changes the image,
  so it is a model decision.
- Reduce the CPU apply cost at equip.
