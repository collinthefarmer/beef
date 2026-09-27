Status: record. Structural texture identities, geometry-wide collection,
ordered acquisition, and verification of shared-resource ownership.

# Texture demand collection and acquisition

The resource-demand slice now collects active surface-output requests for one
geometry across recipe instances before preparing its stacks. The agreed names
are implemented in `planners/ValueIdentity.h` and `planners/TextureDemand.h`.
The collection is a temporary `std::vector<TextureDemand>`.

## Engineering changes

`ValueIdentity.cpp` builds bounded structural identities from operation kinds,
parameters, typed result ports, ordered dependencies and scoped function bodies.
Display names, recipe names and unrelated node indices do not affect equality.
Keys compare the complete encoding rather than trusting a hash. External inputs
come from the render adapter; state owners include graph instance, application
context and operation identity. Coordinate and sampling operations remain in the
graph.

`TextureDemand.cpp` gathers existing source/mask boundaries and compiles mask
interpreter programs before acquisition. Exact value and texture requirements
coalesce. Dependencies precede their users, and every demand retains its affected
uses. A failed root request rolls back its partial additions. Traversal, identity
size and collection size have explicit limits.

`ManagerApply.cpp` gathers eligible contributions and effective texture sizes
first. `CompositorDemand.cpp` collects their demands and then acquires them in
dependency order. `CompositorSource.cpp` binds prepared results; mask preparation
no longer recursively discovers or compiles texture dependencies. Existing source
operations remain atomic acquisition units, including their internal position
bakes, derived maps and readbacks. Splitting those internal passes into standalone
planned producers belongs to the render-plan slice.

`GeometryInputs` owns the prepared resource registry keyed by `TextureKey`.
Recipe/name/context keys remain output aliases and diagnostic locations, not
resource-sharing decisions. Inspection can find an equivalent acquired value
through structural identity. Resource retirement releases the new registry.

Static shared masks capture constant numeric inputs during preparation, and
static image coordinate bindings are resolved to sampling parameters. This
prevents a shared result from reading graph-local handles through another
recipe's SignalState. Dynamic inputs retain execution-instance identity.

`CompositorBake.cpp` keys distance bakes by the resolved origin rather than its
node name. The origin also participates in graph value identity. Existing mesh
and immutable cross-actor backend caches remain underneath the geometry registry.

## Model

```text
TextureKey
  value: ValueIdentity
  requirements: TextureRequirements(size, format, mipPolicy)

TextureDemand
  key: TextureKey
  value: TextureValue(graph, output, instance)
  dependencies: TextureDemandId[]
  uses: TextureUse(placement, output, layer, input)[]
  program: optional validated InterpreterProgram
```

The optional program retains the result of interpreter validation for acquisition;
it is not a second representation of graph identity. The initial format/mip
requirements describe the existing RGBA8 target policy. Borrowed image/material
views retain their existing native resources; requests do not force them into
new targets. Different requested sizes remain distinct even for borrowed views.

External image identities use the compositor's normalized image-cache keys;
loaded images remain owned by that cache. Material identities include retained
texture handles and generations. Geometry sharing is conservatively scoped to
its retained geometry handle. No new cross-actor dynamic sharing is introduced.

## Verification

The native build and all 114 native test suites passed. The final Windows release
build linked the DLL successfully. Layer, formatting and whitespace checks passed.
Tests cover renamed equivalent
recipes/functions, dependency changes, state and clock isolation, ordered shared
dependencies, resolution separation, invalid outputs, backend limits and resolved
distance origins.

In-game acceptance remains required: compare static and animated masks across
multiple outputs, exercise ripple and distance sources, change resolution, and
retire/reapply a geometry. Nothing has been staged or installed.
