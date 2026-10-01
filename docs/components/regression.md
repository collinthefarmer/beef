# regression/

The engine-free core of the **unattended regression run**: the run file, the
case catalog, and the pure state machine that turns one frame's observation
into at most one command and the result lines. It is engine-free and depends
only on `Core.h` and `recipe/` (for the JSON `Reader` that parses the run
file), per its `ALLOWS` row in `tools/gate.py`, `'regression': ('Core.h',
'recipe', 'regression')`. Only `engine/` includes it.

## What it owns

A run is a list of **cases**. A case is a list of **steps** and a list of
cleanup steps. Each frame, the engine side (`engine/RegressionRun.cpp` and
`engine/RegressionWorld.cpp`) observes the game and calls `Advance`.
`Advance` returns the next `RunState`, at most one `Command` for the engine
to carry out, and the `ResultLine`s to write. The core never touches the
game, the clock or a file, so `tests/regression/run_tests.cpp` drives every
case of the catalog against a simulated world in `ctest --preset native`.

It does not own the engine actions, the request store or the host. The
engine side owns the actions (`Execute`, `Observe`) and the request store
(`engine/RegressionRequests.h`). `tools/regression-run.py` launches the game
and reads the results, and `tools/regression_budgets.py` checks the soak
windows against `tests/regression/budgets.json`.

## Data

### Words (`Words.h`)

The enums name the fixed things a step can refer to. Each enum has a
`Named` table that gives its word, and a `static_assert(Complete(...))` keeps
the table in step with the enum. `IndexOf(Role)` and `IndexOf(Item)` turn a
role or an item into an array index, clamped to the array's size.

| Enum | Values | Use |
|---|---|---|
| `Outcome` | `kPass`, `kFail`, `kBlocked`, `kAborted` | The verdict of a step, a case, a run or a request. `OutcomeName` gives `PASS`, `FAIL`, `BLOCKED`, `ABORTED`. |
| `Role` | `kPlayer`, `kWearer`, `kControl` | The actor a step acts on. The wearer and the control are spawned mannequins. |
| `Item` | `kFixture`, `kPlainCuirass` | The demo cuirass and the control's plain cuirass. |
| `Camera` | `kFirstPerson`, `kThirdPerson` | The camera a `SetCamera` step asks for. |
| `QueuedWork` | `kNothing`, `kApply`, `kEdit` | The work a `LoadDuring` step queues in the same frame as the load. |
| `Session` | `kGesture`, `kPaint` | The studio session a `BeginSession` step opens. |
| `TrackedWork` | `kEdit`, `kGesture` | The started work whose outcome an `ExpectCancelled` or `ExpectSettled` step reads. |
| `WorkOutcome` | `kNone`, `kPending`, `kApplied`, `kCancelledByLoad`, `kFailed` | What became of started studio work. `kWorkOutcomePhrases` gives the phrase a failure reason uses. |

### The run file (`RunFile.h`)

The host writes the run file into the SKSE log directory, and the plugin
reads it once at startup. `ParseRunFile` turns its text into a `RunFile` or
an error string. It refuses text over 64 KiB, nesting deeper than four
levels, a run identifier or save name with a character it may not hold, an
unknown case, more than `kMaxSuiteCases` (64) cases, and a file past its
`notAfter` time.

| Type | Description |
|---|---|
| `RunFile{run, save, suite}` | The run identifier, the save to load, and the cases in run order. |

### Steps and cases (`Steps.h`)

A `Step` is a variant with one record per kind of step. `Catalog.cpp` builds
each case as a `constexpr` array of steps, and `FindCase` looks a case up by
name. `StepLabel` gives a step's text for its result line.

| Steps | What they do |
|---|---|
| `Solo`, `RestoreSolo` | Solo the recipe under test in the studio, and restore the isolation from before. |
| `Spawn`, `Despawn`, `Disable`, `Enable` | Change whether a spawned role is in the world. |
| `Equip`, `Unequip`, `Remove` | Add and equip, unequip, or remove an item on a role. |
| `Apply`, `Retire` | Submit an apply or retire request for a role. |
| `AwaitRendered`, `AwaitRetired`, `AwaitBaseline` | Wait until a role renders the fixture anew, holds no live state, or shows no **residue**. |
| `ExpectEffect`, `HoldUntouched` | Check that a role keeps its effect, or that a role gains no plugin state for a number of frames. |
| `LeaveStart`, `ReturnToStart`, `SetCamera` | Travel to the exterior and back, or switch the player's camera. |
| `LoadDuring`, `ExpectAborted`, `ExpectCancelled`, `ExpectSettled`, `AwaitIdle` | Load the save with work queued, then check what the load did to it. |
| `BeginSession`, `AwaitSessionActive` | Open a gesture or paint session and wait until the editor reports it active. |
| `DeleteScratch`, `DuplicateToScratch`, `SetScratchOpacity`, `SaveScratch`, `ExpectScratch` | Studio round trips on the scratch recipe, `kScratchRecipe` (`regression-scratch`). |
| `SpawnCrowd`, `AwaitCrowdRendered`, `DespawnCrowd` | Spawn and remove up to `kMaxCrowd` (64) crowd actors. |
| `HoldWindow`, `BeginWindow`, `EndWindow` | Mark a measured window of the soak. A hold runs on wall-clock time. |
| `WaitFrames` | Wait a number of frames. |

