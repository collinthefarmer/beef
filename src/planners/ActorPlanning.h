#pragma once

#include "planners/ActorPlan.h"
#include "recipe/Merge.h"
#include "recipe/Recipe.h"

#include <functional>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects {
struct GeometryPlacementPlan {
  std::vector<PlacedRecipe> placed;
  std::vector<PlacementId> sources;
  GeometryPlan plan;
};

struct ActorLightPlan {
  std::vector<PlacedRecipe> placed;
  std::vector<InstanceId> sources;
  LightPlan plan;
};

using RecipeResolver =
    std::function<std::vector<ResolvedRecipe>(const Geometry &, GeometryId)>;

[[nodiscard]] ActorPlan MatchActor(std::span<const Geometry> a_geometries,
                                   std::span<const Recipe> a_store);
[[nodiscard]] ActorPlan MatchActor(std::span<const Geometry> a_geometries,
                                   std::span<const Recipe> a_store,
                                   const RecipeResolver &a_resolver);
[[nodiscard]] GeometryPlacementPlan
PlanGeometryPlacement(const ActorPlan &a_plan, std::span<const Recipe> a_store,
                      GeometryId a_geometry, const OutputFilter &a_filter = {});
[[nodiscard]] ActorLightPlan PlanActorLights(const ActorPlan &a_plan,
                                             std::span<const Recipe> a_store,
                                             const OutputFilter &a_filter = {});
}
