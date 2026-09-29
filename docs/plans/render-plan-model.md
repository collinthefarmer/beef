Status: implemented and offline-verified; in-game acceptance remains pending.
See the [implementation checkpoint](../checkpoints/render-plan-2026-09-27.md).
This follows the
[texture-demand slice](../checkpoints/texture-demand-2026-09-27.md).

# Baseline render-plan model

## Recipe graph

Reductions remain ordinary graph operations. Each consumes a field and produces
a uniform value that can supply an ordinary function argument. The initial
supported reductions are mean, sum, minimum and maximum.

```text
ReductionOperation
  kind: mean | sum | minimum | maximum
  value: OutputRef

MapFunctionOperation
  function: FunctionId
  arguments: OutputRef[] in function-parameter order

Example: shape(x, center)
  MapFunctionOperation(shape, [mask, mean(mask)])

mask ──────────────────────> mapped function ──> result
  └──> mean(mask) ───────────────────↑
```

All function arguments are ordinary graph connections. `mean` is a computation
supplying an argument, not a privileged field on the call. Parameter names and
types belong to FunctionDefinition; the call supplies a complete ordered argument
list. The graph validates arity and types.

MapFunctionOperation applies a pure function over a sampling domain: field
arguments are sampled at the current location, and uniform arguments supply
their values at each location. Those properties come from the connected graph
outputs, rather than a second per-argument kind flag. Ordinary CallOperation
continues to evaluate a function once for its supplied values. Supporting an
argument in the graph does not imply every backend can lower that mapping.

A changing mask makes its mean and mapped result dependent on that change.
There is no animated-mean kind, hidden context value, or implicit capture.
A render backend delivers the mean as a **delayed measurement**: the most
recent completed measurement of the field. The section "Reduction lowering and
timing" states the delay and its initialization contract.

## Demand naming

The agreed demand records use explicit identity and dependent names:

```text
TextureKey
  identity: ValueIdentity
  requirements: TextureRequirements

TextureDemand
  key: TextureKey
  value: TextureValue(graph, output, instance)
  dependencies: TextureDemandId[]
  dependents: TextureUse[]
  program: optional InterpreterProgram
```

ValueIdentity identifies the computation with its bound inputs and state owners;
it is not a texture handle or a digest of current pixels. TextureKey adds the
requested representation. The field identity distinguishes that identity
from TextureDemand.value, which locates a graph output.

Dependents are authored consumer locations represented by TextureUse, including
consumers reached transitively. They are not a reverse list of TextureDemandId
edges. Dependencies still identify prerequisite demands. These are agreed
renames; the current implementation still uses TextureKey.value and
TextureDemand.uses.

## Reduction scope

The agreed scope is a fixed set of reductions: mean, sum, minimum and maximum.
Each kind has defined input/output types and numerical behavior. There is no
user-supplied accumulator, combining function or traversal order.

Mapping followed by reduction expresses coverage (mean of a thresholded field),
peak intensity (maximum), moments (mean of a powered field), and weighted centers
(sum of weighted positions divided by sum of weights). A reduction result is
uniform over the sampling domain and can feed other ordinary graph operations.

The following sampling, type and backend contracts are settled for this slice.
They are implementation requirements, not claims about current runtime behavior.

### sampling contract

A reduction operates on the finite grid of its field's render evaluation context:
texel-center samples over the full unit UV rectangle at the requested field
extent. This is a discrete texture-space statistic, not a surface-area integral.
The planner records that domain explicitly on ReduceField. A native source image
is sampled into this domain using its graph-declared coordinates, filtering,
mip selection and decoding; its native image dimensions do not silently choose
a different reduction grid. An already materialized matching field can be read
directly at its base level.

The mean of an image source is defined at zero scroll. Its measured field
samples the image with the source's tile, mirror, transpose and mip, and
without its scroll. A scroll change therefore does not change the measurement
or rerun the reduction, and a tile change does. This definition was decided
in the [render graph correctness plan](render-graph-correctness-2026-09-29.md).

Every grid sample participates. Alpha is an ordinary channel, not an implicit
coverage mask. Mesh padding, cleared regions and dilation affect the input field
and therefore its reduction. Covered-surface statistics would require an explicit
coverage/weight input or a separate domain contract. They are not inferred from
resource type. Texture requirements and reduction domains remain distinct: the
former describes storage, while the latter describes the sampled population.

