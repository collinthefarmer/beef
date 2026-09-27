Status: implemented and offline-verified. In-game
GPU and performance acceptance remains pending. Implements the
[agreed model](../plans/render-plan-model.md) after the
[texture-demand slice](texture-demand-2026-09-27.md).

# Render-plan execution — 2026-09-27

## Engineer stories and file clusters

1. **Make measurements ordinary dependencies.** `recipe/Reduction.*`,
   `GraphOperations.*`, `RecipeGraphLowering.cpp`, `FunctionExecution.cpp` and
   `Signals.cpp` add component-wise reductions and ordered function arguments.
   Expression lookups carry explicit bound graph inputs. Scalar component
   mappings preserve vector curves, and tint still precedes a layer curve.
   RGB image normalization now has explicit projection/reduction/expression
   nodes, retaining its compatibility gain and denominator floor.
2. **Compile the work before acquiring its results.** `planners/RenderPlan.*`,
   `RenderPlanLowering.cpp`, `TextureDemand.*`, `ValueIdentity.cpp` and
   `InterpreterProgram.*` lower typed operations and producer references.
   Demands traverse mapped arguments and reduction prerequisites. The plan
   shares bound values, position bakes, bake buffers, material samples and
   lookups; it retains graph-output aliases for inspection. Uniform masks
   materialize when a texture consumer needs them. Invalid branches become
   typed unavailable producers, so hidden branches do not reject a geometry.
3. **Cache successful observations.** `planners/RenderExecution.h` pairs each
   input reference with its last successfully observed change version. A failed
   measurement invalidates its current result and blocks consumers. An unchanged
   numeric result preserves its version, allowing its lookup to remain cached
   while a changed source remaps. Visibility selects dependencies before data
   evaluation. An empty stack result is successful and distinct from failure.
4. **Execute and own the declared work.** `render/RenderInstance.*` supplies the
   typed engine/GPU backend and retains recipe graphs, imports and step results.
   `TextureLabReadback.cpp` performs bounded float base-level readback for the
   pure reduction accumulator. The target pool and bake scratch distinguish
   format and account for float allocations. Produced targets belong to results;
   scratch holds a weak reuse hint. Stack composition retains its own target and
   the lab's shared alternate, with the final write landing in the owned target.
5. **Publish and inspect the same results.** `engine/ManagerApply.cpp`,
   `ManagerTick.cpp`, `LiveActor.*`, `ManagerSnapshot.cpp` and `render/Compositor*`
   give each geometry one render instance. `RenderOutput` is a placed output
   handle with a weak instance reference and retained latest texture. The old
   recursive mask/ripple preparation and scheduling flags are removed. Failed
   chain contributions are skipped. Inspection and published handles retain
   storage through geometry retirement.

## Implementation details

- `TextureKey.identity` and `TextureDemand.dependents` use the reviewed names.
- `EvaluateValueStep` evaluates uniform expressions/calls derived from renderer
  results. `UnavailableStep` carries a typed branch diagnostic, and
  `RenderValueBinding` preserves inspection aliases. These are lowering/runtime
  details; they add no evaluation-kind axis to recipe nodes.
- Mean and sum use deterministic row-major double accumulation. Min/max compare
  samples. Every texel participates. Invalid domains, non-finite samples/results,
  failed draws and failed measurements make results unavailable. Published
  textures remain RGBA8; measurement intermediates are RGBA32 float.
- `RenderInstance::UpdateInput` lets resource owners report rebinding or in-place
  mutation. Geometry/material rebinding rebuilds its instance. The existing
  material flat-displacement classifier remains a separate import-time policy.
- Produced-result sharing is scoped to a bound plan. Imported mesh caches and
  inspection material-analysis caches remain. Cross-actor compositor target
  caches no longer own execution results.

## Verification

A shared-DAG regression also covers bounded traversal: prerequisites refresh once
per request, including when their existing outputs are reused. Identity storage
is bounded independently of the step count.

- Complete native run: 117 suites passed.
- Final focused rerun after compatibility and traversal fixes: eight suites
  passed, including 22 cache-execution, 29 plan-lowering and 17 reduction checks.
- Windows release DLL build passed; compilation database regenerated.
- Module dependency checks and `git diff --check` passed. Changed C++ files were
  formatted with the pinned formatter.

The focused cases include a changed field with an unchanged mean, a changed mean,
failed measurement blocking consumers, vector lookup reuse, explicit demand
bindings, uniform masks, shared ripple position baking, tint-before-curve
compatibility, shared-DAG traversal and retained output leases on retirement.

## In-game acceptance

Exercise a changing mask with a mean-dependent curve; a changing field whose
mean stays constant; two ripples sharing geometry; material clusters with
separate settings; tinted curves; ID bakes and reoriented normals; and stack
hide/show with an unavailable source. Confirm a failed contribution falls back to
the preceding stack, and retained previews survive geometry retirement.

Check frame time and active target memory with representative texture sizes and
several actors. Reductions deliberately read the full requested field
synchronously; the old fixed-size mean approximation does not establish their
performance. No staging, installation or game run was performed for this slice.
