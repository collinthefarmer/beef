# recipe/

`recipe/` is the pure core of the plugin. It reads a **recipe** file into a checked
`Recipe`, resolves the forms that the recipe names, and compiles the recipe
into a **recipe graph**. It evaluates the graph per tick, merges the recipes on
one **piece** into a **plan**, and writes a `Recipe` back to JSON. It is
engine-free and depends only on `Core.h`, per its `ALLOWS` row in
`tools/gate.py`, `'recipe': ('Core.h', 'recipe')`. The `mesh`, `planners`,
`studio`, `validator`, `render`, `engine`, and `menu` layers may include it.
It compiles natively and is unit-tested through `ctest --preset native`.

## What it owns

A recipe declares **rows**. A **signal** is a row whose value changes per tick.
A **source** is a row whose value changes per texel. A **mask** is a per-texel
expression row. A **curve** is a named expression in `x`. An **output** writes
a **layer** stack into a material or shell **slot**, or describes one
**light**. A **variant** is a named override set. The **shell** is a second,
posed copy of the armor geometry that the recipe can draw on.

The directory owns these parts:

- the recipe model and its word tables (`Recipe.h`, `Words.h`);
- the JSON boundary that every module uses to read and write JSON
  (`Binders.h`);
- the **diagnostic** record and the `Reporter` sink that every load, check,
  and edit writes through (`Recipe.h`);
- resolution of recipes against a worn piece (`Resolve.cpp`);
- the recipe graph compiler and the per-tick evaluator (`RecipeGraph.cpp`,
  `RecipeGraphLowering.cpp`, `Signals.cpp`);
- the expression language that signals, masks, and curves share
  (`Expression.h`);
- the merge of several recipes on one piece (`Merge.h`);
- the importer that turns a vanilla effect shader into a starting recipe
  (`Importer.h`, `Efsh.h`).

The JSON boundary checks input before the model trusts it.
`ParseObjectDocument` refuses text nested deeper than `kMaxRecipeDepth` and
reports duplicate keys. `FloatFrom` refuses a JSON number outside the finite
float range. `RowCapReached` stops a row list at `kMaxRecipeRows` entries,
rejected rows included. `LoadResult::inputDiagnostics` keeps the file errors
that decoding replaced with a default or a dropped row. `Validate` accepts
those diagnostics, because the typed model cannot show them again.

## Data

Each header declares its data types first, then the functions over them.

### The model (`Recipe.h`)

`Recipe` is the whole file in memory. `RecipeRead.cpp` fills it one checked
field at a time. `RecipeWrite.cpp` writes it back with the key order kept. The
studio, `Resolve`, `RecipeGraph::Compile`, and `Merge.cpp` all read this one
struct.

| Type | Description |
|---|---|
| `Recipe` | The recipe: an id, `Metadata`, its `RecipeKey`s, an optional priority, a `MergeMode`, a `Clock`, the row lists (`Signal`s, `Curve`s, `Source`s, `Mask`s, `Output`s, `Variant`s), and `ShellSettings`. `FindSignal`, `FindCurve`, `FindSource`, and `FindMask` look up a row by name. |
| `Metadata` | The name, author, description, version, the importer's `imported` stamp, and a free-form `meta` field that the writer keeps verbatim. |
| `MergeMode` | How recipes on one piece compose: `kStack` appends, `kReplace` clears earlier groups on the selected targets, and `kSampled` chooses one recipe per piece. `SamplingHash` seeds the choice. |
| `RecipeKey` | The worn piece that the recipe applies to: a `KeyKind` (default, enchanted, material, keyword, armor, effectShader, enchantment, magicEffect) and a `KeyOperandValue` (none, a `FormRef`, or a glob). `KeyKindSpec` in `kKeyKinds` gives each kind its default priority and operand. |
| `Selector` | The geometry that an output or variant touches: `SelectorClause`s over addon, geometry name, or texture path, matched any-of by `Matches`. An empty selector matches every geometry. |
| `GeometryIdentity` | The addon, name, and diffuse path that `Matches` tests a `Selector` against. |
| `Signal` | A name, a `SignalKind`, an optional `CurveRef` that shapes the result, and an author `note`. |
| `Curve` | A name, an expression in `x`, and an author `note`. A `CurveRef` applies it to a signal or a layer. |
| `Source` | A name, a `SourceKind`, and an author `note`. |
| `Mask` | A name, a per-texel expression, and an author `note`. Source and mask names stand for images in the expression. Signal names stand for the tick's values. |
| `Output` | `SurfaceOutput` or `LightOutput`. The Outputs group below describes both. |
| `ShellSettings` | The shell: a `ShellMaterial` (pbrCopy or vanilla), a `ShellBlend` (additive or alpha), depth bias, alpha test, the opacity, rimPower, and emissive `Param`s, and a `ShellPose` (inflate, offset, scale, spin). |
| `Variant` | A name, a `VariantKey` (a `FormRef` or a `Selector`), and an `overrides` map from row name to value. `VariantApplies` tests the key, and `ApplyVariant` returns the recipe with the overrides applied. |
| `Clock` | The recipe's time scale. `speed` multiplies the tick clock. |

