#include "planners/ActorPlan.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
FormKey Ench(std::uint32_t a_id) { return FormKey{"Skyrim.esm", a_id}; }

ActorPlan MakePlan() {
  ActorPlan plan;

  Geometry live;
  live.lost = false;
  Geometry lost;
  lost.lost = true;
  plan.geometries.push_back(live);
  plan.geometries.push_back(lost);

  plan.instances.push_back(Instance{RecipeId{0}, std::nullopt, 0});
  plan.instances.push_back(Instance{RecipeId{0}, Ench(0x100), 1});
  plan.instances.push_back(Instance{RecipeId{1}, Ench(0x100), 2});

  Placement p0;
  p0.instance = InstanceId{0};
  p0.geometry = GeometryId{0};
  Placement p1;
  p1.instance = InstanceId{1};
  p1.geometry = GeometryId{0};
  Placement p2;
  p2.instance = InstanceId{0};
  p2.geometry = GeometryId{1};
  plan.placements.push_back(std::move(p0));
  plan.placements.push_back(std::move(p1));
  plan.placements.push_back(std::move(p2));

  return plan;
}
}

int main() {
  const ActorPlan plan = MakePlan();

  Check(GeometryAt(plan, GeometryId{0}) == &plan.geometries[0],
        "GeometryAt returns the row for a valid handle");
  Check(GeometryAt(plan, GeometryId{2}) == nullptr,
        "GeometryAt returns nullptr for an out-of-range handle");
  Check(InstanceAt(plan, InstanceId{2}) == &plan.instances[2],
        "InstanceAt returns the row for a valid handle");
  Check(InstanceAt(plan, InstanceId{3}) == nullptr,
        "InstanceAt returns nullptr for an out-of-range handle");
  Check(PlacementAt(plan, PlacementId{1}) == &plan.placements[1],
        "PlacementAt returns the row for a valid handle");
  Check(PlacementAt(plan, PlacementId{99}) == nullptr,
        "PlacementAt returns nullptr for an out-of-range handle");

  Check(AnyLiveGeometry(plan),
        "AnyLiveGeometry is true when a piece is not lost");
  ActorPlan allLost = plan;
  for (Geometry &piece : allLost.geometries) {
    piece.lost = true;
  }
  Check(!AnyLiveGeometry(allLost),
        "AnyLiveGeometry is false when every piece is lost");
  Check(!AnyLiveGeometry(ActorPlan{}),
        "AnyLiveGeometry is false when there are no pieces");

  const std::optional<InstanceId> noEnch =
      FindInstance(plan, RecipeId{0}, std::nullopt);
  Check(noEnch.has_value() && *noEnch == InstanceId{0},
        "FindInstance matches recipe with no enchantment");
  const std::optional<InstanceId> withEnch =
      FindInstance(plan, RecipeId{0}, Ench(0x100));
  Check(withEnch.has_value() && *withEnch == InstanceId{1},
        "FindInstance separates instances of one recipe by enchantment");
  const std::optional<InstanceId> otherRecipe =
      FindInstance(plan, RecipeId{1}, Ench(0x100));
  Check(otherRecipe.has_value() && *otherRecipe == InstanceId{2},
        "FindInstance separates instances by recipe");
  Check(!FindInstance(plan, RecipeId{0}, Ench(0x999)).has_value(),
        "FindInstance returns nullopt when no instance matches the grain");
  Check(!FindInstance(plan, RecipeId{2}, std::nullopt).has_value(),
        "FindInstance returns nullopt for an unknown recipe");

  const std::vector<PlacementId> ofPiece0 =
      PlacementsOfGeometry(plan, GeometryId{0});
  Check(ofPiece0.size() == 2 && ofPiece0[0] == PlacementId{0} &&
            ofPiece0[1] == PlacementId{1},
        "PlacementsOfGeometry returns every placement on a piece");
  Check(PlacementsOfGeometry(plan, GeometryId{5}).empty(),
        "PlacementsOfGeometry is empty for a piece with no placements");

  const std::vector<PlacementId> ofInstance0 =
      PlacementsOfInstance(plan, InstanceId{0});
  Check(ofInstance0.size() == 2 && ofInstance0[0] == PlacementId{0} &&
            ofInstance0[1] == PlacementId{2},
        "PlacementsOfInstance returns every placement of an instance");
  Check(PlacementsOfInstance(plan, InstanceId{2}).empty(),
        "PlacementsOfInstance is empty for an instance with no placements");

  const std::vector<PieceMatch> piece0Matches =
      MatchesForPiece(plan, GeometryId{0}, 1);
  Check(
      piece0Matches.size() == 2 && piece0Matches[0].instance == 0 &&
          piece0Matches[0].priority == 0 && piece0Matches[1].instance == 1 &&
          piece0Matches[1].priority == 1,
      "MatchesForPiece returns one match per placement in the geometry range");
  Check(MatchesForPiece(plan, GeometryId{0}, 2).size() == 2,
        "MatchesForPiece dedupes an instance placed on more than one geometry");
  Check(MatchesForPiece(plan, GeometryId{5}, 1).empty(),
        "MatchesForPiece is empty for a geometry range with no placements");

  const std::vector<PlacementId> geomPlacements{PlacementId{1}, PlacementId{2}};
  const std::optional<std::size_t> placed =
      PlacedIndexOf(plan, geomPlacements, InstanceId{0});
  Check(placed.has_value() && *placed == 1,
        "PlacedIndexOf returns the position of the instance's placement");
  const std::vector<PlacementId> onlyOther{PlacementId{1}};
  Check(!PlacedIndexOf(plan, onlyOther, InstanceId{0}).has_value(),
        "PlacedIndexOf returns nullopt when the instance is not placed here");

  return test::Finish("planners actorplan");
}
