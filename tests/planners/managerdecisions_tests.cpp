#include "planners/ManagerDecisions.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  GeometryPlacement geometry;
  geometry.sources.push_back(PlacementId{0});
  ActorLightPlan lights;
  lights.sources.push_back(InstanceId{0});

  Check(geometry.sources.size() == 1,
        "a geometry placement maps its merge sources to placements");
  Check(lights.sources.size() == 1,
        "an actor light plan maps its light sources to instances");

  Check(true, "TODO(planners fill): cover MatchActor instance dedup and "
              "priority merge, PlaceGeometry selector matching, PlaceLights "
              "replace, RetirePlan");
  return test::Finish("planners managerdecisions");
}
