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

const Geometry *GeometryAt(const ActorState &a_state,
                           GeometryId a_geometry) noexcept {
  return RowAt(a_state.geometries, a_geometry);
}

const Instance *InstanceAt(const ActorState &a_state,
                           InstanceId a_instance) noexcept {
  return RowAt(a_state.instances, a_instance);
}

const Placement *PlacementAt(const ActorState &a_state,
                             PlacementId a_placement) noexcept {
  return RowAt(a_state.placements, a_placement);
}

bool AnyLiveGeometry(const ActorState &a_state) noexcept {
  return std::any_of(
      a_state.geometries.begin(), a_state.geometries.end(),
      [](const Geometry &a_geometry) { return !a_geometry.lost; });
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

std::vector<PlacementId> PlacementsOfGeometry(const ActorState &a_state,
                                              GeometryId a_geometry) {
  std::vector<PlacementId> found;
  for (std::size_t i = 0; i < a_state.placements.size(); ++i) {
    if (a_state.placements[i].geometry == a_geometry) {
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

std::vector<PieceMatch> MatchesForPiece(const ActorState &a_state,
                                        GeometryId a_firstGeometry,
                                        std::size_t a_geomCount) {
  const std::size_t first = static_cast<std::size_t>(a_firstGeometry);
  std::vector<PieceMatch> out;
  for (const Placement &placement : a_state.placements) {
    const std::size_t flat = static_cast<std::size_t>(placement.geometry);
    if (flat < first || flat - first >= a_geomCount) {
      continue;
    }
    const std::size_t instance = static_cast<std::size_t>(placement.instance);
    if (std::ranges::any_of(out, [&](const PieceMatch &a_match) {
          return a_match.instance == instance;
        })) {
      continue;
    }
    const int priority = instance < a_state.instances.size()
                             ? a_state.instances[instance].priority
                             : 0;
    out.push_back(PieceMatch{instance, placement.key, priority});
  }
  return out;
}

std::optional<std::size_t>
PlacedIndexOf(const ActorState &a_state,
              std::span<const PlacementId> a_placements,
              InstanceId a_instance) {
  for (std::size_t i = 0; i < a_placements.size(); ++i) {
    const std::size_t k = static_cast<std::size_t>(a_placements[i]);
    if (k < a_state.placements.size() &&
        a_state.placements[k].instance == a_instance) {
      return i;
    }
  }
  return std::nullopt;
}

std::optional<InstanceId>
InstanceOfPlaced(const ActorState &a_state,
                 std::span<const PlacementId> a_placements,
                 std::size_t a_placed) {
  if (a_placed >= a_placements.size()) {
    return std::nullopt;
  }
  const Placement *placement = PlacementAt(a_state, a_placements[a_placed]);
  return placement ? std::optional<InstanceId>{placement->instance}
                   : std::nullopt;
}

std::vector<GeometryId>
ThirdPersonGeometriesOfInstance(const ActorState &a_state,
                                InstanceId a_instance) {
  std::vector<GeometryId> found;
  for (const Placement &placement : a_state.placements) {
    if (placement.instance != a_instance) {
      continue;
    }
    const std::size_t flat = static_cast<std::size_t>(placement.geometry);
    if (flat >= a_state.geometries.size() ||
        a_state.geometries[flat].firstPerson) {
      continue;
    }
    found.push_back(placement.geometry);
  }
  return found;
}

std::vector<RecipeId> RecipesOfInactiveInstances(const ActorState &a_state) {
  std::vector<RecipeId> out;
  for (std::size_t i = 0; i < a_state.instances.size(); ++i) {
    bool live = false;
    for (const PlacementId placementId :
         PlacementsOfInstance(a_state, InstanceId{i})) {
      const Placement *placement = PlacementAt(a_state, placementId);
      if (!placement) {
        continue;
      }
      const Geometry *geometry = GeometryAt(a_state, placement->geometry);
      if (geometry && !geometry->lost) {
        live = true;
        break;
      }
    }
    if (!live) {
      out.push_back(a_state.instances[i].recipe);
    }
  }
  return out;
}
}
