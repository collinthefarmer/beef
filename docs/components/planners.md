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
  **geometries** and builds the **matches** (`ActorMatches`). `PlanGeometryPlacement` and
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

`ActorMatches.h` declares five handle types. Each is an `enum class :
std::size_t` over its own index space. The distinct types stop a caller from
indexing one table with another table's handle. No handle wraps a pointer.

| Handle | Indexes |
|---|---|
| `GeometryId` | A row of `ActorMatches::geometries`. |
| `InstanceId` | A row of `ActorMatches::instances`. |
| `PlacementId` | A row of `ActorMatches::placements`. |
| `RecipeId` | A row of the `std::span<const Recipe>` the caller passes, which the engine takes from one `RecipeCatalog`. Declared in `recipe/RecipeCatalog.h`. |
| `OutputId` | A row of the matched recipe's `outputs`. |

### The actor's matches

`ActorMatches` holds what the planners know about one actor as three tables of
value rows. The rows refer to each other by handle. `MatchActor` builds the
matches, and the query functions in `ActorMatches.cpp` read them.

| Type | Description |
|---|---|
| `Geometry` | One mesh on the actor: its `GeometryIdentity`, the armor's `WornPiece` match keys, and the `firstPerson` flag. |
| `Instance` | One **instance**: a `RecipeId`, an optional enchantment `FormKey`, and an optional effect `RecipeKey`. `InstanceFor` in `ActorPlanning.cpp` reuses an instance through `FindInstance` on these three values. |
| `OutputPlacement` | One recipe output on one placement: its `OutputId`, whether it is `selected`, and a `problem` text. |
| `Placement` | One **placement**. It joins an `InstanceId` to a `GeometryId` and records the matched `RecipeKey`, the `priority` and one `OutputPlacement` per output. |
| `ActorMatches` | The `geometries`, `instances` and `placements` tables. |
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
| `TextureFormat` | `kRgba8`, `kRgba32Float` or `kR8`. A scalar field uses `kR8`, a reduction operand `kRgba32Float`, a mesh bake and every other value `kRgba8`. |
| `IsUnorm8`, `BytesPerTexel`, `FormatName` | Whether a format has 8 bits per channel (R8 or RGBA8); its bytes per texel (1, 4 or 16); its name in traces and listings. |
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
| `RenderStep` | One step: a `RenderStepKind` and a `displayName`. |
| `RenderValueBinding` | A graph value, the reference that holds its result, and its geometry. `RenderPlan::values` keeps these for inspection. |
| `TextureUseBinding` | A `TextureUse` and the texture it reads. |
| `StackOutputBinding` | A placement output, its geometry, and the stack step that produces it. |
| `RenderPlan` | The `values`, `inputs`, `steps`, `textureUses` and `stackOutputs` tables. |
| `RenderStackRequest` | One stack to lower: the graph, the `SurfaceOutput`, the instance, the placement output, the requirements and the geometry. |
| `RenderBindingResolver` | A callback that returns the `ValueBindings` for a `TextureValue` on a geometry. |
| `PackedLayerFields` | A `ProgramPack` and the concatenated inputs and lookups of a stack's layer fields. |

### Plan reuse across actors

`RenderPlanKey.h` decides when one actor's **render plan** is reused for
another actor. `RenderPlanCache.h` holds the reused plans.

| Type or function | Description |
|---|---|
| `StackKeyRequest` | What the engine supplies for one stack: the graph, output, placement, geometry, context, side reference, material identity, mesh hash and the `ValueBindings` with the actor left out. |
| `StackKey`, `ChainKey`, `RenderPlanKey` | The **render plan key**: per stack the request's fields and `staticInputs`, the identities of the graph's external inputs that do not change; per slot chain the geometry, surface, slot and links. Equality is exact. `RenderPlanKeyHash` hashes it. |
| `RenderPlanKeyOf` | Returns the key, or none when a request has no graph, its graph reads a node position, its mesh has no hash, or a static input's identity fails. |
| `GraphsNamedBy` | The live graphs that a key's stacks name, once each. |
| `RenderPlanCache` | Up to 64 plans by key. It evicts the least recently used. An entry holds the graphs its key names. |

### Plan builds off the main thread