```text
ReductionDomain
  extent: width, height
  coordinates: unit UV rectangle sampled at texel centers

ReduceField
  kind: mean | sum | minimum | maximum
  value: RenderValueRef
  domain: ReductionDomain
```

Sampling and decoding belong to the field producer/view, so they are not copied
into a second editable description on ReduceField. A planner can reuse an exact
matching field or declare a sampling step to realize the domain. It cannot
substitute the old fixed 64x64 approximation without an explicit approximation
contract. Resolution is part of the reduction's evaluation/cache identity.
The same graph reduction requested at different extents can have different
results, and must not be stored as one global recipe SignalState value.

Sum is the unnormalized total over these samples. A constant field c on N
samples has sum N*c and mean c. Minimum and maximum consider all samples;
averaged mip levels are not valid substitutes for extrema.

### types and failure behavior

All four reductions accept scalar, vec2 and vec3 fields and operate component by
component, preserving the input numeric type. There is no implicit luminance
conversion or vector-magnitude reduction. Those are explicit graph computations
before reduction. Spatially reducing a vector and reducing its components are
different operations.

The mathematical empty-population behavior is zero for sum and a
reported unavailable result for mean/minimum/maximum. Current texture domains
must have positive width and height, so a zero-sized domain is rejected before
execution. A missing texture, failed draw/readback, non-finite sample, or
non-finite final numeric result is a failure, never an empty population and never
a default mean of 0.5. Failure blocks the current dependent branch.

### numeric baseline

The reference samples are decoded float values. Evaluate sum and mean with a
deterministic row-major double-precision CPU accumulator and round the final
result once to the graph's float type; minimum and maximum compare the sampled
float values directly. Canonicalize signed zero. Baseline reference tests compare
the resulting float values exactly for identical sampled inputs. GPU sampling
and source quantization are separate parts of the producer contract, not hidden
reduction error tolerances.

A future parallel or approximate reducer needs a separately reviewed numerical
error contract before replacing this baseline; arbitrary reassociation is not
assumed equivalent. The baseline is a correctness reference, not a claim that
full-resolution synchronous readback is cheap for animated fields.

### backend support

- Support scalar, vec2 and vec3 reductions over supported finite render fields.
  Use existing decoded field data where valid. A newly evaluated field must not
  be clamped into RGBA8 solely to make measurement possible: introduce typed
  float measurement/readback storage where required. Existing published texture
  formats and their already-incurred quantization remain unchanged.
- A one-dimensional lookup supports one sampled scalar parameter, a scalar
  result, and uniform bound arguments whose types the function evaluator supports.
  Retain the current 256-sample [0,1] lookup and its existing clamp/interpolation
  behavior; this is an explicit backend approximation contract.
- Preserve existing component-wise curve application to vector sources by
  lowering it to scalar component mappings and vector reconstruction. Do not
  silently accept a vector argument where a function declares a scalar parameter.
- Multiple independent sampled arguments, unsupported lookup result types,
  unresolved domains, or work exceeding backend limits produce named diagnostics.
  Do not silently freeze arguments, lower resolution, or substitute old values.
- Reduction results feed render operations and their bound function arguments.
  Feeding them into tick-time SignalState execution remains unsupported until
  that CPU/GPU scheduling boundary is explicitly designed.

The existing MeanLuminance/MeanChannel helpers do not satisfy this contract: they
use a fixed 64x64 RGBA8 intermediate, a final-mip estimate, pointer-based caching,
and a 0.5 failure fallback. They cannot simply be renamed ReduceField. Migration
must distinguish compatibility helpers from graph reductions. Existing authored
scalar mean contexts for RGB sources must lower their intended scalar projection
explicitly, preserving the legacy coefficients in that compatibility path rather
than applying implicit luminance to every vector reduction.

Arbitrary FoldFunctionOperation, custom accumulators, histograms and argmax are
deferred extensions. Their capabilities are not implied by the four supported
reduction kinds. Mean expressions below use ReductionOperation(kind = mean).

## Records and references

The notation below describes the model, not final C++ declarations.

