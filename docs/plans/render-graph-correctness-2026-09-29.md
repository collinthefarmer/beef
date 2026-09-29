Status: in progress. Stages 0 to 3 and 5 to 7 are implemented; stage 4 is closed by decision D1. Stages 2 and 5 to 7 passed their in-game checkpoints; the revised stage 6 passed its checkpoint on 2026-09-29. The in-plan chain step of stage 5 is deferred. This plan follows the
[render-plan model](render-plan-model.md) and the
[render-plan checkpoint](../checkpoints/render-plan-2026-09-27.md). It does not
change their numerical, sampling or timing contracts, except where a decision
below says so.

# Render graph correctness

## Goal

The render graph does each operation only when a value that the operation
reads has changed. A cached result is always equal to a from-scratch
evaluation. A test proves both statements for every plan shape that the
planner can emit.

## Invariants

The executor (`RenderExecution`, `planners/RenderExecution.h`) reuses a step
result while every observed input version is unchanged. That rule is correct
when three invariants hold.

| Invariant | Statement | What it gives |
|---|---|---|
| **Purity** | A step's output is a function of its operand values and its static parameters. `Execute` reads nothing else. | Reuse is safe. |
| **Sound versioning** | A value's change version advances whenever the value changes. | No stale result. |
| **Exact versioning** | A value's change version advances only when the value changes. Resource outputs are exempt: a successful texture write, lookup or CPU product always advances. | No repeated work from imports or uniform values. |
| **Sound sharing** | The planner merges two steps only when their **canonical keys** are equal. A canonical key holds the step kind, its static parameters, its operand references and its requirements. | Sharing is safe. |
| **Complete sharing** | The planner never emits two steps with equal canonical keys. | No duplicate work in one plan. |

Purity, sound versioning and sound sharing give correctness. Exact versioning
and complete sharing give minimal work.

## Current breaks

Each row is a break found by code reading on 2026-09-29. None is confirmed in
game.

| Break | Invariant | Location |
|---|---|---|
| `SameRenderValue` has no case for `RenderFirings` or `RenderTransform`. Each import advances their versions. | Exact versioning | `render/RenderInstance.cpp` `SameRenderValue`, `Update` |
| `RenderSlotChain` calls `Update` once per slot. With the break above, a ripple draws once per slot per frame. | Exact versioning | `engine/ManagerTick.cpp` `RenderSlotChain` |
| A reduction poll is counted per `Evaluate` call, not per frame. Readback patience ends N times sooner with N slots. | Exact versioning | `render/TextureLabReadback.cpp` `CollectReduction` |
| The ripple step observes a transform operand that `Execute` does not read. `RootTransformInput` is classed as changing. | Purity | `planners/RenderPlanLowering.cpp` ripple case; `recipe/RecipeGraphLowering.cpp` `Analyze` |
| A chained stack base advances its version from the static `animated` flag. A downstream stack redraws while its upstream stack is cached. A downstream stack stays stale when its upstream stack redraws for a layer-visibility change. | Sound and exact versioning | `engine/ManagerTick.cpp` `RenderSlotChain`; `RenderInstance::Render` |
| `ReduceField` returns `ring.latest` while a newer submission is pending. The result can measure an older field version. The model forbids this. | Purity | `render/TextureLabReadback.cpp` `CollectReduction` |
| `CompositeStackStep` reads `geometry_.material.flatDisplacement` and `Compositor::NeutralHeight()`. `Render` derives the base from `geometry_.material`. None of these are operands. | Purity | `RenderInstance::Execute`, `RenderInstance::Render` |
| `Field` emits a new fill step for each use of a uniform value. | Complete sharing | `RenderPlanLowering.cpp` `PlanBuilder::Field` |
| `Step` compares structure for three step kinds. `Lookup` and the ripple position search use their own comparisons. The ripple search finds only bakes lowered before it. | Complete sharing | `PlanBuilder::Step`, `PlanBuilder::Lookup`, ripple case |
| `SignalState::Tick` evaluates every node in the tick order on every tick. `changing_` is computed and not used. | Exact versioning (CPU) | `recipe/Signals.cpp` `SignalState::Tick` |

## Stages

Each stage ends with native tests passing and a Windows build. Stages 2, 5
and 6 also end at an in-game checkpoint.

### Stage 0: the proof harness

Implemented 2026-09-29. Two native suites hold the harness.

