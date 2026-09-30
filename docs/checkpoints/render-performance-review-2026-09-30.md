Status: fixes implemented, in-game check pending. Review of commits 9007382
(layer fields, exact lookups and pow) and ba39d70 (generated shaders). The
Resolution section records what each finding became.

# Render performance review, 2026-09-30

A read-only review checked the two commits for correctness, readability,
naming and organization against the rules in `CLAUDE.md`. It found no path
where a fused or generated result differs from the unfused or interpreted
result by more than one 8-bit step. `StackShapeOf` reads the same conditions
as `RenderStack`'s constants, numbers stay out of the stack shape, and the
generated program code and `RunProgram` treat empty pops, the 32-slot push
limit and `idx` the same way. The reviewer did not run the tests.

Verdict: **CONFIRMED** means verified in the code; **PLAUSIBLE** means
reasoned but not verified.

## Correctness

| Id | Verdict | Where | Finding | Fix |
|---|---|---|---|---|
| C1 | CONFIRMED | `RenderFusion.cpp` `AttachField`; `RenderInstance.cpp` stack step | A field read by several layers of one stack gets one copy per read and one packed segment per copy. Two layers that read `frost` run its program twice per pixel and reach the 256-instruction limit twice as fast. | Pack each distinct field once per stack; layers share its segment; `FieldsFit` counts distinct fields. |
| C2 | PLAUSIBLE | `TextureLabLifecycle.cpp` shader cache | The `TextureLab` singleton holds `std::async` futures. At process exit a pending future's destructor waits for a worker thread that exit has already ended. The worker holds the device without a reference. `Ready` calls `get`, which rethrows a worker exception on the render thread. `StartCompile` catches only `std::system_error`. | Take a device reference in the worker; catch around `get` and store a failed result; do not wait on pending compiles at shutdown. |
| C3 | CONFIRMED | `GeneratedShaderFor`, `GeneratedStackFor` | Each cache holds at most 256 entries and never evicts. Failed and pending entries count. After 256 shapes in a session, every new stack draws with `PSStack` for the rest of the session, without a log line. | Evict the least recently used entry, and log once when the limit is reached. |
| C4 | CONFIRMED (cost not measured) | `GeneratedShaderFor`, `RenderStack`, stack step | Every draw builds the program text, the stack shape and the field pack again, although all three depend only on the plan step. | Compute them once per plan step and keep them with the step. |
| C5 | CONFIRMED | `ShaderSource.cpp` after-switch text; `ProgramShader.cpp` `KeepsZ` | The rule for opcodes that keep z is written twice: `op != 38 && op != 39 && op != 40` in the interpreter and `KeepsZ` in the generator. | Generate the interpreter line from `KeepsZ`. |
| C6 | CONFIRMED | `ProgramShader.cpp` `StatementOf`; `TextureLabPass.cpp` `RenderStack` segment lambda | `StatementOf` dereferences a `find` result without a check. The segment lambda indexes `segments` without a check and depends on an earlier `CanRenderStack` call. | Checked access. |
| C7 | PLAUSIBLE | `RenderFusion.cpp` `InlineField` | A live stack that reaches the producer only through a readback counts as an accepted consumer but takes no field. The producer then keeps drawing while the other stacks also evaluate it. | Require at least one attached read per accepted stack. |

## Readability and organization

| Id | Where | Finding | Fix |
|---|---|---|---|
| R1 | `RenderPlan.h` `PlannedLayer` | `source` sits beside `sourceField` and `mask` beside `maskField`, so the plan can hold illegal states and `ValidateRenderPlan` rejects "mask field without a mask" at run time. | Source is a value reference or a layer field; the mask is an optional one of those. |
| R2 | `RenderFusion.h` `PackFields` | A render-time helper sits in a load-time planner; `RenderInstance.cpp` includes `RenderFusion.h` only for it. | Move it beside `LayerField` or `PackInterpreters`. |
| R3 | `StackShader.cpp`, `TextureLabPass.cpp` | Two `SegmentExists` with different meanings, and `FieldsBound` repeats one; two identical `FieldOf` overloads; the layer limit 8 in three places (`kMaxGeneratedStackLayers`, `TextureLab::kMaxStackLayers`, `PSStack`). | One segment helper over `InterpreterPack`; one `FieldOf`; one limit. |
| R4 | `RenderInstance.cpp` stack step; `ShaderSource.cpp` `PSStack` | The stack step lambda is about 110 lines. `PSStack`'s two-pass field loop hides two statements. | Extract the per-layer pass builder; write the source and mask field reads as two statements. |
| R5 | `TextureLab.h`, `TextureLabPass.cpp`, `programshader_tests.cpp` | The `CompiledShader` alias is not used in `TextureLabLifecycle.cpp`. `MaterializeFields` copies passes before its bounds check. The opcode-count test compares with 42, which is also the array size, so it cannot fail. | Use the alias; check first; enumerate the opcodes in the test. |

## Naming

