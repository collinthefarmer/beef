#pragma once

#include "recipe/Recipe.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
enum class GeometryId : std::size_t {};
enum class InstanceId : std::size_t {};
enum class PlacementId : std::size_t {};
enum class RecipeId : std::size_t {};
enum class OutputIndex : std::size_t {};

struct Geometry {
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
  GeometryId geometry{};
  RecipeKey key;
  std::vector<OutputPlacement> outputs;
  [[nodiscard]] bool operator==(const Placement &) const = default;
};

struct ActorState {
  std::vector<Geometry> geometries;
  std::vector<Instance> instances;
  std::vector<Placement> placements;
};

[[nodiscard]] const Geometry *GeometryAt(const ActorState &a_state,
                                         GeometryId a_geometry) noexcept;
[[nodiscard]] const Instance *InstanceAt(const ActorState &a_state,
                                         InstanceId a_instance) noexcept;
[[nodiscard]] const Placement *PlacementAt(const ActorState &a_state,
                                           PlacementId a_placement) noexcept;

[[nodiscard]] bool AnyLiveGeometry(const ActorState &a_state) noexcept;
[[nodiscard]] std::optional<InstanceId>
FindInstance(const ActorState &a_state, RecipeId a_recipe,
             const std::optional<FormKey> &a_enchantment) noexcept;
[[nodiscard]] std::vector<RecipeId>
RecipesOfInactiveInstances(const ActorState &a_state);
[[nodiscard]] std::vector<PlacementId>
PlacementsOfGeometry(const ActorState &a_state, GeometryId a_geometry);
[[nodiscard]] std::vector<PlacementId>
PlacementsOfInstance(const ActorState &a_state, InstanceId a_instance);

struct PieceMatch {
  std::size_t instance = 0;
  RecipeKey key;
  int priority = 0;
};

[[nodiscard]] std::vector<PieceMatch>
MatchesForPiece(const ActorState &a_state, GeometryId a_firstGeometry,
                std::size_t a_geomCount);
[[nodiscard]] std::optional<std::size_t>
PlacedIndexOf(const ActorState &a_state,
              std::span<const PlacementId> a_placements, InstanceId a_instance);
[[nodiscard]] std::optional<InstanceId>
InstanceOfPlaced(const ActorState &a_state,
                 std::span<const PlacementId> a_placements,
                 std::size_t a_placed);
[[nodiscard]] std::vector<GeometryId>
ThirdPersonGeometriesOfInstance(const ActorState &a_state,
                                InstanceId a_instance);
}
