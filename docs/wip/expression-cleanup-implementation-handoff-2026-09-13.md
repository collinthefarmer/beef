# Expression cleanup: implementation handoff

Implementation results: [expression cleanup checkpoint](expression-cleanup-2026-09-13.md).

## Objective and authorization

Continue the codebase cleanup with expression type checking and evaluation. The user's priorities are correctness, minimal duplication, readable abstractions, and accurate names. Warning counts are supporting evidence, not the acceptance criterion.

Implementation of the next cleanup priority is already authorized. The user explicitly skipped the source-preparation smoke test and asked to keep moving. Do not introduce another smoke-test approval gate for routine cleanup. Record automated and runtime evidence separately. No deployment or commit has been performed by the agent; neither is part of this handoff's implementation scope.

## Workspace and current checkpoint

Repository: `/home/nixos/projects/skyrim-modding/plugins/WornEnchantmentPBR`. Run the commands below from this directory; the parent workspace is not the repository.

The working tree contains extensive tracked and untracked work from earlier fixes and cleanup passes. Preserve it. Inspect the current diff and take a baseline of files being edited; do not restore files from HEAD or remove untracked files. Avoid `src/_old` and `tests/_old`.

The most recent completed pass is [actor collection and reference validation](collection-validation-cleanup-2026-09-13.md). It separated armor traversal from geometry collection and consolidated signal-reference type checks without changing diagnostics or dependent inert-state propagation. Earlier context is linked through [source cleanup](source-cleanup-2026-09-13.md), [render cleanup](render-cleanup-2026-09-13.md), and [lint cleanup](lint-cleanup-2026-09-13.md).

Recorded baseline, not a fresh validation run for this document:

- Release build: `a98378789c00-39460c1ab9381c75-Release`.
- Source SHA-256: `39460c1ab9381c7535cee859f9733908fffa3d889c70b030671a71970b36fef4`.
- Full clang-tidy: 89/89 files fresh, 50 findings: 44 function-size and six cognitive-complexity warnings.
- 51 ASan/UBSan native suites, 2,005 checks; schema validation and six Python tests passed.
- Formatting and `git diff --check` passed.
- No in-game pass is claimed for source cleanup or the subsequent actor/reference cleanup. An earlier shell/compositor revision had a user-reported smoke pass.

## Start here

| File | Purpose |
| --- | --- |
| [Expression.cpp](../../src/recipe/Expression.cpp) | `Program::Check`, `Program::Evaluate`, component-wise arithmetic helpers, type joining, parser |
| [Expression.h](../../src/recipe/Expression.h) | Public program representation, opcode values, inputs, parser limits |
| [expression_tests.cpp](../../tests/recipe/expression_tests.cpp) | Existing scalar/vector, parsing, curve, and basic type-check coverage |
| [Signals.cpp](../../src/recipe/Signals.cpp) | Expression type inference and execution within the signal graph |
| [RuntimeTexturesPass.cpp](../../src/render/RuntimeTexturesPass.cpp) and [RuntimeTexturesLab.cpp](../../src/render/RuntimeTexturesLab.cpp) | Consumers of expression opcodes; explicit numeric opcode assertions |
| [run-native.sh](../../tests/run-native.sh) | Native regression runner, schema and Python checks |

At the checkpoint, `Program::Check` has cognitive complexity 52 and `Program::Evaluate` has 50. Each also has a function-size finding. Read their actual responsibilities before choosing an extraction; retaining a cohesive dispatch switch is acceptable.

## Implementation sequence

1. Review existing tests and consumers. Identify repeated operand handling and type rules, and distinguish them from intentionally different checking/evaluation behavior. Capture the pre-edit source for diff review.
2. Add focused regression coverage for behavior touched by the extraction, using public parse/check/evaluate APIs. Prefer a separate `tests/recipe/*_tests.cpp` suite if it avoids reformatting the existing test file. The runner discovers these suites automatically.
3. Simplify type checking first. Candidate helpers are scalar-operand validation and joining operand types with the existing diagnostic context. Keep reference/curve diagnostics explicit. A private checker with short methods is reasonable only if it makes the dispatch easier to follow.
4. Simplify evaluation separately. The repeated ordered pops and component-wise application are candidates for small unary/binary/ternary operations. Reuse the existing `Unary`, `Binary`, and `Ternary` behavior. Consider names that distinguish component mapping from consuming stack operands. Keep unusual operations, including `if`, curves, and vector construction, explicit.
5. Review the diff for operand order, error order, numerical behavior, allocation, and public representation changes. Run the validation below, resolve new findings, and write a new checkpoint with the actual results and remaining priorities.

Do not build a generic interpreter framework, introduce virtual dispatch or per-operation heap allocation, or create parameter containers solely to silence lint. Avoid changing parser grammar, public APIs, or renderer code as part of an ordinary extraction.

## Behavior to preserve

