# The documents under `docs/`

- The directories mirror the sections below: canon documents sit at this
  root (plus `components/`), open work under `plans/`, dated records under
  `checkpoints/`, superseded documents under `history/`.
- Every document under `docs/` appears once below, as its own entry or
  through its directory's entry.
- `REQUIREMENTS.md`, `REFERENCE.md`, and `CLAUDE.md` at the repository root
  govern; nothing here overrides them. The reading order lives in the root
  `README.md`; this index owns completeness and status.
- A document in **History** describes a tree that has since been renamed or
  restructured and carries a status header saying so. Read it for the
  decision it recorded, not for the names it uses.
- [This index](README.md) — what each document answers, and whether it is
  current.

## Canon

Current. A new module is expected to follow them.

- [components/](components) — one onboarding map per `src/` directory
  (`recipe`, `mesh`, `planners`, `diagnostics`, `studio`, `render`, `engine`,
  `menu`): what the module owns, the data it defines grouped by domain, how a
  request flows through it, and the file table. Read the sheet for a directory
  before changing it; it links out to `REFERENCE.md` for the facts the code
  cannot state and to `conventions.md` for the patterns it obeys.
- [conventions.md](conventions.md) — the patterns a new module is built on, the
  glossary of one-word-one-meaning names, and the gates (`tools/gate.sh`,
  `tools/layers.sh`, `tools/format.sh`, the native suite).
- [ui-api.md](ui-api.md) — how a studio surface is built: the `menu/` widget
  vocabulary (`Rule`, `Table`, the shared styles), the `Frame`, forms and
  fields, editing through intents, and navigation. The UI-layer counterpart to
  `conventions.md`, grounded in the current headers.
- [in-game-regression.md](in-game-regression.md) — the repeatable manual
  integration run: startup, matching, actor state, GPU output, studio edits,
  persistence, teardown, with the log lines each step prints.
- [ui-design-principles.md](ui-design-principles.md) — the agreed direction for
  the editor's surface, which the UI v2 work applies.

## Active plans (`plans/`)

Open work.

- [plans/release-roadmap-2026-09-20.md](plans/release-roadmap-2026-09-20.md) —
  what must be true before the mod reaches another person: the five gates,
  their status against the current tree, and the ranked list. Supersedes the
  2026-09-09 roadmap in History.
- [plans/format-row-reference-2026-09-21.md](plans/format-row-reference-2026-09-21.md)
  — every authorable row of format 1, kind by kind: parameters, runtime
  semantics, audited status, and the work items the gate-5 freeze must
  settle. Working material for the freeze; the schema stays the contract.
- [plans/texture-budget-2026-09-20.md](plans/texture-budget-2026-09-20.md) —
  gate 1's texture fix, staged against the 2026-09-20 measurement: the
  retention defect, in-play trimming, per-slot resolution factors, then
  demotion and eviction only as the numbers demand.
- [plans/ui-v2-proposal.md](plans/ui-v2-proposal.md) — the frozen UI v2 design
  baseline.
- [plans/ui-v2-implementation-plan.md](plans/ui-v2-implementation-plan.md) —
  the slices that implement that baseline, and which seam each slice owns.
- [plans/ui-backlog-2026-09-16.md](plans/ui-backlog-2026-09-16.md) — the UI
  asks captured mid-flow: the ask, where it lives, the rough direction.
  Several items have shipped; the rest remain open.
- [plans/ui-v2-fine-tuning-backlog-2026-09-15.md](plans/ui-v2-fine-tuning-backlog-2026-09-15.md)
  — the fine-tuning round after the in-game test of the 13 wishlist slices.
- [plans/paint-mask-pass-2026-09-16.md](plans/paint-mask-pass-2026-09-16.md) —
  the cases for the deferred pass over the mask-editing surface (the draft
  bar, the mask inspector, the terms editor), grounded in the current headers.
- [plans/critique-handoff-2026-09-13.md](plans/critique-handoff-2026-09-13.md)
  — the 2026-09-13 critique's findings, the seven plans, their order, how each
  is verified, and the decisions closed with the user.
- [plans/critique-followup-handoff-2026-09-14.md](plans/critique-followup-handoff-2026-09-14.md)
  — where to pick up after Plans F to E: Plan G, the eight UI findings, the
  deferred items, the constants question, the closing critique re-run.
- [plans/critique-plan-g-shell-pose-2026-09-13.md](plans/critique-plan-g-shell-pose-2026-09-13.md)
  — the shell honours its whole pose, not `inflate` alone. Implemented
  (161f6b2); its in-game checkpoint has not run.
