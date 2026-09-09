# Clean-up path

Five stages. The ordering rests on one observation: deleting first makes
every later stage smaller, because a large share of what the structural
work would move and refactor is code nothing reaches.

Detail lives in `deletions.md`, `clusters.md`, `structure.md` and
`format.md`. This file is only the order and the dependencies.

## Stage 0 — commit what exists

Nothing from today is committed: about thirty modified and new paths,
including two fixed memory errors, the warning cleanup, the flake, and
`tools/`. Without a boundary here no later stage is separable, and a
bisect crosses unrelated work.

## Stage 1 — delete the dead pipeline

`deletions.md`, the certain list, as one removal rather than ten. About
1 to 2 days. Re-page `AnimationFPS` and `AnimationSpeed` before removing
the Setup page filter, or they become invisible and functional.

Verification: build, native suite, and the Setup page losing three
controls that never did anything. Regenerate `tidy-baseline.txt`
afterwards, since deleted code takes findings with it.

## Stage 2 — the four cheap fixes

From `clusters.md`: `Post(Intents&, Intent)`, `Match` over `std::get_if`,
`Manager`'s private `Do*` methods, and `Status` moving into `Snapshot`.
About 1 day, retiring 79 findings, and two of them delete a crash class —
a `std::terminate` path on the per-tick code, and the render thread's
unsynchronised read of a map the game thread mutates.

None depends on Stage 1, but all four are smaller after it.

## Stage 3 — the correctness fixes found by reading

About 2 days, and none of it was found by a metric.

- Split `Contribution` into typed index records; one field currently
  means two different index spaces depending on which planner produced it.
- Unify the row checks over `RowTypes`; the editor accepts values the
  loader rejects, so a bad recipe is refused only on reload.
- Guard `ColorFrom` (`src/RecipeJson.cpp:1674`) with `is_number`, as its
  sibling two lines below already does; malformed input currently unwinds
  out of a function that advertises `std::expected`.
- Make `History<T>::Rename` a free function; it writes `value.id` and one
  of its two instantiations has no `id`, so calling it will not compile.
- Bound depth in the three recursive walks that bound only termination,
  and cap the row count in `NamedRows` as `ParsePresets` already does.
- Resolve the empty `if` at `src/Signals.cpp:319`.
- Interpolate `kProgramStack` into the shader source so the interpreter's
  stack bound is named once instead of written twice as a literal.

## Stage 4 — structure

`structure.md`. About 8 to 9 days. Order within it:

1. `Forms.cpp` — three hours, a pure cut of 940 lines out of
   `Studio.cpp`, splitting the tightest co-change pair in the repository.
2. The small moves and renames in `structure.md`.
3. The tree itself, with `CMakeLists.txt`'s hand-maintained
   `HOST_OBJECT_SOURCES` list becoming three globs.
4. The file splits. `ComposePage` wants the `Page` record first and
   `Manager` wants the `Do*` methods from Stage 2 — prerequisites, not
   alternatives.

Batch every change that needs the game into one checkpoint rather than
five.

## Stage 5 — the format

`format.md`. About 5 to 8 days, and it needs a decision before it starts,
because format 1 may be frozen. Its first three items are independent of
the two that need a game checkpoint.

## Parallel execution

Three properties decide the shape.

**There is no barrier.** The previous tree stands frozen under
`src/_old`, and new code is written directly into `src/recipe`,
`src/mesh` and the rest. Nothing rewrites an include across the whole
repository, so nothing has to serialize around it. `src` and `src/_old`
are both include roots: old code's `#include "Recipe.h"` still resolves
to the frozen copy, new code says `#include "recipe/Recipe.h"`, and the
two never collide.

**Partition by file ownership, not by task.** Two streams editing the
same file conflict however unrelated their tasks are. The unit of
parallelism is a set of files one stream owns exclusively.

**Four hub files force serialization.** `RecipeJson.cpp`, `Recipe.h`,
`Manager.cpp` and `Studio.cpp` are each touched by five or more work
items. Each gets exactly one owner per wave.

| Wave | Stream | Owns | Verified by |
| --- | --- | --- | --- |
| 1 | Settings | `SettingsCore`, `Settings`, the ini, `Menu.cpp`'s page filter | native |
| 1 | Render | `RuntimeTextures.{h,cpp}`: dead modes, `kProgramStack` | game |
| 1 | Import | `Importer`, `Timing`, `EngineForms` | native |
| 1 | Scaffolding | `tools/`, `History.cpp`, `CMakeLists`, stale doc sections | build |
| 2 | Format surface | `Recipe.{h,cpp}`, `RecipeJson.cpp`, `schema/` | native |
| 2 | Actor state | `Manager.{h,cpp}`, `Merge.{h,cpp}`, `Snapshot.h` | game |
| 2 | Interface | `ComposePage`, `Studio`, `MenuWidgets`, `MenuState`, `History.h` | build |
| 3 | Four modules | one per new directory, disjoint by construction | mixed |

Two things do not parallelize. Game checkpoints serialize on the user, so
the streams needing Skyrim are batched into one checkpoint per wave. And
the clang-tidy baseline regenerates once per wave rather than per stream,
because deletions move it substantially; each stream instead states what
it expects to retire and integration checks the total.

Each stream runs in its own git worktree. Its exit condition is four
checks: the plugin builds at zero warnings, `tests/run-native.sh` passes,
`tools/tidy-baseline.sh --check` reports only what that stream meant to
add, and the frozen files the stream replaced are gone from the build and
from `src/_old`. A module is not done while both copies are in the tree.

## Decisions this path is waiting on

- Is recipe format 1 frozen? Stage 5 depends on it entirely.
- The judgement calls in `deletions.md`: `ShellMaterial::kVanilla`,
  `Blend::kNormal`, the format vocabulary no recipe exercises, the fields
  with no editing path (`priority`, `clock.speed`, `output.replace`,
  light `selector`), and `UniqueMaterial`.
- Whether `CheckSourceKind` goes, which costs a refusal becoming a
  diagnostic.
