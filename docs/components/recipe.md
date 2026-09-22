# recipe/

The pure core. It turns a **recipe** file into a checked `Recipe`, resolves
the forms the recipe names, builds the per-tick `SignalGraph`, merges the
recipes that land on one **piece** into a **plan**, and writes a `Recipe`
back to JSON. It is engine-free: it compiles natively and is unit-tested
through `tests/run-native.sh`. Every layer above depends on it; it depends
only on `Core.h`.

## What it owns

Recipes and everything a recipe declares:

- **signals** — values that vary per tick;
- **sources** — values that vary per texel;
- **masks**;
- the **layer** stacks written into material **slots**;
- the **shell** and **light** outputs;
- the **diagnostics** raised against any of them.

It also owns the recipe language (the expression and curve grammar a `Ref`
parses to) and the importer that turns a vanilla effect shader into a
starting recipe by patching one of the shipped import templates.

## Data

Each header declares its data first.

### The model (`Recipe.h`)

`Recipe` is the whole file in memory. `RecipeRead.cpp` fills it one checked
field at a time, and `RecipeWrite.cpp` writes it back with key order kept, so
a file survives a load and a save unchanged. Every consumer (the studio,
`Resolve`, `SignalGraph::Compile`, `Merge.cpp`) reads this one struct.

| Type | Description |
|---|---|
| `Recipe` | The **recipe**: an id, `Metadata`, its `RecipeKey`s, an optional priority, a `MergeMode`, a `Clock`, the **row** lists (`Signal`s, `Curve`s, `Source`s, `Mask`s, `Output`s, `Variant`s), and `ShellSettings`. `FindSignal`/`FindCurve`/`FindSource`/`FindMask` look a row up by name. |
| `Metadata` | The recipe's name, author, description, and version, the importer's `imported` stamp, and a free-form `meta` field kept verbatim. |
| `MergeMode` | How the recipe combines with lower-priority recipes contesting the same piece — the wire field `merge`: stack appends per slot, replace drops lower-priority work on its slots, sampled gives each actor one member of the pool by form id. |
| `RecipeKey` | Which worn **piece** the recipe applies to: a `KeyKind` (default, enchanted, material, keyword, armor, effectShader, enchantment, magicEffect) with a form or glob operand; default and enchanted stand alone. |
| `Selector` | Which geometry an **output** or **variant** touches: any-of clauses over addon, geometry name, or texture path; empty matches every geometry. |
| `Signal` | One named per-tick value: a name, a `SignalKind`, an optional `CurveRef` that shapes the result, and an optional author `note`. |
| `Curve` | A named expression in the free variable `x`, with an optional author `note`; a `CurveRef` applies it to a **signal** or a **layer**. In a file the value is a bare expression string, or `{ "expr", "note" }` when the author annotates it. |
| `Source` | One named per-texel value: a name, a `SourceKind`, and an optional author `note`. |
| `Mask` | A named per-texel expression, with an optional author `note`; **source** and **mask** names stand for images in it, signal names for the tick's scalars. In a file the value is a bare expression string, or `{ "expr", "note" }` when annotated. |
| `Output` | `SurfaceOutput` or `LightOutput`; the Outputs group below details both. |
| `ShellSettings` | The **shell**: its `ShellMaterial` (pbrCopy or vanilla), `ShellBlend` (additive or alpha), depth bias, alpha test, the opacity/rim/emissive `Param`s, and a `ShellPose` (inflate, offset, scale, spin). |
| `Variant` | A named override set: a `VariantKey` (a form or a `Selector`) decides when it applies, and its `overrides` map sets row values by name; `ApplyVariant` folds the match in. |
| `Clock` | The recipe's time scale: `speed` multiplies the tick clock. |

### Values (`Recipe.h`; `Ref` in `Core.h`)

A field a recipe can animate is a `Param`. A literal number stays fixed; a
`Ref` reads a signal, and `SignalState::Resolve` makes that read each tick.
`CurveRef` and `FormRef` are the other handle types a row stores as text.

