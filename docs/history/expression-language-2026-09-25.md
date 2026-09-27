Status: proposed, 2026-09-25. Basis: authoring the
[procedural pattern cookbook](../procedural-pattern-cookbook.md) and the
[reusable fragments](../recipe-fragments-cookbook.md) against the current
format, then inspecting the schema, parser, validator and shader. No item here
is a measured runtime defect. The evidence is authored recipes that parse and
validate, and source lines that show why they had to be written that way.

# Recipe expression language and vocabulary alignment

Sections 1, 2, 4, 6 and 7 change the recipe format. Section 9 changes
vocabulary. Both are frozen with the format contract in section 5 of the
[alpha preparation plan](alpha-preparation-2026-09-22.md). Decide each one
before that contract closes, or defer it past alpha deliberately.

Sections 3, 5 and 8 change no authored file. Section 3 removes per-tick work
from static masks and can land first, independently of every format question.
Section 1 is the structural change. The scattered rules an author has to learn
today, and several of the defects below, are consequences of four authoring
surfaces over one evaluator; unifying them deletes rules rather than documenting
them. Sections 5, 9 and 10 shrink once it lands, and each says so.
Section 11 depends on the rest.

## 1. One expression language

A curve, a mask expression, a signal expression and a parameter are four
authoring surfaces over one evaluator. Their differences are enforcement of
implementation shortcuts, not properties of the domain, and the author learns
each one as a separate rule.

The evidence: `Program::Evaluate` reads a row through `Op::kRef` from
`Inputs.refs` whatever the context. `ApplyCurve` builds `Inputs` with only `x`
and `mean` and leaves `refs` and `curves` empty, and `ParseCurve` then rejects a
curve that references a row or calls another curve. The restriction exists so
that the empty spans are never observed. On the GPU the same call is a lookup
fetch, so a curve that reads a row cannot be a baked lookup; it has to be
inlined, which the interpreter already supports because inlined ops are ordinary
ops over ordinary refs.

The target is one language whose contexts differ only in which names are bound
and in what the evaluator may reduce, so that the single remaining boundary is
dimensional: a per-tick scalar cannot sample a per-texel field, and only an
explicit reduction crosses it.

- [ ] **Make a curve a named function with parameters, and let it read rows.**
  The body binds its parameters; `x` stops being a magic atom and becomes an
  ordinary bound name, so using it outside a curve becomes an unbound-name error
  instead of a silent 0. A parameterised function is also the reusable
  expression fragment the cookbook needs: the rotated-coordinate clause takes
  two coordinates, which is exactly why no present curve can hold it.
- [ ] **Let the compiler choose lookup or inline, and never ask the author.**
  Bake a body that reads no rows, or only rows whose value cannot change, into
  the 256-entry lookup as today, substituting those values and rebaking when a
  tuning edit changes them. Inline a body that reads a time-varying row into the
  calling program. State the cost in the same place: an inlined body spends ops
  against `kMaxExpressionOps`, which is also the interpreter's array size, so
  three call sites spend it three times. There are four lookup slots and they
  are unchanged by this.
- [ ] **Thread the curve and ref spans through a nested call.** `Op::kCurve`
  passes the caller's `mean` but no `refs` and no `curves`, so a nested call
  today would silently evaluate to its own argument. That is the whole reason
  for the "a curve cannot call another curve" rule. Pass both spans and the rule
  goes away.
- [ ] **Replace the `mean` atom with `mean(@row)`.** As an atom it means three
  different things: the source's mean in a layer curve, a flat 0.5 in a mask,
  and a hardcoded 0.5 in a curve called from a mask. Named as a reduction over
  an operand it means one thing everywhere, and the question of which source it
  refers to disappears. The readback that computes it already exists
  (`src/render/TextureLabReadback.cpp`).
- [ ] **Decide whether a reduction lets a signal read a field at all.** A signal
  cannot sample a texel because it has one value per tick and a mask has one per
  texel. That is dimensional and should stay. A reduction is the honest
  exception, and `mean(@source)` in a signal is implementable with the existing
  readback, at the cost of a GPU-to-CPU transfer and its latency. Decide whether
  to allow it, and if so which reductions, rather than leaving the prohibition
  absolute and unexplained.
- [ ] **Decide whether a parameter accepts an expression.** A `param` is a
  number or a bare `@name`, so `"opacity": "@level * 0.5"` is refused although
  the same arithmetic in a signal row is ordinary. Desugaring it at load into an
  anonymous expression signal removes the third string-shaped field type and the
  most likely authoring mistake. The cost is real and should decide it: an
  anonymous row needs a generated name for the `where` convention, for the
  editor's row list, and for a rename.
