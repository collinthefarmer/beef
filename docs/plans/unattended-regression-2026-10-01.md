Status: in progress. Stage 1 passed in game on 2026-10-01 (run
20261001T022009, lifecycle PASS, launch to quit in 80 seconds). Stage 2 is
done. Stage 3 passed in game on 2026-10-01 (run 20261001T041756: all five
cases, `unload` three times, launch to quit in about three minutes). Its runs
found one plugin defect, an NPC armor model that attaches after the equip
refresh, now fixed. Stage 4a passed in game on 2026-10-01 (all five load
cases, the edit, gesture and paint cases twice); its runs found a second
defect, a gesture cancelled by a load recorded as cancelled by the user, now
fixed. Stage 4b passed in game on 2026-10-01 (`studio-save + studio-reload`,
two launches). Stage 5 is implemented and ran in game on 2026-10-01; its
budgets fail on two measured findings, recorded under stage 5 below. Stage 6
is open. Supersedes the control
model of [the regression driver plan](../history/in-game-regression-driver.md); that
plan's case table, result vocabulary and observation rules carry over.

# Unattended in-game regression runs

## Purpose

One host command starts Skyrim, loads a known good save, runs a named suite
of cases, writes the results, quits the game and prints a report. No person
operates the game during the run. Cases that need a visual verdict save
evidence for review after the run. They do not stop the run.

## Decisions

| Decision | Reason |
| --- | --- |
| The plugin runs the cases natively; the Papyrus quest runner was deleted after stage 1 passed in game. | A native runner advances on the plugin's frame hook, so deadlines and waits count rendered frames. It needs no script compiler (Caprica is no longer installed), it bakes no script state into the save, and it reads plugin state without a bridge. |
| The case sequencing is an engine-free module in `src/regression/`. | `ctest --preset native` tests it with scripted observations. The engine side gathers observations and executes commands, and nothing else. |
| The **run file** lives in the SKSE log directory, beside the trace. | The host writes it without touching the MO2 mods directory. The plugin reads it with the path it already uses for the log. |
| The plugin deletes the run file when it reads it, and refuses a run file past its `notAfter` time. | A later ordinary game start never repeats a run. A host that stopped before cleanup leaves a file that expires. |
| The plugin loads the save from the main menu through `BGSSaveLoadManager::Load(name, false)`. | `false` skips the missing-content check, so no dialog waits for input. |
| The plugin quits through `RE::Main::quitGame` after the results are flushed. | The host detects the end from the process exit and the final result line. |
| The runner's state is process memory, never save data. | A case can load a save mid-run and continue. |
| The host launches the game through MO2's `moshortcut://` command line. | MO2 keeps its virtual file system and profile; the profile and executable title come from `local.env`. |

## Files and formats

**Run file** `BetterEnchantmentEffects-regression-run.json` in the SKSE log
directory:

| Field | Type | Meaning |
| --- | --- | --- |
| `format` | integer | `1` |
| `run` | string, 1–64 characters `[A-Za-z0-9_-]` | Run identifier; names the results file |
| `save` | string, 1–128 characters, no path separators | Save name without extension, in the profile's save folder |
| `suite` | array of 1–64 case names | Cases in run order |
| `notAfter` | integer | Unix seconds; a later start refuses the file |

**Results file** `BetterEnchantmentEffects-regression-<run>.jsonl` in the same
directory. One JSON object per line, flushed per line:

| `kind` | Fields |
| --- | --- |
| `start` | `run`, `build`, `source`, `trace` (trace file name) |
| `step` | `case`, `step` (index), `action`, `outcome`, `frames`, `reason` |
| `case` | `case`, `outcome` |
| `end` | `outcome`, `reason` |

Outcomes are `PASS`, `FAIL`, `BLOCKED` and `ABORTED`, as in the driver plan.
A case's outcome is the first non-`PASS` step outcome, else `PASS`. The run's
outcome is the first non-`PASS` case outcome. A run that cannot start (bad
run file, failed load) writes `start` and an `end` with `BLOCKED` when it
knows the run identifier, and logs the reason in the plugin log.

The plugin also emits `regression.step` and `regression.case` trace
commands, so `tools/trace-report.py` can split a trace by case.

## Run lifecycle

1. `kDataLoaded`: read, parse and delete the run file. When a valid run is
   present, register a menu sink for the main menu.
2. Main menu opens: queue the save load as an SKSE task.
3. `kPostLoadGame` with success: the runner arms. A failed load ends the run
   as `BLOCKED` and quits.
4. Each player update (`Manager::OnFrame`): build an **observation**, advance
   the runner, execute its command, append its result lines.
5. The runner settles for 120 player updates with the player's 3D loaded,
   then starts the first case. Player updates stop while a menu pauses the
   game, so a menu delays the settle.
