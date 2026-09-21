# Format 1 row reference (2026-09-21)

Every authorable row of recipe format 1: one section per row type, one
subsection per kind, each with its parameters and its audited state.
Working material for the gate-5 freeze
(`docs/plans/release-roadmap-2026-09-20.md`, part 3): refine a kind here,
then carry the result into the freeze writing. The contract is
`schema/recipe.schema.json`; this document adds what the schema cannot
say — runtime semantics, implementation status, and the open questions.

Audited 2026-09-21 against `main` at 0fd276f, by four sweeps: parse
(`src/recipe/`), evaluation and rendering (`src/recipe/Signals.cpp`,
`src/render/`, `src/engine/`, `src/planners/`), tests (`tests/`), and the
studio surface (`src/studio/`, `src/menu/`). The execution round later
the same day landed the closed work items below; the section bodies
describe the tree after it.

Each kind carries a **Status** line with four checks — parsed,
honoured at runtime, tested, studio-editable — and an **Open** line where
the audit found work. A kind with no Open line passed clean.

## Work items

Everything the audits found, as work. Sizes follow the roadmap's scale:
S is hours, M is a day or two, L is a week or more. The 2026-09-21
execution round closed items 1 to 11, 15, 17 to 21, 24, 25, 26, 28 and
37; five decisions drove it: variants wired values-only, the dead records
cut except the plugin origin (specified and wired instead), all five
renames, a new enchanted key, and whole-bus typed payloads with the
trigger declaring both its `payload` type and its `anchor` space. Still
open: 12 to 14, 16, 22, 23, 27, and
the optional adds 29 to 36 (additive, so format 1 freezes without them).

### Format decisions — block the freeze writing

