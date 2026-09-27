Status: record. Exact graph ownership, registration and session validation,
rebuild continuity, native adapter coverage, and pending runtime acceptance.

# Animation observation ownership

The old animation sink attached during every effect apply and detached by looking
up the actor's current graphs. A replacement graph could therefore leave the old
source registered. Recipe rebuilds also erased discovery, and animation callbacks
updated discovery before entering the session queue.

`AnimationSubscriptions` now owns the exact graph reference and actor handle.
Unchanged graphs keep one registration. Replacement invalidates the old token
before detaching its source and attaching the replacement. Actors temporarily
without graphs remain tracked, allowing maintenance to recover or retire them.
The callback copies its strings and registration token; it never reads applied
effect state. Source lookup locking ends before engine Add/Remove calls.

`ManagerAnimation` routes events through the existing session queue. On delivery,
it checks the registration, actor identity and current graph before updating
discovery and firing the event. Final retirement and load clearing invalidate
captured callbacks, including ones submitted after the next session resumes.
`RetireEffects` releases recipe references before store publication while keeping
observation across queued rebuilds. Failed or abandoned rebuilds end observation.
`RetireAll` includes observed actors in rebuild gaps and retains the existing
deferred retirement ordering behind previously queued refreshes.

The native harness compiles the actual owner and `ManagerAnimation.cpp`, including
`RetireAll`, against isolated actor/source/manager doubles and the real application
service and session queue. Tests cover duplicate reconciliation, exact-source
detach, graph retention and replacement, reused actor IDs, missing-graph recovery,
rebuild gaps, retirement ordering, rejected submissions, load cancellation, a
paused callback crossing teardown, malformed events, tracing disabled, and
callback exceptions. It does not execute the full renderer-facing refresh body
or establish Skyrim's graph-array synchronization and replacement timing.

The full release gate passed: all 104 ASan/UBSan CTest suites, Windows Release
compilation and linking, formatting, include layers and source conventions.
The animation suite contains 28 checks. Full clang-tidy covered 128 translation
units and reported the same 62 first-party findings, zero external findings and
no baseline increases, including headers. The initial Windows header rebuild
reported the existing four menu initializer warnings; the final incremental
build reported none. Source-inventory tests and `git diff --check` also passed.
The destructor's narrow upstream diagnostic suppression is explained in
`REFERENCE.md`; no analysis baseline was changed.

Local validation log: `/tmp/beef-animation-release-final.log`.
Built DLL: `build/Release/BetterEnchantmentEffects.dll`, SHA-256
`c5a343ce47b6d6f475d4c085b3ba300ff1e624c39cfba49a22e0ad021408c727`.

Runtime acceptance remains open in the
[animation checklist](../in-game-regression.md#animation-subscription-acceptance)
and the active alpha plan. Discovery survives an ordinary rebuild; signal state
still rebuilds normally, and events delivered without effect state are discarded.
No installation or in-game run was performed.
