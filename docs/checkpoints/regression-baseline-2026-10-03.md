Status: record. Full-suite results and performance figures from run
20261003T035652 (12 launches) and the functional rerun 20261003T045504
(2 launches), on uncommitted work over commit 1d71488. Two findings stay open:
an intermittent game exit in `load-edit` and an intermittent camera flip in
`camera`.

# Regression baseline, 2026-10-03

## Conditions

| Condition | Value |
|---|---|
| Build | `1d714883bcad-dirty-071ff5d6d1e0-Release`: commit 1d71488 plus the uncommitted work of 2026-10-02 and 2026-10-03 (recipe sets in the studio view, the worn signature, the fighter crowd, the camera checks) |
| Save | `BEEFRegression`, in `QASmoke` |
| Frame rate | The game ran at 25.8 to 25.9 fps in every quiet window, under a frame cap. Per-frame plugin cost rises when the frame rate falls, so these figures compare only with runs at the same cap. |
| Idle camera | Held off for the whole run (`fAutoVanityModeDelay` set to 10^9, restored at the end). Earlier soak figures were measured with the idle camera active after two minutes. |
| Excluded | `soak-hour` |
| Launches | Functional cases in one launch, `studio-reload` in a second, and each soak in its own launch so that trace rotation cannot drop a window |

## Results

| Case | Run 20261003T035652 | Rerun 20261003T045504 |
|---|---|---|
| `lifecycle`, `equip-cycle`, `isolation`, `unload`, `load-idle`, `load-apply` | PASS | PASS |
| `camera` | PASS | FAIL: `hold-camera first-person 60`, the camera left first person after 7 frames |
| `load-edit` | CRASHED: the game exited without an end line and without a crash log | PASS |
| `load-gesture`, `load-paint`, `studio-save` | Not run (after the exit) | PASS |
| `studio-reload` | FAIL: no saved scratch recipe, because `studio-save` did not run | PASS |
| All ten soak cases | PASS (every step) | Not run |

`load-edit` also passed in two solo runs on 2026-10-03.

## Performance

Plugin cost is the mean plugin time per frame in the window. The budget for
`steady` and `fight` is 4000 µs. **Target bytes** is the estimated size of the
render targets held at the end of the window, mipmaps included.

| Soak | Crowd | Plugin cost | Worst frame | Target bytes | Budget |
|---|---|---|---|---|---|
| `soak-frost` | 12 mannequins, Winterglass | 992 µs | 1984 µs | 1.68 GB | pass |
| `soak-ward` | 12 mannequins, Resonant Ward | 2189 µs | 4120 µs | 3.92 GB | pass |
| `soak-idle` | 12 idle NPCs, Arcane Circuit | 2874 µs | 4312 µs | 1.40 GB | pass |
| `soak-churn` (`steady`, 60 s) | 12 mannequins, Arcane Circuit | 2939 µs | 4709 µs | 1.40 GB | pass |
| `soak-vanilla` | 12 mannequins in vanilla cuirasses | 3219 µs | 4789 µs | 2.50 GB | pass |
| `soak` | 12 mannequins, Arcane Circuit | 3352 µs | 5752 µs | 1.40 GB | pass |
| `soak-stack` | 12 mannequins, three recipes | 6380 µs | 8524 µs | 6.83 GB | **fail** |
| `soak-fight` (`fight`) | 12 fighters, three recipes | 6558 µs | 35998 µs | 6.98 GB | **fail** |
| `soak-large` | 32 mannequins, Arcane Circuit | 9452 µs | 13259 µs | 3.16 GB | **fail** |

| Soak | Arrival (`burst`) worst frame | Arrival plugin cost |
|---|---|---|
| `soak-fight` | 92078 µs | 13051 µs |
| `soak-stack` | 89329 µs | 30380 µs |
| `soak-vanilla` | 50688 µs | 8028 µs |
| `soak-large` | 47035 µs | 11480 µs |
| `soak` | 41390 µs | 9255 µs |

- Cost is additive per recipe. The three solo costs sum to 6533 µs; stacked,
  the three cost 6380 µs.
- Cost is linear per actor. Arcane Circuit costs 279 µs per actor with 12
  actors and 295 µs per actor with 32.
- In `soak-waves`, each `held-N` window cost 2969 to 3009 µs, and each `gap-N`
  window returned to the same 95114577 bytes.
- In `soak-churn`, each one-second churn window cost 6091 to 6774 µs, with a
  refresh of up to 67606 µs.
- In `soak-fight`, the slowest refresh in the `fight` window took 74758 µs.
  Equip events that leave the worn signature unchanged no longer rebuild, so
  these refreshes follow a change in worn armor. The trace was not checked for
  which item changed.
- Every `recovery` window returned live targets to 0. The idle pool and the
  scratch targets held 95114577 bytes (67130708 after `soak-frost`).
- In the `soak` run 20261003T012045 (commit 1d71488 without the uncommitted
  work), the VRAM profiler counted 3.40 GiB of resident render targets across
  the whole game during the `steady` window. The plugin's own estimate for
  that crowd is 1.40 GB.

## Open findings

| Finding | Evidence | State |
|---|---|---|
| `load-edit` game exit | In launch 20261003T035652-1, the trace ends 2 ms after `load.begin`, on `clear_end`, after an opacity edit refreshed the player. CrashLogger wrote no log. Three later runs passed. Installed actors hold `NiPointer`s to biped clones since 2026-10-02; the load's clear releases them. This is a suspect, not a verified cause. | Open |
| Camera flip after `ForceFirstPerson` | In rerun 20261003T045504-1, the camera state went 0 at 2707 ms, 9 at 3015 ms and 0 at 5559 ms, with no plugin event between. A camera mod in the load order likely animates its own transition; the mod is not identified. | Open |