| # | Item | Finding | Scope |
|---|---|---|---|
| 1 | DONE 2026-09-21: variants wired, values only | Parsed, edited, validated, serialized; `ApplyVariant` and `VariantApplies` (`src/recipe/Variants.cpp`) have zero callers, so no override reaches an actor. The example file and lesson 9 depend on them; the natural call site is where a recipe instance's signals are built per matched piece. See [Variants](#variants). | M to wire, S to cut |
| 2 | DONE 2026-09-21: `lerp` dropped (and the field renamed `merge`, item 19) | No code path in `Merge.cpp` or `Resolve.cpp`; falls through to stack, so a validator accepting it lies. Reserve the word for format 2 in the freeze writing; the mechanic list is the ecosystem-policy decision (roadmap item 9). See [Recipe head](#recipe-head). | S |
| 3 | DONE 2026-09-21: `bulb` cut | The LIGH form resolved for validation in `RecipeStore.cpp` (since removed with the field) but `render/Light.cpp` never created it or inherited its flags. See [Light output](#light-output). | S to cut, M to implement |
| 4 | DONE 2026-09-21: kept — contract specified and delivery wired; the payload contract is superseded by item 37 | Parsed, evaluated, in the UI; at audit time there was no delivery path — `src/main.cpp`'s only listener handled SKSE lifecycle messages, and the payload another plugin would send was specified nowhere. Freezing it as-is freezes an inter-plugin ABI that has never carried a message. See [Trigger origins](#trigger-origins). | S to cut, M to specify and wire |
| 5 | DECIDED 2026-09-21: frozen as-is | The enum mixes three flags with one continuous distance (`hostileDistance`); the one shape wart in the signal set. Freeze it as-is, or split the distance out before the freeze. See [actorState](#actorstate). | S, decision only |
| 37 | DONE 2026-09-21, same day: typed values landed whole-bus; the location half became the trigger-declared `anchor` (`world` or a node; the event origin's `at` dissolved into it, and `uv` waits until the render honours it) | Supersedes the version-1 float contract item 4 landed, before anything ships. A plugin message carries one value of the existing shapes — scalar, vec2, or vec3 — under a closed three-way wire tag. A sender decomposes a rich occurrence into several ids (`precision.hit`, `precision.hit.position`, `precision.hit.normal`), so the one-to-many map lives in the sender, beef ships no mapping config, and suffixed ids compose with the existing globs. Three obligations join the freeze writing: the receiving row declares the type it expects, and a firing of another type is dropped with a diagnostic, so the graph still types at compile time; a ripple's trigger must be vec3-typed and its value is the ring's origin; same-occurrence ids correlate only by arrival, so senders fire them in the same tick with the same lifetime. Internally `TriggerPayload.value` widens from float to `Value` and the `payload` field enum collapses to a typed read. See [Trigger origins](#trigger-origins), [payload](#payload), [ripple](#ripple). | M |

### Contract alignments — block the freeze writing

| # | Item | Finding | Scope |
|---|---|---|---|
| 6 | DONE 2026-09-21: the schema states the 0.3 floor | Code floors it at 0.3 (`src/render/Light.cpp:156`); the schema says 0..1. State the floor in the schema, or honour low values. See [Light output](#light-output). | S |
| 7 | DONE 2026-09-21: the schema states the alias | Not a stored map: displacement when non-flat, else occlusion (`src/render/CompositorSource.cpp:178`). Freeze "best available relief detail" in the schema description, or rename the channel. See [material](#material). | S |

### Tests — strengthen the agreement suite the freeze leans on

| # | Item | Finding | Scope |
|---|---|---|---|
| 8 | DONE 2026-09-21 | Both evaluate real engine state but only parse round-trips cover them; the test environment already provides the overrides (`signals_tests.cpp:26`), unused. | S |
| 9 | DONE 2026-09-21: fires, and each origin is channel-separated | Only if item 4 keeps it; today nothing fires one anywhere. | S |
| 10 | DONE 2026-09-21 | The other five bake sub-kinds have them. | S |
| 11 | DONE 2026-09-21 | Geometry and texture terms are tested; addon is not. | S |
| 12 | Render-side coverage | `tests/render/` holds only `.gitkeep`, so every render verdict — source realization, the mask op-VM, curve LUTs, ripple, normalSlope, the cluster classify pass, slot writes, blends — is integration-only. Standing debt beyond the freeze. | L |

### Studio — beside UI-backlog item 16

| # | Item | Finding | Scope |
|---|---|---|---|
| 13 | A gradient stop editor | `GradientFields` (`Forms.cpp:1044`) binds only `t`; no stop editor exists, so gradient colors are authorable in JSON only. | M |
| 14 | Expose the event trigger's `filter` | Parsed and evaluated (`MatchesFilter`) but the event origin form binds only `event`; the trigger-level payload and anchor fields are bound. | S |

### Nits

| # | Item | Finding | Scope |
|---|---|---|---|
| 15 | DONE 2026-09-21: recorded in deletions.md, looked-dead-keep | `ValueOf` returns `{1,1,1}` for `ComponentIdBake` and `ChartIdBake` (`Mesh.cpp:190`), but `PrepareBake` routes both to `BuildIslandBake` first, so the arms never run. Candidates for `docs/plans/deletions.md`. | S |
| 16 | Preview reads 0 without saying so | The response-graph preview uses a null environment (`ResponseGraph.cpp:108`), so `efsh`, `av`, `actorState`, and `enchantment` preview as 0 with no hint. Correct behaviour, silent surface. | S, optional |

### Naming — decide with the freeze writing (audited 2026-09-21)

A name freezes with the format, and there are no consumers yet, so each
rename is S now and impossible later.

| # | Item | Finding | Scope |
|---|---|---|---|
| 17 | DONE 2026-09-21 | In image-editor vocabulary "normal" is the default over-blend, which this format spells `replace`; here it is reoriented normal mapping (`ShaderSource.cpp:67`), legal only on a normal stack. The technique's own name avoids the collision. | S |
| 18 | DONE 2026-09-21 | A duplicate: modes 0 and 5 both fall to the shader's default `return value` (`ShaderSource.cpp:73`), and every blend is already lerped by opacity (`:93`), so `lerp` is byte-identical to `replace`. With item 2 this retires the wire word "lerp" entirely; it survives only inside expressions, where it belongs. | S |
| 19 | DONE 2026-09-21 | The field's default value `stack` means "do not override", and variants carry `overrides` with an unrelated meaning. `merge: stack \| replace \| sampled` says what each value does. Folds into the ecosystem-policy decision (roadmap item 9), which owns the mechanic list. | S |
| 20 | DONE 2026-09-21 | Names a continuous oscillator; "pulse" reads as a one-shot, which is a trigger's job, and lesson 2 will teach a periodic effect against the name's grain. Its own parameter already says `waveform`. | S |
| 21 | DONE 2026-09-21 | Layers say `opacity`, the shell says `alpha`, one concept. `alphaTest` and `blend: alpha` stay; they name distinct techniques (a discard threshold, a blend mode). | S |
| 22 | Glossary records for the verified shared words | `replace` (three uses, one shape: what is below is discarded), `material` (four aspects of the armor's material), `enchantment` (key and signal, one referent), layer `source` (accepts `@mask` and constant colors too), `minShare` (share of skin weight; settle with item 6). All verified accurate in place; record them so the reuse stays deliberate. | S, part of the freeze writing |

### Optional naming polish

| # | Item | Finding | Scope |
|---|---|---|---|
| 23 | Parameter words | `trigger.max` → `keep` (max of what is unstated); `filter.value` → `range` (it is a `[lo, hi]` pair); `accumulate` → `charge` (the lone verb among noun kinds, and it charges on firings and drains at `decay`). | S each, optional |

### Done

| # | Item | Finding | Scope |
|---|---|---|---|
| 24 | `delta` reads per second | DONE 2026-09-21. The value was the raw per-tick difference, so it scaled with frame rate; `Signals.cpp` now divides by the tick's seconds (a zero-length tick reads 0), and the suite checks two tick lengths. If the naming pass runs, `rate` is now the accurate word for it — decide with items 17 to 21. | — |

### Use-case coverage — decide with the freeze writing (walkthrough 2026-09-21)

The use-case walkthrough found four gaps that change or document existing
rows, so they are format-1 relevant now.

| # | Item | Finding | Scope |
|---|---|---|---|
| 25 | DONE 2026-09-21: `enchanted` key added | `default` matches every worn piece unconditionally (`Resolve.cpp`, `KeyOperand::kNone`), so "a generic look for anything enchanted that lacks its own recipe" — the plugin's own premise — has no key. Add an `enchanted` key kind, or give `default` that meaning and add an `any` key for every piece. | S to M |
| 26 | DECIDED 2026-09-21: values only; paths wait for format 2 | Overrides carry numbers and vectors only, so a per-set texture (an image `path`) forces a full recipe copy. Decide the scope — values only, or paths too — together with item 1's wire-or-cut decision. | S to decide, M if paths land |
| 27 | Name the built-in event vocabulary | The engine publishes four id families: `anim.<graph event>`, `equip`, `hit.received`, `hit.dealt` (`src/engine/Events.cpp`). The list appears nowhere an author will read, and graph-event tags are undiscoverable. Part of the format contract writing; casting and shouting have no id unless a graph event happens to fire. | S, writing |
| 28 | DONE 2026-09-21: stated in the schema | It matches the piece's `diffusePaths` — the same operand as the selector's `texture` term — not a material record or path. Rename or document; joins the naming decisions 17 to 22. | S |

### Use-case coverage — optional adds

Each is additive; none blocks the freeze. Ranked by how often an author
will hit the gap.

| # | Item | Finding | Scope |
|---|---|---|---|
| 29 | A `wearer` identity signal | Every wearer of a recipe pulses in phase; only application-time offsets desync a crowd, and `sampled` gives only file-grained variety. A constant hashed from the actor's form id (0..1) would desync phase and shift hue continuously and cheaply. The highest-value small add the walkthrough found. | S |
| 30 | `image` flipbook and rotation | Frame-animated effects (the vanilla effect-shader staple, so also import fidelity) and spinning circles are inexpressible; `transpose` and `mirror` give only right-angle variety. | M |
| 31 | More wearer state, and any world state | `actorState` lacks swimming (the shipped `WaterBreathingFXS` fixture's one natural trick), sprint or movement speed, mounted; no signal reads the world at all (time of day, weather, interior). | S to M per state |
| 32 | A curvature or edge bake | Edge-glow and edge-wear rank among the most-asked armor looks; `occlusion` approximates cavities and `normalSlope` steepness, neither isolates edges. | M |
| 33 | A second shell per recipe | A two-layer aura (tight sheen, wispy haze) needs two recipes and drags merge semantics into one authored idea. | M |
| 34 | Fade-out on unapply | Unequipping pops the effect; state-driven fades already work through `smooth`, but a lifecycle release envelope needs engine support. | M |
| 35 | Key conjunction | Keys are alternatives only; "fire enchantment on daedric armor" cannot be keyed as both. Priority plus `replace` fires for all daedric; variants only swap constants. | M |
| 36 | Small reads | `enchantment` reads only the costliest effect, so a dual-effect enchant cannot drive two colors; a trigger has no firing rate limit (`max` caps concurrent lives, not frequency). | S each |

## Row types

| Section | What it declares |
|---|---|
| [Recipe head](#recipe-head) | Identity, merge order, override mechanic, clock. |
| [Keys](#keys) | What a recipe attaches to. |
| [Signals](#signals) | A value per tick, from sixteen kinds. |
| [Curves](#curves) | A named one-argument shaping expression. |
| [Sources](#sources) | A value per texel, from seven kinds. |
| [Masks](#masks) | A named per-texel expression. |
| [Selectors](#selectors) | Which geometries an output or variant touches. |
| [Outputs](#outputs) | A material or shell slot with its layer stack, or a light. |
| [Layers](#layers) | One entry of an output's stack. |
| [Shell](#shell) | The cloned geometry's material, blend, and pose. |
| [Variants](#variants) | Per-armor constant overrides. |

## Shared value shapes

The parameter tables below use these shapes.

| Shape | Form | Meaning |
|---|---|---|
| **param** | number or `@signal` | A scalar an author can animate. |
| **value** | number or `[x, y]` or `[x, y, z]` | A literal signal value. |
| **vec2**, **vec3** | `[a, b]` / `[a, b, c]` of params, or `@signal` | Components animate independently. |
| **color** | vec3 | Components above 1 read as 0..255, written back as 0..1. |
| **ref** | `@name` | A reference to another row. |
| **curveRef** | `@curve` or an inline expression in `x` | Where a curve is accepted. |
| **expression** | string | The one expression language: arithmetic, comparisons, `and`/`or`/`not`, `if`, the fixed function set, `time`, `pi`. Division by zero is 0. |
| **form** | editor ID, or `0x<local id>~<plugin file>` | Resolved in game; the validator says so. |
| **glob** | string, `*` the only wildcard | Case-insensitive; `/` equals `\`. |
| **node** | string | A skeleton node name. |
| **note** | string | Author documentation for the row; landed 2026-09-21 on every row kind. |

## Recipe head

Identity fields pass through untouched; the merge fields decide how this
recipe combines with others contesting the same piece.

| Field | Shape | Default | Meaning |
|---|---|---|---|
| `format` | the constant 1 | required | The format this document freezes. |
| `name`, `author`, `description`, `version` | string | — | Identity; not interpreted. |
| `imported` | string | — | `"<plugin> <version>"`, written by the importer; present means generated and unedited. A studio save drops it. |
| `meta` | object | — | Free-form; kept verbatim through load and save. |
| `priority` | integer | by key kind | Merge order among matching recipes, lower first. Defaults: default 0, enchanted 5, material 10, keyword 20, armor 30, effectShader 40, enchantment 50, magicEffect 60. |
| `merge` | `stack` \| `replace` \| `sampled` | `stack` | How this recipe combines with lower-priority recipes: stack appends outputs per slot; replace drops lower-priority work on this recipe's slots (`Merge.cpp` `CutAtReplace`); sampled joins a per-piece pool from which each actor draws one member by form id (`Resolve.cpp` `KeepOneSampled`). The field was `override`, with a reserved `lerp` value, until 2026-09-21. |
| `clock.speed` | number | 1.0 | Multiplies the recipe clock (`ManagerTick.cpp:209`). |

Status: parsed · honoured · merge suite covers priority, replace, sampled ·
studio-editable.
Open: the mechanic list itself is roadmap item 9's decision; `lerp` is
reserved for format 2 in the freeze writing.

## Keys

What a recipe attaches to. A recipe declares one or more keys; the
planner matches them against a wearer's pieces and orders matches by
priority (`src/recipe/Resolve.cpp:170`). A key held by several files
belongs to the last file loaded with it.

| Kind | Shape | Matches |
|---|---|---|
| `default` | the bare string | Every piece, as a fallback: `Resolve` drops default matches when any enchantment-derived key matched the piece. |
| `enchanted` | the bare string | Any worn piece that carries an enchantment (added 2026-09-21, priority 5); suppressed in turn when a specific enchantment key (effectShader, enchantment, magicEffect) matched. |
| `magicEffect` | form | A piece whose enchantment carries the effect. |
| `enchantment` | form | The enchantment itself. |
| `effectShader` | form | The effect shader a vanilla enchantment plays; the importer's key. |
| `keyword` | form | A keyword on the armor. |
| `material` | glob | The piece's diffuse texture paths — the same operand as the selector's `texture` term, despite the name (work item 28). |
| `armor` | form | One armor record. |

Status: all eight parsed · matched · tested (`merge_tests.cpp`,
`actorplanning_tests.cpp`) · studio-editable.
Open: keys are alternatives only, with no conjunction (item 35).

## Signals

A value that varies per tick, evaluated once per tick in dependency order
against live actor state and the event bus. A cycle, an unknown
reference, or a bad expression makes the node inert (0 or black), never a
crash. Every kind takes two optional shared fields: `curve` (a curveRef
shaping the value) and `note`.

Evaluation lives in `src/recipe/Signals.cpp` (the `Evaluator` visitor,
one arm per kind); actor-facing kinds read the `SignalEnvironment`,
concretely `src/engine/Environment.cpp`. The studio's response-graph
preview uses a null environment, so `efsh`, `av`, `actorState`, and
`enchantment` read 0 in preview.

| Kind | One line |
|---|---|
| [`constant`](#constant) | A fixed value. |
| [`wave`](#wave) | A periodic wave on the recipe clock. |
| [`ramp`](#ramp) | From one value to another over the recipe's first seconds. |
| [`efsh`](#efsh) | A field of a vanilla effect shader's animation. |
| [`av`](#av) | An actor value of the wearer. |
| [`actorState`](#actorstate) | A state flag or distance of the wearer. |
| [`enchantment`](#enchantment) | The piece's enchantment magnitude or cost. |
| [`trigger`](#trigger) | The age of the newest firing from an event source. |
| [`payload`](#payload) | A field carried by a trigger's newest firing. |
| [`counter`](#counter) | How many times a trigger has fired. |
| [`accumulate`](#accumulate) | Firings summed, decaying over time. |
| [`noise`](#noise) | Smooth value noise on the recipe clock. |
| [`gradient`](#gradient) | A color read from stops at a scalar position. |
| [`rate`](#rate) | Another signal's change per second. |
| [`smooth`](#smooth) | Another signal, exponentially smoothed. |
| [`expr`](#expr) | An expression over signals. |

### `constant`

A fixed value; the plainest signal, and what a variant overrides.

| Parameter | Shape | Meaning |
|---|---|---|
| `constant` | value | The value. |

Status: parsed · evaluated · eval-tested · studio-editable.

### `wave`

A periodic wave; the wire word was `pulse` until 2026-09-21 (work item
20). The phase integrates `delta / period` each tick, so a live edit of
the period does not jump the phase.

| Parameter | Shape | Default | Meaning |
|---|---|---|---|
| `base` | param | 0 | The wave's centre. |
| `amplitude` | param | — | The wave's height. |
| `period` | param | — | Seconds per cycle, on the recipe clock. |
| `phase` | param | 0 | Phase offset, in cycles. |
| `waveform` | `sine` \| `triangle` \| `square` \| `saw` | sine | The wave's shape. |

Status: parsed · evaluated · eval-tested · studio-editable.

### `ramp`

`from` to `to` over the recipe instance's first `seconds`, then holds
`to`. `seconds` of 0 is already `to`.

| Parameter | Shape | Meaning |
|---|---|---|
| `from`, `to` | param | The endpoints. |
| `seconds` | param | The travel time, on the recipe clock. |

Status: parsed · evaluated · eval-tested · studio-editable.

### `efsh`

A field of a vanilla effect shader's animation, evaluated at the recipe
clock (`Efsh::Evaluate`); the importer's backbone. A missing record
makes the signal 0.

| Parameter | Shape | Meaning |
|---|---|---|
| `field` | `fillAlpha` \| `fillColor` \| `edgeAlpha` \| `edgeColor` \| `scroll` | Which animated field; `fillColor` carries the record's scale, `scroll` is the `[u, v]` offset pair. |
| `record` | form | The EFSH record. |

Status: parsed · evaluated against the real TESEffectShader
(`Environment.cpp:96`) · eval-tested · studio-editable.

### `av`

An actor value of the wearer. The wire word `av` is the community's
abbreviation for actor value.

| Parameter | Shape | Meaning |
|---|---|---|
| `av` | a bare actor-value name, or `{ "of", "measure" }` | The bare form reads `current`. |
| `measure` | `current` \| `base` \| `permanent` \| `temporaryModifier` \| `damage` \| `max` | Which measure of the value. |

Status: parsed · evaluated, all six measures (`Environment.cpp:15`) ·
eval-tested · studio-editable.

### `actorState`

A state of the wearer: three booleans (0 or 1) and one distance.

| Parameter | Shape | Meaning |
|---|---|---|
| `actorState` | `inCombat` \| `sneaking` \| `weaponDrawn` \| `hostileDistance` | `hostileDistance` is the distance to the nearest hostile; the other three are flags. |

Status: parsed · evaluated (`Environment.cpp:55`) · eval-tested ·
studio-editable. The enum's mixed shape (three flags, one distance) is
frozen as-is (decision, item 5).

### `enchantment`

The piece's enchantment, read from the costliest effect item
(`Environment.cpp:78`).

| Parameter | Shape | Meaning |
|---|---|---|
| `enchantment` | `magnitude` \| `cost` | Which number. |

Status: parsed · evaluated against the real MagicItem · eval-tested ·
studio-editable.

### `trigger`

An event source. Each firing lives `lifetime` seconds; the signal's value
is the newest live firing's age divided by `lifetime` (0 at the moment of
firing, rising to 1), and 1 when idle. A curve shapes that ramp into a
flash, a decay, or a hold. `max` bounds live firings; older ones drop.

| Parameter | Shape | Default | Meaning |
|---|---|---|---|
| `lifetime` | param | — | Seconds a firing lives. |
| `max` | integer ≥ 1 | — | Live firings kept. |
| `payload` | `scalar` \| `vec2` \| `vec3` | scalar | The type of the value a firing carries. A firing of another type is dropped and counted; the graph types payload readers from this declaration at compile time. |
| `anchor` | `"world"` or `{ "node": ... }` | absent | The space a firing's location resolves in: `world` means the vec3 payload is a world position (requires payload vec3, compile-checked); a node anchor locates firings at that skeleton node, resolved per geometry, with a firing's own carried node overriding it. Absent means unlocated — the payload is just a value. A `uv` space is anticipated but deferred until the render honours it; adding a space later is additive. |
| one origin | see below | required | Where firings come from. |

Status: parsed · evaluated · firing-tested for all three origins,
channel separation both ways · studio-editable.

#### Trigger origins

| Origin | One line |
|---|---|
| `event` | An id glob on the engine event bus. |
| `plugin` | An id another SKSE plugin sends through the messaging channel. |
| `when` | A rising edge of another signal. |

**`event`** — matches an id glob against the bus. The engine publishes
four id families: `equip`, `anim.<graph event>`, `hit.received`, and
`hit.dealt` (`src/engine/Events.cpp`). Naming this list in the contract
is work item 27.

| Parameter | Shape | Meaning |
|---|---|---|
| `event` | event-id glob | The ids to match. |
| `filter.node` | glob | Match the firing's node name. |
| `filter.arg` | glob | Match the event's argument. |
| `filter.value` | `[lo, hi]`, either null | Bound the event's value. |

Open: `filter` is parsed and evaluated (`MatchesFilter`,
`Signals.cpp:71`) but has no studio editor (the event-origin form,
`Forms.cpp:867`, binds only `event`).

**`plugin`** — an id another SKSE plugin fires through the messaging
channel: an SKSE message of type `0x42454546` carrying a
`PluginEventMessage` (`src/engine/PluginEvents.h`; version, target actor
form id or 0 for every tracked actor, id, value), validated at the
boundary and queued through the manager. `REFERENCE.md` (engine events)
states the full contract. Since 2026-09-21 the channels are separate:
an `event` origin never fires on a plugin message and a `plugin` origin
never fires on an engine event.

| Parameter | Shape | Meaning |
|---|---|---|
| `plugin` | event-id glob | The ids to match. |

Item 37 landed 2026-09-21: the message carries a tagged
scalar/vec2/vec3, rich occurrences arrive as suffixed ids
(`precision.hit.position`) correlated by arrival only, and
`ParsePluginEvent` (`engine/PluginEvents.cpp`, engine-free,
garbage-tested) validates every field at the boundary.

**`when`** — fires on the tick the referenced scalar goes from at most 0
to above 0 (edge-detected in the trigger evaluator, `Signals.cpp:1301`).

| Parameter | Shape | Meaning |
|---|---|---|
| `when` | ref | The scalar watched for a rising edge. |
| `value` | ref | A signal sampled into the firing's value; its type must match the declared `payload`, checked at compile. |

### `payload`

The value carried by a trigger's newest firing, typed by the trigger's
declared `payload`; holds between firings and reads zero before the
first. The `field` enum (value/position/normal) left with item 37: one
typed value per event, and rich occurrences arrive as suffixed ids.

| Parameter | Shape | Meaning |
|---|---|---|
| `payload` | ref | The trigger read; a bare reference, like `rate`. |

Status: parsed · evaluated · eval-tested at scalar and vec3 ·
studio-editable.

### `counter`

Counts firings. Each new firing adds one; a firing of `reset` zeroes it;
`cap` clamps it.

| Parameter | Shape | Meaning |
|---|---|---|
| `trigger` | ref | The counted trigger. |
| `reset` | ref | A trigger that zeroes the count. |
| `cap` | param | The ceiling; unset means unbounded. |

Status: parsed · evaluated · eval-tested · studio-editable.

### `accumulate`

Firings summed with continuous decay: each tick subtracts
`decay × dt` (floored at 0), then adds new firings. Distinct from
`counter` (discrete, resettable, capped) and from `smooth` (which tracks
a signal, not firings).

| Parameter | Shape | Meaning |
|---|---|---|
| `trigger` | ref | The summed trigger. |
| `decay` | param | Units lost per second. |

Status: parsed · evaluated · eval-tested · studio-editable.

### `noise`

Smooth value noise sampled along the recipe clock:
`amplitude × noise(time × frequency, seed)`. Not expressible in `expr`,
which has no randomness.

| Parameter | Shape | Meaning |
|---|---|---|
| `frequency` | param | Samples per second. |
| `amplitude` | param | The output scale. |
| `seed` | integer ≥ 0 | The same seed gives the same noise. |

Status: parsed · evaluated · eval-tested · studio-editable.

### `gradient`

A color read from stops at a scalar position: piecewise-linear between
the nearest stops, clamped at the ends. The structured color ramp;
building one in `expr` takes nested lerps.

| Parameter | Shape | Meaning |
|---|---|---|
| `t` | param | The read position. |
| `stops` | array of `{ "at": number, "color": color }` | At least one stop. |

Status: parsed · evaluated · eval-tested · **stops not studio-editable**.
Open: `GradientFields` (`Forms.cpp:1044`) binds only `t`; no stop editor
exists anywhere, so
gradient colors are authorable in JSON only. A UI gap, not a format gap.

### `rate`

The referenced signal's rate of change, in units per second,
component-wise. The first tick and a zero-length tick read 0. The wire
word was `delta`, and the value the raw per-tick difference, until
2026-09-21 (work items 20 to 24).

| Parameter | Shape | Meaning |
|---|---|---|
| `rate` | ref | The watched signal. |

Status: parsed · evaluated · eval-tested at two tick lengths ·
studio-editable.

### `smooth`

The referenced signal, exponentially smoothed:
`a = 1 − exp(−dt / seconds)` per tick. `seconds` of 0 tracks exactly.

| Parameter | Shape | Meaning |
|---|---|---|
| `of` | ref | The tracked signal. |
| `seconds` | param | The time constant. |

Status: parsed · evaluated · eval-tested · studio-editable.

### `expr`

An expression over signals; the escape hatch the structured kinds sit
beside. Structured kinds stay worth their keep where they carry state
(`pulse`, `ramp`, the trigger family) or studio-editable structure
(`gradient`) that an expression cannot.

| Parameter | Shape | Meaning |
|---|---|---|
| `expr` | expression | Up to 64 signal references. |

Status: parsed · evaluated · eval-tested · studio-editable.

## Curves

A named one-argument expression in `x`, applied through `curve` fields on
signals, layers, and sources being shaped; inside a curve shaping a
source, `mean` is the source's mean. A curveRef also accepts the
expression inline. At render time a curve compiles to a 256-entry lookup
texture sampled per texel (`CreateCurveLookup`, `CompositorSource.cpp:42`).

The row is a bare expression string, or `{ "expr", "note" }`.

Status: parsed · applied on both the tick path and the texel path ·
round-trip-tested · studio-editable. No open items.

## Sources

A value that varies per texel, in a geometry's UV space. All seven kinds
are realized on the GPU by `src/render/CompositorSource.cpp`
(`SourcePreparer`); the bakes rasterise once per geometry. Every kind
takes an optional `note`.

| Kind | One line |
|---|---|
| [`image`](#image) | A texture file, sampled with scroll, tile, mirror, and mip. |
| [`material`](#material) | A channel of the armor's own PBR maps. |
| [`bake`](#bake) | A per-texel fact of the mesh, rasterised once. |
| [`uv`](#uv) | The U or V coordinate itself. |
| [`distance`](#distance) | Distance from a node or point. |
| [`ripple`](#ripple) | An expanding ring from a trigger's firing point. |
| [`materialClusters`](#materialclusters) | A k-means cluster map of the material. |

### `image`

A texture file, sampled per texel. An `rgb` read is mean-luminance
normalized (`CompositorSource.cpp:362`).

| Parameter | Shape | Default | Meaning |
|---|---|---|---|
| `path` | string | required | Relative to `Data/Textures`. |
| `channel` | `rgb` \| `r` \| `g` \| `b` \| `a` \| `luma` | rgb | What is read. |
| `space` | `tiled` \| `mesh` | tiled | Tiled repeats in UV; mesh stretches once over the geometry. |
| `scroll` | vec2 | — | UV per second; animatable. |
| `tile` | vec2 | — | Repeats per UV unit. |
| `mirror` | `[bool, bool]` | — | Mirror alternate repeats per axis. |
| `transpose` | bool | false | Swap U and V. |
| `mip` | number ≥ 0 | 0 | Sample a coarser mip; a cheap blur. |

Status: parsed · rendered with every option honoured
(`CompositorSource.cpp:204`) · **parse-tested only** · studio-editable.

### `material`

A channel of the armor's own PBR maps; how an effect follows the
surface it sits on.

| Channel | Reads | Note |
|---|---|---|
| `diffuseRgb` | diffuse RGB | |
| `diffuseLuma` | diffuse luminance | |
| `normalSlope` | derived from the normal map | Rendered on demand (`RenderNormalSlope`), not a stored map. |
| `roughness` | RMAOS r | |
| `metallic` | RMAOS g | |
| `occlusion` | RMAOS b | |
| `reflectance` | RMAOS a | |
| `displacement` | displacement r | |
| `relief` | displacement r, else RMAOS b | An alias, not a map: displacement when the map is non-flat, else occlusion (`CompositorSource.cpp:178`). The schema states this since 2026-09-21. |

Status: parsed · rendered · **parse-tested only** · studio-editable.

### `bake`

A per-texel fact of the mesh, rasterised once per geometry
(`src/mesh/Mesh.cpp`, `CompositorBake.cpp`). The island kinds need the
mesh analysis and error without it.

| Sub-kind | Shape | Meaning |
|---|---|---|
| `position` | bare word | World position, normalized 0..1. |
| `localPosition` | bare word | Position within the mesh's bound; errors on a zero radius. |
| `worldUp` | bare word | How upward-facing the texel is (normal z, 0..1). |
| `componentId` | bare word | The connected-piece id / 255, from the analysis. |
| `chartId` | bare word | The UV-chart id / 255, from the analysis. |
| `{ "partition": … }` | biped-slot name, or integer 30..61 | 1 where the geometry belongs to the slot; errors if the slot is absent. |
| `{ "boneWeight": [...] }` | array of nodes | The summed skin weight of the named bones. |

Status: all seven parsed · rendered · every sub-kind value-tested ·
studio-editable.

### `uv`

The U or V coordinate itself; the plainest gradient across a chart.

| Parameter | Shape | Meaning |
|---|---|---|
| `uv` | `u` \| `v` | Which coordinate. |

Status: parsed · rendered (`Mesh.cpp:305`) · value-tested ·
studio-editable.

### `distance`

Distance from a skeleton node or a fixed point, per texel.

| Parameter | Shape | Meaning |
|---|---|---|
| `distance` | node, or `{ "from": node \| [x, y, z] }` | The measured origin. |

Status: parsed · rendered (`CompositorBake.cpp:145`) · value-tested ·
studio-editable.

### `ripple`

An expanding ring from a trigger firing's carried position; a real GPU
pass (`PSRipple`, `TextureLabPass.cpp:505`).

| Parameter | Shape | Default | Meaning |
|---|---|---|---|
| `trigger` | ref | required | The firings that spawn rings. |
| `speed` | param | — | Units per second outward. |
| `width` | param | — | The ring's thickness. |
| `decay` | param | — | Fade per second. |
| `shape` | `ring` \| `disc` | ring | Hollow or filled. |

Status: parsed · rendered · **untested** · studio-editable.
The ring's origin is the firing's **anchor**, declared on the trigger
row (`anchor: "world"` or a node) and resolved engine-free by
`AnchorOf` (`recipe/Signals.h`, tested); the render side supplies only
the two resolvers (root-space transform, per-geometry bind position).
An unanchored trigger's ripple falls back to the geometry's origin,
which keeps the plain hit-ring case working without a declaration.

### `materialClusters`

The material's cluster map: each texel the id / 255 of its nearest
k-means cluster, rendered once per geometry. Deterministic per seed.
Every field has a default.

| Parameter | Shape | Default | Meaning |
|---|---|---|---|
| `clusters` | integer 1..8 | 4 | How many clusters. |
| `weights.roughness` | number 0..10 | 1 | Channel weight in the texel distance. |
| `weights.metallic` | number 0..10 | 1 | " |
| `weights.occlusion` | number 0..10 | 0.5 | " |
| `weights.reflectance` | number 0..10 | 0.5 | " |
| `weights.luma` | number 0..10 | 1 | " |
| `seed` | integer ≥ 0 | 1 | Same seed, same clusters. |
| `iterations` | integer 1..256 | 32 | The k-means cap. |

Status: parsed · k-means++ on the CPU (`MaterialClusters.cpp:113`),
classify pass on the GPU · CPU side tested
(`materialclusters_tests.cpp`) · studio-editable.

## Masks

A named per-texel expression: source and mask names stand for images,
signal names for the tick's scalars. Compiled to a stack program and
interpreted per texel on the GPU by one fixed shader
(`ShaderSource.cpp:120`), with a capped op count reported per row. The
row is a bare expression string, or `{ "expr", "note" }`.

Status: parsed · executed on the GPU, references and curves resolved
(`CompositorSource.cpp:655`) · expression suite tested; **no render-side
test** · studio-editable, with the paint surface as its editor. No format
open items.

## Selectors

Which geometries an output or a variant touches. A selector is an array
of terms; any term may match; an empty array means every geometry.

| Term | Shape | Matches |
|---|---|---|
| `addon` | form | The armor addon. |
| `geometry` | glob | The geometry's name. |
| `texture` | glob | The geometry's texture path. |

Status: parsed · matched (`Resolve.cpp:39`) · all three terms tested ·
studio-editable.

## Outputs

What a recipe writes: a **surface output** (a material or shell slot, its
scalars, and a layer stack) or a **light output**. At most one light per
recipe; the studio refuses a second. Both take `selector`, `replace`
(drop lower-priority recipes' work on this slot), and `note`.

| Form | One line |
|---|---|
| [Surface output](#surface-output) | A layer stack composited into one PBR slot plus its scalars. |
| [Light output](#light-output) | A point light on the piece's bones. |

### Surface output

Slots write the real `BSLightingShaderMaterialPBR` fields
(`render/Binding.cpp`); the binder enforces slot exclusions with reasons
(`Binding.cpp:239`): fuzz and glint are suppressed when the material
carries a coat model, coat and subsurface contend.

| Parameter | Shape | Meaning |
|---|---|---|
| `target` | `material` \| `shell` | The armor's own material, or the cloned shell's. |
| `slot` | see the slot table | The written slot. |
| `stack` | array of [layers](#layers) | Composited in order. |
| `selector` | [selector](#selectors) | Which geometries. |
| `replace` | bool | Drop lower-priority stacks and scalars on this slot. |
| `resolution` | `full` \| `half` \| `quarter` | Override the slot's default target size (a fraction of the material's native map). |
| `note` | note | |

Per-slot scalars, required as marked:

| Slot | Required scalars | Optional scalars | Default resolution |
|---|---|---|---|
| `diffuse` | — | — | half |
| `emissive` | `strength` (param) | — | quarter |
| `rmaos` | — | — | half |
| `normal` | — | — | full |
| `height` | `scale` (param) | — | full |
| `fuzz` | `color`, `weight` | — | quarter |
| `glint` | — | `screenSpaceScale`, `logMicrofacetDensity`, `microfacetRoughness`, `densityRandomization` (params) | quarter |
| `coat` | `roughness`, `level` | — | quarter |
| `subsurface` | `color`, `thickness` | — | quarter |

Status: parsed · every slot and scalar wired into the material
(`Binding.cpp:274` on), resolution honoured (`ManagerApply.cpp:179`) ·
parse and merge tested; **render side integration-only** ·
studio-editable. The response slots (fuzz, glint, coat, subsurface) are
genuinely written, not stubs.

### Light output

A point light on the bones a piece is skinned to, inverse-square under
Community Shaders' ISL (`render/Light.cpp`).

| Parameter | Shape | Default | Meaning |
|---|---|---|---|
| `bones` | `{ "skinned": { "max", "minShare" } }` or `{ "named": [nodes] }` | required | Skinned picks the piece's weightiest bones; named lists them, one emitter per node. `minShare` is the least skin-weight share a bone needs, floored at 0.3 to bound the light count (in the schema since 2026-09-21). |
| `offset` | vec3 | — | Bone space. |
| `color` | color | required | |
| `intensity` | param | required | |
| `size` | param | — | ISL source size, 0.01..50. |
| `cutoff` | param | — | ISL cutoff override, 0.01..1; 1 is the default. |
| `shadow` | bool | false | Shadow-casting; fixed at creation, subject to the engine's shadow-light cap. |
| `selector`, `replace`, `note` | | | As on surface outputs. |

Status: parsed · placed, updated, and shadow-capable · merge replace
tested; **placement untested** · studio-editable. `bulb` was cut from
format 1 on 2026-09-21; the freeze writing reserves the word for
format 2.

## Layers

One entry of a surface output's stack, composited in order on the GPU
(`Compositor.cpp`, `TextureLabPass.cpp`).

| Parameter | Shape | Default | Meaning |
|---|---|---|---|
| `source` | ref or `[r, g, b]` | required | `@source`, `@mask`, or a constant color. |
| `curve` | curveRef | — | Shapes the source per texel, via the curve LUT. |
| `blend` | see the blend table | replace | |
| `opacity` | param | required | |
| `color` | color | — | Tints the source. |
| `mask` | ref | — | An `@mask` gating the layer. |
| `channels` | 1..4 of `rgba` | all | Which target channels the layer writes (`ChannelBits`, `Compositor.cpp:34`). |
| `note` | note | — | |

| Blend | Meaning |
|---|---|
| `replace` | The source overwrites. |
| `multiply`, `add`, `subtract`, `screen` | Arithmetic on the target. |
| `reorient` | Normal-map reorientation; allowed only on a normal stack (`BlendAllowed`). Named `normal` until 2026-09-21; `lerp` (byte-identical to `replace` in the shader) was dropped the same day. |

Status: parsed · `channels` and all six blends wired to the shader ·
parse-tested; render side integration-only · studio-editable.

## Shell

A clone of the geometry the plugin owns: its material kind, blend, and
pose. All fields honoured in `render/Shell.cpp`; the pose math in
`mesh/ShellPose.cpp` is value-tested. Plan G (the whole pose through the
render) is implemented; its in-game checkpoint is still owed.

| Parameter | Shape | Default | Meaning |
|---|---|---|---|
| `material` | `pbrCopy` \| `vanilla` | pbrCopy | Copy the piece's PBR material, or a vanilla effect material. |
| `blend` | `additive` \| `alpha` | additive | |
| `depthBias` | bool | true | Pull the shell forward to avoid z-fighting. |
| `alphaTest` | number 0..1 | 0 | Discard threshold. |
| `opacity` | param | — | The shell's opacity; named `alpha` until 2026-09-21. |
| `rimPower` | param | — | Vanilla material only. |
| `emissive` | param | — | Vanilla material only. |

Pose, all optional:

| Parameter | Shape | Meaning |
|---|---|---|
| `inflate` | vec3 | Fractions per bone-space axis; X runs along the bone. |
| `offset` | vec3 | World space. |
| `scale` | param | About `scalePoint`. |
| `scalePoint` | `[x, y, z]` | |
| `spin` | param | Turns. |
| `spinAxis` | `[x, y, z]` | |

Status: parsed · honoured · pose value-tested (`shellpose_tests.cpp`) ·
studio-editable.

## Variants

Per-armor constant overrides: a named variant replaces listed signals
with constants when its key matches. A variant cannot change structure.

| Parameter | Shape | Meaning |
|---|---|---|
| `name` | string | The variant's name. |
| `key` | `{ "armor": form }` or `{ "selector": selector }` | When the variant applies. |
| `overrides` | map of signal name to value | The replaced constants. |

Status: parsed · serialized · validated · studio-editable · **wired
2026-09-21**: `InstanceVariant` (`planners/ActorPlan.cpp`) picks the
first variant, in file order, whose armor or selector matches any of the
instance's placements, and `Manager::InstanceFor` compiles the varied
signal graph for that instance; tested in `actorplanning_tests.cpp`.
Overrides stay values only (decision, item 26); an image-path override
waits for format 2, so a per-set texture still needs a recipe copy.