`RenderPlanJob.h` is the plan build that `engine/` runs on its worker thread.
`IdentityFacts.h` gives value identities from copied facts, so the build
reads no engine state.

| Type or function | Description |
|---|---|
| `IdentityFacts` | The facts that identities read for one stack: the instance string, the mesh and material identities, the geometry and root addresses as numbers, the item hash and the enchantment fields. |
| `InstanceIdentity` | The instance string: graph address, application context and actor. |
| `IdentityBindings` | `ValueBindings` over shared `IdentityFacts`. Missing facts give empty identities. |
| `PlanStackRequest` | One stack with owned inputs: the graph, the `SurfaceOutput` (aliasing its recipe), the recipe id, the output, placement, geometry, context, side and identity facts. |
| `PlanBuildInput` | The stacks and slot chains of one plan build. |
| `PlanBuildOutput`, `PlanBuildPhases` | The plan or its error, the demand warnings as text, and the demand and lowering times with the demand count. |
| `BuildPlanFrom` | Collects the layer demands (`CollectTextureDemand`) and lowers the plan (`BuildRenderPlan`) from a `PlanBuildInput`. The sync and the worker both call it. |

### Retention and identity helpers

| Function | Description |
|---|---|
| `StepOutputRetention` | Whether the step output cache renews, keeps or evicts an entry (`Retention`): renew while another holder has the target, keep while unheld for at most `kStepOutputIdleMs` (500 ms), evict after that (`RetainedCache.h`). |
| `PositionBakeIdentity` | The identity of a ripple's shared position bake, built from the mesh identity with the same length-prefixed encoding as every value identity (`ValueIdentity.h`). |

### Target cost

`TargetCost.h` lists the render **targets** a plan holds and counts their
bytes. The heartbeat compares the count with the measured holdings.

| Type or function | Description |
|---|---|
| `HeldTarget` | One target: its `TextureSize` (`size`), its format and its shared key, if it has one. |
| `PlanTargets` | The targets one plan holds (`withIntermediates`: every drawing step a rendered root reaches) and the stacks among them (`stacks`). A static output is kept until the instance goes; only animated outputs are released when idle. |
| `TargetCost` | The number of targets, their bytes, and the count per size (`targetsBySize`). |
| `TargetBytes` | The bytes of one target: size² × `BytesPerTexel`, × 4/3 for `MipPolicy::kGenerate` (a full mip chain). `TextureLab::RenderTarget::Bytes` uses it. |
| `HeldSteps` | The steps of a plan that hold a target: reached from a rendered root, with texture requirements, and drawing. `TargetsOf` prices them. |
| `TargetsOf` | The `PlanTargets` of a plan at the given `PlanSizes`. |
| `CostOf` | The cost of a list of held targets, with one target per shared key. |

### Sides and resolution levels

A plan names texture sizes by **side reference** (`SideRef`, in `TextureDemand.h`). The instance resolves them to pixels.