`tests/planners/renderexecution_property_tests.cpp` runs `RenderExecution`
with integer values and fake steps. A **reference evaluator** in the test
computes each requested output from scratch.

- The generator makes 200 random acyclic plans. The plans hold shared
  producers, reductions, unavailable steps, stacks with hidden layers and
  imported stack bases. Fake steps fail for some operand values.
- Each plan receives 400 random actions: new input values, repeated imports of
  equal values, visibility changes and evaluations of a stack.
- **Soundness check:** each cached result equals the reference result.
- **Exact versioning check:** an input or output version advances only when
  its value changes.
- **Minimality checks:** a step executes at most once per evaluation. Only
  steps that the requested output reads execute. A cached step reruns only
  when an observed version changed.
- The suite counts, and does not fail on, a **revisited rerun**: a step
  reruns because an operand changed and then returned to its observed value
  before the step was evaluated again. Version-based caching reruns in that
  case by design. The first run counted 21 revisited reruns and no other
  rerun.

`tests/planners/renderplan_sharing_tests.cpp` lowers real plans and reports
any two steps with equal kinds. Each step record has a defaulted equality, so
equal kinds are the canonical key that stage 3 uses.

- The three demo recipes, lowered into one geometry plan at one stack size
  and at two stack sizes, contain no duplicate steps.
- Two synthetic recipes confirm two breaks. Each is a check marked "known
  break until stage 3", which stage 3 inverts:
  - a uniform used as a layer source and as its mask gets two fill steps;
  - a position bake source lowered after a ripple gets a second bake.

The executor itself passes every check. The other breaks live in engine code
(`RenderInstance`, `ManagerTick`, `TextureLab`), which the native suites
cannot build. Stages 1, 2, 5 and 6 each move the deciding logic into an
engine-free function and add it to the harness.

### Stage 1: exact imports

- `SameImportedValue` (`render/RenderInstance.cpp`) compares every
  `RenderValue` alternative through one `SameImport` overload per type. A new
  alternative without an overload does not compile.
  - `RenderFirings` compares by value. It lives in the engine-free
    `planners/RenderFirings.h` with a defaulted equality.
  - `RenderTransform` compares by node pointer.
  - `MaterialInputs`, `TextureView` and `StackResult` compare each texture by
    pointer and allocation generation.
  - Shared CPU products and lookups compare by pointer. Their objects are
    immutable after creation.
- `UpdateInput` advances a version only when the value is not equal. The
  `mutated` parameter is removed after stage 5.
- `ManagerTick` calls `RenderInstance::Update` once per geometry per frame,
  through `UpdateRenderInputs`, before the slot loop.
- Readback poll counting moves to stage 6. The synchronous wait of decision D2
  removes the poll patience.
- The harness checks the executor side of this rule: a repeated import of an
  equal value does not advance its version. The engine-side comparison is
  covered by the compile-time totality above and by the stage 2 checkpoint.

### Stage 2: operands are what execution reads

- `ExecuteStep` (`render/RenderInstance.cpp`) is a free function over
  `(RenderStep, resolved inputs, scratch)`. It has no access to the render
  instance, the geometry or the compositor. It still uses the `TextureLab`
  singleton, which is the GPU backend and holds no graph data.
- `RenderInstance::Render` resolves the whole stack base before it imports it:
  the chain base, else the material base map, else the neutral height texture
  for a flat-displacement height slot. The base operand then fully determines
  what the stack step reads, and `SameImport` compares it.
- The neutral height texture is now resolved even when every layer is hidden.
  If it cannot be created, the stack reports "neutral height base is
  unavailable" in that case too.
- `RippleOperation` has no transform operand. Firing origins reach root space
  when the firings are imported, so the ripple step read no transform.
  `RootTransformInput` stays classed as changing, because `ToRootOperation`
  reads the root transform through the tick environment.
- In-game checkpoint: the three demo recipes render as before, a ripple
  fires on a hit, and the log shows no `render inputs:` or stack errors. The
  execution counter from stage 3 is not yet available, so the checkpoint
  checks appearance and error lines only.
- Checkpoint passed 2026-09-29. The in-game regression runner passed every
  phase with the stage 2 build. The game log holds no render input, stack or
  neutral height errors. A layer mute and a ripple firing were not reported
  separately.

### Stage 3: canonical step construction

- `PlanBuilder::Step` (`planners/RenderPlanLowering.cpp`) returns the existing
  step when one has an equal kind. Every step record has a defaulted equality,
  so the canonical key is the step kind itself.