| Type | Description |
|---|---|
| `Param` | `float \| Ref`: a fixed number or a signal reference (`@name` on the wire). |
| `Vec2Param` / `Vec3Param` | An array of `Param`s, one per component, or one `Ref` to a vector signal. |
| `Ref` | The `@name` handle, declared in `Core.h`: the name of another row. |
| `CurveRef` | Text that names a declared **curve** or holds an inline expression in `x`; `Named()` tells the two apart. |
| `FormRef` | A form named as text (an editor ID, or `0x<local id>~<plugin file>` parsed by `FormKey::Parse`); resolution fills its `FormKey`. |

### Signal kinds (`Recipe.h`)

`SignalKind` is the variant over the seventeen kinds below; a signal produces
one `Value` per tick when `SignalState::Tick` evaluates its node.
`SignalKindId` names each alternative, and `SignalKindSpec` (a table in
`Words.h`) carries the wire word and whether the studio can tune the kind
live; a `Complete` static_assert keeps the table total.

| Alternative | Wire word | Produces |
|---|---|---|
| `ConstantSignal` | `constant` | A fixed `Value`; with `expr`, one of the two kinds the studio marks tunable. |
| `WaveSignal` | `wave` | `base` plus `amplitude` times a repeating waveform (sine, triangle, square, or saw) with `period` and `phase`. |
| `RampSignal` | `ramp` | A value that moves from `from` to `to` over the clock's first `seconds`, then holds `to`. |
| `EfshSignal` | `efsh` | One field of a vanilla effect shader record (fillAlpha, fillColor, edgeAlpha, edgeColor, or scroll), read through `SignalEnvironment::EffectShader`. |
| `ActorValueSignal` | `av` | The wearer's actor value under one `Measure`: current, base, permanent, temporaryModifier, damage, or max. |
| `ActorStateSignal` | `actorState` | One actor fact by selector: seven 0/1 flags (inCombat, sneaking, weaponDrawn, swimming, sprinting, mounted, hasTarget), the scalar movementSpeed, and the vec3 world positions position and target. |
| `EnchantmentSignal` | `enchantment` | The matched enchantment's magnitude or cost. |
| `TriggerSignal` | `trigger` | The newest firing's age as a fraction of `lifetime`: 0 at the firing, 1 once it expires or when none is live. The `TriggerOrigin` is an event glob, another plugin's message id, or a `when` expression edge; at most `max` firings are kept. The row declares its firings' `payload` type (scalar, vec2, or vec3; a firing of another type is dropped and counted) and its `TriggerAnchor` — the space a firing's location resolves in: the world-space payload, a named skeleton node, or none. |
| `PayloadSignal` | `payload` | The named trigger's newest firing value, typed by the trigger's declared payload, held between firings. |
| `CounterSignal` | `counter` | A count of a trigger's firings, cleared by an optional `reset` trigger and limited by an optional `cap`. |
| `AccumulateSignal` | `accumulate` | A total that each firing raises by one and `decay` drains per second, floored at zero. |
| `NoiseSignal` | `noise` | Value noise over time at `frequency`, scaled by `amplitude`, repeatable per `seed`. |
| `GradientSignal` | `gradient` | A color: `t` sampled against the `GradientStop` list, interpolated between the two nearest stops. |
| `RateSignal` | `rate` | The named signal's change per second. |
| `SmoothSignal` | `smooth` | The named signal eased toward its current value by exponential smoothing with time constant `seconds`. |
| `ToRootSignal` | `toRoot` | The named vec3 signal, a world-space point, expressed in the wearer's root space through `SignalEnvironment::WorldToRoot`. |
| `ExprSignal` | `expr` | The value of an expression over other rows; the graph parses it to a `Program`, and the studio tunes its numeric literals. |

### Source kinds (`Recipe.h`)

`SourceKind` is the variant over the six kinds below; a source produces one
value per texel when the render layer rasterises it. `SourceKindId` mirrors
the alternative order, and a static_assert in `Words.h` keeps the wire-word
table aligned with the variant. `CheckSource` (declared in `Signals.h`)
validates a source's params against the graph.