| Id | Current | Problem | Proposed |
|---|---|---|---|
| N1 | `LayerPass::sourceField`, `maskField` (index) vs `PlannedLayer::sourceField`, `maskField` (`LayerField`) | One name, two types | `sourceSegment`, `maskSegment` on `LayerPass` |
| N2 | `InterpreterPack`, `PackedFields`, `StackFields` | Three names for one packed block at three stages | `PackedLayerFields`, `BoundLayerFields` |
| N3 | `MaterializeFields`; "stays materialized" in REFERENCE | Clashes with the executor's `Materialize` | `DrawFieldsToTargets`; "stays a separate draw" |
| N4 | `DrawInterpreter(..., a_shader)` | Also draws generated shaders | `DrawProgramPass` |
| N5 | `FusionCheckTotals`, `EmitFusionChecks` | Also carry the program check | `EquivalenceCheckTotals`, `EmitEquivalenceChecks` |
| N6 | `CheckProgram`, `DrainProgramChecks` | Do not say what is compared | `CheckGeneratedProgram`, `DrainGeneratedProgramChecks` |
| N7 | `generated_`, `GeneratedShaderFor` vs `generatedStacks_`, `GeneratedStackFor`; `PSGenerated` vs `PSGeneratedStack` | Not symmetric | `generatedPrograms_`, `GeneratedProgramFor`, `PSGeneratedProgram` |
| N8 | `CodeShape` | Does not say it zeroes numbers | `CodeWithoutNumbers` |
| N9 | `FusedPlan::layerFields` | A count of attached reads | `layerFieldReads` |
| N10 | `TextureSlot` | Empty for value inputs | `InputTextureSlot` |

## Docs

| Id | Where | Finding |
|---|---|---|
| D1 | `REFERENCE.md`, layer fields | "Each stack runs its program once" is false until C1 is fixed. |
| D2 | `REFERENCE.md`, fallback | "No program pipeline" is listed as a case that draws fields to targets; such a stack fails, because drawing a field needs the program pipeline. |
| D3 | `REFERENCE.md`, generated programs | `c` is not a literal; only its index is. |
| D4 | `REFERENCE.md`, shader cache | The free-threaded device claim has no provenance; it holds only if Skyrim creates the device without `D3D11_CREATE_DEVICE_SINGLETHREADED`. |
| D5 | `Settings.cpp`, `BetterEnchantmentEffects.ini` | The `GeneratedShaders` description names only programs; the `FusionCheck` description does not name the program check. |
| D6 | `docs/plans/render-performance-2026-09-29.md` | The status line says stage 4 is in progress; 4b is not marked done. |

## Resolution

| Id | Outcome |
|---|---|
| C1 | Fixed. A stack step stores each layer field once (`fields`) and layers refer to it by `LayerFieldRef`; the executor packs each visible field once. The fusion test checks three reads of one field store one field. |
| C2 | Fixed. Compiles run on detached threads that hold a device reference and fulfil a promise; `Ready` catches a broken promise. The device threading assumption is recorded in `REFERENCE.md` as unverified (D4). |
| C3 | Fixed. Each cache evicts its least recently used finished entry when full and logs once. |
| C4 | Fixed where it mattered. The executor keeps the field pack per step while the visible fields stay the same. The rest was measured natively on the demo recipes (-O2, Linux): building and looking up a stack shape costs 0.18 us, about 0.011 ms per tick at 58 stacks, and generating a program's text costs 7.9 us at about 0.1 program draws per tick. Both stay per draw; the plugin's whole CPU tick is 1.07 ms per frame. |
| C5 | Fixed. The opcode table holds `keepsZ`; `InterpreterZRule` generates the interpreter's line, and a test pins it. |
| C6 | Fixed. `StatementOf` checks its lookups under a `static_assert` that the table holds `kNormalize`; segments go through the checked `SegmentAt`. |
| C7 | Fixed. A stack counts as taking a field only when at least one read attaches. |
| R1 | Fixed. `LayerRead` is a variant of a value reference and a `LayerFieldRef`; the "mask field without a mask" state cannot be built. |
| R2 | Fixed. `PackLayerFields` and `PackedLayerFields` are in `RenderPlan`. |
| R3 | Fixed. One `SegmentAt`; one `FieldOf` body; one `kMaxStackLayers`, pinned against `StackConstants`. |
| R4 | Fixed. The stack step uses `LayerPassFor`, `FieldsRead`, `FieldPackFor`, `BindLayerFields` and `TextureLab::DrawLayersOneByOne`; `PSStack` reads its two fields in two statements. `FieldsRead` and `PackedSegment` are pure plan functions; the fusion test evaluates layer fields through them under random visibility. |
| R5 | Fixed. `CompiledShader` is used throughout; `DrawFieldsToTargets` checks before it copies; the opcode test compares against the full enum. |
| N1–N10 | Applied as proposed, except `N3` also renames `MaterializedLayers` to `LayersWithDrawnFields`, and `TextureSlots` became `InputTextureSlots`. |
| D1–D6 | Fixed in `REFERENCE.md`, `Settings.cpp`, the shipped INI and the plan. |

