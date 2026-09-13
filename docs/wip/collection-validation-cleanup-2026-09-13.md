# Actor collection and reference validation cleanup — 2026-09-13

The user chose to skip the preceding source-preparation smoke test and continue cleanup. No in-game pass is claimed for that revision or this pass.

## Changes

- Separate armor-piece traversal from geometry inspection in ManagerApply.cpp. Clone identity filtering remains in CollectPieces; shader-property deduplication, material checks, and geometry creation remain together in CollectPieceGeometries.
- Preserve first/third-person root selection, own-shell exclusion, logging, and traversal order. A layout failure rejects the entire piece, including any geometry collected before the failure.
- Replace the nested signal-reference validation lambdas with a private ReferenceTypeChecker. Short signal-kind handlers share scalar, color, and trigger checks and diagnostic reporting.
- Preserve validation order, diagnostic wording and severity, unknown-reference handling, and propagation of inert state to dependent signals.
- Add reference-validation regressions covering incompatible scalar, color, trigger, and reset references, multiple errors on one signal, dependent inert state, and compatible counterparts.

The new helpers are private. No public API, ownership contract, or lint threshold changed.

## Validation

- Release build passed; its generated identity matches the final source state.
- 51 ASan/UBSan native suites passed with 2,005 checks, including 29 new reference-validation checks. Schema validation and six Python tests passed.
- Formatting passed for all five changed source/header/test files, and git diff whitespace checks passed.
- Full clang-tidy refresh: **89/89 files fresh, 50 findings**, down from 54. The remaining findings are 44 function-size and six cognitive-complexity warnings.
- Actor collection and signal-reference validation no longer trigger size or cognitive-complexity warnings. No new lint findings were introduced, and lint thresholds were unchanged.

Build identity: `a98378789c00-39460c1ab9381c75-Release`.
Source hash: `39460c1ab9381c7535cee859f9733908fffa3d889c70b030671a71970b36fef4`.

Native tests exercise signal validation but do not execute Skyrim scenegraph collection or D3D rendering. No deployment or commit was performed.

## Remaining priorities

Review expression validation and evaluation for shared rules and clearer operation names. Keep cohesive dispatch switches where splitting them would only improve a metric. Existing argument-count findings alone do not justify new parameter containers.