| Type or function | Description |
|---|---|
| `SideRef` | An index into the plan's list of distinct stack sides. `TextureRequirements::side` and `SubmitReductionStep::domain` hold one. |
| `PlanSizes` | The `TextureSize` of each side reference (`sizes`). |
| `SizesFor` | Returns the `PlanSizes`, or an error when the list lacks a side that the plan names. |
| `SizeOf` | The `TextureSize` of one side reference, or 64 for an unknown one. `RenderInstance::SizeAt` gives it for the instance's sizes. |
| `SharedKeyOf` | A step's content key with the pixels of its sides in front. The step output cache and `CostOf` use it. |
| `ResolutionLevel` | `kFull`, `kHalf`, `kQuarter` or `kEighth` of every side. |
| `IdealLevel` | The level for a screen fraction. |
| `PerLevel`, `LevelIndex`, `kResolutionLevels` | One value per level, read with `At`; the index of a level; every level from full to eighth. |
| `ActorSight`, `WantedLevel` | One actor's optional screen fraction, whether it is in view, and its role; the level it wants: full for the player, eighth out of view, `IdealLevel` in view, and none when the fraction is unmeasured. |
| `SmallFor`, `SmallSince`, `kStepDownBand`, `kStepDownHoldMS` | Whether a fraction is under 0.8 of the lower threshold of a level, and the start of the step-down hold, kept while the fraction stays small and cleared when it grows. |
| `KeepLevel`, `StepUpTo`, `StepDownTo`, `LevelStep`, `NextLevelStep` | The level change for one tick: a step up straight to the wanted level, one level down after the 2 s hold, or none. The player never steps down. |
| `LargerOnScreen` | Orders two optional fractions, larger first and unmeasured last. |
| `AtLevel` | The `PlanSizes` divided by a level, with 64 as the floor. |
| `CoarserLevel`, `LevelGap` | The next coarser level, if any, and the number of steps between two levels. |
| `ResizeIdle`, `ResizePending`, `ResizeProgress` | The resize states of one actor (`ResolutionResize.h`): none, a level that waits for a resize, and a resize with its old level, new level and first frame. |
| `ResizeDraw`, `ResizeDrawState` | What a tick saw of a resize: `kOutOfView`, `kDrawing` or `kDrawn`. `DrawOf` gives it from a `ResizeDrawState`: the view and the drawn and total stack counts. |
| `SwapDue` | True when a resize is drawn, out of view, or `kResizeSwapAfterFrames` (60) frames old. |
| `ResizeCandidate`, `NextResize` | One actor with its FormID, whether a level is pending, whether it is in view, and its optional screen fraction; the FormID of the pending actor to resize first (in view first, then `LargerOnScreen`), or none while `kResizesInFlight` (1) resizes are in flight. |

`RenderBudget.h` holds the decisions of the render target budget
(render-performance 7d).

| Type or function | Description |
|---|---|
| `VideoMemory`, `BudgetSample` | The DXGI budget and usage of the local segment, and one sample: the optional reading, the plugin's target bytes (`pluginBytes`) and the cap. |
| `MarginOf`, `AllowanceOf` | The margin (the larger of 512 MiB and 5 % of the budget) and the **allowance**: the plugin's bytes plus the **free video memory** (budget less usage) less the margin, capped. No reading, or a budget of 0, gives no allowance. |
| `FreeVideoMemory`, `AllowanceLimit`, `LimitedAllowance`, `LimitedAllowanceOf` | The budget less the usage, and the allowance with the limit that set it. `kCap` means the cap is at or under the free-memory figure. `kFreeVideoMemory` means the free video memory less the margin is lower. `AllowanceOf` returns the bytes of `LimitedAllowanceOf`. |
| `BandOf`, `PressureGoal` | The band (the larger of 256 MiB and 10 % of the allowance, at most half of it) and the allowance less the band. |
| `Pressure`, `BudgetState`, `NextBudget`, `IsOver` | The pressure state with its hysteresis: it starts over the allowance and ends below the goal. An unknown allowance ends it. |
| `Admission`, `AdmitGrowth`, `Charged`, `Released`, `AdmissionName` | Whether a step up of the given bytes may start, the planned bytes after bytes are charged, and the planned bytes after a holding goes. |
| `InstallBound`, `LevelBytes`, `HeadroomOf`, `FittingLevel` | The finest level and the optional headroom of an install, and the bytes with intermediates per level (a `PerLevel`). `HeadroomOf` gives the allowance less the planned bytes without the replaced bytes, the replaced bytes under pressure, and none when the allowance is unknown. A first install replaces 0 bytes. `FittingLevel` gives the finest level from the bound to eighth whose bytes fit the headroom, eighth when none fits, and the bound when there is no headroom. |
| `LevelCost`, `IntermediateHolding`, `PlannedBytes`, `IntermediateBytes`, `StepDownOption`, `StepDownOf` | The bytes of a plan at one level with intermediates (`withIntermediates`) and with stacks only (`stacksOnly`), whether an actor's intermediates are released, the bytes the planned figure counts for each case, the bytes a release frees, and the bytes that one step down frees. |
| `BudgetActor`, `BudgetRelease`, `PressureStep`, `BudgetResponse`, `RespondToPressure` | One actor's facts, and the responses for one heartbeat: the hidden actors that release, by FormID with their bytes, and the next step: `Enough`, `AwaitResize` while a resize is pending or in flight, `StepDown` (one NPC by FormID) or `Exhausted`. The facts include the planned bytes (`plannedBytes`), the optional distance to the player, and the bytes freed at the wanted level (`surplusBytes`). |
| `PreemptionRequest`, `ShortfallOf`, `PreemptionCandidate` | A blocked step up in view with its **shortfall** (planned plus added bytes over the allowance); the NPC to step down for it: one that holds a finer level than it wants and is out of view or under 0.8 of the requester's fraction, ranked as under pressure. None when the requester is out of view, when there is no shortfall, or when the candidates' `surplusBytes` together cannot cover the shortfall. |
| `PressureStartLine`, `PressureEndLine`, `StepDownLine`, `ExhaustedLine` | The text of the budget's log lines. `StepDownLine` names the actor a preemption makes room for. |