- [ ] **Give rows one namespace and reject a duplicate at load.** Today a source
  and a signal may share a name, and a mask resolves sources, then masks, then
  signals, so the signal is silently shadowed. The editor already treats the
  four sections as one namespace when it picks a fresh name
  (`src/studio/Names.cpp`), and no shipped recipe or template has a collision,
  so this ratifies existing behavior at no migration cost. With one namespace
  `@name` means one thing in every context, and the context only decides whether
  that row is readable there.
- [ ] Verify: a curve reading a constant row, a curve reading a wave, a nested
  call, and a parameterised call each evaluate identically on the native
  interpreter and in the shader. A body that overruns the op budget when inlined
  reports the limit against the call site. A duplicate row name is rejected with
  both sections named.

## 2. Complete the coordinate vocabulary

- [ ] **Add `atan2`.** The function table holds 21 entries and no angle function
  (`src/recipe/Expression.cpp`), so polar patterns are unreachable: no spokes,
  no spiral, no angular sweep, no wedge. The cookbook builds concentric rings
  and cannot build their partner. Define the branch cut and the zero-vector
  result, and test CPU and GPU agreement.
- [ ] **Add component access.** `dot(@row, [1, 0])` is the only way to read a
  component. It costs five ops against the 256-op ceiling that is also the
  shader's code array size, and it appears about forty times across the two
  cookbooks. Choose one spelling and keep the vector family's type rules, which
  reject a scalar operand at check time.
- [ ] Verify: both go through type checking, bounds and CPU/GPU parity, and join
  the existing malformed-input suites.

## 3. Stop repeating work that cannot change its result

Nothing in this section changes an authored file. The stack-level gate is
already accurate: `src/render/Compositor.cpp` takes a stack's animation from
`IsAnimated(recipe, Output)`, so a recipe with no time-varying input renders
once and then early-returns. The per-tick waste below happens inside stacks
that are animated for some other reason. The parse waste happens on every
rebuild, animated or not.

- [ ] **Decide a mask's animation from the signal kinds it reads, not from the
  fact that it reads one.** `src/render/CompositorSource.cpp` marks a mask
  animated for any signal reference, whatever that signal is, and propagates the
  flag to every mask that depends on it. So a mask that names only constant
  signals is re-rendered on every tick of an animated stack although its result
  cannot change. In the travelling-packets entry of the pattern cookbook, the
  band pattern reads four constant signals and the coverage mask depends on it;
  both re-render each tick, while only the activity masks need to.
- [ ] **Use the analysis that already exists.** `AnimationQuery`
  (`src/recipe/Vocabulary.cpp`) returns false for a constant signal and folds an
  expression signal through its own time use and references. It is exposed as
  `IsAnimated` and is what the stack and the cross-actor sharing checks use. The
  mask preparation path computes that value and then discards it: the accurate
  result is overwritten by the crude flag whenever the mask is actually
  rendered, so it survives only where nothing renders.
- [ ] Keep the two other reasons a mask is animated: its own use of `time`, and
  an animated source among its dependencies.
- [ ] **Stop a permanently failed stack from retrying every tick.** `Run` clears
  `renderedOnce_` before attempting a render, and one layer whose source or mask
  has a problem aborts the whole stack through `RenderShownInputs`. A permanent
  fault, such as an unresolvable image path or a mask over the fan-in limit,
  therefore repeats a full input walk on every tick and publishes nothing. Skip
  the stack until its inputs change, and report the fault once. Decide
  separately whether one bad layer should still cancel its siblings; cancelling
  keeps a partial composite off the armor, so state the choice rather than
  leaving it to the control flow.
- [ ] **Parse each mask expression once per recipe.** No program cache exists.
  `SingleChannelOf` parses a mask's text only to test for the one-reference
  shortcut and discards the result; `PrepareRenderedMask` parses the same text
  again; `AnimationQuery::Mask` parses it a third time and recursively parses
  every mask it references, memoised only inside one query object that
  `IsAnimated` constructs fresh per call. `IsAnimated` runs per prepared mask,
  per prepared source and per output stack, so a mask used by several layers is
  parsed many times per rebuild. The recipe is immutable between rebuilds, so a
  parsed program keyed by recipe and row name removes nearly all of it. This
  sits in the rebuild path that the long-session analysis measured at 5,478
  refreshes.