An `Expect…` step checks a state that the steps before it produced. It waits
only for the observation to catch up (`kExpectDeadlineFrames`). An `Await…`
step waits for work in progress, with a deadline sized to that work. The
deadlines are constants in `Steps.cpp`.

| Type | Description |
|---|---|
| `Case{name, body, cleanup}` | One case. Cleanup always runs; a failed body step skips the rest of the body. |
| `CaseList` | The cases of one run, as references into the catalog. |

The catalog holds `lifecycle`, `equip-cycle`, `camera`, `isolation`,
`unload`, `load-idle`, `load-apply`, `load-edit`, `load-gesture`,
`load-paint`, `studio-save`, `studio-reload`, `soak` and `soak-hour`.

### The observation (`Observation.h`)

`Observe` in `engine/RegressionWorld.cpp` fills one `Observation` per frame.
The step rules read only this record, so a step's verdict depends on what
the game showed, never on what the run asked for.

| Type | Description |
|---|---|
| `Observation` | One frame: `ActorFacts` per role, `Activity`, `RecipeFacts` for the scratch recipe, `CrowdFacts`, the clock (`nowMs`), the load count, whether each item's form is loaded, whether NPC effects are on, the camera, whether the player is away from the start, and the current request's `RequestState`. |
| `ActorFacts` | Whether the actor is present and wears body armor, `ItemFacts` per item, whether the manager holds live state, the newest application revision that rendered the fixture (`renderedAttempt`), the residue count, and the latest application text. |
| `ItemFacts` | Whether the item is equipped and carried. |
| `Activity` | Pending applications, an open paint session or gesture, a pending file operation, the `WorkOutcome` of the started edit, gesture and file operation, and a detail text for failure reasons. |
| `CrowdFacts` | How many crowd actors are present and how many render. |
| `RecipeFacts` | Whether a recipe is loaded and unsaved, and its first layer's opacity when that is a number. |
| `RequestState` | `NoRequest`, `RequestPending`, or the request's `Outcome`. |

### Commands (`Commands.h`)

A `Command` is a variant with one record per engine action. `Execute` in
`engine/RegressionWorld.cpp` carries out one per frame through its
`Perform` overloads.

| Commands | Engine action |
|---|---|
| `SoloRecipe`, `EndSolo` | Solo a recipe in the studio, and end the solo. |
| `SpawnActor`, `DespawnActor`, `DisableActor`, `EnableActor` | Place, delete, disable or enable a role's mannequin. |
| `AddAndEquip`, `EquipCarried`, `UnequipArmor`, `RemoveArmor` | Change an item on a role. |
| `SubmitApply`, `SubmitRetire`, `AbortRequest` | Start or abort the one regression request. |
| `TravelFromStart`, `TravelToStart`, `SwitchCamera` | Move the player to the exterior and back, or switch the camera. |
| `StartDuplicate`, `StartOpacityEdit`, `StartSave`, `StartDelete`, `OpenSession` | Start the same studio work the menu starts. |
| `SpawnCrowdActors`, `DespawnCrowdActors` | Place or delete the crowd. |
| `LoadSaveWith` | Queue work, then load the run's save. |
| `Quit` | Quit the game. |

### Run state and results (`Run.h`)

`RunState` is all the run remembers between frames. It lives in process
memory, never in the save, so a case can load the save and continue.

| Type | Description |
|---|---|
| `RunState` | The suite, the `Phase`, the run's outcome so far, `RunChanges`, the load count last seen, and each role's rendered revision when the last step that moved it started (`renderedBefore`). |
| `Phase` | `Settling` (waits `kSettleFrames`, 120, with the player present), `Running`, `Ending` (waits `kEndingFrames`, 10, so the files flush), `Finished`. |
| `Running` | The cursor: case index, `Section` (`kBody` or `kCleanup`), step index, frames spent, whether the step started, its start time and load count, and the case's outcome so far. |
| `RunChanges` | What the run changed in the world: the items it added per role, the roles it spawned, whether the player is away, and the crowd size. `AfterCommand` in `Run.cpp` derives it from each issued command; a load resets it. |
| `ResultLine` | `StepResult`, `CaseResult`, `RunEnd`, `WindowBegins` or `WindowEnds`. `ResultLineJson` writes one as a JSON line; `StartLineJson` writes the first line. |
| `Advanced{state, command, lines}` | What `Advance` returns. |

