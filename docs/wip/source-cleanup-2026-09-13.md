# Source preparation and inspection cleanup — 2026-09-13

This pass follows the user-reported successful smoke test of the shell/compositor cleanup. That smoke test predates these source changes.

The subsequent [actor collection and reference validation cleanup](collection-validation-cleanup-2026-09-13.md) records the next pass and its validation.

## Changes

- Split source-kind dispatch into private SourcePreparer and SourceInspector operations. Each source kind has its own short implementation. Preparation keeps a mutable compositor reference; inspection borrows a const compositor and the already-retained mesh entry.
- Share image texture/error/sampling setup, bake-channel selection, bake-result handling, mask-as-source conversion, and material-mask validation.
- Keep resource acquisition at the call sites: preparation may load images, bake textures, create derived maps, and read image luminance; inspection only reads cached rendering resources.
- Preserve the distinction between an absent cache entry and a cached failure, including the existing diagnostic wording and warning/error severity.
- Preserve source animation flags, sampling channels, normalization behavior, cache selection, and TextureRef ownership. The helpers do not retain references to the temporary preparation or inspection contexts.

Only Compositor.h and CompositorSource.cpp changed. The new types are private; public APIs and lint thresholds are unchanged.

## Validation

- Full lint refresh plus final targeted refresh: **89/89 files fresh, 54 findings**, down from 57. All remaining findings are readability-related: 46 function-size and 8 cognitive-complexity warnings.
- Source preparation and inspection no longer trigger cognitive-complexity warnings. The four remaining findings in CompositorSource.cpp are existing argument-count limits.
- Release build passed; its generated identity matches the final source state.
- 50 ASan/UBSan suites passed with 1,976 checks. Schema validation and six Python tests passed.
- Formatting passed for both changed files, and git diff whitespace checks passed.

Build identity: `a98378789c00-bbf9f35856cc0443-Release`.
Source hash: `bbf9f35856cc044300f89b3c1b7c300d2b32501748a3563b396886d099ea8396`.

## In-game smoke checks for this revision

1. Open source previews before a layer references them. Unprepared sources should remain marked as not rendered; browsing should not prepare them.
2. Add layers referencing an image, a mesh bake, a material channel, and a mask. Verify that previews populate and the applied effects remain correct.
3. Check both scalar and vector/position bakes, animated inputs, ripple previews, and material clusters.
4. Check a missing image and an invalid mask: diagnostic text should distinguish failure from a source that has not been prepared yet.
5. Open and close previews while changing or replacing effects, checking that textures remain valid.

Native suites do not exercise the Skyrim/D3D paths. The user explicitly chose to skip this revision's smoke test and continue cleanup. No in-game pass is claimed. This revision has not been deployed or committed by the agent.

## Next priority

Actor geometry collection: separate geometry inspection from piece traversal while preserving duplicate-property filtering, layout validation, and first/third-person identity. Signal and expression validation follows that work.
