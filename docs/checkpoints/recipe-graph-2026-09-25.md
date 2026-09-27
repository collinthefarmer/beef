Status: implemented locally; native tests and Windows build passed. This is the
first slice of the [pipeline stories](../plans/recipe-program-pipeline-plan.md).
It replaces the signal-only compiler rather than retaining it as an adapter.

# Unified recipe graph foundation

## Compilation and execution

`RecipeGraph::Compile(recipe)` registers signals, sources, masks, and named
functions in one namespace. It parses each graph program once, resolves
references and calls to node indices, checks domains and types, orders work,
and propagates animation and inertness. Cycles identify each participant and
the cycle path. Invalid rows disable their dependents while unrelated rows
remain usable. Traversal uses an explicit stack with the existing depth bound;
total named and anonymous nodes are capped at four times `kMaxRecipeRows`.

`SignalState` executes the graph's signal order, retaining its event state and
existing evaluation behavior. Construction binds parameter references and
function pointers and allocates expression scratch. The graph owns its rows
and programs, may be copied before state construction, and must outlive its
state without moving. `SignalGraph` is removed, including engine/editor/test
call sites. CMake already discovers recipe source files and test suites, so no
new source-list registration is necessary.

Named curves are function nodes. Inline signal and layer curves become
anonymous function nodes. Both use the same expression program representation
and bind through handles; there is no separate compiled applied-curve payload.
Each node holds an optional `BoundExpression` grouping its `Program` with
ordered `valueBindings` and `functionBindings`. These map program operand
slots to graph-local node indices; the separate dependency list records graph
edges for ordering and validation.
The authored syntax remains unchanged. Current functions accept scalar `x`
and `mean`, return a scalar, and cannot read rows or call functions. Unbound
`time` and non-scalar results now produce errors instead of silent zero or
scalar conversion. General parameters and reductions remain later stories.

## Graph vocabulary

`RecipeNode.definition` owns the authored signal, source, mask, or function
specification. `diagnosticLocation` identifies its location in recipe errors;
`valueType`, `evaluationDomain`, and `coordinateDomain` describe its value and
evaluation context. `resultTransform` identifies a function applied to the
computed result. `mayChangeOverTime` conservatively marks values that can change
between ticks, including environment and event inputs; it does not promise a
visible animation. `isDisabled` prevents execution after compilation failures
or invalid dependencies.

`FindNodeIndex` and `FindSignalIndex` return graph-local indices; `SignalAt`
returns a signal definition. `DependencyOrder` covers the entire graph and
`SignalEvaluationOrder` selects signal execution. `ResultTransformFor` looks up
an applied function by diagnostic location, including layer locations.

## Consumers

Validation and editor candidate checks use the graph. A changed candidate is
compiled in a modified recipe; consulting the original graph would otherwise
miss invalid edits. Source input checks have a compiler-safe entry point that
does not recursively compile candidates.

The renderer receives the instance graph, including variant overrides. It
uses compiled mask programs, dependency handles, types, and animation flags.
The single-material-channel shortcut inspects compiled operations. Layer
functions use compiled bindings by output/layer location. Constant-dependent
masks no longer become animated merely because they reference a signal.

Prepared masks and ripples are scoped by application context within geometry
caches, including inspection. Variant-overridden signals conservatively disable
global sharing keyed by the original recipe. These are correctness guards;
content-addressed demand, allocation planning, packing, and aliasing remain
future work.

## Verification

- `cmake --build --preset native` passed.
- `ctest --preset native` passed all 112 suites after grouping programs and
  bindings into `BoundExpression` (84.18 seconds).
- `cmake --build --preset windows-release` passed and linked the plugin DLL.
- `tools/layers.sh`, formatting checks on the changed implementation, and
  `git diff --check` passed.

The initial full test run exposed an invalid bare-name mask in an existing
sharing fixture, a vector argument to a scalar function in the new graph
fixture, and one source prose comment. The fixtures and comment were corrected;
the sharing suite now also asserts that invalid masks cannot share targets.

Native coverage includes mixed row dependencies, ordering, cycles, duplicate
names, missing references, domain/type errors, anonymous and named functions,
compilation bounds, copied-graph execution, event behavior, editor candidates,
variant sharing, and application cache isolation.

Rendered acceptance remains an in-game checkpoint: compare the demo recipes,
constant-dependent masks, variant placements, and two independent instances.
No installation or staging is part of this slice.
