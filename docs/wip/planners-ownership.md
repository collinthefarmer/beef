# planners: `.cpp` ownership map

Every function declared in `src/planners/*.h` has exactly one owning `.cpp`.
No function is unowned. Fill agents implement one `.cpp` each, disjoint.

## `src/planners/ActorState.cpp` (owns `ActorState.h` tables + queries)

- `const Piece *PieceAt(const ActorState &, PieceId)`
- `const Instance *InstanceAt(const ActorState &, InstanceId)`
- `const Placement *PlacementAt(const ActorState &, PlacementId)`
- `bool AnyLivePiece(const ActorState &)`
- `std::optional<InstanceId> FindInstance(const ActorState &, RecipeId, const std::optional<FormKey> &)`
- `std::vector<PlacementId> PlacementsOfPiece(const ActorState &, PieceId)`
- `std::vector<PlacementId> PlacementsOfInstance(const ActorState &, InstanceId)`

## `src/planners/StackPlan.cpp` (owns `StackPlan.h`)

- `GeometryStackPlan PlanStacks(std::span<const PlacedRecipe>, const GeometryPlan &)`
- `const SlotStackPlan *SlotStackPlanOf(const GeometryStackPlan &, Surface, Slot)`
- `std::optional<std::size_t> ChainIndexOf(const GeometryPlan &, SlotContribution)`

## `src/planners/BindingDiff.cpp` (owns `BindingDiff.h`)

- `BindingDiff PlanBinding(std::span<const PlacedRecipe>, const GeometryPlan &, std::span<const Slot>, std::span<const Slot>)`

## `src/planners/ManagerDecisions.cpp` (owns `ManagerDecisions.h`)

- `ActorState MatchActor(std::span<const Piece>, std::span<const Recipe>)`
- `GeometryPlacement PlaceGeometry(const ActorState &, std::span<const Recipe>, PieceId)`
- `ActorLightPlan PlaceLights(const ActorState &, std::span<const Recipe>)`
- `std::vector<RecipeId> RetirePlan(const ActorState &)`

## Header → owning `.cpp`(s)

- `ActorState.h` → `ActorState.cpp` (tables and queries). The recipe/state →
  view projections live in `studio/` (the view-model layer); the wave-3
  `SnapshotBuild` glue reads `ActorState` for structure and overlays live
  `SignalState` values and render textures onto studio's rows.
- `StackPlan.h` → `StackPlan.cpp`.
- `BindingDiff.h` → `BindingDiff.cpp`.
- `ManagerDecisions.h` → `ManagerDecisions.cpp`.

## Test fill units (one skeleton per `.cpp`)

- `tests/planners/actorstate_tests.cpp` → `ActorState.cpp`
- `tests/planners/stackplan_tests.cpp` → `StackPlan.cpp`
- `tests/planners/bindingdiff_tests.cpp` → `BindingDiff.cpp`
- `tests/planners/managerdecisions_tests.cpp` → `ManagerDecisions.cpp`
