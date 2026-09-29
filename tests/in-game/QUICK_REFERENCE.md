# Regression test quick reference

## Prepare and start

Use third person, enable effects, empty the body armor slot.

```text
startquest beef_regression
sqv beef_regression
setpqv beef_regression command 1
```

"Command N" in the messages means `setpqv beef_regression command N` in the
console. Close the console after each command so the game can run.

## Reading the messages

Every message has two lines. The first says where the run is and what
happened. The second starts with `Do:` and says what to do next.

```text
BEEF regression: Step 2/4 Apply - plugin check passed
Do: check that Arcane Circuit is visible on the cuirass, then enter command 2 if yes or 3 if no
```

## Steps

| Step | First line while you wait | What you check when the plugin check passes |
| --- | --- | --- |
| 1/4 Equip | Arcane Circuit soloed, demo cuirass added | Nothing; the run continues |
| 2/4 Apply | waiting for the plugin to apply the effects | Arcane Circuit is visible on the cuirass |
| 3/4 Retire | waiting for the plugin to remove the effects | The cuirass is plain dwarven armor and still equipped |
| 4/4 Reapply | waiting for the plugin to apply the effects | Arcane Circuit is back on the cuirass |

Answer each check with command 2 (yes) or 3 (no). The next step starts at
once, and its first line begins with your answer, for example `You answered
yes. Step 3/4 Retire - waiting for the plugin to remove the effects`.

The run solos Arcane Circuit. At the end, the runner removes the cuirass,
restores the previous studio solo and reports one of these results:

| First line | Meaning |
| --- | --- |
| `Run PASS` | Every plugin check passed and you answered yes each time |
| `Run FAIL - you answered no at least once` | A visual check failed |
| `Run FAIL at Step N` | The plugin failed, or the step timed out |
| `Run BLOCKED at Step N` | Nothing rendered, or the player or plugin was not ready |
| `Run ABORTED` | Command 5, or a save or load during the run |

A message that begins `Not started` or `Not accepted` changed nothing; its
`Do:` line says how to fix the cause.

## Status or stop

Command 4 repeats the two lines for the current step. Command 5 aborts the
run.

```text
setpqv beef_regression command 4
setpqv beef_regression command 5
```

Abort removes the runner's armor in the same game session. Avoid saving during
a run. Loading a save invalidates the run; equipment from that save is left
alone and may need manual cleanup.

If no checkpoint arrives, close the console, then check status and logs.
Keep screenshots of each phase and the final result. Papyrus messages begin
`BEEF regression:`; the plugin JSONL trace records `regression.result` and
`regression.visual` events.

This case covers apply/retire/reapply only. Cache reuse, reductions, previews,
and performance still need separate cases. See [the full guide](README.md)
for setup, build instructions, and limitations.

## Updating the original runner

The original instructions incorrectly used `cqf`, which this Skyrim runtime
rejects. This version instead polls the integer `command` variable set by
`setpqv`. Close the console between commands; the variable is a single pending
command, not a queue. `startquest` itself may print no message. `sqv
beef_regression` displays the quest state and script variables.

After exiting Skyrim and installing this updated script, use a disposable
save from before the fixture was started. If the old fixture was already
started but never ran a test, reset only this fixture once:

```text
stopquest beef_regression
resetquest beef_regression
startquest beef_regression
```

Close the console and look for `BEEF regression: Ready`. Then use command 1.
Do not reset an active test; command 5 performs its cleanup first.