```text
RenderInput
  kind: typed input declaration owning its binding and value type
        (texture, mesh, material, graph output, transform, visibility or stack base)

RenderInputRef
  input: RenderInputId

StepOutputRef
  step: RenderStepId
  output: output index

RenderValueRef
  RenderInputRef | StepOutputRef

RenderStep
  kind: typed operation with its operands and result requirements

TextureUseBinding
  use: TextureUse
  texture: RenderValueRef
  sampling: resolved sampling parameters or bound graph inputs

StackOutputBinding
  placement: PlacementId
  output: output index
  result: StepOutputRef returning StackResult

RenderPlan
  inputs: RenderInput[]
  steps: RenderStep[]
  textureUses: TextureUseBinding[]
  stackOutputs: StackOutputBinding[]

RenderInstance
  plan: owned immutable RenderPlan
  graphs: retained compiled graphs referenced by the plan
  inputs: RenderInputState[] indexed by RenderInputId
  stepStates: StepExecutionState[] indexed by RenderStepId

RenderInputState
  value: typed current input, retaining external resources when applicable
  changeVersion

StepExecutionState
  inputs: StepInput[]
  outputs: StepOutput[] indexed by output index
  scratch: operation-specific temporary allocations
  diagnostic: optional failure diagnostic

StepInput
  input: RenderValueRef
  changeVersion: version observed during the last successful execution

StepOutput
  value: optional typed runtime result; absent means unavailable
  changeVersion: local change counter for this output

StackResult
  texture: optional retained texture handle
```

`RenderStep` is preferable to RenderPass because mesh preparation, material
analysis, measurements and lookup generation include CPU work. GPU draws are
executed by specific operations. It is not one generic pass with optional
fields for every backend task.

Input and result references inhabit different index spaces. A result identifies
the output of a step; it is not a physical allocation handle. Operations determine
the number and types of their results. Every executable step declares at least
one result, so result availability can distinguish an unexecuted step from a
cached success, even for a zero-input constant producer. The planner derives dependencies from
operands and validates types, producer availability and a bounded acyclic order.
There is no second hand-maintained dependency array on each step.

A graph-output binding includes the recipe execution instance and OutputRef.
It can supply numeric values or typed resources such as firing data; it is not
limited to scalar values.
External bindings identify sources; the live instance retains their actual
resources. No RE or Direct3D handles enter the engine-free plan. The instance
retains referenced compiled graphs, while engine-owned SignalStates supply
current numeric values during execution.

TextureUseBinding connects an authored use to its planned resource and sampling.
StackOutputBinding connects a completed stack to its placement/output destination.
The stack reference addresses a StackResult, whose texture may be absent when
all layers are hidden. This is a successful result, distinct from an unavailable
StepOutput.value after failure or before execution.

These are different consumers, and neither should reuse the recipe graph's
OutputBinding type. Material-slot publication and restoration remain engine
responsibilities.

## Operation families

The initial operation variants cover current work:

| Operation | Inputs and result |
|---|---|
| BuildBakeBuffers | Mesh plus bake parameters or resolved origin -> CPU bake buffers. |
| BakeMesh | Bake buffers plus texture requirements -> baked texture. |
| NormalSlope | Normal texture -> derived texture. |
| SampleMaterial | Material textures -> CPU material sample. |
| ClusterMaterial | Material sample and settings -> CPU cluster analysis. |
| DrawClusters | Material textures and analysis -> cluster texture. |
| ReduceField | Reduction kind, current field texture, sampling domain and decoding -> uniform numeric result. |
| BuildLookup | Scoped function, sampled parameter, bound arguments and lookup specification -> lookup texture. |
| EvaluateProgram | InterpreterProgram, value bindings, textures and lookups -> texture. |
| DrawRipple | Position texture, firing data and graph parameters -> texture. |
| CompositeStack | Base texture, ordered layers and visibility -> StackResult. |

Concrete variant names will be chosen to avoid collisions with existing API
names. Each variant owns typed operands and settings; there is no generic
`parameters` bag. RenderValueRef is notation for a reference; validation must
reject incompatible result types, such as CPU material samples used as textures.

