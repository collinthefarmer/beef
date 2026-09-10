#include "planners/ActorState.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  ActorState state;
  state.pieces.push_back(Piece{});
  state.instances.push_back(Instance{RecipeId{0}, std::nullopt, 0});
  Placement placement;
  placement.instance = InstanceId{0};
  placement.piece = PieceId{0};
  placement.outputs.push_back(OutputPlacement{OutputIndex{0}, true, {}});
  state.placements.push_back(std::move(placement));

  Check(state.pieces.size() == 1, "an actor state holds a pieces table");
  Check(state.instances.size() == 1, "an actor state holds an instances table");
  Check(state.placements.size() == 1,
        "an actor state holds a placements table");

  Check(true,
        "TODO(planners fill): cover PieceAt/InstanceAt/PlacementAt bounds, "
        "AnyLivePiece, FindInstance, PlacementsOfPiece, PlacementsOfInstance");
  return test::Finish("planners actorstate");
}