- `Program::Op` numeric values and `Node` representation are shared with rendering. Do not reorder opcodes. Inspect renderer/shader consumers before any representation or semantic change.
- Preserve postfix execution and operand order. Pop right before left for binary operations; retain the precise argument order for clamp, smoothstep, lerp, and vector construction. Do not place multiple state-mutating pops in function arguments whose evaluation order is unspecified.
- `Evaluate` remains `noexcept` and uses a fixed 64-value stack. Its current empty-stack fallback is scalar zero; pushes beyond capacity are ignored. Checking uses a separate dynamic type stack. Do not quietly change these policies during extraction.
- Arithmetic broadcasts scalars to vectors; incompatible vec2/vec3 arithmetic falls back to zero in evaluation and is rejected by checking. Unary component operations retain vector shape.
- Preserve positive-only truth (`> 0`), epsilon equality/inequality, near-zero division returning zero, negative square roots clamped to zero, non-finite powers returning zero, reversed clamp bounds, and degenerate smoothstep behavior.
- `if` selects an already-evaluated branch. Logic and conditionals do not acquire short-circuit behavior during cleanup.
- Missing runtime references produce zero. Missing/null curves return their input argument. Curve evaluation forwards `mean`; retain the current handling of other inputs.
- Preserve diagnostic wording and first-error order, including the contexts `operator`, `function`, and `if`. Keep unknown references distinct from incompatible types.
- Preserve parser bounds (4,096 characters, depth 32, 256 operations), interning order, free-variable flags, and curve restrictions.

## Questions to investigate, not assumed fixes

Two details deserve explicit scrutiny before choosing helpers:

- Type checking joins scalar/vector `if` branches to a vector type, while evaluation returns the chosen value directly. Reproduce the actual result type for both conditions and inspect caller expectations and GPU behavior. A confirmed inconsistency should get a focused regression and an explicit compatibility decision, rather than being hidden inside an extraction.
- The parser permits up to 256 operations while evaluation has 64 stack entries. Operation count alone does not establish maximum live stack depth. Determine whether accepted syntax and nesting limits can exceed that depth before describing this as a bug or changing limits.

If either is a demonstrated bug, document the trigger and impact separately from the readability work. Do not lock a suspected defect into a test labeled as desired behavior without assessing the contract.

## Regression coverage

Extend existing coverage where needed; avoid duplicating tests just to exercise new helper names:

- Noncommutative binary operations and ternary argument order with distinct values.
- Scalar broadcasting on either side, vec2/vec3 results, and rejected mixed dimensions.
- Scalar-only vector components, conditions, comparisons, and curve arguments; exact diagnostics and first-error selection.
- Both conditional branches and their actual result types.
- Missing runtime references/curves, default empty program, and `x`/`mean`/`time` forwarding.
- Numerical edge cases affected by any moved arithmetic.

The existing full suite also covers signal-graph integration and studio expression renaming. Native tests do not execute Skyrim or D3D paths.

## Validation commands

Use the project's Nix environment. In this workspace, daemon access may require the tool's escalated execution mode; the offline Nix prefix is already approved. These are commands for the implementer, not checks rerun while writing this document.

```bash
nix develop --offline --max-jobs 0 --builders '' -c tools/format.sh src/recipe/Expression.cpp
nix develop --offline --max-jobs 0 --builders '' -c tools/format.sh --check src/recipe/Expression.cpp
nix develop --offline --max-jobs 0 --builders '' -c env BEEF_SANITIZE=1 tests/run-native.sh
nix develop --offline --max-jobs 0 --builders '' -c cmake --build build/Release -j 4
nix develop --offline --max-jobs 0 --builders '' -c tools/tidy.sh
git diff --check
python3 tools/build-identity.py --root . --output /tmp/beef-expression-identity-check --config Release
cmp /tmp/beef-expression-identity-check/build-identity.json build/Release/generated/build-identity.json
```

Add every edited header/test to formatting commands. The native runner's arguments are passed to test executables; they are not suite selectors. Inspect exit codes and the complete final summaries. For iteration, `tools/tidy.sh src/recipe/Expression.cpp` refreshes just that source; a final unfiltered run gives the repository inventory. Header changes invalidate cached lint results broadly. Count only fresh results and do not relax lint thresholds.

## Completion and subsequent priorities

The pass is complete when repeated work has been reduced with understandable names, semantic changes (if any) have explicit evidence, meaningful regressions and the release build pass, formatting is clean, and no new lint findings remain unexplained. Record the final source/build identity and actual counts in a new checkpoint linked from this handoff. Do not require zero warnings or another in-game test to finish this scoped pass.

After expressions, assess the remaining cognitive-complexity sites by actual readability and risk: `RuntimeTexturesPass::Render` (41), `MeshReader::ReadMesh` (33), and island `Label` (34) / `PairTwins` (26). These are review candidates, not a mandate to split every function. Small argument-count findings remain lower priority.
