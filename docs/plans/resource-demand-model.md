Status: implemented. The boundary and validation are recorded in the
[texture-demand checkpoint](../checkpoints/texture-demand-2026-09-27.md).
This model guided the implementation and follows the
[interpreter compilation slice](../checkpoints/interpreter-program-2026-09-27.md)
and refines story 4 of the [pipeline plan](recipe-program-pipeline-plan.md).

# Resource demand model

## Records

The following is model notation, not final C++ declarations.

```text
ValueIdentity
  canonical computation and bound input identities

TextureRequirements
  size: TextureSize
  format: TextureFormat
  mipPolicy: MipPolicy

TextureKey
  identity: ValueIdentity
  requirements: TextureRequirements

TextureUse
  placement: PlacementId
  output: output index
  layer: layer index
  input: source | mask | functionArgument

TextureDemand
  key: TextureKey
  value: graph instance + OutputRef
  dependencies: TextureDemandId[]
  dependents: TextureUse[]
```

The collection is `std::vector<TextureDemand>`, with no `TextureDemands` or
`DemandSet` wrapper. ManagerApply gathers active outputs for one geometry;
collection expands dependencies and combines matching demands before the
compositor acquires textures. The vector is temporary preparation data, not a
persistent cache or per-tick execution object. Diagnostics remain attributed to
affected uses; introduce a named collection-result type only if the API needs
to return them alongside demands.

`ValueIdentity` represents equality of the computation with its bindings. It
does not own another operation graph. Canonicalization reads existing operation
types, parameters, ordered connections, result ports/types and scoped function
bodies. Expression operands use resolved connections; authored names and local
node indices do not enter structural equality. A hash may index identities but
cannot establish equality without a collision check.

The `value` field locates an existing graph output for acquisition. It is not the
cache key: two graph outputs can request the same computation. Graph instances
must remain alive through acquisition. Demand IDs are local to a collected set.

Texture requirements describe the produced texture. The initial implementation
uses existing square TextureSize, formats and mip behavior; no format selection
or storage optimization is introduced. Exact requirements must match for
coalescing. A larger requested texture does not silently satisfy a smaller one.

Coordinates, channel selection, decoding and sampling operations already belong
to the graph and participate in value identity wherever they affect the result.
They are not copied into an independent sampling description on TextureDemand.
Sampling by a downstream consumer does not change the identity of an unchanged
upstream texture. Existing direct image/material views remain views: demand
collection does not force every sampled graph value into a new render target.

## External inputs and state

The render/engine adapter supplies typed identities for resources and runtime
bindings. Only inputs reached through dependencies contribute to identity.

- Material and image inputs identify the actual resources and their revision
  or retained lifetime; a recipe identifier or filename alone is insufficient
  for mutable/replaced resources.
- Geometry inputs identify mesh data and its lifetime/revision. Transform and
  node-position inputs identify the actual bound input. A distance bake must
  distinguish different resolved origins even if the node names match.
- Time, actor values and event streams identify their runtime binding, not a
  snapshot of the current value. Dynamic results remain tied to their execution
  instance; identity is not a claim that cached pixels are current.
- Stateful operations include an execution-instance and state-owner token.
  Two independently evolving smoothers or triggers never merge merely because
  their definitions match. Repeated consumers of one state owner can share.

Pure computations can coalesce across recipes when their relevant bound inputs
match. The first acquisition batch covers all active surface outputs for one
geometry across recipe instances, matching current geometry cache ownership.
Cross-actor reuse remains restricted to safe immutable results with retained
input identity. Broader batching and dynamic cross-instance sharing are deferred.

## Collection and acquisition

1. ManagerApply resolves eligible surface contributions and effective stack
   sizes before preparing any textures for that batch. Size selection currently
   occurs inside Compositor::Prepare and must become a shared read-only step.
2. The collector walks reachable graph producers and records the existing
   materialization boundaries, including implicit prerequisites such as ripple
   position bakes and derived material maps. Walks are bounded and reject
   disabled/unsupported producers with consumer diagnostics.
3. Interpreter programs are compiled during collection so invalid programs fail
   before texture acquisition. Their backend instruction limits remain separate
   from semantic value identity.
4. Equal texture keys coalesce. The surviving demand retains every
   use and dependencies refer to the coalesced IDs.
5. The compositor acquires demands in dependency order, using existing source,
   bake and interpreter execution paths. Acquisition consumes the collected
   requirements; it does not discover additional resource demands recursively.
6. Consumer mappings attach results and failures to prepared layers. Inspection
   resolves authored graph outputs through these mappings instead of depending
   on display names in resource cache keys.

Layer functions that require a measured mean collect the measurement operand
first. Computing the mean and filling the lookup remain acquisition work; the
readback result is not needed to discover its input demand.

This establishes requests and sharing. Pass scheduling, allocation lifetimes,
invalidation generations and retry rules belong to subsequent stories. Existing
runtime rendering guards remain until that execution-state work is implemented.

## Engineering change clusters

```text
+ planners/ValueIdentity.h/.cpp
    Derive structural equality from graph operations and bound inputs.
+ planners/TextureDemand.h/.cpp
    Gather bounded dependencies, texture requirements and consumer mappings.
+ tests/planners/valueidentity_tests.cpp
+ tests/planners/texturedemand_tests.cpp
    Check rename stability, dependency changes, state isolation, exact texture
    compatibility, shared prerequisites and diagnostics before acquisition.

~ engine/ManagerApply.cpp
    Gather each geometry's active surface contributions before preparation.
~ render/Compositor.h/.cpp
    Resolve effective sizes before acquisition and bind collected results.
~ render/CompositorSource.cpp
~ render/CompositorBake.cpp
    Acquire declared prerequisites and use resolved-input identities.
~ planners/RecipeTextureCache.h
    Replace recipe/name resource keys with TextureKey; preserve
    authored-output lookup separately for inspection.
~ render/TexturePreviews.cpp
    Adapt inspection lookup to consumer/output mappings where necessary.
```

Pure modules and tests require build/source-inventory registration. Component
documentation and the pipeline plan must reflect the implemented boundary.

Acceptance examples: renamed equivalent masks share; different resolutions do
not; two recipes requesting the same geometry bake share; distance bakes with
different bound origins do not; independent state owners do not; a shared failed
prerequisite reports the problem to every affected consumer without duplicate
acquisition attempts in the batch.
