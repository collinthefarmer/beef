# Console-driven lifecycle regression

Status: first slice built and checked offline; quest startup, console invocation,
Papyrus execution, and rendered behavior still require an in-game run. This is
not the full pipeline acceptance suite.

The optional quest applies, retires, and reapplies effects on the player while
keeping the demo armor equipped. It stops for a visual verdict after each
operation. The native bridge checks a new actor application and an actual
rendered surface output on the demo armor; an old successful snapshot cannot
satisfy the request. Retirement checks that the actor's live state was removed.
Neither assertion proves pixel correctness or final GPU resource destruction.

## Install for a test run

Use a disposable save/profile with the current framework DLL, its regular
assets, Community Shaders, and SKSE. Enable the existing
`BetterEnchantmentEffectsDemo.esp` and its Arcane Circuit recipe. Install
`build/regression/BEEF-regression.zip` as a separate optional mod and enable
`BetterEnchantmentEffectsRegression.esp`. This ZIP does not bundle the framework
or the demo armor. Do not enable older fixture copies alongside it.

Start in third person with the player loaded and effects enabled. Empty the
body armor slot and remove any existing Arcane Circuit armor from the player's
inventory. The runner deliberately refuses to replace your equipment or claim
ownership of an existing instance. Keep other matching recipes and armor out
of this first isolation case. Do not change equipment during a run.

## Console controls

Start the quest once, then run the case. These commands are the intended
Skyrim console interface; their runtime acceptance is still pending:

```text
startquest BEEFRegression
cqf BEEFRegression Run lifecycle
```

Close the console after starting or advancing: the game must run to equip and
render. Each operation waits for completion instead of treating command
submission as success. After a machine PASS notification, inspect the player:

| Phase | Expected appearance |
| --- | --- |
| 2: apply | Arcane Circuit visible on the demo armor |
| 3: retire | Baseline armor, still equipped |
| 4: reapply | Arcane Circuit returns |

Record your visual verdict, then advance:

```text
cqf BEEFRegression RecordVisual true
cqf BEEFRegression Next
```

Use `false` for a failed visual check. A failure is retained through the final
summary. `Next` refuses to skip an unrecorded verdict.

```text
cqf BEEFRegression Status
cqf BEEFRegression Abort
```

At the end or on abort, the runner unequips and removes the one fixture armor
it added. It does not save recipes, modify settings, save the game, or stop the
quest. Cleanup requests are issued through ordinary game equipment operations;
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