| Alternative | Wire word | Produces |
|---|---|---|
| `ImageSource` | `image` | A texture under `Data/Textures` sampled per texel: one `ImageChannel` (rgb, r, g, b, a, or luma) in tiled or mesh `ImageSpace`, with optional scroll, tile, mirror, transpose, and mip. |
| `MaterialSource` | `material` | One read of the piece's own material: the raw vec3s diffuseRgb, normalRgb, rmaosRgb; the named scalars diffuseLuma, roughness, metallic, occlusion, reflectance, displacement; and the computed normalSlope and relief. |
| `BakeSource` | `bake` | A value baked from the mesh once per geometry; the `BakeKind` is position, localPosition, normal (the bind-pose surface normal, each axis as 0..1), uv (the coordinates as a vec2), partition (one biped slot), boneWeight (named bones), componentId, or chartId (the mesh analysis' id map, each texel the region id / 255). |
| `DistanceSource` | `distance` | The texel's distance from a named skeleton node. |
| `RippleSource` | `ripple` | A ring or disc that spreads from a trigger firing's anchor at `speed`, `width` wide, fading at `decay`; an unanchored trigger's rings spread from the geometry's origin. An optional `direction` vec3 turns the radial front into a plane sweeping along the vector, and is animatable like the other params. |
| `MaterialClustersSource` | `materialClusters` | The material's cluster map: each texel the id / 255 of its nearest k-means cluster under the channel weights, `seed`, and iteration cap, rendered once per geometry. |

### Outputs (`Recipe.h`)

An **output** is what a recipe writes: a `SurfaceOutput` stacks **layer**s
into one material or shell **slot**, and a `LightOutput` describes one
**light**. `Merge.cpp` composes the `LightOutput`s across recipes into a
`LightPlan`; the schema allows at most one light output per recipe.

| Type | Description |
|---|---|
| `SurfaceOutput` | One slot write: a `Surface` (material or shell), a `Slot`, its `SlotScalars`, a `Selector`, a replace flag that drops lower-priority recipes' work on the slot, an optional `Resolution` that overrides the slot's default target size, the layer `stack`, and an optional author `note`. |
| `Layer` | One entry in a `stack`: a `LayerSource`, an optional `CurveRef`, a `Blend`, an opacity `Param`, an optional color and mask, the `ChannelSet` it writes, and an optional author `note`. |
| `LayerSource` | `Ref \| Vec3`: a source or mask by name, or a constant color. |
| `Blend` | How a layer combines with the stack below: replace, multiply, add, subtract, screen, or reorient; `BlendSpec` maps each to its shader mode and marks `reorient` (normal-map reorientation) as normal-stack only. |
| `SlotScalars` | The per-slot scalar `Param`s (strength, scale, color, weight, and the rest of `ScalarField`); `SlotSpec` says which fields a slot takes and which are required. |
| `Slot` | The nine writable slots: diffuse, emissive, rmaos, normal, height, fuzz, glint, coat, subsurface. |
| `LightOutput` | One light: `Bones` placement (skinned or named), offset, color, intensity, size, cutoff, a shadow flag, a `Selector`, a replace flag, and an optional author `note`. |

### Texture size (`Recipe.h`)

A **surface output**'s stack renders into a runtime **target** sized per
**slot**. `Resolution` scales that size down to save texture memory.
`DefaultSlotResolution` gives each slot its default; a `SurfaceOutput`'s
optional `resolution` overrides that default for the one output. The render
layer and `engine/` size the target from the choice; `ResolutionDivisor`
turns it into the number the full size divides by.

| Type | Description |
|---|---|
| `Resolution` | The three target sizes: `kFull`, `kHalf`, `kQuarter`. `ResolutionName`/`ParseResolution` map each to its wire word (`full`, `half`, `quarter`) through the `kResolutions` table in `Words.h`. |
| `DefaultSlotResolution(Slot)` | The slot's default size: full for normal and height, half for diffuse and rmaos, quarter for emissive, fuzz, glint, coat, and subsurface. |
| `ResolutionDivisor(Resolution)` | The divisor the resolution applies to the slot's full size: 1, 2, or 4. |

### Cross-actor sharing (`Recipe.h`; `Vocabulary.cpp`)

Two actors in the same armour can share one rendered **target** only when
that target's inputs do not vary by actor. These predicates are the
correctness gate for that reuse; the render **compositor** reads them to
decide whether one actor's target can serve another. A shareable input is
static (not `IsAnimated`) and reads no per-actor geometry.

| Function | Description |
|---|---|
| `ShareableAcrossActors(const Recipe&, const Output&)` | True when the output is a `SurfaceOutput`, is not `IsAnimated`, and `RecipeInputsAreActorIndependent` holds. |
| `ShareableAcrossActors(const Recipe&, const Mask&)` | True when the mask is not `IsAnimated` and `RecipeInputsAreActorIndependent` holds. |
| `RecipeInputsAreActorIndependent(const Recipe&)` | True when no source in the recipe is a `BakeSource` or `DistanceSource`; those two read per-actor geometry. |

### Diagnostics (`Recipe.h`)

Every load, validation, and resolution step reports through the same record.
A `Diagnostic`'s `where` names the row (`signal glowLevel`, `output 2 layer
0`), so the menu can show the problem in place. A malformed row becomes a
diagnostic and an inert row, never a crash.

| Type | Description |
|---|---|
| `Diagnostic{Severity, where, message}` | One problem: warning or error, the row it names, and the message. `MakeDiagnostic` builds it; `RowLevel`/`HasErrors`/`ProblemText` read collections of it. |
| `Reporter` | The shared sink a parse or check writes through; `At(where)` scopes a child reporter to one row. |
| `LoadResult` | What `ParseRecipe` returns: an optional `Recipe` plus its diagnostics. |
| `ResolvedRecipe` | One match from `Resolve`: the recipe, the `RecipeKey` that matched, and the effective priority. |
| `WornPiece` | What one worn piece looks like to resolution: its magic effect, enchantment, effect shader, armor, keywords, and diffuse paths. |
| `PieceKey` | One key choice a piece offers (`KeyChoicesOf`); the studio turns the chosen one into a `RecipeKey`. |

### Runtime (`Signals.h`)

`SignalGraph::Compile` turns a recipe's signals and curves into an ordered
node list once, at load. `SignalState` holds the per-actor values and
evaluates the graph each tick against a `SignalEnvironment`. The `Check*`
functions validate curves, sources, masks, and outputs against the graph's
types.

| Type | Description |
|---|---|
| `SignalGraph` | The compiled graph: nodes in dependency order, each with a type and an inert flag; a bad row goes inert and raises a diagnostic instead of failing the recipe. |
| `SignalState` | The per-actor evaluation state: `Tick` computes every node's `Value`, `Fire` feeds an `EventRecord` to the triggers, and `Resolve` reads a `Param` against the current values. |
| `TickInputs` | One tick's clock: the time and the delta. |
| `SignalEnvironment` | The engine questions a tick asks: actor values, actor states (scalar `ActorState` and vec3 `ActorVector`), world-to-root conversion, enchantment fields, effect-shader records. `NullEnvironment` answers zero for native tests. |
| `RowTypes` | A recipe paired with its graph; the `*TypeOf` and `Check*` functions take it. |

### The plan (`Merge.h`)

Several recipes can land on one **piece**; the **plan** is the pure merge of
their outputs. `PlanGeometry` and `PlanLights` take the `PlacedRecipe`s and
return what to render, with replacement and scalar ownership already
decided.

| Type | Description |
|---|---|
| `PlacedRecipe` | One recipe on the piece: the recipe, its priority, and the indices of the outputs that apply. |
| `SlotContribution` / `LightContribution` | One output's claim: a placed-recipe index plus an output index. |
| `SlotPlan` | One slot's outcome: the blend `chain`, the contributions a `replacer` displaced, and the `ScalarOwner` per scalar field. |
| `GeometryPlan` | The `SlotPlan`s for one piece. |
| `LightPlan` | The lights to show, the ones replaced, and the replacer. |

### The language (`Expression.h`)

An `ExprSignal`, a **mask**, and a **curve** share one grammar. `Program`
compiles the text once and runs the compiled ops per tick or per texel,
inside explicit bounds (`kMaxExpressionLength`, `kMaxExpressionDepth`,
`kMaxExpressionOps`).

| Type | Description |
|---|---|
| `Program` | The compiled expression: `Parse` builds the op list, `Check` types it against a `RefTyper`, and `Evaluate` runs it over its `Inputs` (refs, curves, time, `x`, mean). |
| `NumericLiteral` | One number's offset, length, and value in the source text; the studio's tuning edits it through `ReplaceNumericLiteral`. |
| `ExpressionRename` | A from/to pair for `RenameInExpression`, which rewrites row names inside expression text. |

## How a recipe flows

```
recipe.json (text)
  │  ParseObjectDocument                     Binders.cpp  (depth-checked, dup-key)
  ▼
  json  ──Reader binders──▶  Recipe          RecipeRead.cpp  (each field checked)
  │        raises Diagnostics through a Reporter, where = the row it names
  ▼
Recipe  ──Resolve──▶  ResolvedRecipe         Resolve.cpp  (FormKeys matched to the piece)
  │
  ├── SignalGraph::Compile ─▶ evaluate per tick   Signals.cpp
  │       Ref ─▶ Program::Parse / Evaluate      Expression.cpp
  │
  └── PlacedRecipe ─▶ SlotPlan/GeometryPlan/LightPlan   Merge.cpp
                       (one piece, every recipe on it)

Recipe  ──Writer binders──▶  json (text)      RecipeWrite.cpp   (round-trip, key order kept)
Vanilla EFSH ──ParseEffectShaderRecord ─▶ EffectShaderRecord ─┐
templates/{fill,bare}.json ──ParseRecipe─▶ template Recipe ───┴─ImportEffectShader─▶ Recipe   Importer.cpp
```

## The files

| File | What it owns |
|---|---|
| `Recipe.h` | The whole model, its enums, the `Parse*` word-to-enum functions, the `Diagnostic`/`Reporter` declarations. |
| `Recipe.cpp` | `MakeDiagnostic`/`DiagnosticOf`, the `*Where` row-name helpers, `RowLevel`/`HasErrors`, `ProblemText`, `Recipe::Find*`. |
| `RecipeRead.cpp` / `RecipeWrite.cpp` | JSON to `Recipe` and back, one field at a time. |
| `Binders.h` / `Binders.cpp` | `Reader`/`Writer` and `ParseObjectDocument`: the JSON binder vocabulary the recipe reader and writer build on. |
| `Resolve.cpp` | `Recipe` to `ResolvedRecipe`: match the recipe's `FormKey`s against a `WornPiece`. |
| `Variants.cpp` | `VariantApplies` and `ApplyVariant`: pick the matching **variant** and fold it in. |
| `Signals.h` / `Signals.cpp` | `SignalGraph`, the per-tick evaluation, the `CheckSource`/`CheckLayer` validation. |
| `Expression.h` / `Expression.cpp` | The `Ref` expression and curve language: `Program::Parse`/`Evaluate`. |
| `Merge.h` / `Merge.cpp` | Compose the recipes on one piece into a slot, geometry, and light plan. |
| `Importer.h` / `Importer.cpp` | Vanilla effect shader to starting `Recipe`: the `EffectShaderRecord` reader, the reserved `import*` fact table, and the patcher over a template recipe. |
| `Visit.h` | `LocatedVisitor`, `Visit*Params`: walk a recipe's refs and params in place. The editor uses it. |
| `Words.h` | The enum-to-word tables and the `Complete()` static_asserts that keep them total. |
| `Vocabulary.cpp` | The enum-to-word functions over those tables (`FormKey::Parse`, `SignalKindName`, and the rest). |
| `Efsh.h` / `Efsh.cpp` | The vanilla effect-shader fill model an `EfshSignal` reads. |

## See also

- `REFERENCE.md` → *The recipe model*, *The recipe language*, *Recipe format
  details beyond the schema* — the facts these types cannot state: packings,
  format history, the reasons behind constants.
- `docs/conventions.md` → *Diagnostics* and *The JSON boundary* — the
  `Reporter` and `Reader`/`Writer` contracts this module defines and every
  layer reuses.
- `schema/recipe.schema.json` and `schema/example-magicka.json` — the format
  contract these types parse.
