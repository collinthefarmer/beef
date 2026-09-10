#pragma once

#include "recipe/Recipe.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
enum class PieceId : std::size_t {};
enum class InstanceId : std::size_t {};
enum class PlacementId : std::size_t {};
enum class RecipeId : std::size_t {};
enum class OutputIndex : std::size_t {};

struct Piece {
  GeometryIdentity identity;
  WornPiece keys;
  bool firstPerson = false;
  bool lost = false;
};

struct Instance {
  RecipeId recipe{};
  std::optional<FormKey> enchantment;
  int priority = 0;
  [[nodiscard]] bool operator==(const Instance &) const = default;
};

struct OutputPlacement {
  OutputIndex output{};
  bool selected = false;
  std::string problem;
  [[nodiscard]] bool operator==(const OutputPlacement &) const = default;
};

struct Placement {
  InstanceId instance{};
  PieceId piece{};
  RecipeKey key;
  std::vector<OutputPlacement> outputs;
  [[nodiscard]] bool operator==(const Placement &) const = default;
};

struct ActorState {
  std::vector<Piece> pieces;
  std::vector<Instance> instances;
  std::vector<Placement> placements;
};

[[nodiscard]] const Piece *PieceAt(const ActorState &a_state,
                                   PieceId a_piece) noexcept;
[[nodiscard]] const Instance *InstanceAt(const ActorState &a_state,
                                         InstanceId a_instance) noexcept;
[[nodiscard]] const Placement *PlacementAt(const ActorState &a_state,
                                           PlacementId a_placement) noexcept;

[[nodiscard]] bool AnyLivePiece(const ActorState &a_state) noexcept;
[[nodiscard]] std::optional<InstanceId>
FindInstance(const ActorState &a_state, RecipeId a_recipe,
             const std::optional<FormKey> &a_enchantment) noexcept;
[[nodiscard]] std::vector<PlacementId>
PlacementsOfPiece(const ActorState &a_state, PieceId a_piece);
[[nodiscard]] std::vector<PlacementId>
PlacementsOfInstance(const ActorState &a_state, InstanceId a_instance);
}
