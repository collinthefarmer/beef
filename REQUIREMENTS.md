# BetterEnchantmentEffects — Requirements

This is the canonical description of what the plugin must do, how it is
built, and how it is verified. It governs the module-by-module buildup of
the new source tree.

Four artifacts hold what this document does not restate, and it defers to
them:

- `src/_old/` is the frozen previous implementation. It is excluded from
  the active build and retained as the reference for the behaviour the new tree must
  reproduce. Where prose and the frozen tree disagree, the frozen tree is
  the fact.
- `schema/recipe.schema.json` and `schema/example-magicka.json` are the
  recipe format's contract and its canonical file.
- `REFERENCE.md` holds the facts the code cannot state — engine layouts,
  Community Shaders rules, decompile lines, shader packings — grouped by
  module.
- `CLAUDE.md` holds the three rules the code obeys. They are not restated
  here except where a requirement turns on one.

## The plugin

Given a worn armor piece and its enchantment, the plugin produces — per
wearer, per frame — the PBR textures, material scalars, cloned shell
geometry, and point lights that show that enchantment on that piece. It
does this by writing Community Shaders' TruePBR material slots between
frames. It never touches the vanilla effect-shader slot
(`BSEffectShaderData`), so Dirt and Blood, flesh spells, and scripted
`PlayEffectShader` keep working on the same armor.

The unit of authorship is a **recipe**: a JSON file in format 1. A mod
author ships recipes; a user tunes them in an in-game studio; the vanilla
effect shader is only an importer seed that produces a recipe when none is
keyed to a piece. Effects are per-wearer: two actors in the same armor,
only the enchanted one glows.

Community Shaders is required. Without `CommunityShaders.dll` the plugin
logs that the emissive path is disabled and stays idle.

## Scope of this effort

- **Module-by-module buildup.** The frozen tree stays under `src/_old` as
  the behaviour oracle. Each module is written fresh into its target
  directory and replaces its frozen counterpart when it lands and passes.
  There is no in-place edit pass and no dead-code deletion pass: dead code
  is never written, and known defects are never reproduced.
- **Format 1 is frozen.** The `recipe/` module is a faithful
  re-expression of the format, checked by round-trip against the frozen
  fixtures. The format is not redesigned in this effort.
- **Native first.** Everything that can compile and be tested without the
  engine is native and tested. The engine-free surface is maximised.
- **Functional core, thin adapter.** Every engine-facing responsibility
  splits into a pure decision, tested natively, and a thin effect that
  only calls existing engine APIs.

## What the plugin must do

1. **Match** recipes to a wearer's PBR geometries by key — magic effect,
   enchantment, effect shader, keyword, armor, material, or default — and
   order matches by priority. A key held by several files belongs to the
   last file loaded with it.
2. **Evaluate** each matched recipe's signal graph once per tick, in
   dependency order, against live actor state and an event bus. A cycle,
   an unknown reference, or a bad expression makes a node inert (0 or
   black), never a crash.
3. **Composite** each output's layer stack into the target PBR slot and
   its scalars. Multiple recipes on one geometry merge into one shared
   material binding: their stacks append in priority order, and no two
   layers contend for a field.
4. **Restore** every engine field the plugin overwrote, per field, and
   refuse to restore any field another system has since taken.
5. **Isolate** per wearer: an effect on one actor never appears on another
   wearing the same item.
6. Produce the full output set through recipes: the nine PBR surface slots
   with their scalars, a cloned **shell** per geometry per recipe, and
   **point lights** on the bones a piece is skinned to. Lights are
   inverse-square under Community Shaders' Inverse Square Lighting.
7. Import a recipe from a vanilla armor-enchantment effect shader when no
   recipe is keyed to a piece, reproducing the effect-shader look with
   shipped defaults.
8. Provide an in-game **studio** to read and edit recipes live: compose
   layer stacks and signals, paint masks, undo and redo, freeze, scrub,
   and set clock speed, with validation before a change commits.

