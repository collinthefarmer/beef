Status: record. First schema/parser contract pass: format and integer
boundaries, clock shape, default preservation, and remaining coverage.

# Recipe contract review — 2026-09-23

First pass: format tags, integer representation limits, optional clock objects,
material-cluster bounds, and default preservation. This does not close the
full schema/parser agreement review.

## Corrections

- Format tags below 1 now report an unsupported-format error. Previously the
  loader rejected newer versions but accepted zero and negative versions.
  As with other recipe errors, the editable recipe remains available while
  application is held back.
- Reader integer fields check representable bounds before conversion to int.
  For example, format 4294967297 could previously narrow to format 1. Priority
  and other integer fields now report a located diagnostic instead of wrapping.
- The schema records the signed 32-bit reader bounds for priority, seed, and
  max fields; existing narrower domain bounds remain unchanged.
- Numeric biped slots check their range before narrowing, so large positive
  or negative values cannot wrap into slots 30..61.
- Clock configuration uses the existing object reader. Arrays, null, and
  scalars now produce a recipe-level error instead of silently using defaults.

## Regression coverage

`tools_recipe_contract_tests` feeds the same documents to check-jsonschema
and the built native validator. CMake supplies the validator from the selected
build directory, including the sanitizer build. Cases cover required fields,
format tags, priority boundaries, optional objects, unknown fields, cluster
settings, and numeric biped slots. Separate expectations document the schema's
structural limits: malformed expression syntax and missing references pass
its string shape but fail the native semantic validator.

Native recipe tests also check that omitted optional fields preserve model
defaults and survive serialization without changing the recipe, and assert
located errors for unsupported formats and integer overflow.

## Remaining review

Expand coverage to other signal/source/output alternatives, required output
scalars, references and variants, collection/depth limits, and numeric policy.
In particular, review integral floating-point JSON spellings, float range and
non-finite conversion, and fields that clamp invalid values instead of
reporting schema-bound violations. The structural schema cannot replace the
expression/type/reference checks performed by the plugin.

## Validation

Windows Release build passed. Six selected CTest suites passed from the
native-sanitized configuration: recipe_recipe, recipe_schema, recipe_binders,
studio_presets, schema, and tools_recipe_contract_tests. The differential
suite covers 59 input cases, invoking the sanitizer-built validator for each.
Targeted clang-tidy covered all four translation units including Binders.h
and passed the existing baseline with no new findings. Formatting, include
layer checks, and `git diff --check` passed. No game execution or candidate
repackaging was performed; the previous package checkpoint remains evidence
for its recorded build only.

## Float and output follow-up

A shared FloatFrom reader now rejects non-finite values and values outside
float range before narrowing. Scalar parameters, literal vectors, pose points,
plain numeric fields, and event-filter endpoints use it. The schema's shared
number definition carries the same float magnitude limits. Ordinary rounding
and underflow to zero are unchanged; these checks do not guarantee that later
expression arithmetic or rendering calculations remain finite.

Previously clamped negative noise seeds and image mip levels, out-of-range
shell alphaTest, and explicit invalid skinned-light max/minShare values now
report errors instead of silently accepting corrected input. Event filter
endpoints accept numbers or null and reject other types. Omitted fields retain
their defaults, including the internal skinned-light minShare sentinel.

The differential matrix now includes float endpoints and overflow through
several reader paths, event filter bounds/types, these domain bounds, and
required fields for emissive, height, fuzz, coat, subsurface, and light outputs.
Native binder tests exercise NaN and infinities directly (they are not JSON
number literals), both finite float endpoints, and diagnostic row locations.
The existing semantic checks already enforce the tested required output fields.

Remaining work includes integral floating-point spellings for integer fields,
precision near domain boundaries, other row alternatives, references/variants,
collection limits, and arithmetic overflow after parsing. The broad plan item
remains open.

Follow-up validation: Windows Release build passed. All six selected CTest
suites passed again in the native-sanitized configuration, including the
expanded 116-case differential matrix. Binder tests passed direct non-finite
input checks. Clang-tidy covered the four affected translation units and
passed the existing baseline with no new findings. Formatting, include-layer,
and diff whitespace checks passed. No game run or candidate repackaging.

## References, variants, and collection follow-up

Row readers now count attempted entries toward the existing 4,096-entry cap,
including entries rejected by their parser. Previously arrays, named sections,
selectors, bone lists, and variant overrides could keep processing invalid
entries because only accepted rows counted. Gradient stops already counted
every entry. This bounds per-collection processing after JSON parsing; it is
not a file-size limit, total recipe allocation budget, or streaming JSON parser.

The schema now exposes these collection limits. Native checks tie major schema
limits to kMaxRecipeRows, and rejected-row tests verify readers stop attempting
rows at the limit. Differential cases cover 4,096 and 4,097 keys, signals, and
variants. Document depth is tested separately: the native 32-level safety limit
is stricter than the free-form meta structure admitted by the schema.

References must use a valid row name after @. Empty variant names and empty
bone names now report errors, matching existing schema constraints. Variant
structure, valid scalar/vector overrides, unknown signals, mismatched override
types, reference cycles, and unresolved references have comparison coverage.
The existing semantic validator already reports the tested unknown-reference,
cycle, and override-type errors; these are not structural schema checks.

The broad review remains open for other row alternatives, integer spelling and
numeric precision policy, duplicate variant identity policy, and runtime form
resolution. This pass does not establish a global memory budget for hostile
JSON documents.

Validation for this follow-up: Windows Release build passed; all six selected
CTest suites passed in native-sanitized, including the expanded 146-case
schema/parser matrix and rejected-row processing-limit tests. The schema-limit
checks were rebuilt and passed. Clang-tidy covered all four affected translation
units and passed the existing baseline without new findings. Formatting,
include-layer checks, and diff whitespace checks passed. No game run or
candidate repackaging was performed.

## Shared load and edit validation

Loading and studio edits now share typed model validation, including finite
values, collection caps, and the numeric domains audited above. Single edits,
batches, and tuning gestures validate before publishing; refusal preserves the
document and returns a located diagnostic through the existing UI result path.
Existing model errors remain repairable. Incomplete type-switch drafts stay
editable; semantic errors appear in normal row diagnostics, while new field
domain and collection-limit violations refuse the transaction. File-decoding diagnostics persist
across edits until successful save or reload, so unrelated changes no longer
erase errors caused by discarded or defaulted input. In-game display acceptance
remains pending.

Validation for this follow-up: all 48 selected recipe/studio/schema CTest
suites passed, including 40 new shared-validation checks and the differential
schema/parser suite. The Windows Release DLL built successfully. Targeted
clang-tidy gates reported no new findings; formatting and diff checks passed.
