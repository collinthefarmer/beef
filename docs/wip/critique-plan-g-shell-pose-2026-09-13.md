# Plan G: the shell honours its whole pose — 2026-09-13

Status: not started. Runs after Plans F to E and after UI slice 2A has
passed its checkpoint, because it reads the shell form in `Forms.cpp`.

The user decided on 2026-09-13 that the five `ShellPose` fields the format
already carries are to be implemented rather than removed. This plan is
render work with its own in-game checkpoint.

## Current state

- `src/recipe/Recipe.h:647-655`: `ShellPose{inflate, offset, scale,
  scalePoint, spin, spinAxis}`. `inflate` and `offset` are `Vec3Param`,
  `scale` and `spin` are `Param` (so all four can be driven by signals);
  `scalePoint` and `spinAxis` are literal `Vec3`.
- `src/render/Shell.cpp:94`: `InflatedTransform` builds a per-bone
  transform from `inflate` alone. `ShellBinding::Pose` at `:558` takes
  `a_inflate`, alpha and rim power, and at `:584-589` recomputes the
  skin-to-bone transforms only when `inflate` changed.
- `src/engine/ManagerTick.cpp` evaluates the shell parameters and calls
  `Pose`; read it to find the call and how `inflate` is resolved from
  `Vec3Param` through the signal state.
- `src/studio/Forms.cpp:1495-1523` already offers all six controls.
  `src/_old/` never applied the five fields either (`docs/wip/deletions.md:37`),
  so there is no previous behaviour to match; the format's schema
  descriptions in `schema/recipe.schema.json` define the intended meaning.
  Read them first; if a field's description is ambiguous, write down the
  interpretation below before coding it.

## Decisions

- **One pose record.** Add `struct ShellPoseValues { Vec3 inflate; Vec3
  offset; float scale; Vec3 scalePoint; float spin; Vec3 spinAxis; }` in
  `render/Binding.h` beside `ShellBinding`. `Pose` takes it instead of
  `a_inflate`. The tick resolves the four driven params into it.
- **Composition order.** In the shell's bind-pose space: inflate along each
  axis as today, then scale by `scale` about `scalePoint`, then rotate by
  `spin` (radians; confirm the schema's unit) about `spinAxis` through
  `scalePoint`, then translate by `offset`. Write this order into
  `REFERENCE.md` under the bindings heading with the reason: inflate is a
  per-bone skin-space operation and must come before the rigid-body
  operations that treat the shell as one object.
- **`spinAxis` zero is invalid.** `Validate` in `recipe/` reports it
  (`src/_old/Edits.cpp:1273` had the same rule); the renderer normalises and
  falls back to `+Z` if it is still zero at pose time, so the renderer never
  divides by zero regardless of what reached it.
- **Change detection.** `lastInflate_` becomes `lastPose_` of the new
  record type with `operator==`, so the transform recompute happens only
  when any component changed, as now.

## Steps

1. Read the schema descriptions and `ManagerTick.cpp`'s shell path. Record
   the unit of `spin` and the space of `offset` and `scalePoint` in
   `REFERENCE.md` before writing code.
2. Add `ShellPoseValues`; change `Pose` and its caller; keep the
   `Trace::Emit` at `Shell.cpp:572` reporting the whole record.
3. Generalise `InflatedTransform` into `PosedTransform(const
   RestSkinToBone &, const ShellPoseValues &)` composing the four
   operations in the decided order. Keep it a pure function so it can be
   unit-tested natively: put the matrix maths in `src/mesh/` or
   `src/planners/` (engine-free) and have `Shell.cpp` call it; a
   `tests/mesh/shellpose_tests.cpp` then checks identity pose is identity,
   scale about a point leaves that point fixed, spin by a quarter turn about
   `+Z` maps `+X` to `+Y`, and offset adds.
4. Add the `spinAxis` validation to `Validate` with `where` `shell`.
5. Verify `RecipeWrite.cpp` omits defaults for the five fields (the old
   writer at `src/_old/RecipeJson.cpp:1392` did) so unchanged recipes
   round-trip byte-identical; the existing round-trip test proves it.
6. Update `docs/wip/deletions.md:37-40` to "implemented".

## Acceptance

- Native suite green including the new pose maths tests.
- Round-trip test unchanged.
- Release build zero-warning.

## In-game checkpoint

Ask the user to take a recipe with a shell and, one at a time through the
studio form: set `offset` to a visible value and confirm the shell moves
without the armour moving; set `scale` to 1.2 with `scalePoint` at the
origin and then at a bone position and confirm the shell grows about that
point; set `spin` driven by a time signal and confirm it rotates about
`spinAxis`; set all back to defaults and confirm the shell matches the
previous build. Log lines: the `Trace` pose event from `Shell.cpp:572` now
carrying all six fields, and any `shell:` validation diagnostic for a zero
axis.