## The recipe format (format 1)

`schema/recipe.schema.json` is the contract and `Recipe.h` records are its
in-memory truth; the JSON is their serialisation. The round-trip is an
invariant: `ParseRecipe` then `SerializeRecipe` reproduces the file, and
every shipped recipe reads back identical. Defaults are omitted on write.

The model:

- **Recipe** — `keys`, `priority`, `clock`, `signals`, `curves`,
  `sources`, `masks`, `outputs`, `shell`, `variants`, plus metadata.
- **Signals** — a value that varies per tick. Sixteen kinds: `constant`,
  `pulse`, `ramp`, `efsh`, `av`, `actorState`, `enchantment`, `trigger`,
  `payload`, `counter`, `accumulate`, `noise`, `gradient`, `delta`, `smooth`,
  `expr`. The wire word `av` is the modding community's abbreviation for
  actor value. Evaluated once per tick in dependency order.
- **Sources** — a value that varies per texel, in a geometry's UV space.
  Seven kinds: `image`, `material`, `bake`, `uv`, `distance`, `ripple`,
  `materialClusters`.
- **Masks** — a per-texel expression, interpreted on the GPU by one fixed
  shader from a constant buffer, with a capped op count reported per row.
- **Curves** — a one-argument expression in `x`.
- **Outputs** — a surface output (a slot, its scalars, a selector, a
  replace flag, and an ordered layer stack) or a light output.
- **Shell** — a clone of the geometry the plugin owns, with a material
  kind, blend, pose, and alpha.
- **Variants** — replace named signals with constants; they cannot change
  structure.
- **The expression language** — one small language for every expression,
  mask, and curve: numbers, vector literals, `@name` references, the four
  arithmetic operators, comparisons yielding 0 or 1, `and`/`or`/`not`,
  `if(c, a, b)`, and a fixed function set, with `time`, `pi`, `x`, and
  `mean`. Arithmetic is component-wise on vectors; division by zero is 0.
  `recipe/Expression` is also the editor's API over expressions: a compiled
  `Program` exposes the references it uses (`References`, `Curves`) for
  rename and dependency, and the module exposes a tokeniser over expression
  text for the editor. The expression editor itself is deferred; this API
  is not.

## Architecture

### Eight directories, read in dependency order

```
src/
  main.cpp  Identity.h  Core.h  PCH.h    the process, the name, the value shapes
  Settings.*  SettingsFile.*             preferences and their INI text

  recipe/   what an effect is            (pure)
  mesh/     what an effect is applied to (pure)
  planners/ how recipes are placed, merged, and bound (pure)
  studio/   the editor's model           (pure)
  diagnostics/ bounded trace recording  (engine-free)
  engine/   how an effect reaches an actor
  render/   how an effect becomes pixels
  menu/     the editor's surface
```

`recipe/`, `mesh/`, `planners/`, and `studio/` are engine-free. `engine/`, `render/`,
and `menu/` are the adapters over them. `src` is the only include root, so
every include names its directory.

The native object library builds those four pure directories, the engine-free
diagnostic recorder, and `Settings.cpp`. The recorder performs file I/O and is
not part of the pure recipe or planning decisions.
The plugin DLL adds `main.cpp`, `SettingsFile.cpp`, and the three adapter
directories. `CMakeLists.txt` defines both source lists; neither includes `_old`.
The native test runner also compiles `engine/SessionQueue.cpp` and
`engine/ApplicationService.cpp` directly. The scheduler is injected, so load
transitions, task lifetime, application revisions, and controlled preparation/
render outcomes can be tested without SKSE or engine objects.

The diagnostic recorder and queue-context propagation are tested natively for
bounded storage, JSONL framing, concurrent event ordering, load-session identity,
and deferred command identity. Runtime traces are evidence of recorded state,
not proof of rendered pixels or visual correctness. The initial integration
checkpoint is `docs/wip/render-state-diagnostic-checkpoint-2026-09-12.md`.

