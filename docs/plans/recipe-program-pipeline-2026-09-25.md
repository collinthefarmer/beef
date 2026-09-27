Status: proposed, 2026-09-25. Basis: authoring the
[procedural pattern cookbook](../procedural-pattern-cookbook.md) and the
[reusable fragments](../recipe-fragments-cookbook.md) against the current
format, then reading the parser, compositor and shader. The findings inventory
is kept at
[docs/history/expression-language-2026-09-25.md](../history/expression-language-2026-09-25.md).
No item here is a measured runtime defect; three items carry a game checkpoint
that would make them measured.

# Recipe program pipeline and expression language

Read with the
[surface rendering exploration](../checkpoints/surface-rendering-exploration-2026-09-24.md).
That record's section 11 asks for a recipe to describe values and the renderer
to choose what to cache, and names one gap: masks materialised as textures need
another execution representation. The pipeline below is that work, approached
from the language side.

## Decisions this plan assumes

- **Composition stays offscreen.** The plugin renders its own full-screen
  passes and hands the result to Community Shaders by writing a texture into
  the PBR material. It does not participate in the armor draw. The boundary
  refuses a mismatched material rather than corrupting a draw
  (`PbrMaterial::Bind`), and that inert failure is why the boundary stays.
- **The draw-address question is parked.** The effect atlas and direct
  procedural shading both need the game's shading changed. Neither is a
  prerequisite for anything below.
- **View-dependent appearance is already reachable.** Coat, fuzz, subsurface
  and glint are evaluated per fragment by Community Shaders from values this
  plugin writes. Only per-fragment code of our own is out of reach.
- **Generating our own composition shaders would stay inside the boundary.** It
  is a separate decision from direct evaluation and is not taken here.

## What has a deadline

One thing does. Five format changes break an authored file, and the first
recipe authored outside this project is what makes breaking one expensive. The
closed alpha creates that recipe. Everything else in this plan is ungated and
ordered by dependency alone.

No document freezes the recipe format. Section 5 of the
[alpha plan](alpha-preparation-2026-09-22.md) establishes a recipe *contract*,
and its open item checks schema and parser agreement on defaults, bounds,
references and malformed expressions. That is correctness, not stability. The
gate-5 vocabulary freeze belonged to a roadmap superseded on 2026-09-22. The
rendering exploration records the present position: there are no authored
recipes to preserve, so semantics can be clarified now.

The sections below are therefore grouped by constraint, not by pipeline
position: what must be decided before the alpha, what can land at any time,
what forms a dependency chain, and what waits on the rest.

# Before the alpha ships

## Exercise the unexercised vocabulary

Throwaway recipes through the validator and one look in game. No prose and no
cookbook entries: writing those now would write them twice. The point is to
find what is broken or awkward in vocabulary nothing has ever used, while
changing it is still free, and to catch a breaking need before the decisions
below are taken.

- [ ] Exercise the response slots: a coat, a fuzz, a subsurface and a glint
  output. Expect the constraints to bite: glint has no map and cannot vary per
  texel; Community Shaders evaluates coat, hair, subsurface, then either fuzz
  or glint; fuzz excludes the other three; coat and subsurface share one map; a
  hair material takes none of them.
- [ ] Exercise what makes a recipe general: keys other than `armor`, variants
  with overrides, and clock speed. A variant replaces a named signal with a
  constant and drops its curve, which is the format's answer to one rotation
  and one density not fitting an atlas, and no recipe has used it.
- [ ] Exercise the two signal kinds nothing has used: `efsh`, including whether
  its scroll field drives an image source's scroll as the code suggests, and
  `enchantment` magnitude.
- [ ] Exercise a light output, which neither cookbook produces.
- [ ] Record what each one needs, and add any breaking finding to the list
  below before it is decided.

## Breaking format changes

- [ ] Give each source a declared decode so a mask reads real units. Four
  conventions are hand-written at every use site today: `v * 2 - 1` for a
  normal bake, `* 256` for distance and position, `* 255` for chart, component
  and cluster ids, and raw for the rest. Landing this later invalidates every
  authored expression that writes the constant by hand.
