Status: record. Explicit operation inputs, scoped functions, typed event
ports, per-instance history and renderer integration.

# Operation graph and execution

The accepted model replaces compiled copies of authored definitions as execution
nodes. `GraphOperations.h` declares `RecipeNode { displayName, kind, outputs }`.
An `OutputRef` identifies a node and a typed output port. Operation records own
their input references; `InputsOf` derives dependency edges from those records.
There is no evaluation-kind or coordinate-domain field on executable nodes.

## Compilation

`RecipeGraph.cpp` checks authored declarations and reference contracts.
`RecipeCompilation.h` contains this intermediate representation in `detail`;
it retains authored definitions for editor checks and compatibility queries,
not for signal execution. Its categories describe authoring restrictions,
not runtime scheduling.

`RecipeGraphLowering.cpp` converts the declarations into constants, explicit
external inputs, expressions, calls, resource operations and stateful
operations. Time, delta time, sample UV, actor values/state, enchantment data,
resources, transforms and event sources are graph inputs. Literal parameters
become constants; vector components are connected through vector operations.
Image scroll is an offset and has no implicit clock input.

Functions have local parameter nodes and a scoped body graph. Named and inline
curves share this representation. Existing x/mean syntax lowers to ordinary
local input references. Applied signal functions are call nodes; layer functions
map scalar calls over components. Layer source means are represented by
reduction operations. Ordinary expressions reject unbound x/mean. Authored
function syntax still has the existing scalar restrictions; this change does
not add general parameter syntax or nested authored function calls.

Outputs expose property-to-output connections for surface layers, material
scalars, lights and shell parameters. Output-specific type, slot and blend
validation remains the `Validate` / `CheckOutput` / shell-validation boundary;
these connections are not an independently validated render plan.

Dependency order, sample dependence, variability and disabled status are derived
from operation inputs. Tick execution excludes sample-dependent operations and
backend reductions and their consumers. Node-position inputs are supplied by
the geometry backend rather than interpreted as a zero-valued actor input.
Compilation bounds declaration counts, aggregate output bindings and generated
operation nodes. Invalid dependencies and functions disable consumers.

## Execution and rendering

`SignalState` evaluates lowered operations, without parameter lookups into
copied authored definitions. State slots are allocated only for oscillators,
triggers, holds, counters, accumulators, rate and smoothing operations. Values
and state belong to each execution instance. Reading an output does not advance
state. Existing public authored-name queries remain boundary adapters.

Triggers expose separate progress, active-firing and exact integer accepted-count
ports. Event consumers do not infer counts from retained firing lists. A curve
on trigger progress does not hide its event outputs. Oscillators retain
integrated phase when the period changes.

`FunctionExecution.cpp` evaluates scoped, pure function bodies with bounded
local storage and call depth. `Program::BindContext` replaces legacy special
context opcodes with reference slots in graph-owned programs. The standalone
expression API retains its previous inputs for editor/compatibility use.

The compositor consumes output references for expression values, image
scroll/tile parameters and ripple parameters/firings. Function lookup textures
are generated with the same scoped function evaluator used by signal calls.
Texture loading, baking, material decoding, reductions and GPU resource lifetime
remain implemented by the existing compositor backend. They are represented
in the graph, but this slice does not replace backend allocation or scheduling
with a new RenderPlan. Geometry/material resource context still enters through
that backend's existing preparation API.

## Verification

- The full native build passed.
- The full native run passed 110 of 112 suites. The remaining failures were a
  stale test querying functions as value nodes and the accidental inclusion of
  this checkpoint in the curated source archive inventory. Both were corrected;
  graph, signal and archive suites then passed focused reruns. All 112 suites
  are clear across those runs; the signal suite covers 158 checks.
- The Windows release DLL built and linked after correcting an explicit
  string-view conversion at the graph-name/texture-preparation boundary.
- Layer checking, formatting of all 18 implementation/test files checked, and
  `git diff --check` passed.

Build/test logs: `/tmp/beef-operation-graph-native-build.log`,
`/tmp/beef-operation-graph-tests.log`, `/tmp/beef-operation-graph-retests.log`,
and `/tmp/beef-operation-graph-windows-final.log`.
No staging, installation or in-game acceptance is included in this change.