CPU results have explicit lifetimes in StepOutput, just like textures.
Borrowed images/material channels are imported views rather than copy steps.
Image normalization is an explicit measurement dependency. Neutral height is
an explicit retained input or fill result. Ripple position baking is a shared
producer, not work discovered inside ripple acquisition.

Existing TextureLab operations can still encapsulate their fixed backend draw
sequences. In particular, BakeMesh retains its rasterization, dilation and mip
policy. CompositeStack retains the existing two-target alternating strategy.
This slice exposes their operands, work and ownership; it does not expand each
GPU API call into a graph node or allocate a texture for every layer.

## Scheduling and visibility

The plan is immutable. Inputs and operations determine which work must run;
there is no new constant/dynamic evaluation-kind axis on graph nodes.

Cache validity is the rule: reuse a successful result while the values it
actually depended on remain unchanged. Change versions implement this rule;
they are not edit histories, frame counters, or execution counts.

A requested step first ensures its required operands are current. Its cache
records each observation as `{input: RenderValueRef, changeVersion}`. Each
reference identifies either a RenderInputState or a StepOutput; its version
is compared only with that same value's current version. There is no positional
version array that must stay aligned with another input list. Repeated references
to the same value need only one observation.

The operation still owns its connections. Observations are runtime evidence for
cache validity, not an independently editable dependency list. They record the
inputs read by a successful execution, including control inputs such as visibility
and the selected stack base. Control inputs are made current first. A control change forces the step to
reconsider its required branch before refreshing data inputs or reusing a cached
result; a now-hidden failed branch must not block the visible branch. Hidden branches do not need
observations for values they did not read.

A step can reuse its result only when it has a valid result and every observed
input is current and has the same change version. Otherwise it recomputes and
replaces its observations only after success. An empty observation list alone
does not establish a valid cache entry. The frame's graph inputs are fixed while
a requested branch is evaluated; a once-per-frame shortcut must not conceal
a changed base or another changed operand.

Version comparisons are scoped to one immutable plan instance and one referenced
input/output. Counters never restart or wrap while those references remain live;
rebuilding a plan creates a new instance with empty execution caches. An imported
resource's owner must report content or binding changes, including replacement
of a texture behind an otherwise unchanged handle. Pointer equality alone cannot
establish cache validity. Returning from an unavailable result to an available
one must not accidentally reuse an old version for different contents.

Numeric outputs advance their change version when their value changes. A
successful texture write conservatively advances its change version without a
GPU equality test. An updated mask therefore needs a new mean measurement, but
an unchanged measured scalar preserves its change version and allows the cached
lookup to remain usable. Mapping still reruns because its texture input changed.

Failure to produce a current input blocks its consumer. A delayed
measurement is an imported input with its own version, so a consumer observes
exactly which measurement it used. Failed or incomplete writes
cannot be advertised as valid results. Change versions track cache validity,
not immutable historical snapshots of reused GPU allocations.

The baseline retains caching, with per-input observations to validate reuse.
The later execution-state story still owns retry/backoff policy, richer failure
inspection and broader lifecycle consolidation.

Stack chains require explicit fallback semantics: today a failed contribution is
skipped and the next contribution receives the last successful base. Therefore a
chain cannot be lowered as unconditional links between successful texture
results. Initially ManagerTick retains that chain selection and supplies each
CompositeStack base input. Inside a stack, failure of a visible layer prevents
publication of a partial stack, matching current behavior. An all-hidden stack
has no texture result, as today.

This gives an incremental boundary: one geometry owns one RenderInstance for
shared prerequisites and stack operations; engine chain selection requests its
outputs with the current base and visibility. A later explicit chain operation
can absorb that policy without changing texture-producing step types.

## Reduction lowering and timing

The graph connections lower to explicit operands:

```text
s0 = EvaluateProgram(mask program, current inputs)
s1 = ReduceField(kind = mean, texture = s0.texture, reduction specification)
s2 = BuildLookup(shape, sampledParameter = 0,
                 boundArguments = [{parameter: 1, value: s1.scalar}])
s3 = EvaluateProgram(mapping program,
                     texture = s0.texture, lookup = s2.lookup)
```

At the backend boundary, BuildLookup identifies the one parameter varied over
the lookup domain. Each other parameter has an ordinary RenderValueRef binding.
For shape(x, center), x is the sampled parameter and center is supplied by the
mean result. No backend field is named mean.

