Status: superseded 2026-10-01 by
[unattended regression runs](../plans/unattended-regression-2026-10-01.md),
which runs cases natively in the plugin. The case table, result vocabulary
and observation rules below still apply there. The Papyrus quest runner this
plan describes was deleted on 2026-10-01.

# Scripted in-game regression driver

## Purpose

Make the current manual acceptance run repeatable and easier to operate. Start
with the program/render pipeline; retain visual acceptance separately from
machine assertions. The existing demo armor and recipes provide a starting
scene, not complete coverage of the pipeline acceptance cases.

## Control model

Use a test-only quest script for game actions and sequencing, with a small
native bridge for plugin commands and observations. Console entry points start,
advance, inspect, or abort the quest runner. Exact console invocation and quest
packaging must be verified on the supported Skyrim runtime before publishing
copyable commands. Plain console batch files can prepare a scene, but should
not be the scheduler for asynchronous application and rendering.

Proposed runner operations:

- Run(caseName): start one named case, refusing overlapping runs.
- Next(): continue from a visual checkpoint.
- RecordVisual(result): record the human verdict separately from assertions.
- Status(): report the active case, pending condition, and elapsed time.
- Abort(): cancel pending work and restore test-owned changes.

Papyrus owns game actions such as equipping the fixture armor and changing
actor inputs. Plugin commands use existing Manager and RecipeEditor operations;
the bridge must queue them through the normal engine task path. It must not
access renderer objects from a Papyrus worker thread. Capture immutable numeric
observations at a defined frame boundary for the script to poll.

A command being accepted is not proof of completion. Each request needs an ID
and a terminal result. Observations must identify the run, game session, actor,
recipe, and render instance so old snapshots cannot satisfy a new request.
Loading a save cancels the run; do not resume pending commands across sessions.

## Case record

A case contains an ID, fixture requirements, ordered actions, expected
conditions, a gameplay-time deadline, visual checkpoints, and cleanup actions.
Actions and conditions should initially be a small fixed set implemented by
the runner, not a general-purpose command language.

Each recorded result contains the run ID, case ID, build identity, fixture
identity, measured values, assertion outcome, visual outcome, and reason.
Assertion outcomes are PASS, FAIL, BLOCKED, or ABORTED. Visual outcomes are
PENDING, PASS, FAIL, or NOT_REQUIRED. Missing prerequisites are BLOCKED; a
missing terminal result after the deadline is FAIL. Neither counts as a pass.

Wait for the relevant application to render and for a specified number of
subsequent rendered frames. Pause progression and its deadline while the game
is paused. A fixed wall-clock sleep cannot establish that the GPU pipeline ran.
Performance measurements need a separate mode with bounded diagnostics and
no extra diagnostic readbacks in the timed interval.

## First acceptance cases

| Case | Action | Machine assertion | Visual checkpoint |
| --- | --- | --- | --- |
| Apply and retire | Equip, apply, retire, reapply | Relevant application completes; retirement is observed | Baseline returns; effect reappears |
| Changing mean | Drive a fixture mask through two known field values without rebuilding its instance | Reduction result changes and its dependent lookup rebuilds | Output follows the expected values |
| Unchanged mean | Change a field while preserving its mean | Field/reduction execute; mean output version and dependent lookup execution count remain unchanged | Expected spatial change remains visible |
| Hidden invalid input | Hide a fixture contribution with an unavailable input, then show it | Hidden branch is not evaluated; visible failure is recorded | Eligible preceding stack remains visible |
| Shared ripples | Trigger two fixture ripples on one geometry | Shared prerequisites have the expected execution counts | Both ripples appear correctly |
| Retirement with preview | Hold a fixture preview, retire geometry, release preview | Render ownership ends; preview lease survives until release | Retained preview remains usable |

Use exactly representable inputs for the unchanged-mean case; do not rely on
approximate numerical equality to test exact cache behavior. Collect step
execution counts as well as output versions: an unchanged output alone cannot
prove that a lookup was reused. These observations are not currently exposed
through a script interface and are part of the proposed diagnostic work.

Follow with tint, ID bake, and reoriented-normal cases; multiple actors and
resolutions; equipment and cell changes; and save/load cancellation. Actual
hit and animation-event delivery remain separate from injected-event tests.

## Proposed file clusters and engineering purpose

- `tests/in-game/`: quest script source, fixture recipes, expected results,
  and run instructions. Makes inputs and outcomes reviewable together.
- `src/engine/Regression*`: optional bridge and run/session ownership. Reuses
  normal command paths and cancels work on load or abort.
- `src/render/RenderInstance.*`, `src/planners/RenderExecution.h`: bounded
  observations of execution and retained outputs needed for cache assertions.
  No raw renderer pointers cross the bridge.
- `src/diagnostics/` and `tools/trace-report.py`: case markers and result
  reporting tied to the existing trace session and candidate identity.
- Test-plugin generation and build tooling: package the quest and compiled
  script separately from the normal mod. Verify Papyrus compiler availability,
  quest attachment, and console invocation before committing to this route.

## First slice

The lifecycle runner implemented Run, Next,
RecordVisual, Status, and Abort. It polls new actor application attempts and
checks for a rendered output on the demo armor. Retirement observes removal
of the actor runtime state. This first slice has a bounded Papyrus polling
budget, not the proposed frame-count and precise gameplay-deadline mechanism.
Cache counters, immutable diagnostic snapshots, structured aggregate reports,
and preview leases remain future work.

## Implementation order

1. Settle the runner interface, first cases, and permitted test observations.
2. Prove console-to-quest invocation and one equip/apply/retire case on the
   supported runtime. Provide manual checkpoint operation if no bridge exists.
3. Add the command completion and diagnostic bridge, then cache assertions.
4. Add cancellation, cleanup, and structured reports before longer suites.
5. Expand fixtures and performance coverage after the first game run.

Use a dedicated test save/profile. Resolve fixture forms by plugin and local
identity or quest properties, not a hard-coded load-order prefix. Restore only
changes owned by the runner; do not save recipe edits or overwrite user files.
The regression driver does not replace native tests or visual acceptance.
