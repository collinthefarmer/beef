// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ActorPlanning.h"

#include "Core.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
[[nodiscard]] std::optional<RecipeId>
RecipeIdOf(std::span<const Recipe> a_store, const Recipe *a_recipe) noexcept {
  if (!a_recipe || a_store.empty()) {
    return std::nullopt;
  }
  const Recipe *first = a_store.data();
  if (a_recipe < first || a_recipe >= first + a_store.size()) {
    return std::nullopt;
  }
  return RecipeId{IndexOf(a_store, a_recipe)};
}

[[nodiscard]] const Recipe *RecipeAt(std::span<const Recipe> a_store,
                                     RecipeId a_recipe) noexcept {
  const std::size_t index = IndexOf(a_recipe);
  if (index >= a_store.size()) {
    return nullptr;
  }
  return &a_store[index];
}

[[nodiscard]] std::vector<OutputPlacement>
SurfacePlacements(const Recipe &a_recipe, const GeometryIdentity &a_identity) {
  std::vector<OutputPlacement> out;
  for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
    const SurfaceOutput *output = Get<SurfaceOutput>(a_recipe.outputs[i]);
    if (!output) {
      continue;
    }
    const bool selected = Matches(output->selector, a_identity);
    out.push_back(OutputPlacement{OutputId{i}, selected,
                                  selected ? std::string{}
                                           : std::string{"selector did not "
                                                         "match"}});
  }
  return out;
}

bool LightSelected(const Recipe &a_recipe, const Geometry &a_geometry,
                   const OutputFilter &a_filter) {
  for (std::size_t output = 0; output < a_recipe.outputs.size(); ++output) {
    const LightOutput *light = Get<LightOutput>(a_recipe.outputs[output]);
    if (light && LightEligible(a_geometry, *light) &&
        (!a_filter || a_filter(a_recipe, output))) {
      return true;
    }
  }
  return false;
}

std::optional<int> EligibleLightPriority(const ActorPlan &a_plan,
                                         InstanceId a_instance,
                                         const Recipe &a_recipe,
                                         const OutputFilter &a_filter) {
  std::optional<int> priority;
  for (const PlacementId id : PlacementsOfInstance(a_plan, a_instance)) {
    const Placement *placement = PlacementAt(a_plan, id);
    const Geometry *geometry =
        placement ? GeometryAt(a_plan, placement->geometry) : nullptr;
    if (geometry && LightSelected(a_recipe, *geometry, a_filter)) {
      priority = priority ? std::max(*priority, placement->priority)
                          : placement->priority;
    }
  }
  return priority;
}

[[nodiscard]] InstanceId
InstanceFor(ActorPlan &a_plan, RecipeId a_recipe,
            const std::optional<FormKey> &a_enchantment) {
  if (const std::optional<InstanceId> existing =
          FindInstance(a_plan, a_recipe, a_enchantment)) {
    return *existing;
  }
  a_plan.instances.push_back(Instance{a_recipe, a_enchantment});
  return InstanceId{a_plan.instances.size() - 1};
}
}

ActorPlan MatchActor(std::span<const Geometry> a_geometries,
                     std::span<const Recipe> a_store,
                     const RecipeResolver &a_resolver) {
  ActorPlan plan;
  plan.geometries.assign(a_geometries.begin(), a_geometries.end());
  for (std::size_t p = 0; p < plan.geometries.size(); ++p) {
    const Geometry &geometry = plan.geometries[p];
    for (const ResolvedRecipe &resolved : a_resolver(geometry, GeometryId{p})) {
      const std::optional<RecipeId> recipe =
          RecipeIdOf(a_store, resolved.recipe);
      if (!recipe) {
        continue;
      }
      const InstanceId instance =
          InstanceFor(plan, *recipe, geometry.keys.enchantment);
      Placement placement;
      placement.instance = instance;
      placement.geometry = GeometryId{p};
      placement.key = resolved.key;
      placement.priority = resolved.priority;
      placement.outputs =
          SurfacePlacements(*resolved.recipe, geometry.identity);
      plan.placements.push_back(std::move(placement));
    }
  }
  return plan;
}

ActorPlan MatchActor(std::span<const Geometry> a_geometries,
                     std::span<const Recipe> a_store) {
  return MatchActor(a_geometries, a_store,
                    [a_store](const Geometry &a_geometry, GeometryId) {
                      return Resolve(a_geometry.keys, a_store);
                    });
}

GeometryPlacementPlan PlanGeometryPlacement(const ActorPlan &a_plan,
                                            std::span<const Recipe> a_store,
                                            GeometryId a_geometry,
                                            const OutputFilter &a_filter) {
  GeometryPlacementPlan out;
  for (const PlacementId placementId :
       PlacementsOfGeometry(a_plan, a_geometry)) {
    const Placement *placement = PlacementAt(a_plan, placementId);
    if (!placement) {
      continue;
    }
    const Instance *instance = InstanceAt(a_plan, placement->instance);
    if (!instance) {
      continue;
    }
    const Recipe *recipe = RecipeAt(a_store, instance->recipe);
    if (!recipe) {
      continue;
    }
    PlacedRecipe row;
    row.recipe = recipe;
    row.priority = placement->priority;
    row.loadOrder = IndexOf(instance->recipe);
    for (const OutputPlacement &output : placement->outputs) {
      if (output.selected &&
          (!a_filter || a_filter(*recipe, IndexOf(output.output)))) {
        row.outputs.push_back(IndexOf(output.output));
      }
    }
    out.placed.push_back(std::move(row));
    out.sources.push_back(placementId);
  }
  out.plan = PlanGeometry(out.placed);
  return out;
}

ActorLightPlan PlanActorLights(const ActorPlan &a_plan,
                               std::span<const Recipe> a_store,
                               const OutputFilter &a_filter) {
  ActorLightPlan out;
  for (std::size_t i = 0; i < a_plan.instances.size(); ++i) {
    const Instance &instance = a_plan.instances[i];
    const Recipe *recipe = RecipeAt(a_store, instance.recipe);
    const std::optional<int> priority =
        recipe ? EligibleLightPriority(a_plan, InstanceId{i}, *recipe, a_filter)
               : std::nullopt;
    out.placed.push_back(PlacedRecipe{priority ? recipe : nullptr,
                                      priority.value_or(0),
                                      {},
                                      IndexOf(instance.recipe)});
    out.sources.push_back(InstanceId{i});
  }
  out.plan = PlanLights(out.placed, a_filter);
  return out;
}
}