```text
BuildLookup
  function: scoped function reference
  sampledParameter: parameter index
  boundArguments: {parameter: parameter index, value: RenderValueRef}[]
  specification: lookup domain, sample count and format
```

Every parameter must be covered exactly once by the sampled parameter or a bound
argument. The baseline one-dimensional lookup requires one supported scalar
sampled parameter and bound arguments that are uniform over that lookup. A map
with multiple independent field arguments needs a different lowering or a named
unsupported-backend diagnostic; it must not silently freeze an argument.

Literal arguments remain graph constants and lower through ordinary value
bindings. A function that takes no mean argument needs no measurement merely
because the backend uses a lookup. Existing implicit x/mean authoring is lowered
to explicit argument connections without introducing new recipe syntax here.

A reduction lowers to two parts. A `SubmitReductionStep` consumes the field
and queues its GPU reduction and copy. Its result is a constant
acknowledgement, so its change version never advances. A measurement input
(`ReadbackBinding`) names that submission. The executor evaluates the
submission whenever a consumer reads the input, which keeps the submission
current with its field. The backend imports each completed readback into the
input, and the input's version advances only when the measured value changes.

The measurement lags its field by the readback latency, typically one to three
frames. A consumer recomputes when a new measurement arrives, not when its
field is submitted. A failed submission blocks the consumers of its
measurement. A failed readback removes the measurement, which also blocks them.

Material sampling uses the same shape. A `SubmitMaterialSampleStep` copies the
RMAOS and diffuse maps into staging textures, and a readback input of type
material sample receives the decoded sample. Cluster analysis reads that
input. A sample equal to the previous one does not advance its version.

Before the first readback arrives, the input has no value and its consumers
are unavailable. A stack that fails while a measurement of its geometry is
unset and has a submission in flight is **pending**, not failed: it publishes
nothing, reports no diagnostic, and the slot keeps its base. An application
stays prepared, not rendered, while any of its outputs is pending. The backend
collects measurements without waiting, once per frame. This was decided in
the [render graph correctness plan](render-graph-correctness-2026-09-29.md),
decision D2.

Current BakeCurve can measure a prepared mask texture before its first render.
The new ordering fixes that invalid read and replaces the implicit one-time
measurement with dependency-driven updates. Rendered equivalence cannot be
claimed for those previously undefined or stale measurements. Signal feedback
through reductions remains out of scope: ordinary cycles are rejected, and a
future feedback path would require an explicit delay contract.

## Runtime ownership

There is no RenderResources wrapper. Ownership follows the input or step that
needs the object:

- RenderInstance retains its plan and referenced compiled graphs.
- RenderInputState retains imported textures, mesh/material inputs and current
  value snapshots. Engine-owned SignalStates remain with their recipe instances;
  execution reads a consistent snapshot rather than taking ownership of them.
- StepOutput owns CPU products or retains GPU results through resource
  handles. StepOutputRef resolves through stepStates[step].outputs[output].
- StepExecutionState owns only operation-specific scratch that is not a produced
  result, such as temporary readback storage or an intermediate stack target.
- Published outputs and previews retain their own resource handles. Retiring the
  render instance releases its references without invalidating those retained
  handles. Change versions are not an immutable-history guarantee.

Allocation pools still own their reusable storage; runtime handles retain or
lease it. A produced texture is not separately owned by a parallel allocation
registry. This makes CPU results, imported resources and temporary GPU storage
explicit without one broadly named resources object.

## Ownership changes and engineering stories

