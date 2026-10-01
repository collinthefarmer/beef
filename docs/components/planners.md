# planners/

The pure decision layer between a **recipe** and the engine. It is
engine-free. It compiles natively and is unit-tested through
`ctest --preset native`. It may include only `Core.h`, `recipe/`, `mesh/` and
itself, per its `ALLOWS` row in `tools/gate.py`, `'planners': ('Core.h',
'recipe', 'mesh', 'planners')`. The engine and render modules above it
(`engine/ManagerApply.cpp`, `render/RenderInstance`, `render/TextureLab`,
`render/GeneratedShaders`) own the `RE::` and D3D handles and call these
functions with value records. No planner takes or stores an `RE::` pointer.

## What it owns

The directory owns four groups of decisions.

- **Actor planning.** `MatchActor` matches recipes against an actor's
  **geometries** and builds the **actor plan**. `PlanGeometryPlacement` and
  `PlanActorLights` merge the matches through `recipe/Merge`. `PlanStacks`
  marks each **slot** **stack** animated or static. `PlanBinding` decides
  which surface **bindings** a geometry needs and which **contributor** owns
  its **shell**.
- **Render planning.** `CollectTextureDemand` collects the textures a stack
  reads as **texture demands**. `BuildRenderPlan` lowers the demands and the
  stack requests into a **render plan** of typed **steps**, **inlines** fields
  and validates the result. `RenderExecution` runs a render plan through
  caller callbacks and re-executes a step only when an input changed.
- **Programs.** `FieldProgram` compiles a field expression into a **program**
  of `ProgramInstruction`s. `ProgramShader` and `StackShader` generate HLSL
  from programs and from a **stack shape**. `ProgramReference` is the CPU
  model of the **interpreter**, the bytecode loop `RunProgram` in
  `render/ShaderSource.cpp`. `GpuReduction` holds the pure half of GPU
  reductions and **readbacks**.
- **Resource primitives.** `TargetPool`, `ConsumptionLeases`, `TextureLeases`,
  `OwnedState`, `ResourcePool` and `RetainedCache` manage resource lifetime
  with no engine type. `EvictionFor`, `TransformStorageIndex` and the
  texture-identity helpers are pure decisions the engine layers call.

## Data

### Handles

`ActorPlan.h` declares five handle types. Each is an `enum class :
std::size_t` over its own index space. The distinct types stop a caller from
indexing one table with another table's handle. No handle wraps a pointer.

| Handle | Indexes |
|---|---|
| `GeometryId` | A row of `ActorPlan::geometries`. |
| `InstanceId` | A row of `ActorPlan::instances`. |
| `PlacementId` | A row of `ActorPlan::placements`. |
| `RecipeId` | A row of the recipe store the caller passes as `std::span<const Recipe>`. |
| `OutputId` | A row of the matched recipe's `outputs`. |

### The actor plan

`ActorPlan` holds what the planners know about one actor as three tables of
value rows. The rows refer to each other by handle. `MatchActor` builds the
plan, and the query functions in `ActorPlan.cpp` read it.

| Type | Description |
|---|---|
| `Geometry` | One mesh on the actor: its `GeometryIdentity`, the armor's `WornPiece` match keys, and the `firstPerson` and `lost` flags. |
| `Instance` | One **instance**: a `RecipeId`, an optional enchantment `FormKey`, and an optional effect `RecipeKey`. `InstanceFor` in `ActorPlanning.cpp` reuses an instance through `FindInstance` on these three values. |
| `OutputPlacement` | One recipe output on one placement: its `OutputId`, whether it is `selected`, and a `problem` text. |
| `Placement` | One **placement**. It joins an `InstanceId` to a `GeometryId` and records the matched `RecipeKey`, the `priority` and one `OutputPlacement` per output. |
| `ActorPlan` | The `geometries`, `instances` and `placements` tables. |
| `PieceMatch` | The match view for one armor **piece**: an instance index, the `RecipeKey` and the `priority`. `MatchesForPiece` builds it over a contiguous range of geometry ids. |

### Geometry and light plans

`ActorPlanning.h` declares the two merged **plan** records and the resolver
hook. Each record pairs a plan from `recipe/Merge` with the `PlacedRecipe`
rows it was merged from. Its `sources` vector aligns row for row with
`placed`, so a consumer can trace a merged contribution back to its handle.

