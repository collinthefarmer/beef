Status: history. The deferrals it lists were closed by the 2026-09-13 cleanup
checkpoints and the critique plans. Names and paths here predate the critique
remediation of 2026-09-14 (Plan C's file moves and Plan D's renames);
the root `README.md` lists the current set.

# wave 3 follow-ups and tidy triage

The engine/ and render/ clusters build into the first DLL: zero-warning, links
with no undefined symbols. This records what the reduce stage accepted, what it
deferred, and the two seam gaps the integrator surfaced.

## Tidy baseline triage (engine/ + render/)

`docs/wip/tidy-baseline.txt` grew from 23 to the engine/render set. The new
findings fall in three buckets, per `docs/buildup-plan.md`'s inspect stage.

**Cleared by the reduce stage (real improvements):**
- `performance-move-const-arg` (ManagerRecipes) — dropped a `std::move` on a
  const lambda capture that had no effect.
- `readability-simplify-boolean-expr` (CompositorSource flat-map test) — applied
  DeMorgan.
- `bugprone-implicit-widening-of-multiplication-result` (RuntimeTexturesPass
  `SampleMaterial` static_assert) — cast an operand to `std::size_t`.
- `cppcoreguidelines-special-member-functions` (RendererLock ×2) — declared the
  move operations deleted alongside the deleted copies.

**Accepted by scope — engine-interop false positives clang-tidy cannot reason
about (and the no-comments rule forbids inline `NOLINT`, so they stay in the
baseline, not suppressed):**
- `bugprone-exception-escape` on `IsPBRProperty`, `~LightBinding`, and the
  `LoadedRecipe` global's implicit dtor. The `noexcept`/destructor paths call
  engine virtuals and Address-Library relocations that CommonLibSSE does not
  mark `noexcept`; they do not actually throw. The `noexcept` is correct and
  kept.
- `bugprone-suspicious-stringview-data-usage` (RuntimeTexturesLab passing
  `Identity::kName.data()` to `D3DCompile`'s source-name param) — `kName` is a
  compile-time string literal, so `.data()` is null-terminated in practice.
- `cppcoreguidelines-owning-memory` (RuntimeTexturesLab reinterpret_cast of a
  renderer-data pointer) — a cast of an engine-owned pointer, not an allocation.

**Accepted by policy — thin-adapter and per-tick/per-texel size:**
- `readability-function-size` / `readability-function-cognitive-complexity` on
  the Manager apply/tick glue, the compositor stack/source functions, and the
  RuntimeTextures pass functions. These are the engine adapters and the
  per-texel/per-pass paths `docs/conventions.md` keeps flat by design. They have
  no native test oracle (wave 3 is not native-testable), so decomposition that
  compile-green cannot prove safe is deliberately not attempted; correctness
  rides the wave-5 checkpoint.
- `performance-inefficient-vector-operation` (ManagerApply push_backs) — these
  are apply-time (load) paths; `conventions.md` says do not optimise a load-time
  path on speculation, so the `reserve()` is not added.

## Done: pure cores extracted from the engine adapters (native-tested)

Rather than accept every adapter function-size finding, the pure logic trapped
in the engine-only Manager TUs was extracted into the native-tested layer with
tests, and the remaining engine orchestration was decomposed into named phases
(behaviour-preserving, verified by the central DLL build + full native suite):
- `planners/ActorState`: `MatchesForPiece`, `PlacedIndexOf`, `InstanceOfPlaced`,
  `ThirdPersonPiecesOfInstance` — the placement-index queries (were in
  ManagerSnapshot/ManagerApply). Covered by `actorstate`/`placementlookup` tests.
- `studio/RecipeSnapshot`: the recipe-row projection (was ManagerSnapshot's
  per-recipe assembly). Covered by `recipesnapshot` tests.
- `studio/ResolveOutput`: `ResolveOutput`/`ResolveLight` — the scalar/opacity/
  colour resolution of an output through a `SignalState` (was inline in
  ManagerTick's `RenderGeometry`/`UpdateLights`, and duplicated in
  ManagerSnapshot's geometry loop). Covered by `resolveoutput` tests.
- Engine-internal dedup: `NowMS`/`TargetFor`/`OutputAt` (re-derived across the
  four Manager TUs by the parallel decomposition) lifted to
  `engine/ManagerShared`.

`ManagerTick`/`ManagerApply`/`ManagerRecipes` are now sequences of named phases
over these helpers. The function-size findings that remain are the phases that
still need an `RE::`/`render::` type (genuine engine orchestration) plus the
per-texel/per-pass render functions — accepted by policy; their correctness
rides the wave-5 checkpoint.

## Deferred: render-internal helper de-duplication

The `render/` fills re-derived the same file-local helpers (not a bug, not a
tidy finding after the RendererLock fix). Deferred to a dedicated pass: create a
render-internal header and move these identical definitions there
(compile-verified, no behaviour change):
- Lab D3D internals across `RuntimeTexturesLab/Pass/Readback.cpp`: `Failed`,
  `Release<T>`, `DataOf`, `RendererLock`, and the cbuffer mirror structs
  `Constants`/`ProgramConstants`/`RippleConstants`/`ClassifyConstants` (with
  their `Program::Op` and size static_asserts).
- Compositor helpers across `Compositor/CompositorSource/CompositorBake.cpp`:
  `Lower`, `RealTexture`, `MapOf`, `DescribeTexture`, `SamplingNow`.
- `ToNi(const Vec3 &) -> RE::NiColor` across `Binding/Shell/Light.cpp`.
- Optional: `ManagerSnapshot`'s geometry loop can now read `Studio::ResolveOutput`
  instead of its own hand-rolled scalar/opacity resolution (align on the
  `named` flags, not a running index — per the ResolveOutput author's note).

## Seam gaps the integrator surfaced (need decisions before wave 5)

1. **`textureScale` → pixel size. RESOLVED.** `textureScale`
   (`kQuarter`/`kHalf`/`kFull`) is a fraction of the *target geometry's* native
   texture resolution, not a fixed pixel size. `ManagerApply::RuntimeSizes` now
   reads the native resolution from the geometry's material maps
   (`TextureLab::ExtentOf` over rmaos/diffuse/normal/displacement, taking the
   largest side), sets `maxSize` to that native size (clamped to `TextureSize`'s
   64..4096) and the requested `size` to native / {1,2,4} for full/half/quarter.
   From-black slots then render at the scaled size and edit-existing slots at
   their own map resolution up to the native ceiling, matching the Compositor
   rule in `REFERENCE.md`. Falls back to `TextureSize::kMax` when the material
   exposes no real map.
2. **`MatchActor` ignores the studio `View`. RESOLVED.** Restored pin-injection
   and match-time isolate via a resolver hook rather than a result-transform: a
   pure post-pass would have had to re-implement the planner's private
   `SurfacePlacements`/`InstanceFor` in the adapter (the duplication
   `conventions.md` forbids). Instead `MatchActor` gained a 3-arg overload taking
   a `RecipeResolver` (`(const Piece &, std::size_t) -> vector<ResolvedRecipe>`);
   the existing 2-arg version delegates to it with plain `Resolve`, so its
   signature, behaviour, and native tests are unchanged and the planner stays
   pure. `Manager::MatchRecipes` builds a `PieceRef` per piece and passes a
   resolver that wraps `Resolve` with `Studio::ViewedRecipes`, so pins and
   isolate apply at the point resolution happens, with all placement-building
   still in the planner. Verified: DLL zero-warning, full native suite green
   (incl. `planners_actorplanning_tests`).
