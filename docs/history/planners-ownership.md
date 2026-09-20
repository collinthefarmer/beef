# planners: `.cpp` ownership map

Status: history. Names and paths here predate the critique remediation of
2026-09-14 (Plan C's file moves and Plan D's renames); `docs/README.md` indexes
the current set.

Every function declared in `src/planners/*.h` has exactly one owning `.cpp`.
No function is unowned. Fill agents implement one `.cpp` each, disjoint.

## `src/planners/ActorState.cpp` (owns `ActorState.h` tables + queries)

- `const Geometry *GeometryAt(const ActorState &, GeometryId)`
- `const Instance *InstanceAt(const ActorState &, InstanceId)`
- `const Placement *PlacementAt(const ActorState &, PlacementId)`
- `bool AnyLiveGeometry(const ActorState &)`
- `std::optional<InstanceId> FindInstance(const ActorState &, RecipeId, const std::optional<FormKey> &)`
- `std::vector<PlacementId> PlacementsOfGeometry(const ActorState &, GeometryId)`
- `std::vector<PlacementId> PlacementsOfInstance(const ActorState &, InstanceId)`
- `std::vector<RecipeId> RecipesOfInactiveInstances(const ActorState &)`

## `src/planners/StackPlan.cpp` (owns `StackPlan.h`)

- `GeometryStackPlan PlanStacks(std::span<const PlacedRecipe>, const GeometryPlan &)`
- `const SlotStackPlan *SlotStackPlanOf(const GeometryStackPlan &, Surface, Slot)`
- `std::optional<std::size_t> ChainIndexOf(const GeometryPlan &, SlotContribution)`

## `src/planners/BindingDiff.cpp` (owns `BindingDiff.h`)

- `BindingDiff PlanBinding(std::span<const PlacedRecipe>, const GeometryPlan &, std::span<const Slot>, std::span<const Slot>)`

## `src/planners/ActorPlanning.cpp` (owns `ActorPlanning.h`)

- `ActorState MatchActor(std::span<const Geometry>, std::span<const Recipe>)`
- `GeometryPlacementPlan PlanGeometryPlacement(const ActorState &, std::span<const Recipe>, GeometryId)`
- `ActorLightPlan PlanActorLights(const ActorState &, std::span<const Recipe>)`

## Header → owning `.cpp`(s)

- `ActorState.h` → `ActorState.cpp` (tables and queries). The recipe/state →
  view projections live in `studio/` (the view-model layer); the wave-3
  `SnapshotBuild` glue reads `ActorState` for structure and overlays live
  `SignalState` values and render textures onto studio's rows.
- `StackPlan.h` → `StackPlan.cpp`.
- `BindingDiff.h` → `BindingDiff.cpp`.
- `ActorPlanning.h` → `ActorPlanning.cpp`.

## Test fill units (one skeleton per `.cpp`)

- `tests/planners/actorstate_tests.cpp` → `ActorState.cpp`
- `tests/planners/stackplan_tests.cpp` → `StackPlan.cpp`
- `tests/planners/bindingdiff_tests.cpp` → `BindingDiff.cpp`
- `tests/planners/actorplanning_tests.cpp` → `ActorPlanning.cpp`
