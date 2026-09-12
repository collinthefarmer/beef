#include "planners/ActorState.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
FormKey Ench(std::uint32_t a_id) { return FormKey{"Skyrim.esm", a_id}; }

ActorState MakeState() {
  ActorState state;

  Geometry live;
  live.lost = false;
  Geometry lost;
  lost.lost = true;
  state.geometries.push_back(live);
  state.geometries.push_back(lost);

  state.instances.push_back(Instance{RecipeId{0}, std::nullopt, 0});
  state.instances.push_back(Instance{RecipeId{0}, Ench(0x100), 1});
  state.instances.push_back(Instance{RecipeId{1}, Ench(0x100), 2});

  Placement p0;
  p0.instance = InstanceId{0};
  p0.geometry = GeometryId{0};
  Placement p1;
  p1.instance = InstanceId{1};
  p1.geometry = GeometryId{0};
  Placement p2;
  p2.instance = InstanceId{0};
  p2.geometry = GeometryId{1};
  state.placements.push_back(std::move(p0));
  state.placements.push_back(std::move(p1));
  state.placements.push_back(std::move(p2));

  return state;
}
}

int main() {
  const ActorState state = MakeState();

  Check(GeometryAt(state, GeometryId{0}) == &state.geometries[0],
        "GeometryAt returns the row for a valid handle");
  Check(GeometryAt(state, GeometryId{2}) == nullptr,
        "GeometryAt returns nullptr for an out-of-range handle");
  Check(InstanceAt(state, InstanceId{2}) == &state.instances[2],
        "InstanceAt returns the row for a valid handle");
  Check(InstanceAt(state, InstanceId{3}) == nullptr,
        "InstanceAt returns nullptr for an out-of-range handle");
  Check(PlacementAt(state, PlacementId{1}) == &state.placements[1],
        "PlacementAt returns the row for a valid handle");
  Check(PlacementAt(state, PlacementId{99}) == nullptr,
        "PlacementAt returns nullptr for an out-of-range handle");

  Check(AnyLiveGeometry(state),
        "AnyLiveGeometry is true when a piece is not lost");
  ActorState allLost = state;
  for (Geometry &piece : allLost.geometries) {
    piece.lost = true;
  }
  Check(!AnyLiveGeometry(allLost),
        "AnyLiveGeometry is false when every piece is lost");
  Check(!AnyLiveGeometry(ActorState{}),
        "AnyLiveGeometry is false when there are no pieces");

  const std::optional<InstanceId> noEnch =
      FindInstance(state, RecipeId{0}, std::nullopt);
  Check(noEnch.has_value() && *noEnch == InstanceId{0},
        "FindInstance matches recipe with no enchantment");
  const std::optional<InstanceId> withEnch =
      FindInstance(state, RecipeId{0}, Ench(0x100));
  Check(withEnch.has_value() && *withEnch == InstanceId{1},
        "FindInstance separates instances of one recipe by enchantment");
  const std::optional<InstanceId> otherRecipe =
      FindInstance(state, RecipeId{1}, Ench(0x100));
  Check(otherRecipe.has_value() && *otherRecipe == InstanceId{2},
        "FindInstance separates instances by recipe");
  Check(!FindInstance(state, RecipeId{0}, Ench(0x999)).has_value(),
        "FindInstance returns nullopt when no instance matches the grain");
  Check(!FindInstance(state, RecipeId{2}, std::nullopt).has_value(),
        "FindInstance returns nullopt for an unknown recipe");

  const std::vector<PlacementId> ofPiece0 =
      PlacementsOfGeometry(state, GeometryId{0});
  Check(ofPiece0.size() == 2 && ofPiece0[0] == PlacementId{0} &&
            ofPiece0[1] == PlacementId{1},
        "PlacementsOfGeometry returns every placement on a piece");
  Check(PlacementsOfGeometry(state, GeometryId{5}).empty(),
        "PlacementsOfGeometry is empty for a piece with no placements");

  const std::vector<PlacementId> ofInstance0 =
      PlacementsOfInstance(state, InstanceId{0});
  Check(ofInstance0.size() == 2 && ofInstance0[0] == PlacementId{0} &&
            ofInstance0[1] == PlacementId{2},
        "PlacementsOfInstance returns every placement of an instance");
  Check(PlacementsOfInstance(state, InstanceId{2}).empty(),
        "PlacementsOfInstance is empty for an instance with no placements");

  const std::vector<PieceMatch> piece0Matches =
      MatchesForPiece(state, GeometryId{0}, 1);
  Check(
      piece0Matches.size() == 2 && piece0Matches[0].instance == 0 &&
          piece0Matches[0].priority == 0 && piece0Matches[1].instance == 1 &&
          piece0Matches[1].priority == 1,
      "MatchesForPiece returns one match per placement in the geometry range");
  Check(MatchesForPiece(state, GeometryId{0}, 2).size() == 2,
        "MatchesForPiece dedupes an instance placed on more than one geometry");
  Check(MatchesForPiece(state, GeometryId{5}, 1).empty(),
        "MatchesForPiece is empty for a geometry range with no placements");

  const std::vector<PlacementId> geomPlacements{PlacementId{1}, PlacementId{2}};
  const std::optional<std::size_t> placed =
      PlacedIndexOf(state, geomPlacements, InstanceId{0});
  Check(placed.has_value() && *placed == 1,
        "PlacedIndexOf returns the position of the instance's placement");
  const std::vector<PlacementId> onlyOther{PlacementId{1}};
  Check(!PlacedIndexOf(state, onlyOther, InstanceId{0}).has_value(),
        "PlacedIndexOf returns nullopt when the instance is not placed here");

  return test::Finish("planners actorstate");
}