- [ ] Replace the `mean` atom with `mean(@row)`. As an atom it means the
  source's mean in a layer curve, a flat 0.5 in a mask, and a hardcoded 0.5 in
  a curve called from a mask. This is the breaking half of the staging rule in
  the pipeline below, which is otherwise additive.
- [ ] Decide trigger polarity and `ramp`. A raw trigger is normalised age with
  1 at idle, so omitting a release curve lights the effect when nothing
  happened; both cookbooks warn about it four times between them. `ramp` reads
  the raw instance clock with no start offset, so it runs once per instance and
  is otherwise one expression. This changes visible output, so migrate the
  shipped recipes and the templates in the same commit.
- [ ] Resolve the vocabulary collisions. `payload` names both a trigger's value
  type and the signal kind that reads the value. `replace` names both a recipe
  merge mode and a per-output flag. `efsh` requires an explicit record while
  `enchantment` reads the matched enchantment implicitly, so a recipe keyed on
  `effectShader` cannot name the shader that matched.
- [x] Give rows one namespace and reject a duplicate at load. The editor
  already treats the four sections as one namespace when choosing a fresh name
  (`src/studio/Names.cpp`) and no shipped recipe or template collides, so this
  breaks nothing today and ends the silent source-over-signal shadowing inside
  a mask.

# Land at any time

No gate and no dependency on anything else here. These pay on the day they
land.

## Render defects

- [x] Take a mask's variability from the signal kinds it reads. The compositor
  marks a mask animated for any signal reference, including a constant, and
  propagates that to dependents, so static placement masks re-render every tick
  inside any animated stack. `AnimationQuery` already answers this correctly
  and is exposed as `IsAnimated`; the mask path computes it and then overwrites
  it with the crude flag.
- [ ] Parse each row's program once. `SingleChannelOf` parses a mask's text to
  test for a shortcut and discards it, `PrepareRenderedMask` parses it again,
  and `AnimationQuery::Mask` parses it a third time and recurses, memoised only
  inside a query object built fresh per call.
- [ ] Give a failed stack a state. `Run` clears `renderedOnce_` before
  attempting a render and one bad layer aborts the whole stack, so a permanent
  fault repeats a full input walk every tick and publishes nothing. Decide
  separately whether one bad layer should cancel its siblings.
- [ ] Time shader compilation at startup. `CompileShaders` already runs
  `D3DCompile` on eight entry points and the cost is unrecorded. One log line
  turns the compiled-backend question into a number.
- [ ] **Checkpoint.** Refresh and pass counts against the fixture from the
  [long-session
  analysis](../checkpoints/testing-session-analysis-2026-09-25.md), before and
  after.

## Rejections

Each one turns a silent wrong result into a named error.

- [ ] Reject an output scalar the slot does not accept. The slot tables record
  each slot's scalars, the schema lists all eleven on one object and only
  requires the right ones, and validation range-checks whatever is present
  without consulting the slot.
- [ ] Reject a `stack` on a slot that takes no map, which glint is.
- [ ] Replace the `channels` string with a checked set; the pattern admits
  `"rrrr"` and `"aa"`.
- [ ] Reject a trigger whose `max` exceeds the eight firings a ripple keeps.
- [ ] Reject `time` in a curve, and `x` and `mean` outside one until the
  pipeline's staging rule lands. `UsesTime`, `UsesX` and `UsesMean` exist and
  no validation consults them. The studio's live check refuses `x` outside a
  curve, so a hand-written file carries an error the editor would refuse; make
  the two checkers agree in both directions.
- [ ] State the per-mask fan-in limits where an author can find them.

# The pipeline

The [graph foundation checkpoint](../checkpoints/recipe-graph-2026-09-25.md) records the implemented slice and its verification.

A dependency chain. No deadline, and each step is worth landing on its own. The
system layers it implements are described in the rendering exploration; these
are the four pieces of work over them.

## 1. One graph

- [ ] Complete handle-only access downstream. The unified row table and signal/mask execution bindings are implemented; authored output and source lookup APIs still accept names.
- [x] Build one dependency graph over signals, curves, sources and masks on the
  model `SignalGraph` already sets: dependencies, topological order, cycle
  detection that marks participants inert and names the cycle.
