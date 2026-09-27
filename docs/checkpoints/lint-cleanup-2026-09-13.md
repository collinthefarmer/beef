Status: record. The clang-tidy inventory at that build.

# Lint cleanup checkpoint — 2026-09-13

Fresh results cover all 89 current source translation units: **62 findings**, consisting of 49 function-size warnings and 13 cognitive-complexity warnings. No remaining bugprone, performance, or other enabled-check findings. The earlier 75-finding figure came from mixed cached reports and is not a clean before/after baseline.

The subsequent [shell and compositor cleanup](render-cleanup-2026-09-13.md) records newer changes and validation. The inventory here describes this checkpoint's build.

## Completed

- Resolve light removal before attaching lights, so destruction no longer initializes an address-library relocation or enters its potentially allocating failure path.
- Pass an owned, null-terminated shader source name to D3DCompile. The previous string view referenced a literal and was safe today; the call now expresses the API requirement directly.
- Verify the recipe aggregate's move exception contract with a compile-time assertion. The MSVC map move can allocate, and the aggregate correctly propagates its throwing contract. Suppress that specific clang-tidy false positive with an adjacent explanation; do not change the containers or add a misleading noexcept.
- Remove unnecessary diagnostic-location copies in signal validation.
- Separate named-bone placement, influence accumulation/ranking, and skinned-bone placement. Preserve share thresholds, vertex weighting, and ordering.
- Separate stack-output lookup, surface failures, preparation, and diagnostic reporting. Preserve existing problem precedence and active-output handling.
- Give each signal kind an independent evaluation operation with a shared borrowed context. Consolidate firing retention for external events and conditional triggers; retain total firing counts even when old payloads are discarded.
- Split snapshot construction into piece, geometry, and output builders, plus status accumulation and loaded-recipe inventory. Preserve selection/fallback behavior, scalar/layer ordering, and texture retention in the published snapshot. The former BuildSnapshot complexity score was 303; the file now has no lint findings.
- Fix tools/tidy.sh to summarize only fresh results in the requested selection, invalidate on .clang-tidy changes, keep an empty --changed selection empty, and reject previous cached success after a failed refresh.

## Validation

- Full clang-tidy sweep, followed by targeted refreshes for subsequent edits; final summary: 89/89 fresh files, 62 findings.
- Standard repository checks enabled; clang static analyzer was not enabled. One initial clang-tidy process crashed during active editing; the complete rerun and final targeted runs succeeded, and no failed partial result contributes to the report.
- Release DLL built successfully.
- 50 ASan/UBSan native suites, 1,976 checks; recipe schema validation passed.
- Six Python tests passed, including four new lint-cache/selection regressions.
- Ten added native checks cover event and conditional trigger retention, newest-payload ordering, total firing counts, and lifetime expiry.
- Formatting passed for all eight changed source/header files. Shell syntax and git diff whitespace checks passed.

The user completed an in-game run and reports that it "seems to run fine." Record this as a user-reported smoke test with no observed issues. The tested build and individual case coverage were not specified. The requested coverage was light creation/removal; named and skinned light placement; snapshot piece selection and fallback; recipe history/diagnostics; scalar/layer values and source/mask/output previews while effects are replaced. Native tests do not execute the Skyrim/D3D snapshot or light paths. The agent has not deployed or committed this cleanup.

## Remaining priorities

1. **Rendering and shell control flow.** Split ShellBinding::Create into explicit preparation and attachment stages while retaining rollback. Separate mask preparation and compositor pass decisions from resource allocation/submission. These paths combine high complexity with ownership-sensitive behavior and should retain the existing in-game acceptance requirements.
2. **Actor geometry collection.** Extract geometry inspection from Manager::CollectPieces while preserving duplicate-property filtering, layout failure handling, and first/third-person identity.
3. **Signal/reference and expression validation.** Centralize reference-type checks and separate expression operand validation from execution. Preserve diagnostic text and evaluation order with the existing native suites.
4. **Remaining size-only findings.** Review each for actual readability benefit. Several are argument-count or nesting thresholds; do not introduce one-use parameter containers or split cohesive switches just to silence a metric. No lint thresholds were relaxed.

## Remaining inventory

Each row below is from the final current-source logs. Paths and line numbers refer to this checkpoint's source state.

