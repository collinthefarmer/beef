Status: in progress. Stage 1 passed in game on 2026-10-01 (run
20261001T022009, lifecycle PASS, launch to quit in 80 seconds). Stage 2 is
done. Stage 3 passed in game on 2026-10-01 (run 20261001T041756: all five
cases, `unload` three times, launch to quit in about three minutes). Its runs
found one plugin defect, an NPC armor model that attaches after the equip
refresh, now fixed. Supersedes the control
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
5. The runner settles for 120 frames after the player's 3D is loaded and no
   menu is open, then starts the first case.
6. Every case ends with its cleanup steps, which run after a failure too.
7. After the last case: write `end`, wait 10 frames, set `quitGame`.

## Engine-free core (`src/regression/`)

Data:

- `RunRequest`: the parsed run file. `ParseRunRequest(text, now)` returns it
  or an error string.
- `Step`: a variant of `Settle{frames}`, `Solo{recipe}`, `RestoreView`,
  `EquipFixture`, `RemoveFixture`, `Apply` and `Retire`. A step that waits
  on the game has a frame deadline: 600 frames for the fixture, 1800 for an
  apply or retire request.
- `Case`: a name, its steps and its cleanup steps. `FindCase(name)` looks
  it up in a fixed catalog.
- `Observation`: what the engine reports each frame: player ready, fixture
  equipped, fixture carried, and the state of the current apply or retire
  request (`none`, `waiting`, or an outcome).
- `Command`: a variant of `SoloRecipe`, `RestoreSolo`, `AddAndEquipFixture`,
  `UnequipAndRemoveFixture`, `SubmitApply`, `SubmitRetire`, `AbortRequest`
  and `Quit`.
- `RunState`: the phase (`Settling`, `Running`, `Ending`, `Finished`), the
  cursor inside `Running` (case, body or cleanup, step, frames spent), the
  outcomes so far, and whether the run added the fixture. Cleanup removes
  only a fixture the run added.

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

`--manual` writes the run file and waits without launching MO2. `--save`
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

4. **Loads during work and studio round trips.** A case loads the save while
   an apply or a paint preview is pending, then asserts that nothing from the
   old session applied and nothing is pending. Studio cases send menu
   intents through the editor, save, and the host compares the recipe files
   after a second launch.
5. **Soak and budgets.** A `soak` case spawns a crowd in fixture armor and
   holds for a set time. The host checks the trace against a budget file
   through `tools/trace-report.py`.
6. **Evidence for visual verdicts.** Screenshots at named checkpoints, and
   stack-output hashes with the clock frozen, compared with recorded
   goldens.
