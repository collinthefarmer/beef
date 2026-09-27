Status: in progress. The implementation stories for the
[recipe program pipeline](recipe-program-pipeline-2026-09-25.md); each "Current"
line below links the latest checkpoint for its slice.

Current resource-demand slice: [Texture demand collection and acquisition](../checkpoints/texture-demand-2026-09-27.md). Geometry-wide source/mask requests are collected before acquisition, with structural texture keys and separate output aliases. Internal source passes remain encapsulated until render planning.

Current interpreter slice: [Explicit GPU interpreter compilation](../checkpoints/interpreter-program-2026-09-27.md). Story 3 uses an engine-free InterpreterProgram directly, without an extra ProgramPlan wrapper. Slot assignment and backend limits are validated before resource preparation; backend costs and general kernel selection remain later work.

Current implementation: [Operation graph and execution](../checkpoints/operation-graph-2026-09-26.md). Executable nodes now own operation kinds and explicit typed connections; functions are scoped and runtime state is operation-specific. Backend scheduling, storage planning and general authored function syntax remain open.

Implementation checkpoint: [Unified recipe graph foundation](../checkpoints/recipe-graph-2026-09-25.md). The first slice replaces signal compilation and execution and unifies the compiled function representation; general function parameters, reductions, and backend/storage planning remain open.

Each story below pairs an engineering objective with the files it changes. Filenames marked + are proposed additions; ~ means an existing file changes.

1. Compile recipe dependencies into one graph

Currently, signal compilation and compositor preparation resolve dependencies separately. Move resolution, cycle detection, and value-property analysis into one engine-free compilation step.

+ recipe/RecipeGraph.h/.cpp
~ recipe/Expression.h/.cpp
~ recipe/Signals.h/.cpp
~ recipe/Validation.cpp
~ recipe/Vocabulary.cpp
+ tests/recipe/recipegraph_tests.cpp

RecipeGraph owns row handles, resolved dependencies, ordering, and diagnostics.
Expression retains parsed expressions and their authored locations.
Signals keeps runtime signal/event state; its graph-building responsibilities move out.
Validation and variability queries consume the graph instead of independently traversing rows.

Data-model change:

SignalGraph + recursive source/mask analysis
    → RecipeGraph
        rows, functions, nodes, dependencies, order, diagnostics
        type, evaluation domain, variability, coordinates, context dependencies

Done when all row references resolve through this graph and constant-dependent masks are correctly classified as static.

2. Make reusable expressions obey explicit evaluation rules

With references unified, remove curve-specific evaluation shortcuts and define how values cross the tick/field boundary.

~ recipe/Recipe.h
~ recipe/RecipeRead.cpp
~ recipe/RecipeWrite.cpp
~ recipe/Expression.h/.cpp
~ recipe/RecipeGraph.h/.cpp
~ recipe/Signals.h/.cpp
~ schema/recipe.schema.json
~ tests/recipe/

Add function parameters and resolved nested calls.
Replace implicit mean context with an explicit reduction operand.
Define source decoding so expressions consume meaningful values.
Preserve diagnostic locations through function expansion.
Update schema, serialization, templates, and affected recipe fixtures together.

Data-model change:

Curve(expression, implicit x/mean)
    → Function(parameters, body)

Implicit source encoding
    → Declared decode

Implicit mean
    → Reduction(operation, operand, timing contract)

Done when CPU and GPU execution have a defined interpretation for each supported construct. Signal consumption of reductions waits until initialization, latency, and failure behavior are specified.

3. Separate executable programs from interpreter packing

The planner needs to describe computation without inheriting the layout of ProgramConstants.

+ planners/ProgramPlan.h/.cpp
+ planners/Backend.h
+ render/Interpreter.h/.cpp
~ render/ShaderConstants.h
~ render/TextureLabPass.cpp
~ render/CompositorSource.cpp
+ tests/planners/programplan_tests.cpp

ProgramPlan describes typed operations and logical bindings.
Backend declares capabilities, limits, execution site, and cost parameters.
Interpreter validates and lowers that representation into existing bytecode and GPU constants.
CompositorSource stops assembling interpreter-specific programs directly.

Data-model change:

Program → ProgramConstants
    → ProgramPlan + BackendCapabilities
        → InterpreterProgram → ProgramConstants

Done when the existing interpreter executes through this seam with equivalent results and named limit failures.

4. Gather resource demand before acquiring textures

Preparation currently discovers work while acquiring resources. Separate these operations so equivalent demands can be compared across outputs and recipes.

+ planners/ValueDemand.h/.cpp
+ planners/ValueIdentity.h/.cpp
~ render/CompositorSource.cpp
~ render/CompositorBake.cpp
~ engine/ManagerApply.cpp
~ planners/RecipeTextureCache.h
+ tests/planners/valuedemand_tests.cpp

