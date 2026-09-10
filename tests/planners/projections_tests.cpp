#include "planners/ActorState.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  SignalProjection signal;
  signal.name = "glowLevel";
  ScalarProjection scalar;
  scalar.field = ScalarField::kStrength;
  LayerProjection layer;
  layer.blend = "replace";
  OutputProjection output;
  output.target = Target::kMaterial;
  output.scalars.push_back(scalar);
  output.layers.push_back(layer);

  Check(output.scalars.size() == 1,
        "an output projection carries its scalar projections");
  Check(output.layers.size() == 1,
        "an output projection carries its layer projections");

  Check(true, "TODO(planners fill): cover "
              "ProjectSignals/ProjectScalars/ProjectLayers/ProjectOutput "
              "against a fixture recipe and a ticked SignalState");
  return test::Finish("planners projections");
}