6. Every case ends with its cleanup steps, which run after a failure too.
7. After the last case: write `end`, wait 10 frames, set `quitGame`.

## Engine-free core (`src/regression/`)

Data, one header each:

| Header | Data |
| --- | --- |
| `Words.h` | The enums `Outcome`, `Role`, `Item`, `Camera`, `QueuedWork`, `Session`, `TrackedWork` and `WorkOutcome`, and one `Named` table of words for each. |
| `RunFile.h` | `RunFile`: the run identifier, the save and the suite. `ParseRunFile(text, now)` returns it or an error string. |
| `Steps.h` | `Step`: a variant of one record per step (`Equip`, `AwaitRendered`, `LoadDuring`, `HoldWindow` and the rest). `Case`: a name, its body and its cleanup. `Catalog` and `FindCase` read the fixed catalog in `Catalog.cpp`. |
| `Observation.h` | `Observation`: what the engine reports each frame. It holds `ActorFacts` for each role, the state of the current request, `Activity` for the studio work, `CrowdFacts`, `RecipeFacts` for the scratch recipe, the camera, the load count and the clock. |
| `Commands.h` | `Command`: a variant of one record per engine action (`SpawnActor`, `AddAndEquip`, `SubmitApply`, `StartSave`, `LoadSaveWith`, `Quit` and the rest). |
| `Run.h` | `ResultLine`, `RunChanges`, the phases `Settling`, `Running`, `Ending` and `Finished`, and `RunState`. |
| `StepRules.h` | `StepContext` and `StepMove`: the input and output of one step rule. |

`RunChanges` records what the run changed in the world: the actors it
spawned, the items it added, whether the player is away and the crowd size.
`Advance` derives it from each command it issues (`AfterCommand`) and resets
it when a load happens. Cleanup steps read it, so a cleanup step does nothing
when the run made no matching change.

Each step has three rules: `Start` on its first frame, `Check` on each later
frame, and `Label` for its result line (`StepLabels.cpp`). An `Expect…` step
checks a state that earlier steps produced and waits only for the observation
to catch up. An `Await…` step waits for work in progress, with a deadline
sized to that work.

Function: `Advance(RunState, Observation) -> Advanced{RunState, optional
Command, vector ResultLine}`. Pure; one call per frame.

## Host command

`tools/regression-run.py [--save NAME] [--timeout SECONDS] CASE...`

1. Read `MO2_EXE`, `MO2_PROFILE`, `MO2_LAUNCH` and `SKSE_LOG_DIR` from the
   environment or `local.env`; stop with a message naming a missing one.
2. Refuse to start while `SkyrimSE.exe` runs, or when the profile has its own
   save folder and the save is not in it.
3. Write the run file with a new run identifier and `notAfter` ten minutes
   ahead.
4. Launch `ModOrganizer.exe -p <profile> moshortcut://:<launch>`.
5. Poll the results file and the game process. The run ends at the `end`
   line, at game exit without an `end` line (reported as `CRASHED`), or at
   the timeout (reported as `TIMEOUT`). A run file still unread after five
   minutes, or read without a results file after 30 seconds, ends the run as
   `BLOCKED`; the host then deletes its own unread run file.
6. Print one row per case and step, and the trace file name. Exit 0 only
   when the run's outcome is `PASS`.

`+` separates launches: each segment is its own run and report, and the host
waits 15 seconds after the game quits before the next launch. If the run file
is still unread 60 seconds after a launch, the host sends the launch request
once more. `--manual` writes the run file and waits without launching MO2. `--save`
defaults to `BEEFRegression`. Create it once in game: `coc QASmoke`, step
onto open floor, third person, body slot empty, no demo cuirass carried,
then `save BEEFRegression`. `QASmoke` has no NPCs, weather or changing light,
so every run starts from the same scene. Make the save again whenever a
plugin it was made with is removed; the game refuses to load it otherwise.

## Stages

1. **Run loop and lifecycle case.** Everything above, with one case,
   `lifecycle`: equip the demo cuirass, apply, retire, apply again, remove.
   It replaces the Papyrus runner's machine assertions. Checkpoint: the user
   creates the save and runs `tools/regression-run.py lifecycle`.
2. **Delete the Papyrus runner.** Remove the quest scripts, the quest plugin,
   `tools/regression-compile.py`, `tools/regression-package.py`,
   `install-regression.sh`, the Papyrus bridge and `tests/in-game/`
   documents that describe them.
