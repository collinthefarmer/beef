#include "planners/ActorState.h"

#include <algorithm>

namespace BetterEnchantmentEffects {
namespace {
template <typename Handle>
[[nodiscard]] std::size_t IndexOf(Handle a_handle) noexcept {
  return static_cast<std::size_t>(a_handle);
}

template <typename Row, typename Handle>
[[nodiscard]] const Row *RowAt(const std::vector<Row> &a_rows,
                               Handle a_handle) noexcept {
  const std::size_t index = IndexOf(a_handle);
  return index < a_rows.size() ? &a_rows[index] : nullptr;
}
}

const Piece *PieceAt(const ActorState &a_state, PieceId a_piece) noexcept {
  return RowAt(a_state.pieces, a_piece);
}

const Instance *InstanceAt(const ActorState &a_state,
                           InstanceId a_instance) noexcept {
  return RowAt(a_state.instances, a_instance);
}

const Placement *PlacementAt(const ActorState &a_state,
                             PlacementId a_placement) noexcept {
  return RowAt(a_state.placements, a_placement);
}

bool AnyLivePiece(const ActorState &a_state) noexcept {
  return std::any_of(a_state.pieces.begin(), a_state.pieces.end(),
                     [](const Piece &a_piece) { return !a_piece.lost; });
}

std::optional<InstanceId>
FindInstance(const ActorState &a_state, RecipeId a_recipe,
             const std::optional<FormKey> &a_enchantment) noexcept {
  for (std::size_t i = 0; i < a_state.instances.size(); ++i) {
    const Instance &instance = a_state.instances[i];
    if (instance.recipe == a_recipe && instance.enchantment == a_enchantment) {
      return InstanceId{i};
    }
  }
  return std::nullopt;
}

std::vector<PlacementId> PlacementsOfPiece(const ActorState &a_state,
                                           PieceId a_piece) {
  std::vector<PlacementId> found;
  for (std::size_t i = 0; i < a_state.placements.size(); ++i) {
    if (a_state.placements[i].piece == a_piece) {
      found.push_back(PlacementId{i});
    }
  }
  return found;
}

std::vector<PlacementId> PlacementsOfInstance(const ActorState &a_state,
                                              InstanceId a_instance) {
  std::vector<PlacementId> found;
  for (std::size_t i = 0; i < a_state.placements.size(); ++i) {
    if (a_state.placements[i].instance == a_instance) {
      found.push_back(PlacementId{i});
    }
  }
  return found;
}
}