### Values (`Recipe.h`; `Ref`, `Value`, `ValueType` in `Core.h`)

A field that a recipe can animate is a `Param`. A literal number stays fixed.
A `Ref` reads a signal, and `SignalState::Resolve` makes that read each tick.

| Type | Description |
|---|---|
| `Param` | `float \| Ref`: a fixed number or a signal reference (`@name` in the file). `ParseParam` and `ParamText` convert it to and from text. |
| `Vec2Param` / `Vec3Param` | One `Param` per component, or one `Ref` to a vector signal. |
| `Ref` | The name of another row. |
| `Value` / `ValueType` | A scalar, `Vec2`, or `Vec3` value, and the tag that names which one. |
| `CurveRef` | Text that names a declared curve or holds an inline expression in `x`. `Named()` tells the two apart. |
| `FormRef` | A form named as text: an editor ID, or `0x<local id>~<plugin file>` that `FormKey::Parse` reads. Resolution fills its `FormKey`. |

### Signal kinds (`Recipe.h`)

`SignalKind` is the variant over the seventeen kinds below. A signal produces
one `Value` per tick. `SignalKindId` names each alternative. `kSignalKinds` in
`Words.h` gives each kind its file word and marks the kinds that the studio can
tune.

| Alternative | File word | Produces |
|---|---|---|
| `ConstantSignal` | `constant` | A fixed `Value`. |
| `WaveSignal` | `wave` | `base` plus `amplitude` times a sine, triangle, square, or saw `Waveform` with `period` and `phase`. |
| `RampSignal` | `ramp` | A value that moves from `from` to `to` over `seconds`, then holds `to`. |
| `EfshSignal` | `efsh` | One `EfshField` of a vanilla effect shader record: fillAlpha, fillColor, edgeAlpha, edgeColor, or scroll. |
| `ActorValueSignal` | `av` | The wearer's actor value under one `Measure`: current, base, permanent, temporaryModifier, damage, or max. |
| `ActorStateSignal` | `actorState` | One `ActorStateKind`: seven 0/1 flags, the scalar movementSpeed, and the vec3 positions position and target. |
| `EnchantmentSignal` | `enchantment` | The matched enchantment's magnitude or cost. |
| `TriggerSignal` | `trigger` | The age of the newest firing as a fraction of `lifetime`. The `TriggerOrigin` is an `EventOrigin`, a `PluginOrigin`, or a `WhenOrigin`. The row declares its `payload` type and a `TriggerAnchor`, and keeps at most `max` firings. |
| `PayloadSignal` | `payload` | The value of the named trigger's newest firing, held between firings. |
| `CounterSignal` | `counter` | A count of a trigger's firings, with an optional `reset` trigger and `cap`. |
| `AccumulateSignal` | `accumulate` | A total that each firing raises by one and `decay` lowers per second. |
| `NoiseSignal` | `noise` | Value noise over time at `frequency`, scaled by `amplitude`, repeatable per `seed`. |
| `GradientSignal` | `gradient` | A color: `t` sampled against the `GradientStop` list. |
| `RateSignal` | `rate` | The named signal's change per second. |
| `SmoothSignal` | `smooth` | The named signal eased toward its current value with time constant `seconds`. |
| `ToRootSignal` | `toRoot` | The named world-space vec3 signal, expressed in the wearer's root space. |
| `ExprSignal` | `expr` | The value of an expression over other rows. |

### Source kinds (`Recipe.h`)

`SourceKind` is the variant over the six kinds below. The render layer
computes a source once per texel. `SourceKindId` follows the alternative
order, and the asserts in `Words.h` keep `kSourceKindWords` in the same order.

