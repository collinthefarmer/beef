#include "planners/ActorPlan.h"

#include <algorithm>

namespace BetterEnchantmentEffects {
namespace {
template <typename Row, typename Handle>
[[nodiscard]] const Row *RowAt(const std::vector<Row> &a_rows,
                               Handle a_handle) noexcept {
  const std::size_t index = IndexOf(a_handle);
  return index < a_rows.size() ? &a_rows[index] : nullptr;
}

template <typename Match>
[[nodiscard]] std::vector<PlacementId>
PlacementIdsWhere(const ActorPlan &a_plan, Match a_match) {
  std::vector<PlacementId> found;
  for (std::size_t i = 0; i < a_plan.placements.size(); ++i) {
    if (a_match(a_plan.placements[i])) {
      found.push_back(PlacementId{i});
    }
  }
  return found;
}
}

const Geometry *GeometryAt(const ActorPlan &a_plan,
                           GeometryId a_geometry) noexcept {
  return RowAt(a_plan.geometries, a_geometry);
}

const Instance *InstanceAt(const ActorPlan &a_plan,
                           InstanceId a_instance) noexcept {
  return RowAt(a_plan.instances, a_instance);
}

const Placement *PlacementAt(const ActorPlan &a_plan,
                             PlacementId a_placement) noexcept {
  return RowAt(a_plan.placements, a_placement);
}

bool AnyLiveGeometry(const ActorPlan &a_plan) noexcept {
  return std::any_of(
      a_plan.geometries.begin(), a_plan.geometries.end(),
      [](const Geometry &a_geometry) { return !a_geometry.lost; });
}

std::optional<InstanceId>
FindInstance(const ActorPlan &a_plan, RecipeId a_recipe,
             const std::optional<FormKey> &a_enchantment) noexcept {
  for (std::size_t i = 0; i < a_plan.instances.size(); ++i) {
    const Instance &instance = a_plan.instances[i];
    if (instance.recipe == a_recipe && instance.enchantment == a_enchantment) {
      return InstanceId{i};
    }
  }
  return std::nullopt;
}

std::vector<PlacementId> PlacementsOfGeometry(const ActorPlan &a_plan,
                                              GeometryId a_geometry) {
  return PlacementIdsWhere(a_plan, [&](const Placement &a_placement) {
    return a_placement.geometry == a_geometry;
  });
}

std::vector<PlacementId> PlacementsOfInstance(const ActorPlan &a_plan,
                                              InstanceId a_instance) {
  return PlacementIdsWhere(a_plan, [&](const Placement &a_placement) {
    return a_placement.instance == a_instance;
  });
}

std::vector<PieceMatch> MatchesForPiece(const ActorPlan &a_plan,
                                        GeometryId a_firstGeometry,
                                        std::size_t a_geomCount) {
  const std::size_t first = IndexOf(a_firstGeometry);
  std::vector<PieceMatch> out;
  for (const Placement &placement : a_plan.placements) {
    const std::size_t flat = IndexOf(placement.geometry);
    if (flat < first || flat - first >= a_geomCount) {
      continue;
    }
    const std::size_t instance = IndexOf(placement.instance);
    if (std::ranges::any_of(out, [&](const PieceMatch &a_match) {
          return a_match.instance == instance;
        })) {
      continue;
    }
    const int priority = instance < a_plan.instances.size()
                             ? a_plan.instances[instance].priority
                             : 0;
    out.push_back(PieceMatch{instance, placement.key, priority});
  }
  return out;
}

std::optional<std::size_t>
PlacedIndexOf(const ActorPlan &a_plan,
              std::span<const PlacementId> a_placements,
              InstanceId a_instance) {
  for (std::size_t i = 0; i < a_placements.size(); ++i) {
    const std::size_t k = IndexOf(a_placements[i]);
    if (k < a_plan.placements.size() &&
        a_plan.placements[k].instance == a_instance) {
      return i;
    }
  }
  return std::nullopt;
}

std::optional<InstanceId>
InstanceOfPlaced(const ActorPlan &a_plan,
                 std::span<const PlacementId> a_placements,
                 std::size_t a_placed) {
  if (a_placed >= a_placements.size()) {
    return std::nullopt;
  }
  const Placement *placement = PlacementAt(a_plan, a_placements[a_placed]);
  return placement ? std::optional<InstanceId>{placement->instance}
                   : std::nullopt;
}

std::vector<GeometryId> ThirdPersonGeometriesOfInstance(const ActorPlan &a_plan,
                                                        InstanceId a_instance) {
  std::vector<GeometryId> found;
  for (const Placement &placement : a_plan.placements) {
    if (placement.instance != a_instance) {
      continue;
    }
    const std::size_t flat = IndexOf(placement.geometry);
    if (flat >= a_plan.geometries.size() ||
        a_plan.geometries[flat].firstPerson) {
      continue;
    }
    found.push_back(placement.geometry);
  }
  return found;
}

std::vector<RecipeId> RecipesOfInactiveInstances(const ActorPlan &a_plan) {
  std::vector<RecipeId> out;
  for (std::size_t i = 0; i < a_plan.instances.size(); ++i) {
    bool live = false;
    for (const PlacementId placementId :
         PlacementsOfInstance(a_plan, InstanceId{i})) {
      const Placement *placement = PlacementAt(a_plan, placementId);
      if (!placement) {
        continue;
      }
      const Geometry *geometry = GeometryAt(a_plan, placement->geometry);
      if (geometry && !geometry->lost) {
        live = true;
        break;
      }
    }
    if (!live) {
      out.push_back(a_plan.instances[i].recipe);
    }
  }
  return out;
}

const Variant *InstanceVariant(const ActorPlan &a_plan, InstanceId a_instance,
                               const Recipe &a_recipe) {
  for (const Variant &variant : a_recipe.variants) {
    for (const Placement &placement : a_plan.placements) {
      if (placement.instance != a_instance) {
        continue;
      }
      const Geometry *geometry = GeometryAt(a_plan, placement.geometry);
      if (!geometry) {
        continue;
      }
      if (geometry->keys.armor &&
          VariantApplies(variant, *geometry->keys.armor)) {
        return &variant;
      }
      if (VariantApplies(variant, geometry->identity)) {
        return &variant;
      }
    }
  }
  return nullptr;
}
}
