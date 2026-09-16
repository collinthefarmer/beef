# The documents under `docs/`

Every file under `docs/` and `docs/wip/` appears once below. `REQUIREMENTS.md`,
`REFERENCE.md` and `CLAUDE.md` at the repository root govern; nothing here
overrides them. A document in **History** describes a tree that has since been
renamed or restructured and carries a status header saying so; read it for the
decision it recorded, not for the names it uses.

- [This index](README.md) — what each document answers, and whether it is current.

## Canon

Current, and a new module is expected to follow them.

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

## Active plans

Open work. Each carries its own status line.

- [ui-v2-proposal.md](ui-v2-proposal.md) — the frozen UI v2 design baseline.
- [ui-v2-implementation-plan.md](ui-v2-implementation-plan.md) — the slices that
  implement that baseline, and which seam each slice owns.
- [ui-assessment-2026-09-13.md](ui-assessment-2026-09-13.md) — the source review
  of the existing UI against the design principles.
- [wip/ui-v2-framework-checkpoint-2026-09-13.md](wip/ui-v2-framework-checkpoint-2026-09-13.md)
  — the early framework checkpoint, accepted in game on 2026-09-13.
- [wip/ui-primitives-ownership.md](wip/ui-primitives-ownership.md) — which
  `.cpp` owns each function of the UI-primitive headers under `studio/`.
- [wip/critique-handoff-2026-09-13.md](wip/critique-handoff-2026-09-13.md) — the
- `wip/critique-followup-handoff-2026-09-14.md` — where to pick up after Plans F to E: Plan G, the eight UI findings, the deferred items, the constants question, the closing critique re-run.
  2026-09-13 critique's findings, the seven plans, their order, how each is
  verified, and the decisions closed with the user.
- [wip/critique-plan-f-tests-and-bounds-2026-09-13.md](wip/critique-plan-f-tests-and-bounds-2026-09-13.md)
  — the native test harness and the bounds checks (recommendations 9 and 10).
- [wip/critique-plan-a-error-contract-2026-09-13.md](wip/critique-plan-a-error-contract-2026-09-13.md)
  — one error contract and the published `Reader` (1, 2).
- [wip/critique-plan-b-source-kinds-and-snapshot-2026-09-13.md](wip/critique-plan-b-source-kinds-and-snapshot-2026-09-13.md)
  — source kinds enforced by the compiler, and an engine-free snapshot (3, 4).
- [wip/critique-plan-c-structure-2026-09-13.md](wip/critique-plan-c-structure-2026-09-13.md)
  — layering and file structure (5, 7).
- [wip/critique-plan-d-naming-2026-09-13.md](wip/critique-plan-d-naming-2026-09-13.md)
  — one name per concept (6).
- [wip/critique-plan-e-docs-2026-09-13.md](wip/critique-plan-e-docs-2026-09-13.md)
  — documentation that matches the tree (8): this index, the README, the stale
  claims, the constants, and the comments out of the source.
- [wip/critique-plan-g-shell-pose-2026-09-13.md](wip/critique-plan-g-shell-pose-2026-09-13.md)
  — the shell honours its whole pose, not `inflate` alone.
- [wip/expression-cleanup-implementation-handoff-2026-09-13.md](wip/expression-cleanup-implementation-handoff-2026-09-13.md)
  — the separate pass over `recipe/Expression.cpp`, and the bounds Plan F left
  to its owner.
- [wip/render-state-fix-plan-2026-09-12.md](wip/render-state-fix-plan-2026-09-12.md)
  — the staged plan for the rendering-state defects, with its progress notes.
- [wip/deletions.md](wip/deletions.md) — the per-module checklist of apparently
  dead code: what is certain, what needs a decision, and what only looks dead.

## Checkpoints and evidence

Dated records of one pass each. They describe the tree on their date.

- [wip/crash-2026-09-11.md](wip/crash-2026-09-11.md) — the mixed `TextureLab`
  layout crash and the compiler-launcher cache that caused it.
- [wip/settings-publication-2026-09-12.md](wip/settings-publication-2026-09-12.md)
  — publishing a complete settings value under a mutex.
- [wip/recipe-application-service-2026-09-12.md](wip/recipe-application-service-2026-09-12.md)
  — the shared service that coordinates accepted changes and actor rebuilds.