| Alternative | File word | Produces |
|---|---|---|
| `ImageSource` | `image` | A texture sampled per texel: one `ImageChannel` in tiled or mesh `ImageSpace`, with optional scroll, tile, mirror, transpose, and mip. |
| `MaterialSource` | `material` | One `MaterialChannel` of the piece's own material, such as diffuseRgb, roughness, or relief. |
| `BakeSource` | `bake` | One `BakeKind` computed from the mesh: position, localPosition, normal, uv, partition, boneWeight, componentId, or chartId. |
| `DistanceSource` | `distance` | The texel's distance from a named skeleton node. |
| `RippleSource` | `ripple` | A ring or disc (`RippleShape`) that spreads from a trigger firing at `speed`, `width` wide, fading at `decay`. A nonzero `direction` turns it into a plane sweep. |
| `MaterialClustersSource` | `materialClusters` | The id of each texel's nearest material cluster under `ClusterSettings` and its `ChannelWeights`. |

### Outputs (`Recipe.h`)

An output is what a recipe writes. A `SurfaceOutput` stacks layers into one
slot. A `LightOutput` describes one light. `SlotSpec` rows in `Words.h` state
which scalars, channels, and blends each slot takes.

| Type | Description |
|---|---|
| `SurfaceOutput` | A `Surface` (material or shell), a `Slot`, its `SlotScalars`, a `Selector`, a replace flag, an optional `Resolution`, the layer `stack`, and an author `note`. |
| `Layer` | A `LayerSource`, an optional `CurveRef`, a `Blend`, an opacity `Param`, an optional color and mask, the `ChannelSet` it writes, and an author `note`. |
| `LayerSource` | `Ref \| Vec3`: a source or mask by name, or a constant color. |
| `Blend` | How a layer combines with the layers below it: replace, multiply, add, subtract, screen, or reorient. `BlendSpec` gives each blend its shader mode and marks reorient as normal-slot only. |
| `SlotScalars` | The per-slot scalar `Param`s, one per `ScalarField`. |
| `Slot` | The nine writable slots: diffuse, emissive, rmaos, normal, height, fuzz, glint, coat, subsurface. |
| `Target` | The three kinds of target an output writes: material, shell, or light. |
| `LightOutput` | `Bones` placement (`SkinnedBones` or `NamedBones`), offset, color, intensity, size, cutoff, a shadow flag, a `Selector`, a replace flag, and an author `note`. |

### Texture size (`Recipe.h`)

A surface output's stack renders into a runtime **target** texture that is
sized per slot. `Resolution` scales that size down to save texture memory.

| Symbol | Description |
|---|---|
| `Resolution` | `kFull`, `kHalf`, or `kQuarter`. `ResolutionName` and `ParseResolution` convert it through `kResolutions`. |
| `DefaultSlotResolution` | Full for normal and height, half for diffuse and rmaos, quarter for the other slots. |
| `ResolutionDivisor` | The number that divides the slot's full size: 1, 2, or 4. |

### Cross-actor sharing (`Recipe.h`, `RecipeGraph.h`; `Vocabulary.cpp`)

Two actors in the same armor can share one rendered target only when the
target's inputs do not change per actor. The compositor asks these predicates
before it reuses a target. Each predicate has a form that takes a compiled
`RecipeGraph` and a form that compiles one.

| Function | Description |
|---|---|
| `ShareableAcrossActors` (output) | True when the output is a `SurfaceOutput`, `IsAnimated` is false, `RecipeInputsAreActorIndependent` holds, and the graph's signals equal the authored signals. |
| `ShareableAcrossActors` (mask) | True when the mask is enabled, does not change over time, and meets the same two recipe conditions. |
| `RecipeInputsAreActorIndependent` | True when the recipe has no `BakeSource` and no `DistanceSource`, because those two read per-actor geometry. |
| `IsAnimated` | True when a signal, source, mask, or output reads a value that `RecipeGraph::MayChangeOverTime` marks as changing. |

### Diagnostics and resolution (`Recipe.h`)

Every load, check, and resolution step reports through one record. A
`Diagnostic`'s `where` names the row, for example `signal glowLevel` or
`output 2 layer 0`. A malformed row becomes a diagnostic and an inert row.