3. **Lifetime and isolation cases** (alpha plan section 4). Steps take a
   **role**: the player, a spawned **wearer** or a spawned **control**. The
   cases:

   | Case | What it checks |
   | --- | --- |
   | `equip-cycle` | Five unequip and re-equip cycles; each unequip returns the player to baseline and each equip renders anew |
   | `camera` | First- and third-person switches keep the effect, and unequip still returns to baseline |
   | `isolation` | Player and wearer both render the fixture; retiring and unequipping the wearer leaves the player's effect; the control in a plain cuirass never gains plugin state |
   | `unload` | Disabling the wearer retires its state and enabling renders it again; leaving for the exterior `Riverwood` retires it and returning renders it again |

   The spawned actors are mannequins: an NPC with a default outfit re-equips
   it over the fixture after an equip, an enable or a reload. After an enable
   and after the return trip, the case equips the fixture again before it
   waits for the render.

   Every case shares one cleanup: return to the start, third person, delete
   the spawned actors, remove the player's fixture, restore the solo. Each
   cleanup step does nothing when the run made no matching change.

4. **Loads during work and studio round trips.**

   4a, loads during work. The runner survives a load it starts itself: the
   case continues after the load, and the run forgets the spawned actors and
   the marker, which the save does not hold.

   | Case | What is in flight at the load | What must hold after it |
   | --- | --- | --- |
   | `load-idle` | Nothing | The player is at baseline and nothing is pending |
   | `load-apply` | An apply request, queued just after the load | The request reports `ABORTED`; baseline; nothing pending |
   | `load-edit` | A recipe edit, queued just after the load | The edit settled, applied or cancelled, never pending |
   | `load-gesture` | A slider gesture, active before the load | The gesture was cancelled with the load as its reason |
   | `load-paint` | A paint preview, active before the load | The paint session ended; baseline; nothing pending |

   In game, an edit requested in the same frame as a load always lands before
   the load starts. A stale edit that would land after the load began is
   refused by the session generation; only the native tests cover that path.

   4b, studio round trips, run as `tools/regression-run.py studio-save +
   studio-reload`. `studio-save` deletes any leftover `regression-scratch`,
   duplicates `arcane-circuit` to it, sets its first layer opacity to 0.25,
   saves it, and renders it. `studio-reload`, in a fresh game, expects
   `regression-scratch` loaded from disk, clean, with opacity 0.25, renders
   it, and deletes it; its cleanup deletes it after a failure too. The steps
   touch only `regression-scratch`, the one file a run writes under the mods
   directory.

5. **Soak and budgets.** `soak` (steady hold 2 minutes) and `soak-hour`
   (60 minutes) solo Arcane Circuit, then hold named windows: `baseline` 30
   s with no crowd; `spawn-crowd 12` (mannequins in a ring, the fixture
   worn, removal prevented) and `await-crowd-rendered`, which form the
   `burst` window; `settle` 30 s; `steady`; `despawn-crowd`; `recovery`
   30 s. A hold runs on wall-clock time; it fails only if the clock stalls.
   The shared cleanup despawns the crowd.

   After the run the host splits the trace into these windows by the step
   events, sums each window's heartbeats (fps, plugin frame cost, worst
   frame, tick, refresh and readback, render targets and bytes), prints the
   table, and checks `tests/regression/budgets.json`
   (`tools/regression_budgets.py`). A budget names a window and a limit:
   `<measure>_max`, `<measure>_ratio_min` against baseline, or
   `<measure>_over_baseline_max`. A limit that names no known measure, or a
   bound that is not a number, fails the run.

   | Window | Budget | Run 20261001T063900 |
   | --- | --- | --- |
   | `settle` | worst frame 0.5 s, worst readback 100 ms | 4.8 s and 1.7 s: fails |
   | `steady` | fps half of baseline, plugin 4 ms per frame, worst tick and readback 50 ms | 61 %, 2.1 ms, 9 ms, 0: passes |
   | `recovery` | fps 90 % of baseline, targets and bytes above baseline within the idle pool (16, 64 MiB) | 12 targets, 95 MB: fails |

   Findings: the first application of 12 actors at once freezes the game
   for about 13 seconds, with 893 render targets (14 GB accounted) at the
   peak and 136 readbacks; it settles to 301 targets (4.2 GB). After the
   crowd leaves, 95 MB stays allocated, about 26 MB more than the idle
   target pool may hold.

   Fixed on 2026-10-01: a tick admits one actor's first render, and a render
   step's idle output is released after 500 ms instead of 30 of its
   instance's ticks, which stretched to seconds while the burst slowed the
   game. The next soak: burst 2 s at 80 fps, worst frame 105 ms, worst
   readback 1.4 ms, peak 8.6 GB; `settle` passes. Then static render steps
   were shared across actors by content: peak 2.2 GB, steady 136 targets and
   1.4 GB, worst settle frame 24 ms, steady fps 63 % of baseline. Open: the
   95 MB left after recovery.

6. **Evidence for visual verdicts.** Screenshots at named checkpoints, and
   stack-output hashes with the clock frozen, compared with recorded
   goldens.
