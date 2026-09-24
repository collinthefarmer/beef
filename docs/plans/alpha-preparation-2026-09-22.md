# Alpha preparation and source publication

Status: active. Consolidated from the 2026-09-22 planning session. This is
the sole active work plan. All previous plans, backlogs, and handoffs from
`docs/plans/` are archived under `docs/history/`; their remaining tasks are
not automatically carried forward. Checkpoints remain dated evidence.

Use active source and `docs/components/` to establish what exists.
Requirements and the recipe schema define the intended contract;
`REFERENCE.md` records constraints and rationale. Historical measurements
and completion claims need confirmation against the release candidate.

The objective is a small closed alpha with published source: reproducible
builds, safe authoring, bounded runtime costs, a few complete workflows,
and an installable package. Checkboxes record completed implementation and checks; unchecked items
remain outstanding. Build verification is recorded in the linked checkpoint.
Keep unrelated working-tree edits intact.

## 1. Replace build and analysis orchestration

Do this before the code-quality sweep. Build a small replacement alongside
the current process, demonstrate equivalent outputs and coverage, then
remove the old orchestration. Retain convenient command entry points where
practical. A contributor should be able to explain each target and command
from standard CMake, Ninja, CTest, and clang-tidy behavior.

- [ ] Measure the existing clean build, unchanged build, single-source edit,
  shared-header edit, unchanged native tests, targeted/full tidy, and push
  gate. Record elapsed time, peak memory, tool versions, and configuration.
- [x] Define a native CMake configuration for the engine-free library,
  validator, and tests, with CTest execution and separate normal/sanitizer
  build directories. Include the engine-free engine services and existing
  Python/schema checks; preserve suite selection and meaningful coverage.
- [x] Let Ninja own compilation and linking dependencies. Eliminate the
  test runner's unconditional relinking and hand-maintained object cache.
- [x] Define the Windows configuration using the clang-cl/xwin toolchain,
  producing the SKSE DLL and Windows validator. Preserve pinned dependencies,
  Nix tooling, ABI/SDK flags, CommonLib integration, and required symbols.
- [x] Model build identity and presenter textures as generated outputs with
  correct dependencies. Preserve dirty-source identity and avoid repeated
  asset scanning when inputs are unchanged.
- [x] Separate explicit staging/packaging from compilation. Ensure changed
  runtime assets can be staged without requiring a DLL relink.
- [x] Use CMake's compilation database for clangd and a separate clang-tidy
  invocation. Any filtered database should be stable when content is
  unchanged. Remove redundant configure/database-generation work.
- [x] Start without a custom tidy cache. Measure standard execution and
  sensible translation-unit selection before adding complexity. Header-only
  changes must be covered, including affected translation units.
- [ ] If analysis caching proves necessary, invalidate on tool, command,
  configuration, source, and dependency changes. Missing or interrupted
  results must never count as clean; unrelated database entries should not
  invalidate all results.
- [x] Establish memory-aware concurrency and fast local checks plus an
  explicit full validation command. Keep sanitizers and full analysis in
  release validation. Review baseline handling so line movement alone does
  not force regeneration, and header findings cannot escape the gate.
- [ ] Before restoring first-party compiler caching, reproduce the header
  dependency regression described in `REFERENCE.md` (Build and tools) and
  prove dependency recording remains correct on cache hits.
- [x] Verify unchanged runs, source/header changes, compiler-option changes,
  generated files, interrupted runs, and source rename/deletion. Compare
  artifacts and test coverage with the old process and report timings.
- [x] Switch documented commands and hooks to the replacement, remove the
  obsolete scripts/cache machinery, and document clean setup and recovery.

Original inspection findings (addressed by the rewrite): native suites relinked every invocation; the
filtered compile database was rewritten unconditionally and invalidated tidy
by timestamp; tidy selection missed header-only edits; its cache omitted tool
identity; concurrency differed between standalone tidy and the push gate.
These are starting points, not measured performance rankings.

Done when the new process is reproducible, dependency-correct, simpler to
explain, and measured against the old one. A speedup must not weaken checks.