- [plans/expression-cleanup-implementation-handoff-2026-09-13.md](plans/expression-cleanup-implementation-handoff-2026-09-13.md)
  — the separate pass over `recipe/Expression.cpp`, and the bounds Plan F left
  to its owner.
- [plans/render-state-fix-plan-2026-09-12.md](plans/render-state-fix-plan-2026-09-12.md)
  — the staged plan for the rendering-state defects, with its progress notes.
- [plans/deletions.md](plans/deletions.md) — the per-module checklist of
  apparently dead code: what is certain, what needs a decision, and what only
  looks dead.

## Checkpoints and evidence (`checkpoints/`)

Dated records of one pass each. They describe the tree on their date.

- [checkpoints/crash-2026-09-11.md](checkpoints/crash-2026-09-11.md) — the
  mixed `TextureLab` layout crash and the compiler-launcher cache that caused
  it.
- [checkpoints/settings-publication-2026-09-12.md](checkpoints/settings-publication-2026-09-12.md)
  — publishing a complete settings value under a mutex.
- [checkpoints/recipe-application-service-2026-09-12.md](checkpoints/recipe-application-service-2026-09-12.md)
  — the shared service that coordinates accepted changes and actor rebuilds.
- [checkpoints/first-wave-2026-09-12.md](checkpoints/first-wave-2026-09-12.md)
  — the first paint and resource-ownership wave.
- [checkpoints/gpu-ownership-validation-2026-09-12.md](checkpoints/gpu-ownership-validation-2026-09-12.md)
  — the owning `GpuResources` candidate and what it validates.
- [checkpoints/render-state-diagnostic-checkpoint-2026-09-12.md](checkpoints/render-state-diagnostic-checkpoint-2026-09-12.md)
  — the stage-0 diagnostic build and the capture procedure for its traces.
- [checkpoints/texture-lease-checkpoint-2026-09-12.md](checkpoints/texture-lease-checkpoint-2026-09-12.md)
  — generated texture consumers retain the target, not only a presenter.
- [checkpoints/lint-cleanup-2026-09-13.md](checkpoints/lint-cleanup-2026-09-13.md)
  — the clang-tidy inventory at that build.
- [checkpoints/render-cleanup-2026-09-13.md](checkpoints/render-cleanup-2026-09-13.md)
  — the shell and compositor pass.
- [checkpoints/source-cleanup-2026-09-13.md](checkpoints/source-cleanup-2026-09-13.md)
  — the source preparation and inspection pass.
- [checkpoints/collection-validation-cleanup-2026-09-13.md](checkpoints/collection-validation-cleanup-2026-09-13.md)
  — actor collection and reference validation.
- [checkpoints/cleanup-checkpoint-2026-09-13.md](checkpoints/cleanup-checkpoint-2026-09-13.md)
  — the ownership contracts that pass settled, which `REFERENCE.md` cites.
- [checkpoints/expression-cleanup-2026-09-13.md](checkpoints/expression-cleanup-2026-09-13.md)
  — the implementation checkpoint for the expression handoff.
- [checkpoints/ui-assessment-2026-09-13.md](checkpoints/ui-assessment-2026-09-13.md)
  — the source review of the then-current UI against the design principles.
- [checkpoints/critique-plan-f-tests-and-bounds-2026-09-13.md](checkpoints/critique-plan-f-tests-and-bounds-2026-09-13.md)
  — the native test harness and the bounds checks (recommendations 9 and 10).
  Done.
- [checkpoints/critique-plan-a-error-contract-2026-09-13.md](checkpoints/critique-plan-a-error-contract-2026-09-13.md)
  — one error contract and the published `Reader` (1, 2). Done.
- [checkpoints/critique-plan-b-source-kinds-and-snapshot-2026-09-13.md](checkpoints/critique-plan-b-source-kinds-and-snapshot-2026-09-13.md)
  — source kinds enforced by the compiler, and an engine-free snapshot (3, 4).
  Done.
- [checkpoints/critique-plan-c-structure-2026-09-13.md](checkpoints/critique-plan-c-structure-2026-09-13.md)
  — layering and file structure (5, 7). Done.
- [checkpoints/critique-plan-d-naming-2026-09-13.md](checkpoints/critique-plan-d-naming-2026-09-13.md)
  — one name per concept (6). Done.
- [checkpoints/critique-plan-e-docs-2026-09-13.md](checkpoints/critique-plan-e-docs-2026-09-13.md)
  — documentation that matches the tree (8): this index, the README, the stale
  claims, the constants, and the comments out of the source. Done.
