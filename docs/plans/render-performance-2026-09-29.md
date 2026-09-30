Status: in progress. Stages 1 to 4 are implemented and measured; review
findings for stages 3 and 4 are in
[the review record](../checkpoints/render-performance-review-2026-09-30.md);
later stages are open. This plan follows the
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

## Later stages

Stage 1 decides the order. The candidates known now:

- Lower the animation rate for chains whose output changes slowly.
- Draw animated intermediates at a lower resolution. This changes the image,
  so it is a model decision.
- Reduce the CPU apply cost at equip.