| Type | Description |
|---|---|
| `GeometryPlacementPlan` | One geometry's `PlacedRecipe` rows, the `PlacementId` of each row, and the merged `GeometryPlan`. |
| `ActorLightPlan` | One `PlacedRecipe` row per instance, the `InstanceId` of each row, and the merged `LightPlan`. |
| `RecipeResolver` | A caller-supplied `std::function` from `(const Geometry &, GeometryId)` to `std::vector<ResolvedRecipe>`. The default resolver calls `Resolve` on the geometry's `keys`. |

`PlanGeometryPlacement` takes each row's priority from its placement and its
load order from the recipe-store index. `PlanActorLights` gives an instance
the highest priority among its placements whose geometry passes
`LightEligible` (`EligibleLightPriority` in `ActorPlanning.cpp`). An instance
with no eligible placement keeps its row but has a null recipe.
`engine/ManagerApply.cpp` calls `LightEligible` again for the light's
geometry inputs.

### Stack classification and binding

`StackPlan.h` and `BindingPlan.h` classify one geometry's merged plan. The
engine reads the result to decide what to render each tick and what to bind.
`PlanStacks` marks a link animated when its own output animates or when any
link below it does.

| Type | Description |
|---|---|
| `StackLink` | One `SlotContribution` in a chain, with `selfAnimated` (this link's output animates, from `IsAnimated`) and `animated` (this link or a link below it animates). |
| `SlotStackPlan` | One slot's chain of `StackLink`s, its `Surface` and `Slot`, and whether the stack animates. |
| `GeometryStackPlan` | One `SlotStackPlan` per `SlotPlan` in the `GeometryPlan`. `SlotStackPlanOf` finds one by surface and slot. |
| `BindingPlan` | Whether the geometry needs a `material` binding and a `shell` binding, and the `shellOwner`. `ShellTop` in `BindingPlan.cpp` selects the shell owner by priority, then by load order. |

### Texture demands

A **texture demand** is one texture a stack needs, keyed by what it computes.
`ValueIdentity.h` and `TextureDemand.h` declare the key and the demand
records. `CollectTextureDemand` appends demands for one request and removes
the new ones again when the request fails.

| Type | Description |
|---|---|
| `ValueIdentity` | A `canonical` string that `IdentifyValue` encodes from a graph value's structure, external bindings and state owner. |
| `ValueBindings` | The two callbacks `IdentifyValue` uses: `external` names an `ExternalSource`, and `state` names a node's state owner. |
| `TextureFormat` | `kRgba8` or `kRgba32Float`. A reduction operand uses `kRgba32Float`. |
| `MipPolicy` | `kGenerate` or `kNone`. |
| `TextureRequirements` | A `TextureSize`, a `TextureFormat` and a `MipPolicy`. |
| `TextureKey` | A `ValueIdentity` and the `TextureRequirements`. Equal keys are one demand. |
| `TextureUseInput` | The role of a use: `kSource`, `kMask` or `kFunctionArgument`. |
| `TextureUse` | One reader: a `PlacementId`, an output index, a layer index and a `TextureUseInput`. |
| `TextureValue` | A graph pointer, an `OutputRef` and an application-context index. |
| `TextureDemand` | The key, the value, the ids of its `dependencies`, the `dependents` uses, an optional compiled `FieldProgram` and the `GeometryId`. |
| `TextureDemandRequest` | The input of `CollectTextureDemand`: a value, its requirements, the use and the geometry. |
| `kMaxIdentityBytes` | 8 MiB, the limit on the summed identity text of all demands. `WithinCollectionLimits` also stops at 4096 demands. |

### The render plan

`RenderPlan.h` declares the **render plan**, the immutable graph of steps that
`render/RenderInstance` executes for every geometry of one actor. A value
reference points at an imported input or at a step's output. The plan's
binding tables connect consumers to results but hold no runtime values.

| Type | Description |
|---|---|
| `RenderInputRef`, `StepOutputRef`, `RenderValueRef` | A reference to an imported input, to a step's output, and the variant of the two. |
| `RenderResourceType` | The non-numeric value types: `kTexture`, `kMesh`, `kMaterial`, `kTransform`, `kFirings`, `kBakeBuffers`, `kMaterialSample`, `kMaterialAnalysis`, `kLookup`, `kVisibility`, `kStack` and `kSubmission`. |
| `RenderValueType` | A `ValueType` or a `RenderResourceType`. |
| `RenderInput` | One imported input: its type, a binding (`TextureValue`, `StackInputBinding` or `ReadbackBinding`) and its geometry. |
| `StackInputBinding` | The placement, output and geometry of a stack whose result is an input. |
| `ReadbackBinding` | The submission step whose result the input receives one to three frames later. |
| `ReductionDomain` | The width and height a reduction measures. |
| `RenderStep` | One step: a `RenderStepKind` and a `displayName`. |
| `RenderValueBinding` | A graph value, the reference that holds its result, and its geometry. `RenderPlan::values` keeps these for inspection. |
| `TextureUseBinding` | A `TextureUse` and the texture it reads. |
| `StackOutputBinding` | A placement output, its geometry, and the stack step that produces it. |
| `RenderPlan` | The `values`, `inputs`, `steps`, `textureUses` and `stackOutputs` tables. |
| `RenderStackRequest` | One stack to lower: the graph, the `SurfaceOutput`, the instance, the placement output, the requirements and the geometry. |
| `RenderBindingResolver` | A callback that returns the `ValueBindings` for a `TextureValue` on a geometry. |
| `PackedLayerFields` | A `ProgramPack` and the concatenated inputs and lookups of a stack's layer fields. |

### Render steps

`RenderStepKind` is a variant with one alternative per step. `InputsOf` lists
a step's operands, and `OutputType` gives its result type. `ValidateRenderPlan`
checks each alternative's operand types and order.

| Step | Description |
|---|---|
| `UnavailableStep` | A branch that failed to lower. It keeps its type and `problem` so that visibility can exclude it. |
| `ConstantRenderStep` | A constant `Value`. |
| `BuildBakeBuffersStep` | Builds bake buffers from a mesh for a `BakeKind` or a computed operation. |
| `BakeMeshStep` | Rasterizes bake buffers into a texture, optionally with `nearest` sampling. |
| `NormalSlopeStep` | Derives a slope texture from a material's normal map. |
| `SubmitMaterialSampleStep` | Copies a material's maps for a readback. |
| `ClusterMaterialStep` | Clusters a material sample with `ClusterSettings`. |
| `DrawClustersStep` | Draws a cluster analysis into a texture. |
| `SampleFieldStep` | Samples a texture at computed coordinates. |
| `SubmitReductionStep` | Submits a reduction (`ReductionKind`) of a value over a `ReductionDomain`. |
| `BuildLookupStep` | Samples a recipe function into a **lookup** of 256 entries, with the other arguments bound (`LookupArgument`). |
| `EvaluateValueStep` | Evaluates a uniform graph value on the CPU. |
| `EvaluateProgramStep` | Draws a `FieldProgram` with its inputs and lookups into a texture. |
| `MapFieldStep` | Maps a field through a lookup. |
| `ComposeVectorStep` | Composes two or three components into one vector field. |
| `DrawRippleStep` | Draws a ripple field from bake positions. |
| `CompositeStackStep` | Composites a stack: a base, a visibility input, `PlannedLayer`s, the requirements, the `Slot` and the stack's `LayerField`s. |

### Layer fields

A **layer field** is a field evaluated inside a stack pass instead of in its
own step. `InlineFields` moves a qualifying producer into its stacks, and the
stack then reads it through a `LayerFieldRef`. The render layer packs a
stack's fields with `PackLayerFields`.

| Type | Description |
|---|---|
| `LayerField` | The replaced value reference, its `FieldProgram`, and the program's inputs and lookups. |
| `LayerFieldRef` | An index into `CompositeStackStep::fields`. |
| `LayerRead` | A `RenderValueRef` or a `LayerFieldRef`. `ReadField` and `ReadValue` resolve one. |
| `PlannedLayer` | One layer: a source `LayerRead`, an opacity, an optional mask `LayerRead`, an optional color, a `Blend` and a `ChannelSet`. |
| `ProgramLikeStep` | The common view of `EvaluateProgramStep`, `MapFieldStep` and `ComposeVectorStep` that `AsProgramLike` returns. |
| `InlinedPlan` | The plan after `InlineFields`, the count of programs inlined into other programs, and the count of layer field reads. |

### Render execution

`RenderExecution.h` declares the engine-free executor. `render/RenderInstance`
instantiates it as `RenderExecution<RenderValue, RenderScratch>`. The executor
re-runs a step only when an input's change version differs from the version
it last observed.

| Type | Description |
|---|---|
| `ChangeVersion` | A `std::uint64_t` counter. |
| `StepInput` | One observed input reference and its change version. |
| `StepOutput<T>` | An optional result and its change version. |
| `RenderInputState<T>` | An imported input's optional value and change version. |
| `StepExecutionState<T, Scratch>` | One step's observed inputs, outputs, scratch, last diagnostic, `released` flag and the clock value of its last use. |
| `ResolvedRenderInput<T>` | A reference, its value and its change version, as a step callback receives it. |
| `RenderExecution<T, Scratch>` | Holds a validated plan and its state. `Evaluate` materializes one reference through the `ExecuteStep`, `SameValue` and `SelectInputs` callbacks. `SetInput` imports a value. `AdvanceClock` sets the time in the caller's unit, and `ReleaseIdle` drops results unused for longer than a given span of it. A walk stops at depth 64 or 65536 visits. |

### Programs

A **program** is a compiled field expression and everything that holds or
runs it. `FieldProgram.h` declares it. `FieldProgram::Compile` builds one from
a recipe graph output, and `FieldProgram::Inline` inlines a producer program
into one input of a consumer.

| Type | Description |
|---|---|
| `ProgramOpcode` | The instruction set. The values are the shader's opcode numbers. |
| `ProgramInstruction` | An opcode, a `number`, an `index` and a `components` count. |
| `ProgramValueInput`, `ProgramTextureInput`, `ProgramInput` | An input read as a uniform value, an input read from a texture slot, and the variant of the two. |
| `FunctionLookup` | A recipe function, its sampled parameter and its bound arguments. `SamplesAsLookup` decides whether a function call becomes a lookup. |
| `ProgramLimits` | The instruction, input, texture, lookup and stack limits a program must fit. |
| `InputRenumbering` | The consumer and producer input indices after `FieldProgram::Inline` merges two input lists. |
| `FieldProgram` | An immutable compiled program: instructions, inputs, lookups, texture count, stack size and result type. `Sample`, `Map` and `Compose` build the fixed programs for sampling, lookup mapping and vector composition. |
| `ProgramSegment` | The first instruction and the instruction count of one program inside a pack. |
| `ProgramPack` | Several programs concatenated by `PackPrograms`, with input and lookup indices offset and one `ProgramSegment` per program. |
| `ProgramCode` | A view of one program's instructions, inputs and result type. `CodeOf` and `SegmentCode` return one. |

`ProgramOpcode` groups its enumerators as follows.

| Enumerators | Description |
|---|---|
| `kNumber`, `kInput`, `kLookup` | Push a constant, an input, or a lookup sample. |
| `kMakeVec2`, `kMakeVec3` | Build a vector from stack values. |
| `kNeg`, `kNot`, `kAbs`, `kSaturate`, `kFloor`, `kCeil`, `kFrac`, `kSqrt`, `kSin`, `kCos`, `kNormalize` | Unary operations. |
| `kAdd`, `kSub`, `kMul`, `kDiv`, `kMin`, `kMax`, `kPow`, `kStep` | Binary arithmetic. |
| `kLt`, `kGt`, `kLe`, `kGe`, `kEq`, `kNe`, `kAnd`, `kOr` | Comparisons and logic. |
| `kIf`, `kClamp`, `kSmoothstep`, `kLerp` | Three-operand operations. |
| `kLength`, `kDistance`, `kDot`, `kCross` | Vector operations. |
| `kQuantize`, `kSplat` | Round to RGBA8, and copy a scalar into three components. `InlinedProducerCode` appends them to an inlined producer. |

| Constant | Value | Purpose |
|---|---|---|
| `kProgramInstructions` | 256 | Instructions per program or pack. |
| `kProgramInputs` | 16 | Inputs per program or pack. |
| `kProgramTextures` | 8 | Texture inputs per program or pack. |
| `kProgramLookups` | 4 | Lookups per program or pack. |
| `kProgramStack` | 32 | Stack slots the interpreter provides. |

### Generated shaders and the CPU model

`ProgramShader.h` and `StackShader.h` turn programs into HLSL text.
`render/GeneratedShaders` compiles that text, and `render/ShaderSource.cpp`
builds the interpreter's switch from the same table. `ProgramReference.h` is
the CPU model of the interpreter that the tests compare against.

| Type | Declared in | Description |
|---|---|---|
| `OpcodeStatement` | `ProgramShader.h` | One opcode's HLSL statement and whether it `keepsZ`. `OpcodeStatements` returns the table. `InterpreterSwitch` and `InterpreterZRule` generate the interpreter's parts from it. |
| `InputTextureSlot` | `ProgramShader.h` | The texture slot of one program input, or none for a value input. `InputTextureSlots` computes them. |
| `SourceTexture`, `MaskTexture`, `SegmentRead` | `StackShader.h` | A layer read from a texture, with `meshSpace` for a source, and a layer read from a packed program segment. |
| `SourceRead`, `MaskRead` | `StackShader.h` | The variant of absent, texture or segment read for a layer's source and mask. |
| `LayerShape` | `StackShader.h` | One layer's source read, channel, blend, channel bits, mask read and mask channel. |
| `StackShape` | `StackShader.h` | The **stack shape**: whether a base exists, the `LayerShape`s, the packed code from `CodeWithoutNumbers`, the texture slots and the segments. Equal shapes share one generated shader. |
| `kMaxStackLayers` | `StackShader.h` | 8, the most layers `CheckStackShape` accepts. |
| `LookupTable`, `ProgramTexel` | `ProgramReference.h` | A 256-entry lookup, and the inputs and lookups of one texel. `EvaluateProgramOnCpu` runs a program on them. `QuantizeUnorm8` models the RGBA8 store. |

### Reductions and readbacks

`GpuReduction.h` declares the pure half of a GPU reduction. The render layer
draws the passes, and these functions size them and decode the result. A
`ReadbackRing` tracks the staging copies so that an old result never
replaces a newer one.

| Type | Description |
|---|---|
| `ReductionExtent` | A width and height. `ReductionLevels` lists the extents of each 4x4 pass down to one texel, for sides up to `kReductionMaxSide` (4096). |
| `ReducedTexel` | The four floats of the final texel. `DecodeReduction` rejects a texel whose alpha is not 0 and divides by the texel count for `ReductionKind::kMean`. |
| `ReductionResult` | A `Value` or an error text. |
| `ReadbackRing` | `kReadbackSlots` (3) pending sequence numbers, the next sequence and the last accepted sequence. `ReserveReadback`, `PendingNewestFirst`, `AcceptReadback` and `DropReadback` operate on it. |
| `RenderFiring`, `RenderFirings` | One ripple firing's optional origin and start time, and the list `render/RenderInstance` imports as a `kFirings` input. |

### Resource lifetime

These types tie a resource's release to an observed condition. None of them
names an engine type. Each takes its resource as a template parameter or hands
out a plain index, and the render layer instantiates them.

| Type | Declared in | Description |
|---|---|---|
| `TargetPool` | `TargetPool.h` | Hands out render-**target** indices as `shared_ptr<const std::size_t>` **leases**. `Acquire` reuses an index once its lease expires and returns an empty pointer when every index is held. |
| `ConsumptionLeases<Resource>`, `Ticket` | `ConsumptionLeases.h` | `Retain` holds a resource until the consumer calls `Ticket::Consumed`. It returns null when the pending list reaches the limit. |
| `TextureLeases<Target>`, `TextureLeaseLookup<Target>` | `TextureLeases.h` | `Register` files a generated texture under its **presenter** address and generation. `Retain` returns the generation and the target if the target is still alive. |
| `OwnedState<State>` | `OwnedState.h` | Remembers a field group's original value and last written value. `Restore` returns the original only while the current value equals the last write. |
| `ResourcePool<T>` | `ResourcePool.h` | Keeps idle resources up to a count and a byte limit. `Take` removes the first entry a predicate accepts. `render/RenderTargetPool` keeps idle targets in it. |
| `RetainedCache<Key, Value>` | `RetainedCache.h` | A `std::map` of values with a last-used time. `Sweep` erases entries older than a maximum age and then the oldest unused entries above a count. `render/Compositor` keeps material records in it. |
| `SharedResource<T>`, `ResourceCache<T>` | `ResourceCache.h` | A cache of `std::weak_ptr` entries under a string key. `Adopt` returns a live entry or makes and publishes one. Only `tests/planners/resourcecache_tests.cpp` uses it. |

### Pure engine decisions and texture identity

These values let the engine layers make a decision or build a key without an
engine handle.

| Type | Declared in | Description |
|---|---|---|
| `EvictionAction` | `Eviction.h` | `kNone`, `kEvict` or `kRestore`. |
| `EvictionFor` | `Eviction.h` | A `constexpr` function. It evicts an applied actor beyond the eviction distance and restores an evicted actor only inside 80% of that distance. A distance of 0 or less disables eviction. `Manager::SweepEviction` calls it. |
| `TransformStorage` | `TransformStorage.h` | An engine transform array's base address, `count`, `stride` and `offset`. `TransformStorageIndex` decodes a pointer into a row index. `render/SkinPalette.cpp` uses it. |
| `ImageCacheKey`, `IsPlaceholderExtent` | `TextureIdentity.h` | `ImageCacheKey` lower-cases a path into a cache key. `IsPlaceholderExtent` reports a width or height at or under `kPlaceholderTextureExtent` (4). |
| `RecipeTextureKey`, `RecipeTextureCache<T>` | `RecipeTextureCache.h` | A key of recipe name, texture name, pixel count and context, and a `std::map` from it to a `shared_ptr`. `FindRecipeTexture` and `LargestRecipeTexture` read it. Only `tests/planners/recipetexturecache_tests.cpp` uses it. |

## How an actor's plans flow

```
Geometry[] + Recipe[] (the store)                         engine/ManagerApply.cpp
  │
  ▼
MatchActor(geometries, store[, resolver])                  ActorPlanning.cpp
  │  resolver: default Resolve(geometry.keys, store)         recipe/Resolve.cpp
  │  InstanceFor reuses an instance through FindInstance
  ▼
ActorPlan{ geometries, instances, placements }              ActorPlan.cpp
  │
  ├─▶ PlanActorLights(plan, store, filter) -> ActorLightPlan  ActorPlanning.cpp
  │      PlanLights -> LightPlan                              recipe/Merge.cpp
  ▼
PlanGeometryPlacement(plan, store, geometryId, filter)      ActorPlanning.cpp
  │  PlanGeometry(placed) -> GeometryPlan                    recipe/Merge.cpp
  ▼
GeometryPlacementPlan{ placed, sources, plan }
  ├─▶ PlanStacks -> GeometryStackPlan                        StackPlan.cpp
  ├─▶ PlanBinding -> BindingPlan                             BindingPlan.cpp
  ▼
BuildActorRender: one RenderStackRequest per placed output  engine/ManagerApply.cpp
  │  CollectTextureDemand per layer source and mask          TextureDemand.cpp
  │    IdentifyValue -> TextureKey; equal keys coalesce      ValueIdentity.cpp
  │    FieldProgram::Compile for a sampled expression        FieldProgram.cpp
  ▼
BuildRenderPlan(demands, stacks, resolver)                  RenderPlanLowering.cpp
  │  LowerRenderPlan: PlanBuilder lowers each node to steps
  │  InlineFields: programs into programs, then layer fields  FieldInlining.cpp
  │  ValidateRenderPlan                                      RenderPlan.cpp
  ▼
RenderInstance(plan)  ->  RenderExecution::Evaluate          render/RenderInstance.cpp
  │  CompositeStackStep: PackLayerFields -> ProgramPack      RenderPlan.cpp
  ▼
StackShapeOf -> CheckStackShape -> StackFor                  render/TextureLabPass.cpp
  │  GenerateStackShader(shape)                              StackShader.cpp
  ▼
PSGeneratedStack, or PSStack until the compile finishes     render/GeneratedShaders.cpp
```

## The files

| Files | Concern | What they own |
|---|---|---|
| `ActorPlan.h`/`.cpp` | Actor planning | The handles, the three-table `ActorPlan`, and its queries: `GeometryAt`, `InstanceAt`, `PlacementAt`, `FindInstance`, `PlacementsOfGeometry`, `PlacementsOfInstance`, `MatchesForPiece`, `ThirdPersonGeometriesOfInstance`, `RecipesOfInactiveInstances`, `InstanceVariant`, `LightEligible`. |
| `ActorPlanning.h`/`.cpp` | Actor planning | `MatchActor`, `PlanGeometryPlacement`, `PlanActorLights`. |
| `StackPlan.h`/`.cpp`, `BindingPlan.h`/`.cpp` | Actor planning | `PlanStacks`, `SlotStackPlanOf`, `ChainIndexOf`; `PlanBinding`. |
| `ValueIdentity.h`/`.cpp`, `TextureDemand.h`/`.cpp` | Render planning | `IdentifyValue`; `CollectTextureDemand` and the demand records. |
| `RenderPlan.h`/`.cpp` | Render planning | The plan and step records, the step queries (`InputsOf`, `StepDependencies`, `ChangingSteps`, `LiveConsumers`, `TypeOf`), `MarkShareableSteps`, `PackLayerFields` and `ValidateRenderPlan`. |
| `RenderPlanLowering.cpp` | Render planning | `LowerRenderPlan` and `BuildRenderPlan`. |
| `FieldInlining.h`/`.cpp` | Render planning | `AsProgramLike` and `InlineFields`. |
| `RenderExecution.h` | Render planning | `RenderExecution` and its state records. |
| `RenderFirings.h` | Render planning | `RenderFiring` and `RenderFirings`. |
| `FieldProgram.h`/`.cpp` | Programs | `FieldProgram`, the opcode set, the limits, `PackPrograms`. |
| `ProgramShader.h`/`.cpp`, `StackShader.h`/`.cpp` | Programs | The opcode statement table, `GenerateProgramShader`, `ProgramFunction`; `StackShape`, `CheckStackShape`, `GenerateStackShader`. |
| `ProgramReference.h`/`.cpp` | Programs | `EvaluateProgramOnCpu` and `QuantizeUnorm8`. |
| `GpuReduction.h`/`.cpp` | Programs | Reduction levels, `DecodeReduction`, the `ReadbackRing` functions. |
| `TargetPool.h`, `ConsumptionLeases.h`, `TextureLeases.h`, `OwnedState.h`, `ResourcePool.h`, `RetainedCache.h`, `ResourceCache.h` | Resource primitives | The lease, pool and cache templates. |
| `Eviction.h`, `TransformStorage.h`/`.cpp`, `TextureIdentity.h`/`.cpp`, `RecipeTextureCache.h` | Resource primitives | `EvictionFor`, `TransformStorageIndex`, `ImageCacheKey`, `IsPlaceholderExtent`, the recipe texture key. |

## See also

- `REFERENCE.md` → *planners*: the three-table `ActorPlan` decision, the
  handle spaces, the `StackPlan` and `BindingPlan` rules, and the lease and
  eviction contracts.
- `REFERENCE.md` → *The lab's shaders*: how `ProgramShader` and
  `StackShader` output reaches the GPU.
- `REFERENCE.md` → *Compositor*: the render plan, reductions and readbacks,
  and when `InlineFields` inlines a program or a layer field.
- `REFERENCE.md` → *Generated texture references*: the `TextureRef`
  lifetime that `TargetPool` and `TextureLeases` support.
- `docs/conventions.md` → *Runtime identities and editor commits* →
  *Handles* and *Planning*.
- `docs/conventions.md` → *House rules* → *Glossary*: **plan**, **binding**,
  **lease**, **target**, **contributor**, **texture identity**.
- `docs/conventions.md` → *Gates* → *Layers*.
- [Program checkpoint](../checkpoints/interpreter-program-2026-09-27.md),
  [texture demand checkpoint](../checkpoints/texture-demand-2026-09-27.md),
  [render plan checkpoint](../checkpoints/render-plan-2026-09-27.md).