```text
~ recipe/GraphOperations.h/.cpp
~ recipe/RecipeGraphLowering.cpp
~ recipe/RecipeGraphAccess.cpp
~ recipe/FunctionExecution.cpp
~ recipe/Signals.cpp
~ planners/ValueIdentity.cpp
~ planners/TextureDemand.h/.cpp
~ planners/InterpreterProgram.h/.cpp
~ tests/recipe/
~ tests/planners/
    Replace MeanOperation with the fixed-kind ReductionOperation.
    Replace the map's special value/mean fields with ordered arguments.
    Update edge traversal, validation, evaluation, structural identity and
    backend lowering together. Validate backend limits separately from graph
    function arity and types.

+ planners/RenderPlan.h/.cpp
+ tests/planners/renderplan_tests.cpp
    Expand collected demands into typed operations; validate references,
    shared producers, current-result reductions, hidden branches and output bindings.
    Lower graph reductions and function arguments to explicit step operands.

+ render/RenderInstance.h/.cpp
    Own retained inputs, graphs and per-step runtime state. Keep produced
    resources with their output states and temporary allocations with their step.
    Track output changes and input observations; release ownership on retirement.

~ render/CompositorDemand.cpp
~ render/CompositorSource.cpp
~ render/CompositorBake.cpp
~ render/TextureLabReadback.cpp
~ render/TextureLab.h
~ render/Compositor.h/.cpp
    Move prerequisite discovery into planning. Execute declared operations.
    Split scheduling/ownership out of PreparedSource, RenderedMask and
    RenderedRipple. Add typed measurement/readback support where required;
    preserve published texture formats and existing stack allocation policy.

~ engine/LiveActor.h/.cpp
~ engine/ManagerApply.cpp
~ engine/ManagerTick.cpp
    Give LiveGeometry the render instance. PlacedOutput keeps an output
    handle and status instead of owning its own recursive RenderedStack.
    Retain existing chain fallback and material publication behavior.

~ engine/ManagerSnapshot.cpp
~ render/TexturePreviews.cpp
    Resolve inspection handles through the instance's results; retain
    resources while a preview or published output still uses them.
```

RenderPlan belongs here because its connected tables and references define one
validated executable object. A temporary list of texture demands still needs no
collection wrapper. RenderInstance owns the live inputs and step states for one
execution of that plan.

Acceptance cases cover all four reduction kinds under their agreed sampling and
numerical contract. They also include a position bake shared by two ripples, one lookup used
by multiple expressions, successful empty stack results versus unavailable outputs,
input replacement and version isolation between plan instances, a mean measurement ordered after a valid producer,
a changing mask with a changing mean, a changed mask with an unchanged mean
(reuse its lookup but rerun mapping), failed measurements blocking consumers,
independent dynamic recipe bindings, hidden invalid layers, failed stack-chain
contributions, and retained output/preview handles during geometry retirement.
Optimization, channel packing and new allocation reuse remain out of scope.

## Consistency review

The StepInput/StepOutput renames preserve the distinction between an immutable
connection in RenderStep and the last successful observation in StepInput.
They do not turn the runtime input records into a second editable graph.

This review also makes three contracts explicit: successful empty stacks carry a
StackResult rather than looking like failed output production; control inputs
select branches before their data dependencies are refreshed; and imported
resource owners participate in change tracking. These contracts are implemented by the plan executor; GPU acceptance is tracked
in the implementation checkpoint.

The texture-demand collector remains the upstream request/coalescing stage. It
now traverses mapped function arguments and reduction prerequisites and exposes
internal fields to render planning. Interpreter lookup requests carry explicit
bound graph arguments. Lowering details include uniform value evaluation,
typed unavailable producers for failed branches, and graph-output aliases for
inspection; these add no new evaluation-kind axis to recipe nodes.

## Review completion and implementation order

The graph operations, demand names, render references, ownership model, cache
validity, reduction semantics and initial backend support are settled. Further
model approval is not needed for routine implementation choices within these
contracts. A change to numerical meaning, sampling domain or timing would be a
model change rather than an invisible optimization.

Implement in this order:

1. Graph operations and pure reduction reference tests: fixed reduction kinds,
   ordered function arguments, type checks and numerical/failure behavior.
2. Render-plan lowering: explicit reduction domains, source prerequisites,
   bound lookup arguments, typed references and output bindings.
3. Runtime inputs/results and cache validity: StepInput/StepOutput ownership,
   change versions, branch selection and failure propagation.
4. Backend execution and integration: float measurement/readback where needed,
   current-result ordering, compositor/manager migration and retained outputs.
5. Native and Windows verification, followed by in-game acceptance.

Arbitrary folds, implicit approximation, delayed reductions, tick-time reduction
feedback, richer retry policies and storage optimization remain outside this
slice. In-game acceptance of the already implemented interpreter and texture-demand
changes remains outstanding independently of this completed model review.
