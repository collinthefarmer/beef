# Expression checking and evaluation cleanup

Implementation checkpoint for [the expression handoff](../plans/expression-cleanup-implementation-handoff-2026-09-13.md).

## Scope and review

`Expression.cpp` now gives type checking and evaluation separate private stack types. `TypeStack` owns the existing dynamic type storage, scalar-operand validation, and ordered type joining. `ValueStack` owns the fixed 64-value storage and unary, binary, scalar-binary, and ternary operand consumption. Component mapping continues to use `Unary`, `Binary`, and `Ternary`; comparisons continue to convert whole operands with `AsScalar` rather than mapping over vector components.

The opcode switches remain together. Reference lookup, curve calls, vector construction, and branch selection remain explicit. Pops are sequenced in declarations, right to left; joins are performed left to right, preserving first-error selection and diagnostic context. Evaluation remains `noexcept`, with no per-operation allocation, zero on empty pops, and ignored pushes at capacity. Parser code, opcode numbers, nodes, headers, renderer code, and public APIs were not changed.

The existing dirty workspace was preserved. Pre-edit expression sources and the starting tracked diff were captured in `/tmp/expression-cleanup-baseline`. No commit or deployment was performed.

## Regression coverage

The new `tests/recipe/expressionoperations_tests.cpp` suite exercises noncommutative operations, vec2 and vec3 broadcasting, ternary argument order, numeric edge cases, exact diagnostics and error precedence, default and missing inputs, curve forwarding, and the 64-live-value boundary. Mixed conditional branch tests are explicitly labeled compatibility coverage; they are not a declaration that the existing type inconsistency is desirable.

## Confirmed existing issues and compatibility decisions

### Mixed conditional branch types

A probe compiled against the captured pre-edit source produced:

```text
if(1, 2, [3, 4]): check=vec2 runtime=scalar scalar=2
if(0, 2, [3, 4]): check=vec2 runtime=vec2 scalar=3
```

`Program::Check` joins scalar/vector branches, but `Evaluate` returns the selected branch without coercion. Signal graph inference stores the joined type (`Signals.cpp`), while expression execution and signal state storage retain the returned variant. Consumers using `AsVec2`/`AsVec3` may broadcast scalar values, but this does not establish an invariant between graph type and runtime variant. The GPU interpreter in `ShaderSource.cpp` uses float3 stack entries, broadcasts literals at insertion, and selects already-evaluated branches at opcode 22; it has no equivalent scalar/vec2/vec3 variant tag. Renderer consumers also assert the numeric opcode values.

Decision: preserve branch selection in this extraction, with explicit compatibility regressions for both conditions. A later semantic fix should define branch promotion and conversion behavior across signal consumers and the GPU before changing it. This pass does not claim to fix the inconsistency or to validate GPU execution.

### Accepted expression exceeding runtime stack capacity

Reproducer using only public parsing/checking/evaluation APIs:

```cpp
std::string expression = "1+2*3";
for (int i = 0; i < 31; ++i) {
  expression = "lerp(1,2," + expression + ")";
}
auto program = Program::Parse(expression).value();
auto type = program.Check({}); // scalar
float result = AsScalar(program.Evaluate({})); // 4, mathematically 38
```

The accepted program has 98 operations and needs 65 live values: two pending operands for each of 31 nested calls, plus three innermost literals. Precedence permits the multiply's operands to coexist with the add's left operand without increasing unary nesting depth. This disproves the assumption that nesting 32 and operation count 256 enforce a 64-value live-stack limit. Ignored pushes corrupt the result without an out-of-bounds write. The reference manual's claim that excessive stack input is rejected is not true for this example.

Decision: preserve the existing runtime policy during this extraction and track parser/live-stack validation as a separate correctness fix. Do not make the erroneous result a desired-behavior unit test. The regression suite instead checks the valid boundary: the same 31 calls around `1+2` use 64 live values and return 34. A follow-up should reject excess live depth during parsing, with a diagnostic and boundary tests, and review GPU stack limits before choosing shared acceptance rules.

## Validation

- Final focused run: both expression suites pass under ASan/UBSan (141 + 107 = 248 checks), formatting passes, and targeted lint reports 1/1 fresh source with four findings; command exit 0.
- New expression suite: all 107 checks pass with ASan/UBSan against the extracted implementation. All 107 also pass when compiled against the captured pre-edit `Expression.cpp`. The two diagnostic probes reproduce the same results before and after extraction.
- Formatting: both edited C++ files pass `tools/format.sh --check`. `git diff --check` passes. A direct source comparison confirms the parser, component mapping, `ApplyCurve`, and public header are unchanged.
- Expression lint: four existing findings remain (two function-size, two cognitive-complexity). `Check` decreases from 52 to 36. `Evaluate` changes from 50 to 51 because the metric charges nesting inside the operation lambdas; the repeated operand consumption has moved into short private stack methods. The cohesive switch is retained deliberately. No additional expression warning category or finding is introduced; thresholds were not relaxed.
- Release builds passed twice. The second build manifest is `a98378789c00-eef755028d2ce08b-Release`, source SHA-256 `eef755028d2ce08b23b5f924668c2859cf83bafae2e58b13cbcaddfbeadcbb33`.
- **Concurrent-workspace limitation:** unrelated studio/editor files and headers changed, and new sources were added, during validation. Independent source identity comparison failed after both builds. The successful build manifest therefore is not claimed to match the final workspace. Whole-tree acceptance needs a stable-tree build, fresh lint inventory, and matching identity after that concurrent work settles.
- No in-game or D3D runtime pass is claimed.

- The initial full sanitizer run passed 52 suites / 2,103 checks, schema validation, and six Python tests (exit 0). That run used the first extraction iteration and 98 new checks; it is supporting evidence, not a full pass of the final stack-class implementation.
- The final full native run passed 15 suites / 829 checks, including both expression suites, before it was deliberately interrupted (exit 130). Concurrent header edits repeatedly rebuilt studio dependencies between suites. It did not reach schema/Python checks and is not counted as a full pass.
- The full lint command exited 0 but reported only 12/90 files fresh, with 22 findings among those fresh files (19 size, two redundant-member-init, one cognitive-complexity). Concurrent edits invalidated the other results and added source files after selection. This is explicitly not the repository's final warning count, nor evidence of warning-count improvement.

Logs and the reproducer are retained in [regression evidence](regression-evidence/2026-09-13/expression-cleanup/). The native log named `native-interrupted.log` ends partway through the suite; it must not be interpreted as success. A stable-workspace full sanitizer run, unfiltered lint run, Release build, and matching source identity remain outstanding before whole-tree acceptance.

The scoped source SHA-256 is `4b1874d0da765a7fac8504b29530717900158aff705167f79ff97d62cbe209c3` (`src/recipe/Expression.cpp`); the regression suite SHA-256 is `6c731c16bf023b7eeb11c9165388386178ff92689d2cfa01914436ae6bb65bbc`.

## Next priorities

Resolve the confirmed expression stack-acceptance bug and decide mixed-branch type semantics as explicit correctness work. Remaining readability candidates from the handoff are `RuntimeTexturesPass::Render`, `MeshReader::ReadMesh`, and island `Label` / `PairTwins`; assess cohesion and risk before extracting more code.