- [ ] Verify: count mask passes per tick for an animated recipe whose placement
  masks are static, and confirm only the time-varying masks re-render. Confirm a
  recipe with one unresolvable layer input renders no repeated passes and logs
  once. Compare refresh counts with the fixture used for the long-session
  baseline.

## 4. Decide intermediate precision against target cost

- [ ] **Decide the mask target format.** `src/render/RenderTargetPool.cpp` sets
  `DXGI_FORMAT_R8G8B8A8_UNORM` for every target, so a named mask quantizes to
  256 steps and clamps to 0..1. That single line produces the format's most
  intrusive authoring rule: signed coordinates, cell indices and values above 1
  must stay inside one expression. It also produces the cookbook's caveat that
  fine UV coordinates lose precision when shared through a row.
- [ ] **Weigh the decision against target storage.** A half-float target doubles
  per-texel storage. The preserved testing session estimated 3.66 GiB of target
  storage at peak; see the long-session follow-up in section 5 of the alpha
  plan. Prefer a shared expression from section 1 over a new mask row: an
  inlined or looked-up body adds no target, a shared row adds one.
- [ ] **Account for the readback path.** Mean and sample readback assert
  `R8G8B8A8_UNORM` (`src/render/TextureLabReadback.cpp`), so a format change
  reaches mean luminance, channel means and the sampling preview.
- [ ] **Account for per-resolution duplication before advising reuse.** The mask
  cache is keyed by recipe, name and pixel count
  (`src/planners/RecipeTextureCache.h`), so one mask row requested at two sizes
  becomes two targets, and so does every mask it depends on. Slot resolution
  defaults disagree: normal and height are full, diffuse and rmaos half,
  emissive and the response slots quarter. An author who shares a coverage mask
  between an emissive output and a diffuse output, and omits `resolution` on
  both, pays for two copies of the whole chain. Mesh bakes are keyed the same
  way (`src/mesh/Mesh.h`), so the duplication reaches the rasterised bakes
  underneath, which cost more than a mask pass. Bakes at least live on the mesh
  entry and are shared between recipes; masks are keyed by recipe and are not.
  Decide whether a shared mask renders once at the largest requested size, and
  whether the editor reports the duplication.
- [ ] Verify: the same recipe renders identically at the chosen format, the
  readback suites pass, and target storage is measured before and after with the
  fixture used for the long-session baseline.

## 5. Reject context-invalid atoms at load

Section 1 dissolves most of this: `x` becomes a bound parameter name and `mean`
becomes a reduction over a named operand, so both stop being context-dependent
atoms. These items are what remains, and they are worth landing first because
they are rejections rather than changes.

- [ ] **Reject `time` in a curve.** It reads 0 and is then baked into the
  256-entry lookup (`src/render/CompositorSource.cpp`), so the value is
  permanently wrong rather than stale. `Program` exposes `UsesTime`
  (`src/recipe/Expression.h`) and no validation consults it. A curve that should
  vary with time is section 1's inlined body reading a time-varying row.
- [ ] **Reject `x` and `mean` outside a curve until section 1 lands.** Both
  parse and evaluate today: in a mask `x` reads 0 and `mean` reads 0.5
  (`src/render/ShaderSource.cpp`). `UsesX` and `UsesMean` already exist. The
  studio's live check refuses `x` outside a curve (`src/studio/FieldCheck.cpp`),
  so a hand-written file carries an error the editor would have refused.
- [ ] **Make the two checkers agree.** They disagree in both directions. The
  studio catches `x` outside a curve and the loader does not; the loader rejects
  a row reference in an inline curve with a message that names the boundary,
  while the studio's curve check delegates to the signal-context expression
  check and accepts it. Decide which surface owns each rule and test both
  against the same cases. Section 1 removes the second disagreement by allowing
  the reference.
- [ ] Verify: a native test per rejection, and the studio shows each error on
  the offending row following the `where` convention.

## 6. Normalize source decoding

- [ ] **Give each source a declared decode so a mask reads real units.** Four
  conventions are hand-written at every use site: a normal bake needs `v * 2 -
  1`, `distance` and `position` need `* 256`, chart, component and cluster ids
  need `* 255`, and every other source is raw 0..1. Each convention costs a
  caveat sentence in the fragments cookbook and a magic number in each
  expression.
- [ ] Keep the stored encoding. Change only what a mask reads.
- [ ] **Decide whether an id compare gets a helper.** Two fragments hand-write
  `1 - step(0.5, abs(@row * 255 - id))` to select one integer id.
- [ ] Decide whether this follows section 3, because an exact signed decode
  depends on the storage format.
