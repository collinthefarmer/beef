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
enum class OutputId : std::size_t {};

struct Geometry {
  GeometryIdentity identity;
  WornPiece keys;
  bool firstPerson = false;
  bool lost = false;
};

struct Instance {
  RecipeId recipe{};
  std::optional<FormKey> enchantment;
  [[nodiscard]] bool operator==(const Instance &) const = default;
};

struct OutputPlacement {
  OutputId output{};
  bool selected = false;
  std::string problem;
  [[nodiscard]] bool operator==(const OutputPlacement &) const = default;
};

struct Placement {
  InstanceId instance{};
  GeometryId geometry{};
  RecipeKey key;
  std::vector<OutputPlacement> outputs;
  int priority = 0;
  [[nodiscard]] bool operator==(const Placement &) const = default;
};

struct ActorPlan {
  std::vector<Geometry> geometries;
  std::vector<Instance> instances;
  std::vector<Placement> placements;
};

[[nodiscard]] const Geometry *GeometryAt(const ActorPlan &a_plan,
                                         GeometryId a_geometry) noexcept;
[[nodiscard]] const Instance *InstanceAt(const ActorPlan &a_plan,
                                         InstanceId a_instance) noexcept;
[[nodiscard]] const Placement *PlacementAt(const ActorPlan &a_plan,
                                           PlacementId a_placement) noexcept;

[[nodiscard]] bool LightEligible(const Geometry &a_geometry,
                                 const LightOutput &a_light);

[[nodiscard]] bool AnyLiveGeometry(const ActorPlan &a_plan) noexcept;
[[nodiscard]] std::optional<InstanceId>
FindInstance(const ActorPlan &a_plan, RecipeId a_recipe,
             const std::optional<FormKey> &a_enchantment) noexcept;
[[nodiscard]] std::vector<RecipeId>
RecipesOfInactiveInstances(const ActorPlan &a_plan);
[[nodiscard]] std::vector<PlacementId>
PlacementsOfGeometry(const ActorPlan &a_plan, GeometryId a_geometry);
[[nodiscard]] std::vector<PlacementId>
PlacementsOfInstance(const ActorPlan &a_plan, InstanceId a_instance);

struct PieceMatch {
  std::size_t instance = 0;
  RecipeKey key;
  int priority = 0;
};

[[nodiscard]] std::vector<PieceMatch>
MatchesForPiece(const ActorPlan &a_plan, GeometryId a_firstGeometry,
                std::size_t a_geomCount);
[[nodiscard]] std::optional<std::size_t>
PlacedIndexOf(const ActorPlan &a_plan,
              std::span<const PlacementId> a_placements, InstanceId a_instance);
[[nodiscard]] std::optional<InstanceId>
InstanceOfPlaced(const ActorPlan &a_plan,
                 std::span<const PlacementId> a_placements,
                 std::size_t a_placed);
[[nodiscard]] std::vector<GeometryId>
ThirdPersonGeometriesOfInstance(const ActorPlan &a_plan, InstanceId a_instance);
[[nodiscard]] const Variant *InstanceVariant(const ActorPlan &a_plan,
                                             InstanceId a_instance,
                                             const Recipe &a_recipe);
}