Implementation and current verification:
[build rewrite checkpoint](../checkpoints/build-rewrite-2026-09-22.md).
First-party compiler caching remains disabled; no custom tidy cache was
introduced, so the two conditional cache tasks above are not prerequisites
for using this replacement. Broader benchmark comparisons remain open.

## 2. Prepare the source tree for publication

Keep cleanup patches small and separate from behavior changes. Complete
this pass before release-candidate validation.

- [x] Inventory `_old` source/tests, debug fixtures, generated artifacts,
  captured logs, and developer-specific paths. Keep intentional test inputs
  clearly identified and remove accidental distribution/repository debris.
- [x] Identify unique behavior expectations and fixtures still held only in
  `_old`; preserve useful tests or documentation, then remove the frozen
  implementations from the active tree. Git history retains their context.
- [ ] Require evidence before deleting apparently unused APIs: check build
  membership, references, callbacks, visitors, and integration entry points.
- [x] Reconcile the shipped INI with `SettingTable()`, parsing, serialization,
  and actual runtime behavior. Remove obsolete controls and explanations.
- [ ] Review `ManagerApply`, `ManagerTick`, and `RecipeEditor` for duplicated
  policy and unclear ownership. Extract testable decisions where useful;
  do not split files merely to reduce their line count.
- [ ] Trace recipe pointers/indices across store republication, reload,
  actor rebuilds, and deferred edits. Review callback lifetimes, GPU leases,
  field restoration, error propagation, and cancellation boundaries.
  The [retirement review](../checkpoints/source-ownership-2026-09-23.md#retirement-and-republication-review)
  confirms the inspected publication boundaries and fixes distance eviction
  leaving prepared application records pending. Engine scheduling and external
  ownership takeover still require runtime evidence.
- [x] Bound obsolete recipe-scoped application history and release canceled
  actor lists. Retain 256 terminal recipe revisions, preserve every unfinished
  actor, and inherit pending/failed retry targets plus current manager candidates.
  Completed historical wearers no longer accumulate across edits; resume clears
  prior cancellation summaries. This is a history bound, not an active workload
  cap. See the [retention checkpoint](../checkpoints/application-retention-2026-09-24.md).
- [x] Release lost-geometry resources, run cache maintenance with no applied
  actors, expire unused material analysis and weak-cache keys, and bound the idle
  target pool. Native ownership/policy tests and adapter compilation are recorded
  in the [resource-retention checkpoint](../checkpoints/resource-retention-2026-09-24.md).
  Runtime recovery, external takeover, and supported workload budgets remain open.
- [ ] Make accepted, prepared, rendered, and saved outcomes distinguishable
  in diagnostics and the editor. Failures must leave valid state and reach
  the user with an actionable location/message.
- [ ] Review clang-tidy findings by category; fix demonstrated problems and
  justify retained findings instead of bulk suppression or mechanical churn.
  The [static-analyzer review](../checkpoints/static-analyzer-2026-09-24.md)
  covers 126 translation units, documents retained warnings, fixes mixed-case
  diagnostic reporting, and refreshes tuning after removing its duplicate parse.
  The [hook safeguard](../checkpoints/hook-startup-2026-09-24.md) then removes
  the zero-address relocation path and exposes its failure before a snapshot.
- [ ] Refresh `MAP.md`, component sheets, build instructions, and current
  cross-references. Correct the edit-versus-save flow, variant behavior, and
  native service coverage. Historical documents remain historical.
- [ ] Verify third-party attribution/license notices, dependency provenance,
  and the source/package contents intended for distribution.
  The [offline notice audit](../checkpoints/licenses-2026-09-23.md) records
  bundled texts and verified vendored headers. Matching-source delivery,
  first-party linking permissions, game-derived fixture provenance, and the
  unrecorded cimgui generator revision remain publication review items.
- [x] Inventory pinned third-party notices and ship the license bundle in
  staging, mod archives, and symbols archives, with integrity and omission tests.

Done when a new contributor can locate the active code, build and test it,
understand its ownership boundaries, and distinguish examples from experiments.

Initial ownership review and targeted validation:
[source ownership checkpoint](../checkpoints/source-ownership-2026-09-23.md).
This records the live-instance index correction and the store publication,
queue cancellation, and texture ownership boundaries; the broader review
and in-game lifetime checks remain open.

## 3. Establish the alpha candidate and compatibility envelope

- [ ] Record revision, build/source identity, dependency versions, supported
  Skyrim runtime, Community Shaders configuration, and menu requirements.
  Verify actual PBR ABI compatibility in game; static assertions alone do
  not validate the installed Community Shaders binary.
- [x] Build candidate archives from the explicit package inventory, pass the
  full native/sanitizer and normal analysis checks, and retain results with
  candidate identity and archive checksums. The [offline gate](../checkpoints/offline-gate-2026-09-24.md)
  passed 99/99 suites in each native configuration and full 126-TU tidy against
  the unchanged baseline. Builds were incremental; fresh bootstrap and the
  separate static-analyzer mode are not established by this result.
- [ ] Define the supported workload and provisional frame-time/resource
  budgets, then select release defaults using the measurements below.

## 4. Validate runtime lifetime, isolation, and performance

Investigate `Manager`, `SessionQueue`, `ApplicationService`, `LiveActor`,
bindings, shells, compositor/mesh caches, `TextureRef`, render-target pools,
previews, readbacks, and eviction.

- [ ] Exercise repeated equip/unequip, cell changes, actor unloading,
  first-/third-person switching, disable/retire/reapply, and new game.
- [ ] Load saves during queued edits, gestures, and paint previews. Verify
  old work cannot affect the new session and no application stays pending.
- [ ] Verify two wearers of the same armor remain isolated, including an
  unenchanted control. Verify vanilla effect-shader coexistence and return
  to baseline without changing equipment.
- [ ] Exercise genuine material/shell ownership takeover by another system;
  retirement must preserve that owner's state. Mark unavailable fixtures
  blocked rather than treating ordinary unequip as equivalent evidence.
- [ ] Verify all shell pose controls, lights, slot composition, selectors,
  animation, and restoration through visible in-game results.
- [ ] Measure static and animation-heavy crowds, armor variety and large
  textures, first-application bursts, frame-time spikes, target count/bytes,
  readback stalls, and recovery after actors leave.
  Source-side readback assessment removed unnecessary curve mean acquisition
  and added lock/Map timing and completion fields; see the
  [readback assessment](../checkpoints/source-ownership-2026-09-23.md#readback-assessment).
  Candidate measurements and asynchronous readback remain outstanding.
- [ ] Run an hour-long crowd soak and an extended authoring/play session
  with repeated preview cycles and save loads. Capture complete evidence.
- [ ] Rerun using the actual packaged defaults. Eviction currently defaults
  off; results with eviction enabled do not establish default behavior.

Done when the supported workload has bounded resource use and acceptable
application spikes, with no crashes, freezes, stale effects, isolation
failures, or destructive restoration. Logs alone do not prove rendered pixels.

## 5. Protect authored files and establish the recipe contract

- [x] Implement the approved [recipe resolution contract](../recipe-resolution.md):
  identity-based overrides, independent same-key candidates, deterministic
  sampling, placement-local precedence, grouped replacement, and consistent
  shell/light planning and diagnostics. The document supplies acceptance
  examples and supersedes exclusive key ownership. Implementation and offline
  verification are recorded in the [resolution checkpoint](../checkpoints/recipe-resolution-2026-09-23.md).
  Rendered acceptance remains pending under section 4. The completed breakdown
  was measured against the contract acceptance examples.

  1. Extract definition precedence, remove exclusive key ownership, and pin
     canonical sampled selection with hash vectors.
  2. Carry placement priority and definition order through geometry and light
     planning; share precedence comparison and group replacement by recipe.
  3. Align shell ownership, runtime diagnostics, and preview overrides; retain
     validation, import coverage, and preparation failure behavior.
  4. Add boundary regressions, run native/sanitized suites, Windows compilation
     and targeted analysis, then update current documentation. Rendered output
     and save/reload acceptance stay on the deferred in-game list.

- [x] Implement failure-safe recipe replacement: the current `WriteText`
  truncates the destination. Failed writes must preserve the previous file
  and report failure accurately. Review settings persistence as well.
- [ ] Test create/edit/undo/redo/save/restart/reload, stale revisions,
  interrupted or refused operations, gesture commits, and paint commits.
  Native cross-component scenarios now cover edit/history/save failure/retry,
  paint assignment refusal/undo/redo/reopen, and old-session queued saves.
  See the [offline lifecycle checkpoint](../checkpoints/authoring-lifecycle-2026-09-23.md).
  Actual RecipeEditor/store callbacks now run in a native integration target
  with platform test doubles; Skyrim restart and displayed UI acceptance remain open.
  Revision tracking and revert file preparation now also use shared production
  helpers covered by reset/reused-ID, stale gesture/paint, and failed-read
  regressions. The production operation journal and task guards now have
  SessionQueue coverage for load cancellation, rejected/dropped work, late
  completion and same-ID replacement. Integration coverage now verifies failed
  revert preservation/retry, gesture save/revert ordering, reload history reset,
  stale queued edits, pending-save load cancellation and import promotion routing.
  Actor retirement/rebuild and form discovery still require the game.
- [x] Exercise actual editor/store queued callbacks natively for failed revert,
  gesture file operations, reload/load cancellation, import-promotion state,
  creation/duplication, rename/delete, shipped/user definition ownership and paint.
  Paint coverage includes commit/discard, source transfer, history, persistence,
  stale requests, destination changes and load/reset cancellation. Keep now refuses
  reserved `peek` names that save preparation would otherwise discard.
  Validation coverage includes refused/no-op batches preserving redo, input versus
  semantic diagnostic lifetime across save/reload/revert, and gesture error recovery
  and cancellation. Actual editor results now also exercise UI acknowledgement
  gates, refusal/retry, stale snapshots, navigation and paint/load reset handling.
  A late rename no longer resets another document's navigation.
  Shipped save routing now preserves source files and adopts a user override only
  after successful writing, with failure/retry, nested-path and reload coverage.
  The isolated platform harness passes all 267 checks natively and with sanitizers;
  it does not substitute for the actor/render and in-game acceptance checks above.
- [x] Make recipe save preparation/write engine-free and regression-test
  failure-safe import promotion and paint cleanup. The store adopts the exact
  written document only after success; see the lifecycle checkpoint above.
- [ ] Verify duplicate identities, rename/delete, user overrides, shipped
  file preservation, and imported-recipe lifecycle.
  Rename/delete failure refusal and missing resource rename controls have
  landed with targeted native coverage; see the
  [authoring follow-up](../checkpoints/source-ownership-2026-09-23.md#authoring-follow-up).
  Actual callback tests now cover creation/duplication through save/reload,
  rename history/view transfer and refusal, delete retry and ID reuse, shipped
  rename/delete preservation, and user-override precedence/removal. Owned rename
  also fixes the saved baseline ID so undo can become clean again. See the
  [integration follow-up](../checkpoints/authoring-lifecycle-2026-09-23.md#creation-identity-and-file-ownership-integration).
  Shipped saves now write user overrides; failure preserves the prior origin and
  both files. Existing nested user paths remain in place. See the
  [save ownership follow-up](../checkpoints/authoring-lifecycle-2026-09-23.md#save-ownership-and-shipped-overrides).
  In-game verification remains open.
- [ ] Check schema/parser agreement beyond vocabulary: defaults, required
  fields, bounds, references, malformed expressions, and output semantics.
  The [first contract pass](../checkpoints/recipe-contract-2026-09-23.md)
  adds differential schema/parser coverage and fixes format, integer-narrowing,
  and clock-object errors. Its float/output follow-up rejects overflowing
  literals and silently clamped inputs, and checks required output fields.
  Reference/variant cases and collection boundaries are covered; rejected rows
  now count toward parser limits, which are also exposed in the schema.
  Typed field validation now also guards live edits and gestures; incomplete
  drafts retain semantic diagnostics, and edits preserve file-decoding errors
  until save or reload. In-game diagnostic display, other alternatives, and
  numeric policies remain open.
- [ ] Provide validated, round-tripped, visibly tested examples for constant
  glow, animation/actor state, material masks, shell/light, and two merging
  recipes, with short walkthroughs.
- [ ] Document key ownership, priority versus merge, load order, import and
  save locations, preset status, known limitations, and alpha format-change
  expectations. State the supported format and migration policy explicitly.

Done when a tester can modify an example, understand a refusal, and retain
their work across failures and restarts.

## 6. Package, reproduce, and release

- [x] Add explicit-inventory candidate archives, separate symbols, payload
  manifests and checksums, and integrity regression tests. Packaging bypasses
  stale staging contents. See the [package checkpoint](../checkpoints/package-verification-2026-09-23.md). This does not close release packaging or licensing.
- [x] Verify installer upgrade/failure behavior against temporary directories,
  preserving authored recipes and existing settings. Both rsync and tar paths
  are covered; see the [installer follow-up](../checkpoints/package-verification-2026-09-23.md#installer-follow-up).
  Real MO2/Windows installation checks remain open.
- [ ] Verify and publish the user-facing dependency matrix: distinguish direct
  requirements, dependencies of peer plugins, editor-only requirements, and
  recommendations; pin tested versions per compatibility profile. Audit
  missing/incompatible dependency handling, document runtime prerequisites,
  and provide one reproducible TruePBR armor-and-recipe setup. Confirm in a
  fresh profile before treating the list as supported. The
  [preliminary dependency findings](../checkpoints/dependencies-preliminary-2026-09-23.md)
  record source inspection and upstream documentation, not runtime validation.
- [x] Audit missing-dependency handling offline: require a loaded menu module
  and the editor's export inventory before registration, report missing CS
  even without a runtime snapshot, and regression-test the refusal policy.
  See the dependency checkpoint above. ABI/version acceptance and visible
  fresh-profile checks remain open.
- [x] Implement candidate compatibility profiles driving dependency pins, loader
  declarations, runtime guards, package labels, and configuration-sensitive
  identity. Initial target: Steam 1.6.1170 / SKSE 2.2.6. See the
  [profile audit](../checkpoints/compatibility-profiles-2026-09-24.md).
- [ ] Accept the initial profile in game and record exact Address Library,
  Community Shaders/TruePBR, and menu versions with candidate archive checksums.
  Compiled SE/AE support and candidate peer baselines do not establish tested support.

- [ ] Create a versioned archive containing the DLL, corrected INI,
  templates, presets, presenter textures, validator, and concise setup and
  example instructions. Choose explicit example installation so debug
  recipes are not accidentally loaded. Supply symbols separately.
- [x] Verify the package includes the validator: the build rewrite added it
  to explicit staging. Record actual archive contents and build identity.
  The [reporting follow-up](../checkpoints/package-verification-2026-09-23.md#reporting-follow-up)
  records byte comparison against the built Windows executable.
- [ ] Install in a fresh MO2 profile without development leftovers; verify
  missing-dependency behavior, upgrades, preservation of settings/recipes,
  disabling, and removal.
- [ ] Update and execute `docs/in-game-regression.md` against current source.
  Record PASS/FAIL/BLOCKED per case, fixture/configuration files, expected
  versus actual visuals, logs/traces, and candidate identity.
- [x] Provide a bug-report channel/template requesting build identity,
  effective settings, recipe inputs, reproduction steps, and relevant
  logs/screenshots. State supported dependencies and known limitations.
  The [report template](../bug-report.md) ships as `BUG_REPORT.md` and directs
  closed-alpha testers back to the candidate's delivery conversation. It
  explicitly records the unverified runtime matrix; a public tracker and
  tested compatibility claims remain release decisions.
- [ ] Publish the matching source revision with build instructions, license
  and attribution, release notes, and the tested alpha package after the
  acceptance checks pass.

Alpha blockers: crashes/freezes, unbounded resource growth inside the
supported workload, authored-file loss, incorrect isolation/restoration,
or a delivered package that differs materially from what was tested.

Deferred: localization, broad UI polish, new output types, exhaustive
tutorials, broad renaming/reformatting, and speculative abstraction changes.
Reopen archived work only through an explicit scope decision in this plan.
