#include "planners/ActorState.h"
#include "test_support.h"

#include <array>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
ActorState MakeState() {
  ActorState state;

  Geometry thirdA;
  thirdA.firstPerson = false;
  Geometry first;
  first.firstPerson = true;
  Geometry thirdB;
  thirdB.firstPerson = false;
  state.geometries.push_back(thirdA);
  state.geometries.push_back(first);
  state.geometries.push_back(thirdB);

  state.instances.push_back(Instance{RecipeId{0}, std::nullopt, 0});
  state.instances.push_back(Instance{RecipeId{1}, std::nullopt, 0});

  const auto place = [&](InstanceId a_instance, GeometryId a_piece) {
    Placement p;
    p.instance = a_instance;
    p.geometry = a_piece;
    state.placements.push_back(std::move(p));
  };
  place(InstanceId{0}, GeometryId{0});
  place(InstanceId{0}, GeometryId{1});
  place(InstanceId{1}, GeometryId{2});
  place(InstanceId{0}, GeometryId{0});
  place(InstanceId{0}, GeometryId{5});

  return state;
}
}

int main() {
  const ActorState state = MakeState();

  const std::array<PlacementId, 3> placements{PlacementId{2}, PlacementId{0},
                                              PlacementId{99}};

  const std::optional<InstanceId> first =
      InstanceOfPlaced(state, placements, 0);
  Check(first.has_value() && *first == InstanceId{1},
        "InstanceOfPlaced resolves a placed slot to its owning instance");
  const std::optional<InstanceId> second =
      InstanceOfPlaced(state, placements, 1);
  Check(second.has_value() && *second == InstanceId{0},
        "InstanceOfPlaced resolves each placed slot independently");
  Check(!InstanceOfPlaced(state, placements, 2).has_value(),
        "InstanceOfPlaced is nullopt when the placement id is out of range");
  Check(!InstanceOfPlaced(state, placements, 3).has_value(),
        "InstanceOfPlaced is nullopt when the placed index is out of range");
  Check(!InstanceOfPlaced(state, std::span<const PlacementId>{}, 0).has_value(),
        "InstanceOfPlaced is nullopt for an empty placement list");

  const std::vector<GeometryId> ofInstance0 =
      ThirdPersonGeometriesOfInstance(state, InstanceId{0});
  Check(
      ofInstance0.size() == 2 && ofInstance0[0] == GeometryId{0} &&
          ofInstance0[1] == GeometryId{0},
      "ThirdPersonGeometriesOfInstance keeps one entry per placement in order, "
      "skipping first-person and out-of-range pieces");

  const std::vector<GeometryId> ofInstance1 =
      ThirdPersonGeometriesOfInstance(state, InstanceId{1});
  Check(ofInstance1.size() == 1 && ofInstance1[0] == GeometryId{2},
        "ThirdPersonGeometriesOfInstance returns the third-person piece of an "
        "instance");

  Check(ThirdPersonGeometriesOfInstance(state, InstanceId{9}).empty(),
        "ThirdPersonGeometriesOfInstance is empty for an instance with no "
        "placements");

  return test::Finish("planners placementlookup");
}
