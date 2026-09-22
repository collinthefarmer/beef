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

- [ ] Inventory `_old` source/tests, debug fixtures, generated artifacts,
  captured logs, and developer-specific paths. Keep intentional test inputs
  clearly identified and remove accidental distribution/repository debris.
- [ ] Identify unique behavior expectations and fixtures still held only in
  `_old`; preserve useful tests or documentation, then remove the frozen
  implementations from the active tree. Git history retains their context.
- [ ] Require evidence before deleting apparently unused APIs: check build
  membership, references, callbacks, visitors, and integration entry points.
- [ ] Reconcile the shipped INI with `SettingTable()`, parsing, serialization,
  and actual runtime behavior. Remove obsolete controls and explanations.
- [ ] Review `ManagerApply`, `ManagerTick`, and `RecipeEditor` for duplicated
  policy and unclear ownership. Extract testable decisions where useful;
  do not split files merely to reduce their line count.
- [ ] Trace recipe pointers/indices across store republication, reload,
  actor rebuilds, and deferred edits. Review callback lifetimes, GPU leases,
  field restoration, error propagation, and cancellation boundaries.
- [ ] Make accepted, prepared, rendered, and saved outcomes distinguishable
  in diagnostics and the editor. Failures must leave valid state and reach
  the user with an actionable location/message.
- [ ] Review clang-tidy findings by category; fix demonstrated problems and
  justify retained findings instead of bulk suppression or mechanical churn.
- [ ] Refresh `MAP.md`, component sheets, build instructions, and current
  cross-references. Correct the edit-versus-save flow, variant behavior, and
  native service coverage. Historical documents remain historical.
- [ ] Verify third-party attribution/license notices, dependency provenance,
  and the source/package contents intended for distribution.

Done when a new contributor can locate the active code, build and test it,
understand its ownership boundaries, and distinguish examples from experiments.

## 3. Establish the alpha candidate and compatibility envelope

- [ ] Record revision, build/source identity, dependency versions, supported
  Skyrim runtime, Community Shaders configuration, and menu requirements.
  Verify actual PBR ABI compatibility in game; static assertions alone do
  not validate the installed Community Shaders binary.
- [ ] Build from clean staging, pass the full native/sanitizer and analysis
  checks, and retain results with the candidate identity.
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
- [ ] Run an hour-long crowd soak and an extended authoring/play session
  with repeated preview cycles and save loads. Capture complete evidence.
- [ ] Rerun using the actual packaged defaults. Eviction currently defaults
  off; results with eviction enabled do not establish default behavior.

Done when the supported workload has bounded resource use and acceptable
application spikes, with no crashes, freezes, stale effects, isolation
failures, or destructive restoration. Logs alone do not prove rendered pixels.

## 5. Protect authored files and establish the recipe contract

- [ ] Implement failure-safe recipe replacement: the current `WriteText`
  truncates the destination. Failed writes must preserve the previous file
  and report failure accurately. Review settings persistence as well.
- [ ] Test create/edit/undo/redo/save/restart/reload, stale revisions,
  interrupted or refused operations, gesture commits, and paint commits.
- [ ] Verify duplicate identities, rename/delete, user overrides, shipped
  file preservation, and imported-recipe lifecycle.
- [ ] Check schema/parser agreement beyond vocabulary: defaults, required
  fields, bounds, references, malformed expressions, and output semantics.
- [ ] Provide validated, round-tripped, visibly tested examples for constant
  glow, animation/actor state, material masks, shell/light, and two merging
  recipes, with short walkthroughs.
- [ ] Document key ownership, priority versus merge, load order, import and
  save locations, preset status, known limitations, and alpha format-change
  expectations. State the supported format and migration policy explicitly.

Done when a tester can modify an example, understand a refusal, and retain
their work across failures and restarts.

## 6. Package, reproduce, and release

- [ ] Create a versioned archive containing the DLL, corrected INI,
  templates, presets, presenter textures, validator, and concise setup and
  example instructions. Choose explicit example installation so debug
  recipes are not accidentally loaded. Supply symbols separately.
- [ ] Verify the package includes the validator: the current build creates
  it but does not stage it. Record archive contents and build identity.
- [ ] Install in a fresh MO2 profile without development leftovers; verify
  missing-dependency behavior, upgrades, preservation of settings/recipes,
  disabling, and removal.
- [ ] Update and execute `docs/in-game-regression.md` against current source.
  Record PASS/FAIL/BLOCKED per case, fixture/configuration files, expected
  versus actual visuals, logs/traces, and candidate identity.
- [ ] Provide a bug-report channel/template requesting build identity,
  effective settings, recipe inputs, reproduction steps, and relevant
  logs/screenshots. State supported dependencies and known limitations.
- [ ] Publish the matching source revision with build instructions, license
  and attribution, release notes, and the tested alpha package after the
  acceptance checks pass.

Alpha blockers: crashes/freezes, unbounded resource growth inside the
supported workload, authored-file loss, incorrect isolation/restoration,
or a delivered package that differs materially from what was tested.

Deferred: localization, broad UI polish, new output types, exhaustive
tutorials, broad renaming/reformatting, and speculative abstraction changes.
Reopen archived work only through an explicit scope decision in this plan.
