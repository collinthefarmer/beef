Status: implementation, native/sanitizer/Windows checks and targeted analysis
complete. In-game acceptance remains pending.
This records the dirty working tree, not a published release or source tag.
The [alpha plan](../plans/alpha-preparation-2026-09-22.md) owns remaining work.

# Recipe resolution implementation checkpoint

## Behavior and structure

The [resolution contract](../recipe-resolution.md) is implemented through
small records and free functions in the existing resolver and planners:

- `AppendDefinition` replaces a successfully decoded same-ID definition at
  its later traversal position. Validation still decides whether the new
  definition can be applied. Imports retain authored-key coverage checks.
- Resolution matches independent recipe identities, suppresses fallbacks,
  selects one sampled identity, then orders selected contributions. Shared
  keys no longer generate ownership warnings.
- Sampling sorts case-sensitive UTF-8 identities and uses 32-bit FNV-1a over
  four little-endian actor form-ID bytes. Golden vectors pin the algorithm;
  priority, file ordering and output edits do not influence the choice.
- `Precedence` compares effective priority and definition load order.
  Surface priority belongs to `Placement`; instances share signal state and
  clocks without promoting another piece's composition priority.
- Surface replacement cuts at the start of a recipe placement's target
  group, retaining sibling outputs. Scalar ownership still follows the last
  surviving explicit value. Shell settings use shared precedence across slots.
- Light planning groups eligible instances by recipe identity and takes the
  maximum eligible placement priority for that actor-wide group. Recipe and
  output replacement retain all instances in the replacing group.
  `LightEligible` checks live third-person geometry and the light selector;
  planning and actual node placement use the same predicate. Light selectors
  were previously ignored by those paths.
- Preview isolation and pinning retain original definition order and sort
  through shared precedence. Normal runtime matching and selected-piece
  outcomes use the actual actor ID. The UI distinguishes preview overrides,
  sampled-out/fallback/nonmatching outcomes, and actor-wide light replacement.

Overridden and held-back definitions retain their load log messages. Surface
selector, replacement and preparation diagnostics retain their existing
output locations. The new selection report does not imply prepared or
rendered pixels, and preparation failures do not reroll sampling or revive
contributions already displaced by replacement.

The scalar acceptance example now uses the legal fuzz color/weight pair;
no slot supports the original example's color/strength scalar pair.

## Verification

Verification commands ran inside `nix develop`. Main build commands used two
jobs; the final sanitizer suite also capped nested CMake builds at two jobs.

- Native: 90/90 suites passed; after the final analysis-driven helper
  extraction, all 79 C++ suites passed again. Tool/schema inputs were unchanged.
- ASan/UBSan: 90/90 suites passed on the final implementation.
- Windows Release: plugin DLL and standalone validator built successfully.
  A broad rebuild reported existing initializer/unused-helper warnings in
  other source locations; this is not a claim of a warning-free repository.
- `planners_resolution`: 35 contract checks, including golden hashes, UTF-8
  ordering, same-key/mixed sampling, definition precedence, strongest keys,
  fallback outcomes, cross-piece priority, grouped surface/light replacement,
  selector/first-person/lost-light eligibility, scalar ownership, shell ties,
  no reroll after selector exclusion, and isolation/pinning.
- Existing merge, actor-planning and preview tests were updated for the
  intentional contract changes. Formatting and include-layer checks passed.
- Targeted clang-tidy covers the eleven resolver/planner/loader/runtime/UI
  translation units changed by this work: six existing function-size findings,
  no new findings against the reviewed baseline (`tidy-baseline.py --gate`).
  The full release analysis gate remains separate.

Local command output is in `/tmp/beef-resolution-*.log`; the successful tidy
report and individual diagnostics are under `build/tidy/`. These are local
verification artifacts, not shipped candidate evidence.

## Deferred acceptance

[The in-game procedure](../in-game-regression.md#recipe-resolution-acceptance-pending-runtime-access)
covers visible composition, shell settings, light node placement and
replacement, save/reload selection stability, controlled preparation failure,
and restoration. None can be established by a native plan test or DLL link.
No MO2 installation, game execution, archive publication or license change
was performed.

## Addon selector review follow-up

Review found that the newly enforced light selectors received no addon identity
from the engine adapter. Collection now takes `BIPOBJECT::addon` from the same
biped entry as the armor clone, stores its optional `FormKey` on `LivePiece`,
and forwards it into planner geometry identity. This also supplies the existing
surface addon selectors. No addon pointer is retained, and absent addon data
remains absent.

Six regression checks cover matching/different/missing addons, plugin identity,
light replacement participation, nonmatching replacement refusal, and
first-person/lost exclusions. Runtime validation should include two addons
with the same geometry name/texture and verify only the selected addon emits
the light; native tests cannot establish the game's clone-to-addon association.

Follow-up verification passed: five targeted native suites, the resolution
suite under ASan/UBSan, and the Windows Release build. Targeted ManagerApply
analysis retained four existing findings with none added against the baseline.
Formatting and diff checks passed. No game run or installation was performed.