- `Lookup` and the ripple lowering call `Step` like every other constructor.
  The separate lookup comparison and the ripple's position-bake scan are gone.
  A position bake is now shared whatever order the ripple and the bake source
  are lowered in.
- `Field` goes through `Step`, so one uniform used as a source and as a mask
  gets one fill step.
- Sharing is sound because `ExecuteStep` reads only its operands and the
  static parameters in the step kind (stage 2).
- The two sharing checks in `renderplan_sharing_tests.cpp` are now positive
  assertions.
- The metrics heartbeat in the JSONL trace records `frames`,
  `render_evaluations` and `step_executions`. `tools/trace-report.py` prints
  the step executions and stack evaluations per frame.

### Stage 4: requirements per operand

Closed without code by decision D1. Every current step kind needs its own
requirements from each field operand: a reduction needs float from its field,
and a draw needs its own size and format from the fields it samples. Per-operand
declarations would therefore reproduce the current propagation. CPU steps
(`BuildBakeBuffersStep`, `SubmitMaterialSampleStep`, `ClusterMaterialStep`) carry no
requirements, so stage 3 already shares them across formats. A future step
kind that needs different requirements from an operand reopens this stage.

### Stage 5: the stack chain base

- A stack's imported base carries the content version of its texture.
  `RenderInstance::Render` returns the stack output's change version in
  `StackResult::contentVersion`. `RenderOutput::ContentVersion` keeps it, and
  `ManagerTick` passes it to the next contribution in `StackBase`.
- `SameImport` compares the texture and the content version, so a downstream
  stack recomposites exactly when its upstream stack drew new content. A
  material base map has content version 0.
- `StackBase::animated` and the `mutated` parameter of `UpdateInput` are
  removed. `RenderOutput::Animated` remains for the snapshot row.
- The harness models stack outputs as reused texture handles that a chained
  stack reads through. With the content version in the base, every result is
  sound. A control run that imports only the handle produces stale results,
  which shows that the soundness check detects this break.
- Deferred: moving the chain into the plan as a step. The base import already
  satisfies the invariants. The chain step would move the fallback selection
  from `ManagerTick` into the plan, where the harness covers it, and needs an
  optional-operand feature in the executor.

### Stage 6: explicit delayed measurements

The first implementation waited synchronously for every reduction. The stage 5
to 7 checkpoint measured stalls of up to 216 ms, so decision D2 was revised.

- A reduction lowers to a `SubmitReductionStep` and a measurement input
  (`ReadbackBinding`). The submission returns a constant acknowledgement.
  The executor evaluates the submission whenever the measurement input is read.
- `RenderInstance::CollectReadbacks` runs once per frame from `ManagerTick`
  and imports each completed readback without waiting. The engine-free
  `ReadbackRing` in `planners/GpuReduction.h` reserves one of three staging
  slots per submission, accepts the newest completion and discards older ones.
- No readback waits. A stack that fails while a measurement is unset and has a
  submission in flight returns `StackPending`. `Compositor::Render` reports
  `StackRender::kPending`, so the output is neither rendered nor failed, and the
  application stays prepared until its outputs render.
- Material sampling uses the same shape: `SubmitMaterialSampleStep` copies the
  RMAOS and diffuse maps to staging, and a readback input receives the decoded
  sample. The cluster analysis reads it. `ReadbackBinding` covers both kinds of
  readback. The run after the reduction change measured material sample
  readbacks of up to 522 ms, because each one waited for all GPU work queued by
  the apply.
- A first version waited for the first measurement at apply. Its test run
  measured 20 waits of up to 942 ms, because a blocking map waits for all GPU
  work queued by the apply, not only the reduction.
- The harness models submissions and a backend that delivers any pending
  measurement later. Every result is sound, and consumers rerun only when a
  measurement changes. `tests/planners/renderplan_tests.cpp` states the
  contract on a lowered plan: reading a measurement submits the field, an
  unchanged measurement leaves the lookup cached, a changed one rebuilds it, and
  a failed submission blocks consumers.
- In-game checkpoint: see stage 7.

### Stage 7: recipe-graph evaluation

- `RecipeGraph::ChangingTickOrder` lists the tick nodes that are classed as
  changing. `SignalState::Tick` evaluates the whole tick order on the first
  tick and only the changing tick order after that.
- Every node kind that reads the game environment is classed as changing:
  time, actor values and state, enchantment, effect shader and world-to-root.
  Stateful nodes are changing too.
