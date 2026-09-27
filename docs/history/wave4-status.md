Status: history. Wave 4 landed; the menu surface is now the UI v2 plan's. Names
and paths here predate the critique remediation of 2026-09-14 (Plan C's file
moves and Plan D's renames); the root `README.md` lists the current set.

# Wave 4 integration status

Recovery checkpoints on `cleanup/stage-0`:

- `f40a3e7`: recovered menu implementations, headers, ownership map and seam.
- `1b2ba04`: CMake integration and registration at `kDataLoaded`.

The resumed inspection corrected the following:

- Recipe context table column count and same-frame scratch-intent dispatch.
- Selector operand reconstruction and refused field-creation diagnostics.
- Ambiguous engine/Studio intent names and Timeline compiler warnings.
- Status and loaded-recipe table reads now use owned snapshot data, populated
  on the game thread. The displayed tick value is the configured interval.
- Board cells carry their output texture; Clear Mask marks the scratch preview
  dirty. Both have native regression assertions.
- Resource creation uses the shared taken-name lookup, including collisions
  between source and mask names.
- Recipe priority/clock-speed and output selector forms existed without a
  render path. They now render in the context rows, with typed snapshot
  projections and an output-settings recovery editor when no geometry matches.
- The first full tidy pass found 39 menu findings. The reduction split draw
  functions and the intent visitor, grouped widget arguments into records,
  reserved resource-name vectors, and removed unused helpers.
- The tidy runner propagates tool failure and preserves diagnostics instead
  of treating a failed invocation as a clean cached result. A deliberately
  failing stand-in verified this failure path.

Verification completed on 2026-09-11:

- Release DLL builds and links with zero warnings (`./build.sh Release -j 4`).
- All 35 native suites and schema validation pass (`tests/run-native.sh`),
  including board texture, cleared mask, typed selector, and unplaced-output
  projection regressions.
- Full tidy covers 72 sources. All 11 menu sources have zero findings. The
  82 pre-existing findings outside the menu retain exactly the previous
  per-file/check counts; no new finding was accepted into the baseline.
- Full source formatting and the refreshed tidy-baseline check pass.
- Ninja's crash-damaged generated metadata was compacted; the final build no
  longer reports its recovery warning.

Wave 5 remains: installation, the in-game material/animation/light/shell/studio
checkpoint, then removal of `src/_old` after that checkpoint passes.