### Step rules (`StepRules.h`)

Each step kind has a `Start` overload for its first frame and a `Check`
overload for each later frame, in `Steps.cpp`. `StartStep` and `CheckStep`
visit the `Step` variant and call them. The overloads have names that the
entry points do not use, per the overload-set rule in `docs/conventions.md`.

| Type | Description |
|---|---|
| `StepContext` | The rule's input: the observation, frames spent in the step, the step's start time and load count, `RunChanges`, and `renderedBefore`. |
| `StepMove{verdict, command}` | The rule's output: a `Verdict` and at most one command. |
| `Verdict` | `Pending`, or `Completed{outcome, reason}`. |
| `RolesAffected` | The roles a step moves. `Advance` records their rendered revision when such a step issues a command, so a render from before the step cannot satisfy a later `AwaitRendered`. |

## How a run flows

```
<SKSE log dir>/BetterEnchantmentEffects-regression-run.json   written by tools/regression-run.py
  │
  ▼
ReadRegressionRun at plugin load                              engine/RegressionRun.cpp
  │  reads and deletes the file
  │  ParseRunFile(text, now) ──▶ RunFile or refusal            regression/RunFile.cpp
  │  writes the start line (StartLineJson)
  ▼
main menu opens ──▶ load the save as an SKSE task              engine/RegressionRun.cpp
  │
  ▼
FinishRegressionLoad ──▶ BeginRun(RunFile)                     engine/RegressionRun.cpp
  │
  ▼  once per frame, from Manager::OnFrame                     engine/ManagerTick.cpp
AdvanceRegressionRun                                           engine/RegressionRun.cpp
  │  Observe(world) ──▶ Observation                            engine/RegressionWorld.cpp
  │  Advance(state, observation)                               regression/Run.cpp
  │    Settling ──▶ Running
  │    StartStep / CheckStep ──▶ StepMove                      regression/Steps.cpp
  │    AfterCommand ──▶ RunChanges
  │    StepResult, CaseResult, window edges, RunEnd
  │  ResultLineJson ──▶ results file, log and trace            regression/RunFile.cpp
  │  Execute(command, world)                                   engine/RegressionWorld.cpp
  ▼
Ending ──▶ Quit ──▶ ReleaseWorld                               engine/RegressionRun.cpp
  │
  ▼
BetterEnchantmentEffects-regression-<run>.jsonl ──▶ tools/regression-run.py
                                                     prints the report and checks the budgets
```

## The files

| File | What it owns |
|---|---|
| `Words.h` | The enums, their `Named` tables, `OutcomeName`, `WordOf` and `IndexOf`. |
| `RunFile.h` / `RunFile.cpp` | `RunFile` and `ParseRunFile`. `RunFile.cpp` also writes the result lines (`StartLineJson`, `ResultLineJson`). |
| `Steps.h` | The step records, `Step`, `Case`, `CaseList`, `kScratchRecipe`, `kMaxCrowd`, and the declarations of `StepLabel`, `Catalog` and `FindCase`. |
| `Catalog.cpp` | The fixed case catalog, built with `Joined` and `Repeated`. |
| `Observation.h` | `Observation` and the facts records. |
| `Commands.h` | The command records and `Command`. |
| `Run.h` / `Run.cpp` | `RunState`, the phases, `RunChanges`, the result lines, and `BeginRun`, `Advance` and `Done`. |
| `StepRules.h` / `Steps.cpp` | `StepContext`, `StepMove`, the `Start` and `Check` rules, the deadlines, and `RolesAffected`. |
| `StepLabels.cpp` | The `Label` overloads and `StepLabel`. |

## See also

- `REFERENCE.md` → *Unattended regression runs* (the engine facts: the main
  menu sink, the load timing, the forms, residue, travel, and the Expect/Await
  deadlines) and *Regression requests*.
- `docs/plans/unattended-regression-2026-10-01.md`: the stages, the host
  command and the soak budgets.
- `docs/components/engine.md` → *Regression fixture*: the engine side.
- `docs/conventions.md` → *Naming patterns* (the overload-set rule and the
  `Expect…` and `Await…` rows) and *Glossary* (**residue**, **facts**).