- [ ] **Decide whether an rgb image read keeps its silent auto-normalization.**
  An image source with `channel: "rgb"` is rescaled so its mean luminance
  becomes 0.5, with the divisor clamped at 0.05, so a dark texture is amplified
  up to tenfold (`src/render/CompositorSource.cpp`). The same file read as
  `luma` or as a single channel is raw. Nothing records this: not the schema
  description, not `REFERENCE.md`, not either cookbook. Changing `channel` from
  `luma` to `rgb` therefore changes magnitude for a reason the author cannot
  see. Keep it and record it as a fact under the module's heading in
  `REFERENCE.md`, or make it explicit in the source.
- [ ] Verify: goldens for each source kind before and after, and the importer
  templates load unchanged.

## 7. Make trigger polarity and `ramp` honest

- [ ] **Change the trigger default so an unshaped trigger is dark at idle.** The
  raw value is normalized age with 1 at idle, so omitting a release curve lights
  the effect exactly when nothing is happening. The two cookbooks warn about
  this four times between them, and the fragments file states it in its
  composition contract. A default that documentation must repeat that often is
  the wrong default. Choose one: default the envelope to `1 - x`, add an
  envelope shorthand, or invert the raw value to 0 at idle.
- [ ] **Give `ramp` a trigger input, or remove it.** It evaluates to `from + (to
  - from) * saturate(time / seconds)` against the raw instance clock with no
  start offset (`src/recipe/Signals.cpp`), so it can run only once per instance
  and is otherwise one `expr`. `accumulate` and `counter` both take a trigger.
  The cookbook's growth-front entry has to warn that the ramp is not a fresh
  timer per hit or per editor visit.
- [ ] Migrate the shipped recipes and the importer templates in the same commit
  as the polarity change.
- [ ] Verify: signal suites cover idle value, first firing, retrigger and
  expiry. Both changes alter visible output, so they need an in-game checkpoint;
  give the log lines to look for.

## 8. Close the schema and validator gaps

- [ ] **Reject an output scalar the slot does not accept.** `src/recipe/Words.h`
  records each slot's scalar list. The schema puts all eleven scalars on one
  output object and uses conditionals only to require the right one.
  `src/recipe/Validation.cpp` then loops every scalar field and range-checks
  whatever is present without asking whether the slot accepts it. An emissive
  output carrying `thickness` loads clean and the thickness is dropped.
- [ ] **Reject a `stack` on a slot that takes no map.** The glint slot is
  declared with no map, and the schema states that glint is its four scalars
  alone, yet a glint output with layers is accepted.
- [ ] **Replace the `channels` string with a checked set.** The pattern admits
  `"rrrr"` and `"aa"`, where duplicates and order carry no meaning.
- [ ] **Reject a trigger whose `max` exceeds what a ripple keeps.** A ripple
  renders at most eight firings and silently stops filling
  (`src/render/CompositorSource.cpp`), while a trigger's `max` is bounded only
  by the 32-bit maximum in the schema. Both numbers are known before anything
  renders, so a trigger feeding a ripple can be checked at load. The footstep
  fragment stays inside the cap by authoring choice, not because the format says
  so.
- [ ] **State the per-mask fan-in limits where an author can find them.** A mask
  reads at most 8 images, 16 names and 4 curves (`src/render/TextureLab.h`),
  enforced at prepare time with a readable message. `REFERENCE.md` records that
  the shader arrays are sized to those constants and never states them as
  authoring limits; the schema and both cookbooks omit them. The four-curve
  ceiling is tight against the shared functions in section 1, whose looked-up
  bodies consume those slots while inlined bodies consume ops instead.
- [ ] **Remove or document the one-reference mask cliff.** A mask whose
  expression is exactly one reference to a material source is sampled directly
  and costs no target; one extra operation in that row allocates one
  (`SingleChannelOf`, `src/render/CompositorSource.cpp`). An image source in the
  same position does not qualify. The cliff is invisible to the author, and it
  exists because a layer's `mask` accepts only a mask row while its `source`
  accepts a source, a mask or a constant colour, which forces trivial wrapper
  rows. Decide whether `mask` should accept a source directly, and whether the
  shortcut should cover every direct texture read.
- [ ] Add these as differential cases under the pending schema and parser
  agreement item in section 5 of the alpha plan.

## 9. Resolve vocabulary collisions

- [ ] **`payload` names two things:** the value type declared on a trigger, and
  the signal kind that reads the value. Rename one.