### Functional core, thin adapter

For each engine-facing responsibility, the decision is pure and the effect
is a thin call over existing APIs:

- The **binding** planner computes required surfaces and their shell owner as
  a plan over records; the writer owns coupled field journals and retirement.
- The **compositor** planner decides which layers, static or animated,
  their stack order, and the cut at the highest `replace`; the execution
  runs the GPU passes.
- The **manager** is a pure state machine over actor state — apply,
  retire, tick, merge — behind a shell that owns the queue, posts tasks,
  and holds engine handles.
- The **mesh** reader fetches raw geometry; decode and analysis are pure.

If an engine module is not much smaller than its frozen counterpart, logic
has leaked across the boundary. The engine-free build target covers the
large majority of the tree.

### Data-driven

A feature lands as a row of a spec table, an alternative of a variant, a
field of the view, a render pass, or an event provider — never as a new
hand-maintained dispatch. Every enum carries one `constexpr` spec table,
one row per value in enum order, with a count assertion. A word or rule is
spelled once and read by the parser, the writer, validation, the board,
and the bindings alike.

## Safety

Memory safety is the first rule: this code is never the cause of a crash.
It rests on two disciplines.

- **Parse, don't validate.** Untrusted input enters at seven boundaries —
  a recipe file, an expression, an imported effect shader, raw mesh bytes,
  a form name, a field being typed, and the INI. Each parses once into a
  typed record; everything downstream trusts its arguments. Malformed
  input makes a row inert and a log line, never undefined behaviour. The
  bounds live in the core: expression depth, op count, and stack size; a
  depth limit on every recursive walk; the texture-size clamp; the mask op
  cap.
- **Null-check engine pointers at every use.** A pointer's validity
  belongs to the engine, not to us, so the pure core cannot own it. The
  adapter checks it every frame. Forms are looked up, never assumed.

## Constraints

- **Community Shaders coupling.** `PBRMaterial` mirrors the TruePBR
  material layout byte for byte, asserted at compile time against the
  vendored `src/cs/BSLightingShaderMaterialPBR.h`. The plugin writes only
  inside Community Shaders' material slots and relies on it rebinding the
  emissive texture each draw. Lights use the Inverse Square Lighting
  overlay. `REFERENCE.md` holds the layouts and flag packings.
- **Performance.** Per-tick and per-texel paths — signal evaluation, mask
  interpretation, the binding's writes — are written for cost and
  measured. Load-time paths — parsing, validation, resolution — are
  written for clarity; a recipe is read once. A stack is static if no
  layer reads a scrolling image, a non-constant signal, or a mask over
  one; a static stack bakes once per geometry and caches, an animated
  stack re-renders every tick. The budget is 60 Hz with twelve driven
  geometries.
- **No comments, anywhere.** The code says it through a name, a type, or a
  small named helper. A fact the code cannot state goes in `REFERENCE.md`.
- **Complete type signatures.** No `auto` in a signature; no `Any`-like
  escape hatch. Illegal states are unrepresentable where a type can
  enforce it.

## Verification

- The repeatable [in-game regression flow](docs/in-game-regression.md) exercises
  the integrated plugin with existing logs and visual checkpoints, and records
  branches that require additional fixtures or instrumentation.
- **Native first.** `recipe/`, `mesh/`, `studio/`, and every planner
  extracted from the engine modules are tested through the native suite.
  The recipe module is checked by round-trip against the frozen fixtures.
- **A module is done when four things hold:** the plugin builds at zero
  warnings, the native suite passes, the clang-tidy baseline shows only what
  the module meant to add, and the frozen files it replaced are gone from the
  build. `src/_old` itself stays whole: it is the behaviour oracle every
  module is diffed against, and the release gate in the roadmap is what
  removes it.