- [checkpoints/ui-v2-core-checkpoint-2026-09-13.md](checkpoints/ui-v2-core-checkpoint-2026-09-13.md)
  — the core editor integration, ahead of the complete-editor checkpoint.
- [checkpoints/ui-v2-framework-checkpoint-2026-09-13.md](checkpoints/ui-v2-framework-checkpoint-2026-09-13.md)
  — the early framework checkpoint, accepted in game on 2026-09-13.
- [checkpoints/ui-v2-layout-wishlist-2026-09-14.md](checkpoints/ui-v2-layout-wishlist-2026-09-14.md)
  — the in-game feedback wishlist from the complete-editor build; the
  implementation plan below sequenced it.
- [checkpoints/ui-v2-wishlist-implementation-plan-2026-09-14.md](checkpoints/ui-v2-wishlist-implementation-plan-2026-09-14.md)
  — the 13 slices that implemented the wishlist. All landed.
- [checkpoints/ui-standardization.md](checkpoints/ui-standardization.md) — the
  studio UI inconsistency cases and what each changed. All cases closed.
- [checkpoints/game-object-service-2026-09-17.md](checkpoints/game-object-service-2026-09-17.md)
  — the design and landed implementation of the game-object discovery service:
  the eight kinds, the request-driven keyed cache of immutable catalogs, and
  the fold-in of the editor-ID index and the actor-value path.
- [checkpoints/creation-seam-2026-09-19.md](checkpoints/creation-seam-2026-09-19.md)
  — the studio creation-flow findings and the case for the `studio/Create`
  seam that now exists.
- [checkpoints/regression-feedback-2026-09-12.md](checkpoints/regression-feedback-2026-09-12.md)
  — the user's in-game findings from the 2026-09-12 run, with the follow-up
  analysis.
- [checkpoints/regression-feedback-2026-09-12-current.log](checkpoints/regression-feedback-2026-09-12-current.log)
  — the game log that feedback was taken from.
- [checkpoints/regression-evidence/](checkpoints/regression-evidence) — the
  captured logs and JSONL traces those checkpoints cite: `2026-09-12/` for the
  rendering-state runs, `2026-09-13/expression-cleanup/` for that pass's build
  and tidy output.

The clang-tidy baseline the push gate compares against lives at
`tools/tidy-baseline.txt` (see `conventions.md`, Gates); it is gate data, not
a document.

## History (`history/`)

Superseded. Each names what replaced it in a status header at its top.

- [history/release-roadmap-2026-09-09.md](history/release-roadmap-2026-09-09.md)
  — the first release roadmap: the gates' reasoning and the 2026-09-09
  decisions. The 2026-09-20 roadmap under `plans/` replaced it.
- [history/buildup-plan.md](history/buildup-plan.md) — how the new tree was
  written by many agents at once: shape, fill, barrier, reduce.
- [history/wave3-seam.md](history/wave3-seam.md) — the reconciled `engine` to
  `render` interface the wave-3 fills followed.
- [history/wave3-followups.md](history/wave3-followups.md) — what the wave-3
  reduce stage accepted, deferred, and left as seam gaps.
- [history/wave4-seam.md](history/wave4-seam.md) — the frozen `menu/`
  interface.
- [history/wave4-status.md](history/wave4-status.md) — the wave-4 recovery
  checkpoints.
- [history/engine-ownership.md](history/engine-ownership.md) — which `.cpp`
  owned each `engine/` function at the shape step.
- [history/render-ownership.md](history/render-ownership.md) — the same for
  `render/`.
- [history/menu-ownership.md](history/menu-ownership.md) — the same for
  `menu/`.
- [history/planners-ownership.md](history/planners-ownership.md) — the same
  for `planners/`.
- [history/studio-ownership.md](history/studio-ownership.md) — the same for
  `studio/`.
- [history/ui-primitives-ownership.md](history/ui-primitives-ownership.md) —
  which `.cpp` owned each function of the UI-primitive headers under
  `studio/`. Predates the critique remediation and the standardization pass.
- [history/engine-types-survey-2026-09-12.md](history/engine-types-survey-2026-09-12.md)
  — the survey of engine-facing types that proposed the ownership work.
- [history/runtime-review-2026-09-12.md](history/runtime-review-2026-09-12.md)
  — the engine and render seam review that followed the layout crash.
- [history/render-state-investigation-2026-09-12.md](history/render-state-investigation-2026-09-12.md)
  — the source investigation behind the rendering-state fix plan.
- [history/paint-flow-review-2026-09-12.md](history/paint-flow-review-2026-09-12.md)
  — the review of the paint offer and term flow, and its coverage plan.