- [ ] **`replace` names two scopes:** recipe-level `merge` and the per-output
  flag. Rename one, or separate them in the schema descriptions.
- [ ] **`efsh` and `enchantment` disagree about implicit context.** The
  `enchantment` signal reads the matched enchantment implicitly
  (`src/engine/Environment.cpp`), while `efsh` requires an explicit record even
  when the recipe is keyed on `effectShader`. A recipe cannot say "the shader
  that matched". Add the implicit form, or record why it stays explicit.

## 10. Make the field rules discoverable

The rules themselves are coherent, and each one follows from an implementation
fact: a signal is a per-tick scalar evaluated on the CPU, a mask is a per-texel
program on the GPU, and a curve is a 256-entry lookup baked once. An author
meets the rules as prohibitions and has to infer the mechanism.

- [ ] **Say why a row is unavailable, not that it does not exist.** A signal
  expression naming a mask reports `unknown row '@name'`, because the signal
  typer consults only the signal graph while the mask typer walks sources, then
  masks, then signals (`SignalTypeOf` and `TexelTypeOf`,
  `src/recipe/Signals.cpp`). The row is in the file the author is reading. The
  curve path already sets the standard: "a curve is a function of x and reads no
  rows ('@name')". Give the signal path the same shape of message.
- [ ] **Record the mechanism beside each restriction in `REFERENCE.md`.** Why a
  signal cannot sample a texel, why a curve reads no rows, and why a `param`
  takes a bare reference rather than an expression are all consequences of where
  the value is computed. Both cookbooks state these as bare prohibitions.
- [ ] **Document what a name means and where a row is readable.** Section 1
  reduces `@name` to one meaning and one namespace, so what remains to state is
  which rows a context can read and why, and the shape each field accepts. Until
  the parameter question in section 1 is decided, the parameter error should
  name the remedy as well as the shape: put the arithmetic in a signal and
  reference that.
- [ ] **Offer the eligible rows where an expression is edited.** The expression
  shelf lists only the numeric literals in a field, with per-number editing and
  a promote-to-signal action (`src/menu/ExpressionShelf.cpp`). It offers no
  palette of referenceable rows and no function list, although `FieldCheck`
  already computes the eligible-name set for the field's context and
  `SignalNamesOf` already builds typed candidate lists for parameters.
- [ ] Verify: each message is asserted in a native test, including the signal
  path naming a mask; the schema descriptions are checked by the existing
  schema/parser agreement cases.

## 11. Cookbook and documentation follow-through

- [ ] After sections 1, 2 and 5 land, regenerate the affected cookbook entries
  from their facts rather than editing their sentences. The rotated-coordinate
  clause, the two radius expressions and every decode constant change.
- [ ] **Add the fragments that need no format change**, identified in the
  2026-09-25 gap review: body-part placement from a partition bake; material
  selection from two vector reads with channel extraction; square and saw wave
  utilities; enchantment magnitude; effect-shader inheritance, whose scroll
  field feeds an image source's scroll directly; animation-event globs with an
  argument filter; movement speed; a steered ripple front and a disc ripple; and
  a fixed-point falloff. Record which ones await in-game observation.
- [ ] **Add the recipe-structure section both cookbooks lack:** variants with
  overrides, which replace a named signal with a constant and drop its curve
  (`src/recipe/Variants.cpp`), and which are the format's answer to the
  cookbook's own complaint that one rotation and one density cannot fit an
  atlas; keys other than `armor`, so a recipe can generalize past one item;
  `clock` speed; output selectors and replacement; and the light output target,
  which neither cookbook produces although the fragments contract advertises
  driving light intensity and color.
- [ ] **Give the cookbooks a cost model rather than cost advice.** They tell an
  author to reuse common rows and start at half resolution without saying that a
  row shared across two resolutions duplicates, that a mask naming any signal
  re-renders per tick today, or what the per-mask fan-in limits are. Write the
  rules once, after sections 3, 4 and 8 settle what they are.
- [ ] Correct the two statements the format contradicts: a mask may read `time`
  directly, and the compositor marks such a mask animated
  (`src/render/CompositorSource.cpp`), so routing phase through a signal is a
  convention; and a declared curve can be called on any value inside an
  expression, which the files use only as a trigger envelope.

## Evidence and limits

Every finding above comes from authoring, schema reading or source inspection.
None comes from a rendered frame or a profile. Sections 3 and 6 change visible
output and need the in-game checkpoint procedure. Section 1 is the structural
change and removes rules; sections 3 and 8 are defects; the rest are costs,
spellings or documentation.
