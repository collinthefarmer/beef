#include "planners/ActorState.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
FormKey Ench(std::uint32_t a_id) { return FormKey{"Skyrim.esm", a_id}; }

ActorState MakeState() {
  ActorState state;

  Piece live;
  live.lost = false;
  Piece lost;
  lost.lost = true;
  state.pieces.push_back(live);
  state.pieces.push_back(lost);

  state.instances.push_back(Instance{RecipeId{0}, std::nullopt, 0});
  state.instances.push_back(Instance{RecipeId{0}, Ench(0x100), 1});
  state.instances.push_back(Instance{RecipeId{1}, Ench(0x100), 2});

  Placement p0;
  p0.instance = InstanceId{0};
  p0.piece = PieceId{0};
  Placement p1;
  p1.instance = InstanceId{1};
  p1.piece = PieceId{0};
  Placement p2;
  p2.instance = InstanceId{0};
  p2.piece = PieceId{1};
  state.placements.push_back(std::move(p0));
  state.placements.push_back(std::move(p1));
  state.placements.push_back(std::move(p2));

  return state;
}
}

int main() {
  const ActorState state = MakeState();

  Check(PieceAt(state, PieceId{0}) == &state.pieces[0],
        "PieceAt returns the row for a valid handle");
  Check(PieceAt(state, PieceId{2}) == nullptr,
        "PieceAt returns nullptr for an out-of-range handle");
  Check(InstanceAt(state, InstanceId{2}) == &state.instances[2],
        "InstanceAt returns the row for a valid handle");
  Check(InstanceAt(state, InstanceId{3}) == nullptr,
        "InstanceAt returns nullptr for an out-of-range handle");
  Check(PlacementAt(state, PlacementId{1}) == &state.placements[1],
        "PlacementAt returns the row for a valid handle");
  Check(PlacementAt(state, PlacementId{99}) == nullptr,
        "PlacementAt returns nullptr for an out-of-range handle");

  Check(AnyLivePiece(state), "AnyLivePiece is true when a piece is not lost");
  ActorState allLost = state;
  for (Piece &piece : allLost.pieces) {
    piece.lost = true;
  }
  Check(!AnyLivePiece(allLost),
        "AnyLivePiece is false when every piece is lost");
  Check(!AnyLivePiece(ActorState{}),
        "AnyLivePiece is false when there are no pieces");

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
      PlacementsOfPiece(state, PieceId{0});
  Check(ofPiece0.size() == 2 && ofPiece0[0] == PlacementId{0} &&
            ofPiece0[1] == PlacementId{1},
        "PlacementsOfPiece returns every placement on a piece");
  Check(PlacementsOfPiece(state, PieceId{5}).empty(),
        "PlacementsOfPiece is empty for a piece with no placements");

  const std::vector<PlacementId> ofInstance0 =
      PlacementsOfInstance(state, InstanceId{0});
  Check(ofInstance0.size() == 2 && ofInstance0[0] == PlacementId{0} &&
            ofInstance0[1] == PlacementId{2},
        "PlacementsOfInstance returns every placement of an instance");
  Check(PlacementsOfInstance(state, InstanceId{2}).empty(),
        "PlacementsOfInstance is empty for an instance with no placements");

  return test::Finish("planners actorstate");
}
