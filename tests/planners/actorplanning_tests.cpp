#include "planners/ActorPlanning.h"
#include "test_support.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
Recipe GlowRecipe() {
  Recipe recipe;
  recipe.id = "glow";
  recipe.keys.push_back(RecipeKey{});
  SurfaceOutput surface;
  surface.surface = Surface::kMaterial;
  surface.slot = Slot::kEmissive;
  recipe.outputs.push_back(surface);
  return recipe;
}

Recipe RingRecipe() {
  Recipe recipe;
  recipe.id = "ring";
  recipe.keys.push_back(
      RecipeKey{KeyKind::kMaterial, KeyOperandValue{std::string{"*"}}});
  SurfaceOutput surface;
  surface.surface = Surface::kMaterial;
  surface.slot = Slot::kEmissive;
  surface.selector.anyOf.push_back(
      SelectorClause{SelectorKind::kGeometry, std::string{"*ring*"}});
  recipe.outputs.push_back(surface);
  recipe.outputs.push_back(LightOutput{});
  return recipe;
}

Geometry MakeGeometry(std::string a_name, std::string a_diffuse,
                      std::optional<FormKey> a_enchantment) {
  Geometry piece;
  piece.identity.name = std::move(a_name);
  piece.identity.diffusePath = a_diffuse;
  piece.keys.diffusePaths.push_back(std::move(a_diffuse));
  piece.keys.enchantment = std::move(a_enchantment);
  return piece;
}

std::size_t PlacementsForInstance(const ActorState &a_state,
                                  InstanceId a_instance) {
  std::size_t count = 0;
  for (const Placement &placement : a_state.placements) {
    if (placement.instance == a_instance) {
      ++count;
    }
  }
  return count;
}
}

