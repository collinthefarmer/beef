# Console-driven lifecycle regression

For an in-game command sheet, see the [quick reference](QUICK_REFERENCE.md).

Status: first slice built and checked offline; quest startup, console invocation,
Papyrus execution, and rendered behavior still require an in-game run. This is
not the full pipeline acceptance suite.

The optional quest applies, retires, and reapplies effects on the player while
keeping the demo armor equipped. It stops for a visual verdict after each
operation. The runner solos the Arcane Circuit recipe (`arcane-circuit`) for
the whole run, so other recipes that match the demo armor stay hidden. When
the run ends or is aborted, the runner restores the studio isolation that was
active before the run. The native bridge checks a new actor application and an
actual rendered surface output of Arcane Circuit on the demo armor; an old
successful snapshot cannot satisfy the request. Retirement checks that the actor's live state was removed.
Neither assertion proves pixel correctness or final GPU resource destruction.

## Install for a test run

Use a disposable save/profile with the current framework DLL, its regular
assets, Community Shaders, and SKSE. Enable the existing
`BetterEnchantmentEffectsDemo.esp` and its Arcane Circuit recipe. Install
`build/regression/BEEF-regression.zip` as a separate optional mod and enable
`BetterEnchantmentEffectsRegression.esp`. `./install-regression.sh` copies the
package into the MO2 mod `BetterEnchantmentEffects Regression`; set
`MO2_MODS_DIR` to use another mods folder. This ZIP does not bundle the framework
or the demo armor. Do not enable older fixture copies alongside it.

Start in third person with the player loaded and effects enabled. Empty the
body armor slot and remove any existing Arcane Circuit armor from the player's
inventory. The runner deliberately refuses to replace your equipment or claim
ownership of an existing instance. Other recipes that match the demo armor
may stay enabled, because the solo hides them. Do not change the studio solo
or equipment during a run.

## Console controls

Start the quest once, then run the case. These commands are the intended
Skyrim console interface; their runtime acceptance is still pending:

```text
startquest beef_regression
sqv beef_regression
setpqv beef_regression command 1
```

Close the console after starting or advancing: the game must run to equip and
render. Each operation waits for completion instead of treating command
submission as success. Every message has a status line and a `Do:` line; the
[quick reference](QUICK_REFERENCE.md) lists them. After the
`Step N/4 ... - plugin check passed` line, the `Do:` line says what to check:

| Step | Plugin check | You check |
| --- | --- | --- |
| 1/4 Equip | The demo cuirass is equipped | No checkpoint |
| 2/4 Apply | A new application rendered an Arcane Circuit output | Arcane Circuit is visible on the cuirass |
| 3/4 Retire | The player has no live effect state | The cuirass is plain dwarven armor and still equipped |
| 4/4 Reapply | As step 2, with a newer application | Arcane Circuit is back on the cuirass |

A failed run names its step and cause, for example `Run BLOCKED at Step 2/4
Apply - no Arcane Circuit output rendered, or the player was not ready`.

Answer yes with command 2 or no with command 3. The answer is recorded and the
next step starts at once. A no is retained through the final result. Commands 2
and 3 are accepted only while a step waits for an answer. Command 4 repeats the
current step's messages, and command 5 aborts the run.

```text
setpqv beef_regression command 2
setpqv beef_regression command 3
setpqv beef_regression command 4
setpqv beef_regression command 5
```

At the end or on abort, the runner unequips and removes the one fixture armor
it added. It does not save recipes, modify settings, save the game, or stop the
quest. A save or load during a run cancels the run and restores the solo on
the runner's next update. Cleanup requests are issued through ordinary game equipment operations;
confirm the armor is gone and effects have settled. Save/load invalidates native
requests and cancels the runner on its next update. It intentionally does not
alter equipment from the loaded save; inspect that equipment manually before
starting again. Do not save in the middle of a test.

An operation times out after 100 non-menu polls at 0.1-second intervals. This is
a bounded polling budget, not a precise ten-second gameplay timer: Papyrus
scheduling can extend the duration. A stalled VM cannot be diagnosed by its own
polling loop. No notification after closing the console warrants checking the
Papyrus log and quest state, not recording a pass.

## Evidence and limitations

`BEEF regression:` notifications and Papyrus traces report the active phase and
final outcome. The plugin JSONL trace records `regression.result` and
`regression.visual` commands with request IDs and separate outcomes, under the
existing trace session/build identity. Capture all checkpoints and the final
cleanup. A missing match is BLOCKED, a rendering failure is FAIL, and a stale
request/session is ABORTED. A machine PASS never supplies a visual verdict.

This slice does not expose reduction values, lookup execution counts, retained
preview leases, or per-case performance counters. Those cases remain in the
[driver plan](../../docs/plans/in-game-regression-driver.md).

## Rebuild without Creation Kit

The scripts were compiled with the upstream Windows Caprica v0.3.0 release,
using its explicit Skyrim target. Obtain Caprica from
<https://github.com/Orvid/Caprica/releases/tag/v0.3.0>, extract it under
`build/regression-tools/caprica`, and run inside `nix develop`:

```sh
python3 tools/regression-compile.py --compiler build/regression-tools/caprica/Caprica.exe
python3 tools/regression-package.py
```

The compile wrapper supports a native Caprica binary too; on WSL it converts
paths for the Windows executable. Caprica requires the bridge script's `Native`
class annotation. The compiler is a local build tool and is not packaged.

`imports/` contains only the minimal API declarations needed to compile this
runner; the installed SKSE script-source set lacks some vanilla dependencies.
These files are not replacement game implementations. The packager explicitly
includes only the two `BEEFRegression*.pex` scripts and their source; it never
packages these import declarations or their bytecode. The game and SKSE supply
the actual functions.

The generated plugin has one manually started quest, no aliases or fragments,
and one attached script with no properties to fill. Its QUST/VMAD layout follows
[xEdit's Skyrim record definitions](https://github.com/TES5Edit/TES5Edit/blob/dev-4.1.5/Core/wbDefinitionsTES5.pas).
No startup quest/SEQ behavior is relied on. Validate the generated plugin with
xEdit and perform the console startup smoke test before a wider test run.

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