`BudgetRadius.h` holds the decisions of the **budget radius**, the third
response of the budget.

| Type or function | Description |
|---|---|
| `BudgetRadius` | The radius in game units, the time of its last change or of the last heartbeat under pressure (`sinceMs`), and whether the current blocked episode was reported (`blockReported`). The manager keeps it optional; no record means no radius. |
| `LimitsDistance`, `EffectiveEvictDistance` | Whether an `EvictDistance` sets a limit (above 0), and the distance that eviction uses: the smaller of `EvictDistance` and the radius. Without a radius it is `EvictDistance` unchanged. |
| `RadiusShrink`, `ShrinkRadius` | The FormID of the actor that is evicted, the new radius and the bytes freed. The candidate is the farthest NPC inside the current radius that is not resizing. The new radius is 0.99 × its distance (`kRadiusShrinkFactor`), raised to the next nearer distance, so that `EvictionFor` evicts that actor alone. The player and missing, non-finite or non-positive distances never count. A resizing actor beyond the candidate stops the shrink. |
| `EvictedActor`, `RingActor`, `RingOf` | An evicted NPC: its optional distance and its optional **return bytes**. The **ring** is the evicted NPCs that `Returns` does not return at the current eviction distance and returns at the eviction distance after a growth. `RingOf` keeps only finite distances and orders the ring nearest first. |
| `PriceOf`, `RingBytes` | The price of one ring NPC: its return bytes, or the given **unpriced bytes** when it has none. The sum of the prices of a ring. |
| `RingRoom`, `RingRoomOf`, `kRingMarginDivisor` | The headroom (`HeadroomOf` with no replaced bytes) and the margin, half of the band (`BandOf`). An unknown allowance gives no room. |
| `RingFit`, `FitRing`, `kUnpricedPerGrowth` | How many ring NPCs return and their bytes. NPCs at one distance return together. The ring stops at the first distance where the sum passes the room, or where a second unpriced NPC would return. |
| `RadiusStoppingAt`, `kRadiusRoundingSteps` | The largest radius whose 0.8 × value is at or below the distance, so that `Returns` returns every nearer NPC and not that one. |
| `RadiusGrowthRequest`, `RadiusGrowth`, `GrowRadius` | One heartbeat of the radius without a shrink: `RadiusUnchanged`, `RadiusGrown`, `RadiusBlocked` or `RadiusCleared`. Under pressure the distance is unchanged and the dwell restarts. After `kRadiusDwellMs` (10 s) the radius grows by 1 / 0.8 (`kRestoreFraction`) when the ring fits the headroom less the margin. When part of the ring fits, the radius grows to `RadiusStoppingAt` the first NPC that does not fit. When the nearest NPC does not fit, `RadiusBlocked` keeps the distance, restarts the dwell, carries the ring bytes and the room, and marks the first block of the episode (`firstInEpisode`). The radius clears when the grown radius reaches `EvictDistance`, or the farthest loaded NPC when `EvictDistance` sets no limit, and the ring of the clear fits. Otherwise the clear is a growth to the fitting radius. Without an allowance the ring is not priced. |
| `RadiusShrinkLine`, `RadiusGrowLine`, `RadiusBlockedLine`, `RadiusClearLine` | The text of the radius's log lines. |

`RenderMemory.h` turns the counts into the render-target log lines.