int main() {
  const std::vector<Recipe> store{GlowRecipe(), RingRecipe()};
  const FormKey enchantment{"Test.esp", 0x123};
  const std::vector<Geometry> pieces{
      MakeGeometry("ring01", "ring.dds", std::nullopt),
      MakeGeometry("body01", "body.dds", std::nullopt),
      MakeGeometry("amulet01", "amulet.dds", enchantment)};

  ActorState state = MatchActor(pieces, store);

  Check(state.geometries.size() == 3, "MatchActor keeps the actor's pieces");
  Check(state.instances.size() == 4,
        "MatchActor instances at per-recipe-per-enchantment grain");
  Check(state.placements.size() == 6,
        "MatchActor makes one placement per piece per matched recipe");

  const auto unenchantedGlow = FindInstance(state, RecipeId{0}, std::nullopt);
  const auto enchantedGlow = FindInstance(state, RecipeId{0}, enchantment);
  Check(unenchantedGlow.has_value() && enchantedGlow.has_value() &&
            *unenchantedGlow != *enchantedGlow,
        "the same recipe splits into distinct instances by enchantment");
  Check(unenchantedGlow && PlacementsForInstance(state, *unenchantedGlow) == 2,
        "unenchanted pieces share one instance per recipe");

  GeometryPlacementPlan ring =
      PlanGeometryPlacement(state, store, GeometryId{0});
  Check(ring.placed.size() == 2 && ring.sources.size() == 2,
        "PlanGeometryPlacement builds a placed row per placement of the piece");
  Check(ring.plan.slots.size() == 1 && ring.plan.slots[0].chain.size() == 2,
        "PlanGeometryPlacement merges both selected outputs into the emissive "
        "slot");

  GeometryPlacementPlan body =
      PlanGeometryPlacement(state, store, GeometryId{1});
  const bool ringOutputDropped =
      std::ranges::any_of(body.placed, [](const PlacedRecipe &a_row) {
        return a_row.outputs.empty();
      });
  Check(ringOutputDropped,
        "PlanGeometryPlacement drops an output whose selector does not match");
  Check(body.plan.slots.size() == 1 && body.plan.slots[0].chain.size() == 1,
        "the unselected ring output does not reach the body's merge");

  bool sawSelectorProblem = false;
  for (const Placement &placement : state.placements) {
    if (placement.geometry != GeometryId{1}) {
      continue;
    }
    for (const OutputPlacement &output : placement.outputs) {
      if (!output.selected && output.problem == "selector did not match") {
        sawSelectorProblem = true;
      }
    }
  }
  Check(sawSelectorProblem,
        "MatchActor records why an output's selector did not match");

  ActorLightPlan lights = PlanActorLights(state, store);
  Check(lights.placed.size() == 4 && lights.sources.size() == 4,
        "PlanActorLights offers one light source per instance");
  Check(lights.plan.shown.size() == 2,
        "PlanActorLights shows a light only for instances of a light recipe");
  bool lightsMapToRingInstances = true;
  for (const LightContribution &shown : lights.plan.shown) {
    const std::size_t at = static_cast<std::size_t>(shown.placed);
    if (at >= lights.sources.size()) {
      lightsMapToRingInstances = false;
      continue;
    }
    const Instance *instance = InstanceAt(state, lights.sources[at]);
    if (!instance || instance->recipe != RecipeId{1}) {
      lightsMapToRingInstances = false;
    }
  }
  Check(lightsMapToRingInstances,
        "each shown light maps back to a ring-recipe instance");

  Check(RecipesOfInactiveInstances(state).empty(),
        "RecipesOfInactiveInstances is empty while every geometry is live");

  ActorState onePieceLost = state;
  onePieceLost.geometries[0].lost = true;
  Check(RecipesOfInactiveInstances(onePieceLost).empty(),
        "an instance is active while another live geometry still backs it");

  ActorState enchantedLost = state;
  enchantedLost.geometries[2].lost = true;
  const std::vector<RecipeId> inactiveRecipes =
      RecipesOfInactiveInstances(enchantedLost);
  Check(inactiveRecipes.size() == 2,
        "RecipesOfInactiveInstances reports recipes of instances with no live "
        "geometry");
  const bool reportsBothRecipes =
      std::ranges::find(inactiveRecipes, RecipeId{0}) !=
          inactiveRecipes.end() &&
      std::ranges::find(inactiveRecipes, RecipeId{1}) != inactiveRecipes.end();
  Check(reportsBothRecipes,
        "both inactive enchanted instances are reported by recipe");

  Recipe replacedRecipe = GlowRecipe();
  SurfaceOutput replacing = *Get<SurfaceOutput>(replacedRecipe.outputs.front());
  replacing.replace = true;
  replacedRecipe.outputs.push_back(replacing);
  const std::vector<Recipe> replacingStore{replacedRecipe};
  const ActorState replacingState = MatchActor(pieces, replacingStore);
  const GeometryPlacementPlan normal =
      PlanGeometryPlacement(replacingState, replacingStore, GeometryId{0});
  Check(normal.plan.slots.size() == 1 &&
            normal.plan.slots.front().chain.size() == 1 &&
            normal.plan.slots.front().chain.front().output == 1 &&
            normal.plan.slots.front().replaced.size() == 1,
        "normal planning records the earlier output as replaced");
  const OutputFilter onlyEarlier = [](const Recipe &, std::size_t a_output) {
    return a_output == 0;
  };
  const GeometryPlacementPlan solo = PlanGeometryPlacement(
      replacingState, replacingStore, GeometryId{0}, onlyEarlier);
  Check(solo.plan.slots.size() == 1 &&
            solo.plan.slots.front().chain.size() == 1 &&
            solo.plan.slots.front().chain.front().output == 0 &&
            solo.plan.slots.front().replaced.empty(),
        "output Solo is filtered before replacement and recovers the replaced "
        "output");
  const GeometryPlacementPlan restored =
      PlanGeometryPlacement(replacingState, replacingStore, GeometryId{0});
  Check(restored.plan.slots.front().chain == normal.plan.slots.front().chain &&
            restored.plan.slots.front().replaced ==
                normal.plan.slots.front().replaced,
        "unsolo restores the original replacement chain");
  const GeometryPlacementPlan none =
      PlanGeometryPlacement(replacingState, replacingStore, GeometryId{0},
                            [](const Recipe &, std::size_t) { return false; });
  Check(none.plan.slots.empty() && none.sources == normal.sources,
        "view filtering removes contributions without corrupting placement "
        "source mapping");

  Recipe lowerLight = GlowRecipe();
  lowerLight.id = "lowerLight";
  LightOutput additiveLight;
  additiveLight.replace = false;
  lowerLight.outputs = {additiveLight};
  lowerLight.priority = 1;
  Recipe upperLight = lowerLight;
  upperLight.id = "upperLight";
  upperLight.keys = {
      RecipeKey{KeyKind::kMaterial, KeyOperandValue{std::string{"*"}}}};
  upperLight.priority = 2;
  Get<LightOutput>(upperLight.outputs.front())->replace = true;
  const std::vector<Recipe> lightStore{lowerLight, upperLight};
  const ActorState lightState = MatchActor(pieces, lightStore);
  Check(FindInstance(lightState, RecipeId{0}, std::nullopt).has_value() &&
            FindInstance(lightState, RecipeId{1}, std::nullopt).has_value(),
        "light isolation fixture independently matches both recipes");
  const ActorLightPlan visibleLower = PlanActorLights(
      lightState, lightStore, [](const Recipe &a_recipe, std::size_t) {
        return a_recipe.id == "lowerLight";
      });
  Check(
      !visibleLower.plan.shown.empty() && visibleLower.plan.replaced.empty() &&
          std::ranges::all_of(visibleLower.plan.shown,
                              [&](const LightContribution &a_light) {
                                const std::size_t placed =
                                    static_cast<std::size_t>(a_light.placed);
                                return placed < visibleLower.placed.size() &&
                                       visibleLower.placed[placed].recipe &&
                                       visibleLower.placed[placed].recipe->id ==
                                           "lowerLight";
                              }),
      "light output isolation is also applied before replacement");

  Recipe saved = GlowRecipe();
  saved.masks.push_back(Mask{"scratch", "0.25"});
  Layer masked;
  masked.source = Vec3{1.0f, 1.0f, 1.0f};
  masked.mask = Ref{"scratch"};
  Get<SurfaceOutput>(saved.outputs.front())->stack.push_back(masked);
  Recipe scratch = saved;
  scratch.id = "paint";
  const std::vector<Recipe> savedStore{saved};
  const std::vector<Recipe> scratchStore{scratch};
  const auto savedState = MatchActor(pieces, savedStore);
  const auto scratchState = MatchActor(pieces, scratchStore);
  const auto savedPlan =
      PlanGeometryPlacement(savedState, savedStore, GeometryId{0});
  const auto scratchPlan =
      PlanGeometryPlacement(scratchState, scratchStore, GeometryId{0});
  Check(savedState.placements.size() == scratchState.placements.size() &&
            savedPlan.sources == scratchPlan.sources &&
            savedPlan.plan.slots.size() == 1 &&
            scratchPlan.plan.slots.size() == 1 &&
            savedPlan.plan.slots.front().chain ==
                scratchPlan.plan.slots.front().chain &&
            savedPlan.plan.slots.front().slot ==
                scratchPlan.plan.slots.front().slot,
        "identical saved and scratch recipe data follows the same matching and "
        "placement pipeline");

  return test::Finish("planners actorplanning");
}