- [wip/first-wave-2026-09-12.md](wip/first-wave-2026-09-12.md) — the first paint
  and resource-ownership wave.
- [wip/gpu-ownership-validation-2026-09-12.md](wip/gpu-ownership-validation-2026-09-12.md)
  — the owning `GpuResources` candidate and what it validates.
- [wip/render-state-diagnostic-checkpoint-2026-09-12.md](wip/render-state-diagnostic-checkpoint-2026-09-12.md)
  — the stage-0 diagnostic build and the capture procedure for its traces.
- [wip/texture-lease-checkpoint-2026-09-12.md](wip/texture-lease-checkpoint-2026-09-12.md)
  — generated texture consumers retain the target, not only a presenter.
- [wip/lint-cleanup-2026-09-13.md](wip/lint-cleanup-2026-09-13.md) — the
  clang-tidy inventory at that build.
- [wip/render-cleanup-2026-09-13.md](wip/render-cleanup-2026-09-13.md) — the
  shell and compositor pass.
- [wip/source-cleanup-2026-09-13.md](wip/source-cleanup-2026-09-13.md) — the
  source preparation and inspection pass.
- [wip/collection-validation-cleanup-2026-09-13.md](wip/collection-validation-cleanup-2026-09-13.md)
  — actor collection and reference validation.
- [wip/cleanup-checkpoint-2026-09-13.md](wip/cleanup-checkpoint-2026-09-13.md) —
  the ownership contracts that pass settled, which `REFERENCE.md` cites.
- [wip/expression-cleanup-2026-09-13.md](wip/expression-cleanup-2026-09-13.md) —
  the implementation checkpoint for the expression handoff.
- [wip/ui-v2-core-checkpoint-2026-09-13.md](wip/ui-v2-core-checkpoint-2026-09-13.md)
  — the core editor integration, ahead of the complete-editor checkpoint.
- [regression-feedback-2026-09-12.md](regression-feedback-2026-09-12.md) — the
  user's in-game findings from the 2026-09-12 run, with the follow-up analysis.
- [regression-feedback-2026-09-12-current.log](regression-feedback-2026-09-12-current.log)
  — the game log that feedback was taken from.
- [regression-evidence/](regression-evidence) — the captured logs and JSONL
  traces those checkpoints cite: `2026-09-12/` for the rendering-state runs,
  `2026-09-13/expression-cleanup/` for that pass's build and tidy output.
- [wip/tidy-baseline.txt](wip/tidy-baseline.txt) — the clang-tidy findings the
  push gate compares against. Regenerated only for an intended change.

## History

Superseded. Each names what replaced it in a status header at its top.

- [buildup-plan.md](buildup-plan.md) — how the new tree was written by many
  agents at once: shape, fill, barrier, reduce.
- [wip/wave3-seam.md](wip/wave3-seam.md) — the reconciled `engine` to `render`
  interface the wave-3 fills followed.
- [wip/wave3-followups.md](wip/wave3-followups.md) — what the wave-3 reduce
  stage accepted, deferred, and left as seam gaps.
- [wip/wave4-seam.md](wip/wave4-seam.md) — the frozen `menu/` interface.
- [wip/wave4-status.md](wip/wave4-status.md) — the wave-4 recovery checkpoints.
- [wip/engine-ownership.md](wip/engine-ownership.md) — which `.cpp` owned each
  `engine/` function at the shape step.
- [wip/render-ownership.md](wip/render-ownership.md) — the same for `render/`.
- [wip/menu-ownership.md](wip/menu-ownership.md) — the same for `menu/`.
- [wip/planners-ownership.md](wip/planners-ownership.md) — the same for
  `planners/`.
- [wip/studio-ownership.md](wip/studio-ownership.md) — the same for `studio/`.
- [wip/engine-types-survey-2026-09-12.md](wip/engine-types-survey-2026-09-12.md)
  — the survey of engine-facing types that proposed the ownership work.
- [wip/runtime-review-2026-09-12.md](wip/runtime-review-2026-09-12.md) — the
  engine and render seam review that followed the layout crash.
- [wip/render-state-investigation-2026-09-12.md](wip/render-state-investigation-2026-09-12.md)
  — the source investigation behind the rendering-state fix plan.
- [wip/paint-flow-review-2026-09-12.md](wip/paint-flow-review-2026-09-12.md) —
  the review of the paint offer and term flow, and its coverage plan.