- [ ] Carry four properties per node: value type, domain (tick value or field),
  variability (fixed, tuning-fixed, time-varying, event-driven), and coordinate
  domain. The last has one value while composition stays in the original UV
  domain. State now what a node reading two domains means: it is either
  rejected or it requires a conversion, and the planner is what inserts one. A
  label with no rule behind it is not insurance.
- [ ] Make a curve a named function with parameters, and let it read rows.
  `Program::Evaluate` already reads rows through `Op::kRef` in any context;
  `ApplyCurve` supplies empty spans and `ParseCurve` guards that shortcut. Keep
  a bare `x` working as the single implicit parameter and this breaks nothing.
- [ ] Thread the ref and curve spans through a nested call. `Op::kCurve` passes
  the caller's `mean` and neither span, which is the only reason a curve cannot
  call a curve.
- [ ] Adopt the staging rule: a field reads a tick value freely, and a tick
  value reads a field only through a declared reduction. Allowing a reduction
  in a signal adds a capability; the readback exists, at the cost of a transfer
  and its latency.
- [ ] Decide whether a parameter accepts an expression. Desugaring `"opacity":
  "@level * 0.5"` into an anonymous signal removes the third string-shaped
  field type and the most likely authoring mistake, at the cost of a generated
  name for the `where` convention, the editor's row list and renames.
- [ ] Verify: the graph answers the variability question directly, and
  cross-actor sharing uses the same property rather than a second analysis.

## 2. Backend seam

- [ ] Define the program representation the planner emits, with its resource
  bindings. Define it independently of `ProgramConstants`. If it becomes
  whatever fills the interpreter's 256 nodes, 16 refs and 8 texture parameter
  sets, it is the interpreter's input under a new name and a second consumer
  gains nothing from it.
- [ ] Define a backend capability record: maximum ops, images, names, lookups
  and stack depth, and the supported function set. The interpreter is the first
  backend and declares today's 256, 8, 16, 4 and 32.
- [ ] Carry execution site and cost shape in the same record, not just
  capacity. A backend differs in where it runs and in what things cost, and
  those decide the planner's answers as much as the limits do. The interpreter
  runs per texel of a target once per invalidation, so a materialised value
  costs storage and inlining costs recomputation per consumer. A draw-time
  backend would run per visible fragment every frame, which inverts both.
- [ ] Add `atan2` and component access as capabilities of that backend. The
  function table has 21 entries and no angle function, so polar patterns are
  unreachable; `dot(@row, [1, 0])` is the only way to read a component and
  costs five ops against the 256-op ceiling.
- [ ] Let the editor grey out what the active backend cannot do, instead of the
  format pretending a function does not exist.
- [ ] Verify: no behavior change, and the limits are read from one record
  rather than spread through the compositor.

## 3. Demand, identity and lifetime

This step gathers the facts the planner decides over. It changes nothing about
how work is arranged.

- [ ] Collect demand before acquiring anything: which nodes are wanted, at
  which sizes, for which geometry, in which coordinate domain. Today resources
  are created as preparation walks the outputs and found again in a cache keyed
  by recipe, row name and pixel count, so a row wanted at two sizes becomes two
  resources and so does everything beneath it, mesh bakes included. Slot
  resolution defaults disagree, which makes the duplication easy to cause by
  omitting `resolution`.
- [ ] Give every value an identity from the work that produces it, not from its
  row name. Two recipes computing the same bake or the same mask then resolve
  to one value, and renaming a row invalidates nothing.
- [ ] Record each value's lifetime: when it is first wanted, when it is last
  wanted, and whether it survives a tick. The planner needs this for both
  sharing decisions.
- [ ] Replace the lifecycle booleans with states: planned, acquired, valid,
  stale, failed.
- [ ] Verify: the same recipe reports one demand per distinct value, and two
  recipes sharing a bake report one.

## 4. The planner

Allocation, and the decisions are coupled: packing changes how many image slots
a program spends, which changes what is worth inlining, which changes which
values are materialised, which changes their lifetimes, which changes what can
share a texture. Storage depends on what each value carries. None is decidable
alone, which is why this step exists rather than a set of local rules.