| Type | Description |
|---|---|
| `Diagnostic` | A `Severity` (warning or error), the `where` row name, and the message. `MakeDiagnostic` builds one. `RowLevel`, `HasErrors`, `HasRecipeErrors`, and `ProblemText` read them. |
| `Reporter` | The sink that a parse or check writes through. `At` returns a child reporter for one row. |
| `LoadResult` | What `ParseRecipe` returns: an optional `Recipe`, all diagnostics, and the `inputDiagnostics` from decoding. |
| `WornPiece` | What resolution sees of one worn piece: magic effects, enchantment, effect shaders, armor, keywords, and diffuse paths. |
| `ResolvedRecipe` | One match from `Resolve`: the recipe, the strongest matching `RecipeKey`, the placement priority, and the load order. |
| `SelectionOutcome` / `RecipeSelection` | The optional per-recipe report of `Resolve`: nonmatching, fallback-suppressed, sampled-out, or selected. |
| `PieceKey` | One key choice that a piece offers (`KeyChoicesOf`). `RecipeKeyOf` turns the chosen one into a `RecipeKey`. |
| `Precedence` | Priority, then load order, compared as one value (`Precedence.h`). `Merge.cpp`, `Resolve.cpp`, `planners/BindingPlan.cpp`, and `studio/Selection.cpp` order recipes by it. |

### The recipe graph (`RecipeGraph.h`, `GraphOperations.h`, `RecipeCompilation.h`)

`RecipeGraph::Compile` turns a recipe into a list of **operation nodes**. Each
node holds its input references as `OutputRef`s and declares typed outputs.
Curves become `FunctionDefinition`s with the parameters `x` and `mean`. The
graph derives dependency order, per-tick order, change over time, and sample
dependence from these connections.

| Type | Description |
|---|---|
| `RecipeGraph` | The nodes, the function definitions, the `OutputBinding`s, the name indexes, the derived orders, and the diagnostics. `TickOrder`, `ChangingTickOrder`, `SampleDependent`, and `MayChangeOverTime` read the analysis. |
| `NodeId` / `FunctionId` | Indexes into the graph's nodes and functions. |
| `OutputRef` | One output port: a `NodeId` and an output index. |
| `NodeOutput` | A port's name and its `GraphValueType`. |
| `GraphValueType` | A `ValueType` or a `ResourceType` (firings, count, events, texture, material, geometry, transform, effectShader). |
| `RecipeNode` | A `displayName`, a `NodeKind`, and the `outputs`. |
| `OutputBinding` | A property name, such as `output 0 layer 1 opacity` or `shell spin`, and the `OutputRef` that feeds it. The render layer reads these. |
| `ExternalInput` | A node that brings a value in from outside: time, delta time, sample UV, root transform, geometry, material, texture, effect shader, events, node position, actor value, actor state, or enchantment (`ExternalSource`). |
| `ConstantOperation`, `VectorOperation`, `ParameterOperation` | A constant value, a vector built from components, and a function parameter. |
| `ExpressionOperation` | A `BoundExpression`. |
| `BoundExpression` | A compiled `Program`, its `valueBindings` to `OutputRef`s, and its `BoundFunction`s. |
| `BoundFunction` / `BoundFunctionArgument` | A called `FunctionId`, the parameter it samples, and the other arguments bound to graph outputs. |
| `CallOperation` / `MapFunctionOperation` | A function call on a signal value, and a function applied to each component of a layer value. |
| `WaveOperation` … `SmoothOperation` | One operation per signal kind: wave, ramp, effect shader, noise, gradient, toRoot, trigger, hold (payload), counter, accumulate, rate, and smooth. |
| `TextureCoordinatesOperation`, `ImageOperation`, `MaterialOperation`, `BakeOperation`, `DistanceOperation`, `RippleOperation`, `MaterialClustersOperation` | One operation per source kind, and the texture coordinates an image reads. |
| `ReductionOperation` | A `ReductionKind` (mean, sum, minimum, maximum) over a numeric value. Lowering uses the mean for an image mean and for a layer curve's `mean`. |
| `FunctionDefinition` / `FunctionParameter` | A curve as a small node list with typed parameters and a result port. |
| `detail::RecipeDeclaration` | One authored row during compilation: its definition, `DeclarationCategory` (signal, spatial, function), dependencies, parsed expression, and disabled flag. |

### Signal evaluation (`Signals.h`)

`SignalState` holds one actor's values for one graph and runs the graph each
tick. It reads the engine only through `SignalEnvironment`. `RowTypes` pairs a
recipe with its graph for the `Check*` and `*TypeOf` functions.