| Type or function | Description |
|---|---|
| `NextHoldingStep`, `HoldingStep`, `Crossing` | The 1 GiB step of the live target bytes, with a **crossing** when it rises a step or falls half a step below one. |
| `ActorTargets`, `HoldingsOf`, `ActorHolding` | One actor's targets (`targets`), split into its own bytes (`ownBytes`) and its shared bytes. A shared target has a content key that another actor also holds. The own bytes of all actors plus each shared target once make the plans' total. |
| `LargestHoldings` | The five actors with the most own bytes. |
| `TargetBurst`, `IsBurst` | The targets created and destroyed in one heartbeat interval; a **burst** creates 256 MiB or more. |
| `ScreenSpread`, `SpreadOf` | The least, median and most screen fraction of the rendered actors. |
| `HoldingLine`, `BurstLine`, `Mebibytes` | The text of the two log lines. The holding line gives the measured total and the plans' total. |

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
it last observed. A changed input marks **dirty** every step downstream of it
(`DownstreamStepsOf`, computed once per plan; a readback's submission step
counts as upstream of the readback's readers). A walk returns at once for a
clean step that holds its output, so a demand visits only the steps an input
change can reach.

| Type | Description |
|---|---|
| `ChangeVersion` | A `std::uint64_t` counter. |
| `StepInput` | One observed input reference and its change version. |
| `StepOutput<T>` | An optional result and its change version. |
| `RenderInputState<T>` | An imported input's optional value and change version. |
| `StepExecutionState<T, Scratch>` | One step's observed inputs, outputs, scratch, last diagnostic, `released` flag, the clock value of its last use, and whether it is `dirty`. |
| `ResolvedRenderInput<T>` | A reference, its value and its change version, as a step callback receives it. |
| `AdvancedRenderClock`, `kMaxRenderAdvanceMS` | The **render clock**: it advances by the gap between two frames of one instance, at most 100 ms, so idle release counts only time in which the instance rendered. |
| `RenderExecution<T, Scratch>` | Holds a validated plan and its state. `Evaluate` materializes one reference through the `ExecuteStep`, `SameValue` and `SelectInputs` callbacks. `SetInput` imports a value. `SetClock` sets the time in the caller's unit, and `ReleaseIdle` drops results unused for longer than a given span of it. A walk stops at depth 64 or 65536 visits. |

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
| `kShuffle`, `kHash`, `kRandRange`, `kHue`, `kHueShift` | The functions `shuffle`, `hash`, `randrange`, `hue` and `hueShift` (opcodes 45 to 49). `rand()` has no opcode: its identity arrives as an input and its hash is `kHash`. |

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
| `EvictionFor`, `kRestoreFraction` | `Eviction.h` | A `constexpr` function. It evicts an actor with something installed beyond the eviction distance and restores an actor with nothing installed only inside 80% (`kRestoreFraction`) of that distance. A distance of 0 or less disables eviction. `Actors::InRange` calls it. |
| `Returns` | `Eviction.h` | A `constexpr` function. It tells if an actor with nothing installed returns at an eviction distance. `Actors::InRange` and `RingOf` call it. |
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
ActorMatches{ geometries, instances, placements }              ActorMatches.cpp
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
| `ActorMatches.h`/`.cpp` | Actor planning | The handles, the three-table `ActorMatches`, and its queries: `GeometryAt`, `InstanceAt`, `PlacementAt`, `FindInstance`, `PlacementsOfGeometry`, `PlacementsOfInstance`, `MatchesForPiece`, `ThirdPersonGeometriesOfInstance`, `InstanceVariant`, `LightEligible`. |
| `ActorPlanning.h`/`.cpp` | Actor planning | `MatchActor`, `PlanGeometryPlacement`, `PlanActorLights`. |
| `StackPlan.h`/`.cpp`, `BindingPlan.h`/`.cpp` | Actor planning | `PlanStacks`, `SlotStackPlanOf`, `ChainIndexOf`; `PlanBinding`. |
| `ValueIdentity.h`/`.cpp`, `TextureDemand.h`/`.cpp` | Render planning | `IdentifyValue`; `CollectTextureDemand` and the demand records. |
| `RenderPlan.h`/`.cpp` | Render planning | The plan and step records, the step queries (`InputsOf`, `StepDependencies`, `ChangingSteps`, `AnimatedSteps`, `RenderedRoots`, `Draws`, `LiveConsumers`, `TypeOf`), `MarkContentKeyedSteps`, `PackLayerFields` and `ValidateRenderPlan`. |
| `RenderPlanLowering.cpp` | Render planning | `LowerRenderPlan` and `BuildRenderPlan`. |
| `RenderPlanKey.h`/`.cpp`, `RenderPlanCache.h`/`.cpp` | Render planning | `RenderPlanKeyOf`, `RenderPlanKeyHash`, `GraphsNamedBy`; `RenderPlanCache`. |
| `RenderPlanJob.h`/`.cpp`, `IdentityFacts.h`/`.cpp` | Render planning | `PlanBuildInput`, `BuildPlanFrom`; `IdentityFacts`, `IdentityBindings`, `InstanceIdentity`. |
| `RenderMemory.h`/`.cpp` | Render planning | `NextHoldingStep`, `HoldingsOf`, `LargestHoldings`, `IsBurst`, `SpreadOf`, `HoldingLine`, `BurstLine`. |
| `RenderBudget.h`/`.cpp` | Render planning | The render target budget: `AllowanceOf`, `NextBudget`, `AdmitGrowth`, `FittingLevel`, `StepDownOf`, `RespondToPressure` and the log lines. |
| `BudgetRadius.h`/`.cpp` | Render planning | The budget radius: `EffectiveEvictDistance`, `ShrinkRadius`, the ring (`RingOf`, `FitRing`), `GrowRadius` and the log lines. |
| `InstallPacing.h`/`.cpp` | Render planning | The install budget per frame and the order of deferred installs: `InstallMayStart`, `Synced`, `NextInstall`, `Deferred`, `Removed`, `WaitedMS`. |
| `DrawPacing.h`/`.cpp` | Render planning | The hidden draw budget per tick, the order of hidden draws and the end of a first draw: `MayDrawStack`, `Drew`, `HiddenDrawOrder`, `FirstDrawDone`. |
| `TargetCost.h`/`.cpp` | Render planning | `HeldTarget`, `PlanTargets`, `TargetCost`, `TargetBytes`, `TargetsOf`, `CostOf`. |
| `PlanSizes.h`/`.cpp`, `ResolutionLevel.h`/`.cpp`, `ResolutionResize.h`/`.cpp`, `ActorRole.h` | Render planning | `PlanSizes`, `SizesFor`, `SizeOf`, `SharedKeyOf`; `ResolutionLevel`, `PerLevel`, `IdealLevel`, `StepUpFor`, `CoarserLevel`, `LevelGap`, `AtLevel`, `LevelName`; `DrawOf`, `SwapDue`, `NextResize`; `ActorRole`. |
| `FieldInlining.h`/`.cpp` | Render planning | `AsProgramLike` and `InlineFields`. |
| `RenderExecution.h` | Render planning | `RenderExecution` and its state records. `ReleaseIdle` and `ReleaseWhere` release step outputs. |
| `RenderFirings.h` | Render planning | `RenderFiring` and `RenderFirings`. |
| `FieldProgram.h`/`.cpp` | Programs | `FieldProgram`, the opcode set, the limits, `PackPrograms`. |
| `ProgramShader.h`/`.cpp`, `StackShader.h`/`.cpp` | Programs | The opcode statement table, `GenerateProgramShader`, `ProgramFunction`; `StackShape`, `CheckStackShape`, `GenerateStackShader`. |
| `ProgramReference.h`/`.cpp` | Programs | `EvaluateProgramOnCpu` and `QuantizeUnorm8`. |
| `GpuReduction.h`/`.cpp` | Programs | Reduction levels, `DecodeReduction`, the `ReadbackRing` functions. |
| `TargetPool.h`, `ConsumptionLeases.h`, `TextureLeases.h`, `OwnedState.h`, `ResourcePool.h`, `RetainedCache.h`, `ResourceCache.h` | Resource primitives | The lease, pool and cache templates. |
| `Eviction.h`, `TransformStorage.h`/`.cpp`, `TextureIdentity.h`/`.cpp`, `RecipeTextureCache.h` | Resource primitives | `EvictionFor`, `Returns`, `TransformStorageIndex`, `ImageCacheKey`, `IsPlaceholderExtent`, the recipe texture key. |

## See also

- `REFERENCE.md` → *planners*: the three-table `ActorMatches` decision, the
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