| Location | Check | Finding |
| --- | --- | --- |
| [src/engine/ManagerApply.cpp:215](../../src/engine/ManagerApply.cpp#L215) | `readability-function-size` | function 'PreparePlacement' exceeds recommended size/complexity thresholds |
| [src/engine/ManagerApply.cpp:267](../../src/engine/ManagerApply.cpp#L267) | `readability-function-size` | function 'MarkReplaced' exceeds recommended size/complexity thresholds |
| [src/engine/ManagerApply.cpp:389](../../src/engine/ManagerApply.cpp#L389) | `readability-function-size` | function 'PlaceLight' exceeds recommended size/complexity thresholds |
| [src/engine/ManagerApply.cpp:468](../../src/engine/ManagerApply.cpp#L468) | `readability-function-size` | function 'Refresh' exceeds recommended size/complexity thresholds |
| [src/engine/ManagerApply.cpp:534](../../src/engine/ManagerApply.cpp#L534) | `readability-function-size` | function 'CollectPieces' exceeds recommended size/complexity thresholds |
| [src/engine/ManagerApply.cpp:534](../../src/engine/ManagerApply.cpp#L534) | `readability-function-cognitive-complexity` | function 'CollectPieces' has cognitive complexity of 43 (threshold 25) |
| [src/engine/ManagerEvents.cpp:28](../../src/engine/ManagerEvents.cpp#L28) | `readability-function-size` | function 'FireAt' exceeds recommended size/complexity thresholds |
| [src/engine/ManagerInspection.cpp:8](../../src/engine/ManagerInspection.cpp#L8) | `readability-function-size` | function 'RequestMesh' exceeds recommended size/complexity thresholds |
| [src/engine/ManagerTick.cpp:104](../../src/engine/ManagerTick.cpp#L104) | `readability-function-size` | function 'RenderSlotChain' exceeds recommended size/complexity thresholds |
| [src/engine/ManagerTick.cpp:205](../../src/engine/ManagerTick.cpp#L205) | `readability-function-size` | function 'InstanceTimeFor' exceeds recommended size/complexity thresholds |
| [src/engine/ManagerTick.cpp:223](../../src/engine/ManagerTick.cpp#L223) | `readability-function-size` | function 'SweepBoundMeshes' exceeds recommended size/complexity thresholds |
| [src/engine/MeshReader.cpp:135](../../src/engine/MeshReader.cpp#L135) | `readability-function-size` | function 'ReadMesh' exceeds recommended size/complexity thresholds |
| [src/engine/MeshReader.cpp:135](../../src/engine/MeshReader.cpp#L135) | `readability-function-cognitive-complexity` | function 'ReadMesh' has cognitive complexity of 33 (threshold 25) |
| [src/engine/MeshReader.cpp:266](../../src/engine/MeshReader.cpp#L266) | `readability-function-size` | function 'NodeBindPosition' exceeds recommended size/complexity thresholds |
| [src/engine/RecipeEditor.cpp:143](../../src/engine/RecipeEditor.cpp#L143) | `readability-function-size` | function 'ReloadRecipes' exceeds recommended size/complexity thresholds |
| [src/engine/RecipeEditor.cpp:193](../../src/engine/RecipeEditor.cpp#L193) | `readability-function-size` | function 'BeginPaint' exceeds recommended size/complexity thresholds |
| [src/engine/RecipeStore.cpp:490](../../src/engine/RecipeStore.cpp#L490) | `readability-function-size` | function 'SaveRecipe' exceeds recommended size/complexity thresholds |
| [src/engine/SessionQueue.cpp:54](../../src/engine/SessionQueue.cpp#L54) | `readability-function-size` | function 'Post' exceeds recommended size/complexity thresholds |
| [src/menu/StudioPage.cpp:349](../../src/menu/StudioPage.cpp#L349) | `readability-function-size` | function 'RenderStudio' exceeds recommended size/complexity thresholds |
| [src/mesh/Islands.cpp:178](../../src/mesh/Islands.cpp#L178) | `readability-function-size` | function 'Label' exceeds recommended size/complexity thresholds |
| [src/mesh/Islands.cpp:178](../../src/mesh/Islands.cpp#L178) | `readability-function-cognitive-complexity` | function 'Label' has cognitive complexity of 34 (threshold 25) |
| [src/mesh/Islands.cpp:276](../../src/mesh/Islands.cpp#L276) | `readability-function-cognitive-complexity` | function 'PairTwins' has cognitive complexity of 26 (threshold 25) |
| [src/mesh/MaterialClusters.cpp:215](../../src/mesh/MaterialClusters.cpp#L215) | `readability-function-size` | function 'ClusterMaterial' exceeds recommended size/complexity thresholds |
| [src/mesh/MeshFacts.cpp:43](../../src/mesh/MeshFacts.cpp#L43) | `readability-function-size` | function 'BonesOf' exceeds recommended size/complexity thresholds |
| [src/recipe/Expression.cpp:480](../../src/recipe/Expression.cpp#L480) | `readability-function-size` | function 'Check' exceeds recommended size/complexity thresholds |
| [src/recipe/Expression.cpp:480](../../src/recipe/Expression.cpp#L480) | `readability-function-cognitive-complexity` | function 'Check' has cognitive complexity of 52 (threshold 25) |
| [src/recipe/Expression.cpp:605](../../src/recipe/Expression.cpp#L605) | `readability-function-size` | function 'Evaluate' exceeds recommended size/complexity thresholds |
| [src/recipe/Expression.cpp:605](../../src/recipe/Expression.cpp#L605) | `readability-function-cognitive-complexity` | function 'Evaluate' has cognitive complexity of 50 (threshold 25) |
| [src/recipe/RecipeRead.cpp:366](../../src/recipe/RecipeRead.cpp#L366) | `readability-function-size` | function 'ReadRows' exceeds recommended size/complexity thresholds |
| [src/recipe/RecipeRead.cpp:1414](../../src/recipe/RecipeRead.cpp#L1414) | `readability-function-size` | function 'NamedRows' exceeds recommended size/complexity thresholds |
| [src/recipe/RecipeRead.cpp:1530](../../src/recipe/RecipeRead.cpp#L1530) | `readability-function-size` | function 'ParseRecipe' exceeds recommended size/complexity thresholds |
| [src/recipe/RecipeWrite.cpp:32](../../src/recipe/RecipeWrite.cpp#L32) | `readability-function-size` | function 'DescribeSource' exceeds recommended size/complexity thresholds |
| [src/recipe/RecipeWrite.cpp:348](../../src/recipe/RecipeWrite.cpp#L348) | `readability-function-size` | function 'SignalToJson' exceeds recommended size/complexity thresholds |
| [src/recipe/RecipeWrite.cpp:627](../../src/recipe/RecipeWrite.cpp#L627) | `readability-function-size` | function 'SerializeRecipe' exceeds recommended size/complexity thresholds |
| [src/recipe/Signals.cpp:270](../../src/recipe/Signals.cpp#L270) | `readability-function-size` | function 'CheckVector' exceeds recommended size/complexity thresholds |
| [src/recipe/Signals.cpp:477](../../src/recipe/Signals.cpp#L477) | `readability-function-size` | function 'OrderNodes' exceeds recommended size/complexity thresholds |
| [src/recipe/Signals.cpp:604](../../src/recipe/Signals.cpp#L604) | `readability-function-size` | function 'CheckReferenceTypes' exceeds recommended size/complexity thresholds |
| [src/recipe/Signals.cpp:604](../../src/recipe/Signals.cpp#L604) | `readability-function-cognitive-complexity` | function 'CheckReferenceTypes' has cognitive complexity of 40 (threshold 25) |
| [src/render/Compositor.cpp:107](../../src/render/Compositor.cpp#L107) | `readability-function-size` | function 'Prepare' exceeds recommended size/complexity thresholds |
| [src/render/Compositor.cpp:175](../../src/render/Compositor.cpp#L175) | `readability-function-size` | function 'Render' exceeds recommended size/complexity thresholds |
| [src/render/Compositor.cpp:175](../../src/render/Compositor.cpp#L175) | `readability-function-cognitive-complexity` | function 'Render' has cognitive complexity of 58 (threshold 25) |
| [src/render/CompositorSource.cpp:254](../../src/render/CompositorSource.cpp#L254) | `readability-function-size` | function 'PrepareSource' exceeds recommended size/complexity thresholds |
| [src/render/CompositorSource.cpp:254](../../src/render/CompositorSource.cpp#L254) | `readability-function-cognitive-complexity` | function 'PrepareSource' has cognitive complexity of 31 (threshold 25) |
| [src/render/CompositorSource.cpp:374](../../src/render/CompositorSource.cpp#L374) | `readability-function-size` | function 'PrepareMask' exceeds recommended size/complexity thresholds |
| [src/render/CompositorSource.cpp:416](../../src/render/CompositorSource.cpp#L416) | `readability-function-size` | function 'BakeCurve' exceeds recommended size/complexity thresholds |
| [src/render/CompositorSource.cpp:455](../../src/render/CompositorSource.cpp#L455) | `readability-function-size` | function 'InspectSource' exceeds recommended size/complexity thresholds |
| [src/render/CompositorSource.cpp:455](../../src/render/CompositorSource.cpp#L455) | `readability-function-cognitive-complexity` | function 'InspectSource' has cognitive complexity of 31 (threshold 25) |
| [src/render/CompositorSource.cpp:587](../../src/render/CompositorSource.cpp#L587) | `readability-function-size` | function 'PrepareRenderedMask' exceeds recommended size/complexity thresholds |
| [src/render/CompositorSource.cpp:587](../../src/render/CompositorSource.cpp#L587) | `readability-function-cognitive-complexity` | function 'PrepareRenderedMask' has cognitive complexity of 59 (threshold 25) |
| [src/render/Light.cpp:262](../../src/render/Light.cpp#L262) | `readability-function-size` | function 'Update' exceeds recommended size/complexity thresholds |
| [src/render/RuntimeTexturesLab.cpp:116](../../src/render/RuntimeTexturesLab.cpp#L116) | `readability-function-size` | function 'Init' exceeds recommended size/complexity thresholds |
| [src/render/RuntimeTexturesLab.cpp:179](../../src/render/RuntimeTexturesLab.cpp#L179) | `readability-function-size` | function 'CompileShaders' exceeds recommended size/complexity thresholds |
| [src/render/RuntimeTexturesPass.cpp:306](../../src/render/RuntimeTexturesPass.cpp#L306) | `readability-function-size` | function 'Render' exceeds recommended size/complexity thresholds |
| [src/render/RuntimeTexturesPass.cpp:306](../../src/render/RuntimeTexturesPass.cpp#L306) | `readability-function-cognitive-complexity` | function 'Render' has cognitive complexity of 41 (threshold 25) |
| [src/render/RuntimeTexturesPass.cpp:410](../../src/render/RuntimeTexturesPass.cpp#L410) | `readability-function-size` | function 'RenderProgram' exceeds recommended size/complexity thresholds |
| [src/render/RuntimeTexturesPass.cpp:496](../../src/render/RuntimeTexturesPass.cpp#L496) | `readability-function-size` | function 'BakeMesh' exceeds recommended size/complexity thresholds |
| [src/render/RuntimeTexturesPass.cpp:628](../../src/render/RuntimeTexturesPass.cpp#L628) | `readability-function-size` | function 'SampleMaterial' exceeds recommended size/complexity thresholds |
| [src/render/RuntimeTexturesPass.cpp:696](../../src/render/RuntimeTexturesPass.cpp#L696) | `readability-function-size` | function 'RenderClusters' exceeds recommended size/complexity thresholds |
| [src/render/Shell.cpp:118](../../src/render/Shell.cpp#L118) | `readability-function-size` | function 'Create' exceeds recommended size/complexity thresholds |
| [src/render/Shell.cpp:118](../../src/render/Shell.cpp#L118) | `readability-function-cognitive-complexity` | function 'Create' has cognitive complexity of 67 (threshold 25) |
| [src/render/Shell.cpp:472](../../src/render/Shell.cpp#L472) | `readability-function-size` | function 'Pose' exceeds recommended size/complexity thresholds |
| [src/studio/RecipeSnapshot.cpp:29](../../src/studio/RecipeSnapshot.cpp#L29) | `readability-function-size` | function 'BuildRecipeRow' exceeds recommended size/complexity thresholds |

Release build identity: `a98378789c00-a6c908fa3a4ac079-Release`.