| Type | Description |
|---|---|
| `SignalState` | The per-actor values. `Tick` runs the tick order, `Fire` gives an `EventRecord` to the triggers, and `Resolve` reads a `Param` against the current values. It allocates state only for nodes that `Stateful` marks. |
| `TickInputs` | One tick's time and delta. |
| `SignalEnvironment` | The engine questions that a tick asks: `ActorValue`, `ActorState`, `ActorVector`, `WorldToRoot`, `Enchantment`, and `EffectShader`. `NullEnvironment` answers zero for native tests. |
| `EventRecord` / `TriggerPayload` | An event id, its payload value, node, and argument, and whether a plugin sent it. |
| `TriggerFiring` | One kept firing: its start time and payload. |
| `FiringAnchor` | Where a firing's location resolves: nowhere, a `CarriedPoint`, or an `AnchorNode`. |
| `RowTypes` | A recipe and its graph. |

### The plan (`Merge.h`)

Several recipes can land on one piece. The plan is the pure merge of their
outputs. `PlanGeometry` and `PlanLights` take the `PlacedRecipe`s and decide
replacement and scalar ownership.

| Type | Description |
|---|---|
| `PlacedRecipe` | One recipe on the piece: the recipe, its priority, its load order, and the selected output indexes. |
| `SlotContribution` / `LightContribution` | One output's claim: a `SlotContributor` or `LightContributor` index and an output index. |
| `ScalarOwner` | The contribution that sets one `ScalarField`. |
| `SlotPlan` | One slot's result: the `chain` of contributions, the ones `replaced`, the `replacer`, and the `ScalarOwner`s. |
| `GeometryPlan` | The `SlotPlan`s for one piece. |
| `LightPlan` | The lights `shown`, the ones `replaced`, and the `replacer`. |

### The expression language (`Expression.h`)

An `ExprSignal`, a mask, and a curve share one grammar. `Program::Parse`
compiles the text once. `Program::Evaluate` runs the compiled ops within
`kMaxExpressionLength`, `kMaxExpressionDepth`, and `kMaxExpressionOps`.

| Type | Description |
|---|---|
| `Program` | A compiled expression: an op list (`Program::Op`, `Program::Node`), the row names it reads, the curves it calls, and its use of `time`, `x`, and `mean`. `Check` types it against a `RefTyper`. `BindContext` turns `time`, `x`, and `mean` into ordinary reference slots. |
| `NumericLiteral` / `NumericLiteralSelection` | One number's position and value in the text. `ReplaceNumericLiteral` edits one for studio tuning. |
| `ExpressionRename` | A from/to pair for `RenameInExpression`. |

### Import (`Importer.h`, `Efsh.h`)

The importer turns a vanilla effect shader into a starting recipe. It patches
a template recipe from `templates/fill.json` or `templates/bare.json`, chosen
by `ImportTemplateId`.

| Symbol | Description |
|---|---|
| `EffectShaderRecord` | The form key, editor ID, fill texture, `Efsh::EffectParams`, and tiling that `ParseEffectShaderRecord` reads. |
| `kFillTextureToken` | The `$fillTexture` text that the importer replaces with the record's texture. |
| `kImportTemplateIds` | The template ids `fill` and `bare`. |
| `Efsh::EffectParams` / `Efsh::AlphaParams` | The vanilla fill and edge colors, alpha timing, and scroll speeds. |
| `Efsh::FillState` | The color, alpha, and scroll that `Efsh::Evaluate` computes for one moment. |

### Bounds (`Recipe.h`, `Expression.h`)

These constants cap every list and every nested walk.

| Constant | Value | What it caps |
|---|---|---|
| `kRecipeFormat` | 1 | The one supported file format. |
| `kMaxRecipeRows` | 4096 | Every row list. The graph compiler also caps named rows and inline curves at 4 times this value, and lowered nodes at 16 times. |
| `kMaxRecipeDepth` | 32 | JSON nesting and the depth of a dependency path. |
| `kMaxExpressionLength` / `kMaxExpressionDepth` / `kMaxExpressionOps` | 4096 / 32 / 256 | Expression text, parse depth, and op count. |
| `kMaxMaterialClusters` / `kMaxClusterIterations` / `kMaxChannelWeight` | 8 / 256 / 10 | Material cluster requests. |

## How a recipe flows

