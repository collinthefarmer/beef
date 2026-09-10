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

## Deferred: render-internal helper de-duplication

Parallel fills on disjoint files each re-derived the same file-local helpers
(the expected "parallel agents re-derive the same helper" merge cost). None is a
bug and none is a tidy finding after the RendererLock fix, so the lift is
deferred to a dedicated pass. When done, create a render-internal header and
move these identical definitions there (compile-verified, no behaviour change):
- Lab D3D internals across `RuntimeTexturesLab/Pass/Readback.cpp`: `Failed`,
  `Release<T>`, `DataOf`, `RendererLock`, and the cbuffer mirror structs
  `Constants`/`ProgramConstants`/`RippleConstants`/`ClassifyConstants` (with
  their `Program::Op` and size static_asserts).
- Compositor helpers across `Compositor/CompositorSource/CompositorBake.cpp`:
  `Lower`, `RealTexture`, `MapOf`, `DescribeTexture`, `SamplingNow`.
- `ToNi(const Vec3 &) -> RE::NiColor` across `Binding/Shell/Light.cpp`.

## Seam gaps the integrator surfaced (need decisions before wave 5)

1. **`textureScale` → pixel size.** The frozen Manager read
   `settings.runtimeTextureSize` / `glossMapSize`; those settings were removed
   (dead per `deletions.md`) and the live setting is `Settings::textureScale`
   (`kQuarter`/`kHalf`/`kFull`). `ManagerApply::RuntimeSizes` currently maps
   these to 1024/2048/4096 with the max at `TextureSize::kMax`. This is a guess;
   the intended base resolutions want confirming (a checkpoint-tunable — a wrong
   value shows as too-coarse or too-fine textures, not a crash).
2. **`MatchActor` ignores the studio `View`.** The frozen match ran
   `Studio::ViewedRecipes(Resolve(...), piece, ref, view, loaded)` for
   pin-injection and match-time isolate filtering. The wave-2 planner
   `MatchActor` calls plain `Resolve(piece.keys, store)` with no `View`, so
   **pinning a recipe onto a piece it does not natively match is lost**
   (render-time isolate via `view_.OutputShown`/`RecipeShown` still works). This
   is a wave-2 planner boundary: either thread the `View` (pins + isolate) into a
   Manager pre-pass over `MatchActor`'s result, or extend the planner. Pinning is
   a studio-editing feature, so this matters most for the wave-5 "studio editing
   a live recipe" checkpoint.
