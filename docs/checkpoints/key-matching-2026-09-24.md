Status: implemented; full offline release gate passed. In-game acceptance pending.

# Keyword requirements and enchantment effect selection

## Contract

- Every keyword is required. Other keys remain alternative selectors; a
  keyword-only recipe needs all listed keywords. Missing or unresolved required
  keywords exclude the candidate before fallback suppression or sampling.
- Magic-effect and effect-shader keys consider all effects on the effective
  worn-item enchantment. Duplicate forms are offered once in the editor.
  Shader selection within each effect retains enchant-visuals-first precedence.
- The strongest matching non-keyword key determines priority, fallback
  classification and effect signal context. Keyword-only recipes use priority 20.
- Winning effect keys read magnitude and cost from the highest-cost matching
  effect entry, with first-entry ties. Missing selected effects return zero.
  Non-effect keys retain the generic costliest-effect fallback.
- Planner evaluation instances and carried clocks include the selected effect
  key. Same contexts share state; distinct contexts remain independent. Light
  replacement continues to group contributions by recipe identity.

## Implementation and regression coverage

`WornKeys.cpp` collects engine inputs; `Resolve.cpp` applies keyword conditions
and selector precedence. `EnchantmentEffects.cpp` selects the signal source,
using the winning key carried through `ActorPlan` to `ActorEnvironment`.
`InstanceTime` keeps effect contexts separate during short retire/reapply gaps.

Native regressions exercise mixed keyword/selector requirements, priority,
fallback, sampling, serialization, secondary effects and shaders, editor key
choices, duplicate effect cost ties, missing effects, instance sharing and
isolation, light grouping, and independent carried phases. The engine adapter
suite compiles the actual collection and signal-selection implementation
against small form doubles.

## Offline validation

- Targeted ASan/UBSan run: 3 suites passed, 125 checks.
- Full ASan/UBSan CTest run: 105 suites passed, zero failures.
- Windows release DLL: built successfully against the actual engine headers.
- Formatting, include layers, schema and source-inventory checks passed.
- Full static analysis: 130 translation units, 62 existing first-party findings,
  zero external findings; baseline check passed with no new findings.

Evidence: `/tmp/beef-key-targeted.log`, `/tmp/beef-key-style.log`, and
`/tmp/beef-key-release.log`.

Built DLL: `build/Release/BetterEnchantmentEffects.dll`

SHA-256: `9fbdc5d0a77becf94a80095ebb0d0a52f805d886c09990e4ec2fc8a6d135a91b`

## Remaining runtime verification

Follow [keyword and enchantment-effect acceptance](../in-game-regression.md#keyword-and-enchantment-effect-matching-acceptance)
with a demo enchantment containing distinguishable effect magnitudes, costs and
shaders. Confirm effective inventory enchantments, editor choices, visible
signal values and edit/reapply continuity in Skyrim. No install or game run has
been performed for this change.

This is a deliberate pre-alpha semantic change within recipe format 1;
existing keyword alternatives become requirements, effect keys can match more
items, and effect-keyed signal values can change.