```
recipe.json (text)
  │  ParseObjectDocument: depth check, duplicate keys        Binders.cpp
  ▼
json ──Reader binders──▶ Recipe                              RecipeRead.cpp
  │     Reporter raises Diagnostics, where = the row name
  ▼
Recipe ──Resolve(WornPiece)──▶ ResolvedRecipe                Resolve.cpp
  │
  ├─ RecipeGraph::Compile                                     RecipeGraph.cpp
  │    CompilationLimitProblem → Register → AppliedFunctions
  │    → ParseDefinitions (Program::Parse)                    Expression.cpp
  │    → BindDependencies → Order → Analyze
  │    LowerRecipeGraph: Functions → Nodes → Analyze          RecipeGraphLowering.cpp
  │  ▼
  │  RecipeGraph ──SignalState::Tick──▶ values per tick       Signals.cpp
  │    Program::Evaluate for ExpressionOperation              Expression.cpp
  │    EvaluateFunction for a curve                           FunctionExecution.cpp
  │  Validate: RowTypes → CheckCurve/CheckSource/CheckMask/CheckOutput  Signals.cpp
  │
  └─ PlacedRecipe[] ──PlanGeometry/PlanLights──▶ GeometryPlan, LightPlan   Merge.cpp

Recipe ──Writer binders──▶ json (text)                       RecipeWrite.cpp
EffectShaderRecord + template Recipe ──ImportEffectShader──▶ Recipe   Importer.cpp
```

## The files

| Concern | Files | What they own |
|---|---|---|
| Model and words | `Recipe.h`, `Words.h`, `Vocabulary.cpp`, `Recipe.cpp` | The model, the enums and their spec tables with `Complete` asserts, the word-to-enum functions, `IsAnimated`, the sharing predicates, the `*Where` builders, and the diagnostic predicates. |
| JSON boundary | `Binders.h`, `Binders.cpp`, `RecipeRead.cpp`, `RecipeWrite.cpp` | `Reader`, `Writer`, `ParseObjectDocument`, `FloatFrom`, `ParseRecipe`, and `SerializeRecipe`. |
| Field checks | `Validation.cpp` | `CheckRecipeFields`: finite numbers, numeric domains, list sizes, selectors, bones, shell, and variant fields. |
| Resolution | `Resolve.cpp`, `Variants.cpp`, `Precedence.h`, `DefinitionOrder.h` | `Resolve`, `GlobMatch`, `Matches`, `SamplingHash`, `KeyChoicesOf`, `VariantApplies`, `ApplyVariant`, `Precedence`, and `AppendDefinition`, which puts a redefined id at its new position. |
| Graph compilation | `RecipeGraph.h`, `RecipeGraph.cpp`, `RecipeCompilation.h`, `RecipeGraphLowering.cpp`, `RecipeGraphAccess.cpp` | `RecipeGraph::Compile` and its `RecipeGraphBuilder` phases, `RecipeGraphLowering`, and the graph's read accessors. |
| Graph operations | `GraphOperations.h`, `GraphOperations.cpp`, `Reduction.h`, `FunctionExecution.cpp` | The operation node types, `InputsOf`, `Stateful`, `FunctionResultTypeOf`, `ReductionKind`, and `EvaluateFunction`. |
| Evaluation and checks | `Signals.h`, `Signals.cpp` | `SignalState`, `SignalEnvironment`, `Validate`, and the `Check*` and `*TypeOf` functions. `CheckSource` compiles a changed candidate row before it compares diagnostics. |
| Expressions | `Expression.h`, `Expression.cpp` | `Program`, the parser, the type check, `ReplaceNumericLiteral`, `RenameInExpression`, and `ExpressionSummary`. |
| Merge | `Merge.h`, `Merge.cpp` | `PlanGeometry`, `PlanLights`, and the plan lookups. |
| Import | `Importer.h`, `Importer.cpp`, `Efsh.h`, `Efsh.cpp` | `ParseEffectShaderRecord`, `ImportEffectShader`, `ImportSignalNames`, and the vanilla fill model that an `EfshSignal` reads. |
| Editor walks | `Visit.h` | `ForEachParam`, `Visit*Params`, `LocatedVisitor`, and `PropertyLocation`, which the studio and menu use to walk a recipe's refs and params. |

## See also

- `REFERENCE.md` → *The recipe model*, *The recipe language*, *Recipe format
  details beyond the schema*, and *Unified recipe graph and signal execution*.
- `docs/conventions.md` → *Diagnostics*, *The JSON boundary*, *Variants and
  closed sets*, and *Multi-phase algorithms → Bounds*.
- [The recipe resolution contract](../recipe-resolution.md) for identity
  overrides, sampling, priority, and replacement.
- [The operation graph checkpoint](../checkpoints/operation-graph-2026-09-26.md)
  and [the render-plan model](../plans/render-plan-model.md).
- `schema/recipe.schema.json` and `schema/example-magicka.json` for the file
  format.
