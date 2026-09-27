Status: record. The shell and compositor pass.

# Shell and compositor cleanup — 2026-09-13

This pass follows the user-reported successful in-game smoke test of the previous lint cleanup. That run predates these changes.

The subsequent [source preparation and inspection cleanup](source-cleanup-2026-09-13.md) records the next pass and its validation.

## Changes

- Shell creation now uses explicit stages for clone-property validation, PBR or vanilla material creation, property configuration, private skin-data preparation, and attachment. The binding owns partial results throughout construction, so failure still invokes its existing teardown. Material-install validation is shared by the PBR and vanilla branches.
- Emissive-storage preparation returns a named result distinguishing allocated storage from cloned storage, with allocation failure represented separately.
- Shell inflation's transform calculation is separate from pose change tracking. Shared skin instances remain unmodified; failure to copy shared skin data still disables inflation rather than dropping the shell.
- Removed the second, redundant palette reset during teardown.
- Stack rendering now separates visible-input rendering, layer parameter resolution, render-target submission, and publication. The first write target still depends on the number of visible layers, ensuring that the final pass lands in the stack's owned target. Cache keys, hidden-layer behavior, dependency-render order, and failure retry state are preserved.
- Mask construction separates texture/reference binding, curve lookup creation, and expression type checking. Cache placeholders still precede recursion; cycle detection, depth/resource limits, failure messages, and final target allocation retain their existing order.
- Layer curves and mask curves share the same lookup sampling/allocation helper. Their different source-dependent means and diagnostic wording remain at the call sites.
- The temporary signal graph used for mask checking is now a local value; no shared ownership is required.

The new helpers are private implementation types. No public rendering API or resource ownership contract was changed. No lint thresholds were relaxed.

## Validation

- Full clang-tidy refresh: **89/89 files fresh, 57 findings**, down from 62 at the preceding checkpoint. All remaining findings are readability-related: 47 function-size warnings and 10 cognitive-complexity warnings.
- `Shell.cpp` has no lint findings. Shell creation (previous complexity 67), stack rendering (58), and mask construction (59) no longer trigger cognitive-complexity warnings. The remaining stack-render entry-point warning is its existing five-parameter signature.
- Release build passed; the generated build identity was checked against the final source state.
- 50 ASan/UBSan native suites passed with 1,976 checks; schema validation and six Python tests passed. These exercise existing pure-logic compatibility, not the Skyrim/D3D paths being refactored.
- Formatting passed for all five changed source/header files. `git diff --check` passed.
- The user reports that this shell and compositor cleanup passes the in-game smoke test. Individual case results and runtime logs were not supplied.

Build identity: `a98378789c00-c282c0dffcb40a2f-Release`.
Source hash: `c282c0dffcb40a2f2000d8215926003a925ee47f999c0bc1153f8f0e82aaa7b0`.

## In-game checks for this build

1. Create and remove both vanilla and PBR-copy shells; exercise alpha/additive blending and repeated effect replacement.
2. Inflate a skinned shell, change the inflation, then remove it. Check both appearance and the original geometry's pose.
3. Render stacks with zero, one, two, and three visible layers; hide all layers and show them again. Check static-cache reuse and animated updates.
4. Exercise nested masks, named curves, and ripple inputs. Confirm pixels and diagnostics agree when an input is missing or invalid.
5. Keep previews open while changing the effects above, then close and reopen the UI.

Native checks do not execute Skyrim or D3D rendering. No deployment or commit was performed by the agent.

## Remaining priorities

The remaining compositor complexity is in source preparation and cached source inspection. Any further consolidation should preserve their different allocation behavior. Actor geometry collection and signal/expression validation remain the next broader cleanup targets. Small argument-count findings do not by themselves justify new parameter containers.