- **The irreducible engine surface** rides one deliberate integration
  checkpoint in the game, because native tests cannot reach it: the GPU
  pixel output, the `PBRMaterial` layout's agreement with Community
  Shaders' actual ABI, form lookup, actor reads, mesh fetch, event-sink
  wiring, the single `PlayerCharacter::Update` vtable hook, and the
  material writes and restores. The checkpoint is aimed at exactly this
  set.

## Architectural debts the buildup must fix

The frozen runtime carries four defects that motivate the rebuild. The new
tree must not reproduce them.

1. **The material fight.** Several recipes on one geometry's material each
   install a private copy, and the first is dropped on the first tick.
   The fix is one material binding per geometry, shared by the piece's
   recipes, into which stacks append — requirement 3 above.
2. **Synchronous GPU readbacks** at apply and on request hold the renderer
   lock across a map that waits for the GPU. Readbacks are asynchronous:
   copy now, poll the map later.
3. **Crowd instability.** The animation-graph sink is removed and re-added
   on every apply and retire of every NPC. A graph is watched only when
   loaded and once per actor; actors, distance, and targets are capped.
4. **Absolute texture sizes** while modded armor ships 2K and 4K. Texture
   size is relative — full, half, quarter — with a 64 floor and a 4096
   ceiling.

## Non-goals

- Weapons. Nothing may assume the biped, since a weapon is a geometry with
  a PBR material and the same pipeline, but weapons are out of scope here.
- Any effect that needs a shader inside Community Shaders, including
  view-dependent effects the PBR lighting cannot isolate.
- A general A/B or preset system beyond variants and hold-to-preview.
- The future spikes — environment-map shell, emitter output, EFSH
  membrane, projected material, refraction, back lighting, presenter
  textures without files, mask post steps, cross-piece continuity, a
  Community Shaders feature pass. They wait until the core passes.

## Build and workflow

- Build with `./build.sh Release -j 4`. More jobs exhaust WSL and kill the
  instance. Install with `./install.sh`.
- The language standard is C++23 (`CMAKE_CXX_STANDARD 23` in
  `CMakeLists.txt`); `tests/run-native.sh` compiles the native suites with
  the same `-std=c++23`. The two must not disagree.
- Work inside `nix develop`; every script names the tool it wants if you
  are outside the shell.
- The game runs on the user's machine. Batch every change that needs the
  game into one checkpoint per integration, install, and hand the user the
  exact log lines to look for.
- `Identity.h` is the only place the plugin name is spelled.
- Do not modify `decompiled/`, `reference/`, or anything under
  `/mnt/a/mods/`. Recipe files are the one exception.

## Shell palette repair checkpoint

A cloned shell must preserve effective bone world-transform references, including
flattened bones without individual scene nodes. Missing entries may be restored
only after identifying and retaining their owning tree. The shell retains private
skin data and pointer arrays; unknown owners or incompatible skins reject shell
creation. Ownership changes invalidate the shell, and retirement clears repaired
references before releasing their retained owners. Engine-free storage membership
checks have native boundary tests; live animation and zero-inflation appearance
remain an in-game acceptance check.

## Texture consumer lifetime checkpoint

Generated texture consumers retain the render target and acquisition generation,
not only an engine presenter pointer. A producer can retire while snapshots,
material journals or queued previews still use its target; the pool may recycle
it only after the last retained reference releases it. Expired generated handles
and live presenter reassignment are rejected. Static engine textures retain
ordinary engine ownership. These references preserve identity, not frozen pixels.
Preparation/publication, application lifecycle and per-field restoration are
separate follow-up checkpoints in the rendering-state plan.

## Cleanup acceptance (2026-09-13)

Material retirement must preserve external changes by coupled physical field
group, without restoring unrelated flag bits or releasing generated textures
still installed after takeover. Presenter capacity is concurrent retained usage,
not cumulative allocations over loads. Pending UI draws retain their targets
until draw submission is acknowledged; a tick delay is not completion evidence.
The concrete contracts and outstanding in-game cases are recorded in
`docs/wip/cleanup-checkpoint-2026-09-13.md`.
