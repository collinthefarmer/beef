Status: in progress. Stage 1 (the timing harness) is implemented and
measured; later stages are open. This plan follows the
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

## Later stages

Stage 1 decides the order. The candidates known now:

- Lower the animation rate for chains whose output changes slowly.
- Draw animated intermediates at a lower resolution. This changes the image,
  so it is a model decision.
- Reduce the CPU apply cost at equip.
