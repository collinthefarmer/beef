Status: active (2026-10-14) on branch `pure-tick`. The user decided D1–D5 on 2026-10-14: rename the keys, **material group** (and **part**), per-geometry item facts, the pick order and `minCover` 0.05 as recommended. Stages 0 and 1 are done; option (ii) of stage 1 is done: item signals are per geometry in masks, sources and layer parameters, and refused in output scalars, shell and light parameters. It covers stage 3's `geometryIndex` and `geometryCount`. Stage 2 (the `mesh` and `material.groups` keys) is done. Stage 4 (`pick`) is built and native-tested; its draw is not yet checked in game. `seed` is a required parameter, not the piece identity (see REFERENCE, Area pick). Stage 5 is done: the fill template lights `@glowDepth × @lit` from one `pick` (one geometry, `charts` from the magnitude-scaled `chartLit`, seed `frac(rand() + @itemHash)`).

# Source families, item facts and the nested pick

## Context

A recipe reads per-texel values from **sources** and single values from **signals**. The recipe format names seven source kinds and seventeen signal kinds in flat lists, and the studio offers them in those flat lists.

| Source kind | Variants today |
|---|---|
| `image` | a file or an effect-shader texture |
| `material` | 11 channels: `diffuseRgb`, `diffuseLuma`, `normalRgb`, `normalSlope`, `roughness`, `metallic`, `occlusion`, `reflectance`, `rmaosRgb`, `displacement`, `relief` |
| `bake` | `position`, `localPosition`, `normal`, `uv`, `partition`, `boneWeight`, `componentId`, `chartId`, `geometry` |
| `distance` | the distance to a node's bind-pose position |
| `ripple` | travelling bands from trigger firings |
| `materialClusters` | the material's groups (`merge`, `smoothing`, `pick`, `areaBias`, `palette`) |
| `piece` | `itemHash`, `chartCount` |

The signal kinds are `constant`, `wave`, `ramp`, `efsh`, `av`, `actorState`, `enchantment`, `trigger`, `payload`, `counter`, `accumulate`, `noise`, `gradient`, `rate`, `smooth`, `toRoot` and `expr`.

The studio groups the same things three other ways: the flat kind choice, the paint term offers (parts, charts, materials, bones, partitions, channels, presets, masks, sources) and the resource tabs (signals, curves, sources, masks). One thing has several names: part, `componentId` and component; cluster, group and "materials"; chart and `chartId`.

The import template (`templates/fill.json`) lights `cavities × chartPick × materialPick`. The chart pick and the material pick are independent, so their product can be empty.

## Findings

| # | Finding |
|---|---|
| F1 | `bake` holds three kinds of answer: where a point is (`position`, `localPosition`, `normal`), where it sits on the texture (`uv`) and which part it belongs to (`partition`, `boneWeight`, `componentId`, `chartId`, `geometry`). |
| F2 | Material groups come from the material's own maps but are a kind of their own. |
| F3 | `piece` holds numbers that are constant for a geometry, among the per-texel sources. |
| F4 | A recipe cannot tell which of a piece's geometries it draws, or how many the piece has. |
| F5 | Independent picks multiplied together can select nothing. |

## Families

Families group by **origin**: where the data comes from. Whether a member is a source or a signal follows from its shape.

| Family | Shape | Members |
|---|---|---|
| **material** | source | the material channels; derived: relief, normal slope, material groups |
| **mesh** | source | position, localPosition, normal, uv, partition, bone weight, part, chart, geometry |
| **image** | source | image files, effect-shader textures |
| **special** | source | distance, ripple, pick (new) |
| **item** | signal | itemHash, chartCount, geometryIndex (new), geometryCount (new) |
| **enchantment** | signal | `enchantment` (magnitude, cost, idHash, effectHash), `efsh` |
| **actor** | signal | `av`, `actorState` |
| **events and time** | signal | `trigger`, `payload`, `counter`, `wave`, `ramp`, `noise`, `gradient` |
| **operators** | signal | `accumulate`, `rate`, `smooth`, `toRoot` |
| **authored** | signal | `constant`, `expr` |

`distance` is a mesh fact by origin, because it measures to a bind-pose position. It sits in **special** beside `ripple`, because the two read as one kind of thing.

## Format changes

| Today | After |
|---|---|
| `{"bake": "uv"}` and the other bakes | `{"mesh": "uv"}` |
| `{"bake": "componentId"}` | `{"mesh": "part"}` |
| `{"bake": "chartId"}` | `{"mesh": "chart"}` |
| `{"materialClusters": {…}}` | `{"material": {"groups": {…}}}` |
| `{"piece": "itemHash"}` as a source | `{"item": "itemHash"}` as a signal |
| none | `{"item": "geometryIndex"}`, `{"item": "geometryCount"}` |
| none | `{"pick": {…}}`, a special source |

`distance`, `ripple` and `image` keep their keys. A recipe with a retired key fails validation and names the new key. The repo's recipes (`recipes/examples/*.json`, `templates/*.json`, `tests/fixtures`) and the MO2 debug recipe are rewritten in the same change.

## Item facts are per geometry

`itemHash` and `chartCount` are constant for a geometry, not for a recipe instance. A recipe instance spans every geometry of its piece, and the signal state holds one value per instance. Today the two facts are `ExternalInput`s that the render path binds per geometry (`NumberValueOf`, `render/RenderInstance.cpp`).