It never has to prove a plan exists. A per-row arrangement is always available
as a fallback, so the difficulty is quality, not feasibility.

- [ ] Decide the program set, after demand is coalesced and repeated arithmetic
  is shared. Sharing repeated arithmetic is what removes the rotated-coordinate
  clause the cookbook repeats in eleven entries without anyone naming it, which
  also demotes the named functions in step 1 from a performance need to a
  readability one.
- [ ] Decide each value's representation: fold it, inline it into its
  consumers, bake it to a lookup, or materialise it. Today the choice comes
  from which section of the file the author typed in, and `SingleChannelOf` is
  a hand-rolled exception where a mask that is exactly one material reference
  costs no target while one added operation costs one. Decide too whether a
  layer's `mask` should accept a source directly, since needing a wrapper row
  is why that exception exists.
- [ ] Decide storage per materialised value as a layout: which texture, which
  channels of it, which format, and whether it needs mips. A scalar mask
  currently writes `float4(result.xxx, 1)`, so three channels are a duplicate
  and the fourth a constant; every target allocates a full mip chain although
  only the mean-read and density-sampled ones need one; and four scalar masks
  of one size on one geometry can share an RGBA texture a channel each, which
  spends one image slot instead of four. Block compression suits the bakes,
  which never change, but not anything redrawn per tick.
- [ ] Decide physical sharing: give two values one texture when their lifetimes
  do not overlap. The stack renderer already does the simplest case by bouncing
  between its own target and one scratch; this generalises it. Packing shares
  across channels, aliasing shares across time, and both are this step's calls.
- [ ] Decide bit depth last, once packing and aliasing have been accounted for.
  Every target is `R8G8B8A8_UNORM` from one line in `RenderTargetPool`, which
  is what forces signed values, cell indices and values above 1 to stay inside
  one expression, and the readback paths assert that format. Let a value
  declare the range and signedness it needs.
- [ ] State what the planner optimises and against which measurements. These
  decisions trade against each other, so a cost model is the deliverable, not a
  pile of heuristics. Take the cost shape from the backend record rather than
  assuming the interpreter's, or the model does not survive a second backend.
- [ ] Invent no cuts, and keep rejection where it already is. A single
  hand-written row over a limit is rejected today with the message pointed at
  that row; leave it there. A parameterised function is the one construct with
  no fallback, because a call site with its own arguments cannot be
  materialised, so a body too large to inline at every call site is rejected by
  name and call count. Say so in the cookbook when functions are documented.
- [ ] **Checkpoint.** Target storage measured with the long-session fixture,
  and no visual change on the demo recipes.

# Follow-through

Waits on whatever landed above, because those change what the documents say.

- [ ] Regenerate the affected cookbook entries from their facts rather than
  editing their sentences: the rotated-coordinate clause, the two radius
  expressions, and every decode constant.
- [ ] Add the entries the exercise pass validated: the response slots, the
  fragments that needed no format change, and the recipe-structure section.
- [ ] Correct two statements the format contradicts: a mask may read `time`
  directly and the compositor marks such a mask animated, and a declared curve
  can be called on any value inside an expression.
- [ ] Record the rgb image auto-normalization in `REFERENCE.md`. An image
  source read as `rgb` is rescaled so its mean luminance becomes 0.5, with the
  divisor clamped at 0.05; the same file read as `luma` is raw.
- [ ] Give the cookbooks a cost model instead of cost advice, once the demand
  and planner work has settled what the rules are.

# Parked

The effect atlas and its draw-time coordinates, direct procedural material
shading, and a compiled-shader backend. Each needs the game's shading changed
or a second execution path. Nothing above depends on them. Four things above
are what would carry them: the program representation, which a code generator
or a draw could consume; the content-addressed identity, which is the key a
bytecode cache needs; the coordinate domain and its conversion rule, which the
atlas would use; and the layout decision, of which an atlas is a larger case.
What none of them carries is the draw-side work itself: patching or replacing
the armor's shading, staging that patch independently, and identifying the same
triangles on the CPU and the GPU. Those remain the unproven part, and the
rendering exploration's open questions are where they are recorded.
