Status: record. Immutable backend programs, validated slot/stack limits and
separate runtime bindings.

# Explicit GPU interpreter compilation

The graph-to-interpreter boundary is now `InterpreterProgram::Compile(graph,
output, limits)` in `planners/InterpreterProgram.h/.cpp`. It is engine-free and
returns either an immutable validated program or an error carrying the node's
display name. No additional ProgramPlan wrapper duplicates the graph.

## Model

An `InterpreterProgram` owns backend instructions, ordered input slots,
function lookup requests, result type, texture count and maximum stack depth.
Inputs distinguish a graph value from a texture-backed graph output. Texture
inputs receive their slot during compilation. Lookup requests identify a scoped
function and its bound mean argument; the lookup coordinate supplies x.

Only enabled numeric expression outputs are currently compiled. Existing source
and mask dependencies retain the baseline texture materialization strategy;
other sample-dependent inputs require a supported producer. Requested limits
can reduce available capacity but cannot exceed the shader's hard capacities:
256 instructions, 16 inputs, 8 textures, 4 lookups and a 32-value stack.
Unsupported nodes, invalid ports, disabled inputs, unsupported function
signatures and capacity failures return named errors before GPU preparation.

`InterpreterOpcode` owns explicit shader ABI values. Compilation translates
recipe opcodes through a switch rather than relying on enum-number equality.
Legacy time/x/mean instructions cannot cross this boundary: graph lowering has
already bound them to ordinary value inputs or local function parameters.

## Runtime integration

`RenderedMask` stores the validated program. The compositor prepares textures
and lookup tables requested by it, then supplies `TextureLab::InterpreterBindings`
containing current numeric values and resource handles. TextureLab verifies
binding counts, packs backend instructions and bindings into constants, and
submits the existing fullscreen pass. Program encoding no longer lives in the
compositor, and texture preparation no longer chooses expression input slots.

Instructions carry vector width. The shader clears unused vec2 lanes after
arithmetic and uses the correct width for length, distance, dot and normalization.
Previously, a scalar broadcast after vec2 construction could introduce a third
component into a later reduction. GPU acceptance should compare, for example,
`length([1,2] + 3) / 10` against the CPU result `sqrt(41) / 10`.

Pass scheduling, texture ownership, cache identities and target formats remain
with the existing compositor. General graph-kernel compilation, inlining,
resource demand coalescing and a RenderPlan are subsequent changes.

## Verification

The native build and all 113 native test suites passed. The Windows release
build linked the DLL successfully, and the layer check passed.
Tests cover mixed value/texture/function
bindings, explicit clock inputs, shader ABI encoding, exact capacities and
failures, hard texture caps, stack demand, vector widths and invalid handles.
In-game shader execution and rendered acceptance remain outstanding. Nothing
is staged or installed by this change.