Moving them into the signal section changes where they are written, not where they are evaluated. A signal that reads an item fact is per geometry, like a mask. Stage 1 checks whether the graph already evaluates such a row in the render path, as it does the masks (`DeclarationCategory::kSpatial`). If it does not, an item fact is readable from masks and sources only, and validation says so.

## Stage 1 result (2026-10-14)

The recipe graph has three evaluation domains (`DeclarationCategory`, `recipe/RecipeCompilation.h`). The domain comes from the recipe section a row is in (`RecipeGraph.cpp:767-770`).

| Domain | Rows | Evaluated | May read |
|---|---|---|---|
| signal | `signals` | once per recipe instance, on the CPU | signals |
| spatial | `sources`, `masks` | per geometry, in the render path | signals and spatial rows |
| function | `curves` | where called | — |

Checked with `beef-validate` on three recipes:

| Recipe | Result |
|---|---|
| a signal `expr` reads `@charts` (a `piece` source) | refused: "'@charts' has the wrong evaluation domain" |
| masks chain over `@charts` (`lit = @charts * 2`, `area = @lit * 0.5`) | accepted: a mask can be a per-geometry scalar |
| a layer `opacity` or an output `strength` reads that mask | refused: "reads unknown signal '@lit'" (`recipe/Signals.cpp:106`) |

- Per-geometry scalars exist today: they are masks over `piece` sources. The template's `chartChoices` and `chartLit` are such masks.
- A row is per geometry when it reads an item fact, directly or through other rows. The graph can infer that: a signal that reads an item fact moves to the spatial domain.
- Scalar parameters (layer `opacity`, layer colour, output `strength`, light and shell parameters) accept signals only. `LayerField::value` is a `RenderValueRef` (`planners/RenderPlan.h:145`), so the render plan can carry a per-geometry value there. The lowering path for it is not checked.

## The nested pick

`pick` is a special source that returns 1 on the picked area and 0 elsewhere. It picks in three steps, each from what the step before left.

| Step | Picks | From | Seed |
|---|---|---|---|
| 1 | 1 to `geometries` geometries | the piece's geometries | the piece identity (enchantment, effect, item) |
| 2 | one material group | the groups the lit geometry has | the piece identity and the geometry index |
| 3 | 1 to `charts` charts | the charts where the picked group covers at least `minCover` of the chart | the piece identity, the geometry index and the group |

```json
"lit": { "pick": { "geometries": 1, "charts": "@litCharts", "minCover": 0.05 } }
```

- Step 3 always has a candidate, because the picked group covers some chart. The pick is never empty.
- `geometries` and `charts` are parameters. A signal or an expression can scale them by magnitude, cost, an actor value or an actor state.
- The material group settings are the source's `groups` settings, with the same defaults as `material.groups`.
- The worker computes each group's cover of each chart at grouping time and keeps that small table in the grouping result.
- The draw is the cluster draw with two inputs added: the chart-id bake, and a 256-entry chart lookup made like a curve lookup. It writes 1 where the texel's group is the picked group and its chart is lit.

The template's `glowArea` becomes `@glowDepth × @lit`, and `chartPick` and `materialPick` go away.

## Scaling by enchantment or actor

The template already scales by magnitude through an expression: `magnitudeLevel = m / (m + magnitudeHalf)`, with `magnitudeHalf` 50. This plan keeps that pattern: a count reads a signal or an expression, and the expression normalises.

A studio picker for the driving value (an enchantment field, an actor value, an actor state) and a per-record normalisation (the value over the base enchantment's magnitude) are follow-ups. The base magnitude as an enchantment field is not checked.

## Studio

- The kind choice for sources and for signals becomes family, then kind.
- One name for each thing: **part**, **chart**, **geometry**, **material group** (D2). The paint term offers use the same names.
- The material group chart, the source inspector and the paint term read `material.groups`.

## Stages

| Stage | Work | Tests |
|---|---|---|
| 0 | The material clustering review's findings 1 to 3: a refused or lost grouping job requests again; the grouping result drops the raster and keeps the chart cover table; the shader stops counting the group weight twice. | native suites for the grouping service, the result size and the packing |
| 1 | Check the per-geometry evaluation of a signal that reads an item fact (D3). | a graph test |
| 2 | The format changes and validation messages; the rewritten repo recipes; the schema. | recipe, schema and contract suites |
| 3 | `item: geometryIndex` and `geometryCount`: the engine sorts a piece's geometries by name and binds both per geometry. | render plan suite; in-game check of two-geometry pieces |
| 4 | `pick`: lowering, the chart lookup, the draw. | render plan suite; a CPU reference for the three steps |
| 5 | The template moves to `pick`. | importer and template-cost suites |
| 6 | The studio's family-then-kind choice and the names. | studio panels and term template suites |

Each stage ends with the native suites. Stages 3 to 5 end with a Windows build and an in-game check.

## Decisions for the user

| # | Decision | Recommendation |
|---|---|---|
| D1 | Rename the source keys (`mesh`, `material.groups`, `item`) or keep the keys and group in the studio only. | Rename: the format then says what each source is, and the repo's recipes are few. |
| D2 | The user-facing names. | **part** (not component) and **material group** (not cluster). |
| D3 | A signal that reads an item fact: per geometry, or not allowed. | Per geometry if stage 1 shows the graph supports it. |
| D4 | The pick's order and the group count. | Geometry, then material group, then charts; one group per lit geometry. |
| D5 | `minCover` default. | 0.05: a chart where the group covers less than 5% would show a sliver. |