ManagerApply gathers demands for the application before resource acquisition.
Demand records include context, resolution, coordinates, sampling, and consumers.
Identity describes computation and relevant inputs independently of row names.
Existing cache callers migrate incrementally to these identities.

Data-model change:

Recipe/name/size cache key
    → ValueIdentity(computation, dependency identities, external inputs)
    → MaterializationKey(value identity, extent, layout, sampling)

Recursive acquisition
    → DemandSet → coalesced demands → acquisition

Done when equivalent bake requests coalesce, renames preserve identity, and incompatible sampling or resolution requirements remain distinguishable.

5. Emit and execute a baseline render plan

Turn the current per-row execution strategy into an explicit plan before introducing optimization.

+ planners/RenderPlan.h/.cpp
+ render/RenderResources.h/.cpp
~ render/Compositor.h/.cpp
~ render/CompositorSource.cpp
~ render/CompositorBake.cpp
~ engine/ManagerApply.cpp
~ engine/ManagerTick.cpp
~ engine/LiveActor.h
+ tests/planners/renderplan_tests.cpp

RenderPlan records passes, dependencies, logical allocations, and output bindings.
Compositor acquires resources and executes those passes.
Source and bake modules supply inputs and perform requested work.
Application state holds the execution instance and its resources.

Data-model change:

PreparedSource / PreparedMask / RenderedMask
    mixing resolution, scheduling, and ownership
        → PlannedValue + PlannedPass + OutputBinding
        → RenderResources

Done when existing recipes run through explicit plans using today’s materialization strategy, with matching rendered output.

6. Make invalidation and failure part of execution state

Replace overlapping lifecycle booleans with explicit state and retry rules.

~ render/RenderResources.h/.cpp
~ render/Compositor.h/.cpp
~ engine/ManagerTick.cpp
~ render/TextureRef.h/.cpp
~ tests/planners/renderplan_tests.cpp

Track the input generation used to produce each result.
Recompute only invalidated values.
Record failures and the changes that permit retry.
Preserve resource ownership through publication and restoration.
Decide whether a failed layer cancels its stack or permits partial output.

Data-model change:

animated / renderedOnce / preparationFailed / renderedTick
    → status: planned | acquired | valid | stale | failed
      evaluated input generation
      diagnostic
      retry condition

Done when permanent failures stop repeating work every tick and changed inputs reliably invalidate their dependents.

7. Optimize computation using the baseline as a reference

Once execution is explicit, choose where computation happens using backend costs and limits.

~ planners/RenderPlan.h/.cpp
~ planners/ProgramPlan.h/.cpp
~ planners/Backend.h
~ render/Interpreter.h/.cpp
~ tests/planners/renderplan_tests.cpp

Select constant folding, inlining, lookup generation, or materialization.
Reuse equivalent arithmetic where evaluation semantics permit.
Record the reason and estimated cost of each choice.
Retain a valid baseline plan for accepted inputs.

Data-model change:

One materialized result per authored row
    → PlannedValue(representation, producer, consumers, invalidation)

Done when optimized and baseline plans agree, with measured pass-count and execution-cost comparisons.

8. Plan storage layout and allocation reuse

Use value requirements and lifetimes to reduce texture storage without changing numeric or sampling behavior.

~ planners/RenderPlan.h/.cpp
~ render/RenderResources.h/.cpp
~ render/RenderTargetPool.h/.cpp
~ render/ShaderConstants.h
~ render/ShaderSource.cpp
~ render/TextureLabPass.cpp
~ render/TextureLabReadback.cpp
~ tests/planners/renderplan_tests.cpp

Allocate by extent, format, and mip policy.
Pack compatible values into channels.
Reuse allocations across nonoverlapping lifetimes.
Carry channel and decode information into shader bindings.
Make readback support the selected formats.
Include published results and retained consumers in lifetime calculations.

Data-model change:

Value → dedicated mipmapped RGBA8 target
    → StorageLayout(allocation, channels, format, mip policy)
      Lifetime(first use, last use, retained ownership)

Done when storage savings are measured and precision, filtering, readback, and ownership checks pass.

9. Expose the same plan to inspection and previews

Make the execution model visible to developers and authors, and ensure previews participate in resource planning.

~ render/TexturePreviews.h/.cpp
~ studio/SourceRows.cpp
~ studio/Snapshot.h
~ menu/ContextRows.cpp
~ docs/components/
~ docs/procedural-pattern-cookbook.md
~ docs/recipe-fragments-cookbook.md

Data-model change:

Preview request → separate preparation
    → demand consumer

Row status
    → status + representation + dependencies + cost + diagnostic provenance

Done when inspection explains the actual executed plan and preview ownership is included in resource lifetimes.

Stories 1–6 establish the execution architecture; 7–8 optimize it; 9 exposes it. New pure modules and tests also require registration in cmake/Native.cmake. The effect-atlas experiment remains a separate change cluster for coordinate layout and draw integration.