- `tests/recipe/tickfolding_tests.cpp` ticks each demo recipe 50 times. Each
  node that was evaluated only once equals its value from a fresh tick at the
  final time.
- The layer source mean is the named builder `SourceMean`. The unscrolled
  image copies (`ImageAt`) and the image normalization (`Normalized`) were
  already named builders.
- The render-plan model states decision D3 in its sampling contract. Two tests
  in `tests/recipe/recipegraph_tests.cpp` state it: the image mean and a curve
  mean over a scrolled image both read coordinates without scroll and with
  tile.
- In-game checkpoint for stages 5 to 7:
  - The regression runner passes every phase.
  - The three demo recipes render as before. Normalized images and curves
    that use a mean show their final brightness from the first frame.
  - `python3 tools/trace-report.py <trace.jsonl>` on the newest
    `BetterEnchantmentEffects-trace-*.jsonl` prints a "Render steps" line.
    The line reports step executions and stack evaluations per frame.
  - The game log holds no `render inputs:`, stack or
    `reduction readback failed` errors.

## Checkpoint findings, 2026-09-29

The stage 5 to 7 build passed two regression runs. Every plugin check and
every visual answer passed, and the game log holds no render input, stack,
neutral height or reduction readback errors. The traces also record these
measurements. They are observations, not checks against a budget.

| Measurement | Stage 2 build, three recipes | Stage 5 to 7 build, three recipes | Stage 5 to 7 build, Arcane Circuit soloed |
|---|---|---|---|
| Peak live targets | 433 | 433 | 156 |
| Peak target memory | 5868 MiB | 5856 MiB | 2055 MiB |
| 2048 targets at the peak | 205 | 205 | 84 |
| Reductions | 80, mean 5.7 ms, max 103 ms (asynchronous) | 40, mean 12.7 ms, max 216 ms (synchronous) | 9, mean 32 ms, max 52 ms (synchronous) |
| Step executions per frame | not recorded | 4.67 | 16.13 |

- **Target memory.** Each texture-producing step keeps its own target for
  as long as its render instance lives, so memory grows with the number of
  steps. Retire releases the targets: in the stage 2 trace the live count falls
  from 396 to 30 on retire and returns on reapply, so this is not a leak. The
  stages in this plan do not add steps; stage 3 removes duplicates. The plan
  model lists allocation reuse as out of scope.
- **Reduction stalls.** Decision D2 makes each reduction wait for the GPU. The
  wait happens when a measured field changes, mostly while an application is
  prepared. The longest wait was 216 ms.
- **Per-frame counts.** The heartbeat averages over every frame of the trace,
  including frames with no effect applied, so the per-frame counts are lower
  bounds for an equipped actor.

### Revised stage 6 checkpoint

With all three demo recipes live, every readback polled without waiting:

| Readback | Polls | Found data ready | Longest poll |
|---|---|---|---|
| Reduction | 94 | 80 | 0.01 ms |
| Material sample | 19 | 10 | 0.10 ms |

The regression runner passed every step, and the game log holds no errors.
The longest application refresh was 424 ms. The refresh metric times the CPU
apply in `ManagerApply` (instances, plans, meshes), not GPU work or readbacks.
Earlier traces show refresh maxima of 541 to 668 ms, so this cost predates the
plan and is outside it.

## Decisions

| Id | Question | Options |
|---|---|---|
| D1 | A display consumer and a measurement consumer need the same producer. Which format does the producer use? | Decided 2026-09-29: draw twice, once in each format. The format is part of every field's canonical key. No step is shared across formats. |
| D2 | Can a reduction result lag its field? | Decided 2026-09-29: yes, explicitly. The first choice, a synchronous wait on every change, stalled the game for up to 216 ms (see Checkpoint findings). The revised decision: a delayed measurement input; consumers are unavailable, and their stacks pending, until the first measurement arrives. |
| D3 | The mean of a scrolled image uses the unscrolled image. Is that part of the contract? | Decided 2026-09-29: the measurement is defined as the mean of the image at zero scroll. Tile, mirror, transpose and mip still apply. Scroll changes do not rerun the reduction. |

## Out of scope

- Algebraic rewrites that are exact only in real numbers, such as deriving the
  mean of a normalized image from the image mean. They need a rewrite system
  with stated error bounds.
- Content equality for GPU textures. A texture write always advances its
  version.
- Immutable snapshots for previews and published outputs. They keep a handle
  to storage that the step can overwrite, as the model states.
