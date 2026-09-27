// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/StackPlan.h"
#include "recipe/Merge.h"
#include "recipe/Recipe.h"
#include "test_support.h"

#include <utility>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
SurfaceOutput EmissiveStatic(bool a_replace) {
  SurfaceOutput output;
  output.surface = Surface::kMaterial;
  output.slot = Slot::kEmissive;
  output.replace = a_replace;
  return output;
}

SurfaceOutput EmissiveAnimated(bool a_replace) {
  SurfaceOutput output;
  output.surface = Surface::kMaterial;
  output.slot = Slot::kEmissive;
  output.replace = a_replace;
  Layer layer;
  layer.opacity = Param{Ref{"wave"}};
  output.stack.push_back(std::move(layer));
  return output;
}

Recipe StaticRecipe() {
  Recipe recipe;
  recipe.outputs = {Output{EmissiveStatic(false)}};
  return recipe;
}

Recipe StaticReplaceRecipe() {
  Recipe recipe;
  recipe.outputs = {Output{EmissiveStatic(true)}};
  return recipe;
}

Recipe AnimatedRecipe() {
  Recipe recipe;
  recipe.signals.push_back(Signal{"wave", WaveSignal{}, std::nullopt});
  recipe.outputs = {Output{EmissiveAnimated(false)}};
  return recipe;
}
}

int main() {
  {
    const Recipe animated = AnimatedRecipe();
    Check(IsAnimated(animated, animated.outputs.front()),
          "the fixture's animated output reads a non-constant signal");
    const Recipe fixedStatic = StaticRecipe();
    Check(!IsAnimated(fixedStatic, fixedStatic.outputs.front()),
          "the fixture's static output reads nothing that animates");
  }

  {
    const Recipe fixedStatic = StaticRecipe();
    Check(ShareableAcrossActors(fixedStatic, fixedStatic.outputs.front()),
          "a static surface output over shared inputs is shareable");
    const Recipe animated = AnimatedRecipe();
    Check(!ShareableAcrossActors(animated, animated.outputs.front()),
          "an animated output is not shareable across actors");

    Recipe withBake = StaticRecipe();
    withBake.sources.push_back(Source{"pos", BakeSource{PositionBake{}}});
    Check(!ShareableAcrossActors(withBake, withBake.outputs.front()),
          "a recipe holding a bake source is not shareable (per-actor mesh)");

    Recipe withDistance = StaticRecipe();
    withDistance.sources.push_back(Source{"d", DistanceSource{}});
    Check(!ShareableAcrossActors(withDistance, withDistance.outputs.front()),
          "a recipe holding a distance source is not shareable (per-actor)");

    Recipe light;
    light.outputs = {Output{LightOutput{}}};
    Check(!ShareableAcrossActors(light, light.outputs.front()),
          "a light output is not a shareable surface stack");

    Recipe masks;
    masks.sources.push_back(
        Source{"roughness", MaterialSource{MaterialChannel::kRoughness}});
    masks.masks.push_back(Mask{"steady", "@roughness"});
    masks.masks.push_back(Mask{"moving", "time"});
    Check(ShareableAcrossActors(masks, masks.masks.front()),
          "a static mask over shared inputs is shareable");
    Check(!ShareableAcrossActors(masks, masks.masks.back()),
          "a mask that reads time is not shareable");
    Recipe invalid = masks;
    invalid.masks.front().text = "roughness";
    Check(!ShareableAcrossActors(invalid, invalid.masks.front()),
          "an invalid static mask cannot share a rendered result");
    Recipe maskWithBake = masks;
    maskWithBake.sources.push_back(Source{"pos", BakeSource{PositionBake{}}});
    Check(!ShareableAcrossActors(maskWithBake, maskWithBake.masks.front()),
          "a static mask is not shareable when the recipe holds a bake");
  }

  {
    const Recipe lower = StaticRecipe();
    const Recipe upper = AnimatedRecipe();
    const std::vector<PlacedRecipe> placed{
        PlacedRecipe{&lower, 5, {0}},
        PlacedRecipe{&upper, 10, {0}},
    };
    const GeometryPlan geometry = PlanGeometry(placed);
    const GeometryStackPlan stacks = PlanStacks(placed, geometry);

    const SlotStackPlan *emissive =
        SlotStackPlanOf(stacks, Surface::kMaterial, Slot::kEmissive);
    Check(emissive != nullptr,
          "an animated-over-static geometry has an emissive stack");
    Check(SlotStackPlanOf(stacks, Surface::kMaterial, Slot::kNormal) == nullptr,
          "a slot no recipe wrote has no stack plan");
    if (emissive) {
      Check(emissive->chain.size() == 2, "both contributions become links");
      Check(emissive->chain.front().contribution ==
                SlotContribution{SlotContributor{0}, 0},
            "the lower-priority static link leads the chain");
      Check(emissive->chain.back().contribution ==
                SlotContribution{SlotContributor{1}, 0},
            "the higher-priority animated link ends the chain");
      Check(!emissive->chain.front().selfAnimated,
            "the static link is not self-animated");
      Check(!emissive->chain.front().animated,
            "a static link over the static base does not animate");
      Check(emissive->chain.back().selfAnimated,
            "the animated link is self-animated");
      Check(emissive->chain.back().animated, "the animated link animates");
      Check(emissive->animated, "any animated link makes the slot animated");
    }
  }

  {
    const Recipe lower = AnimatedRecipe();
    const Recipe upper = StaticRecipe();
    const std::vector<PlacedRecipe> placed{
        PlacedRecipe{&lower, 5, {0}},
        PlacedRecipe{&upper, 10, {0}},
    };
    const GeometryPlan geometry = PlanGeometry(placed);
    const GeometryStackPlan stacks = PlanStacks(placed, geometry);
    const SlotStackPlan *emissive =
        SlotStackPlanOf(stacks, Surface::kMaterial, Slot::kEmissive);
    Check(emissive != nullptr,
          "a static-over-animated geometry has an emissive stack");
    if (emissive) {
      Check(emissive->chain.size() == 2, "both contributions become links");
      Check(emissive->chain.front().selfAnimated,
            "the lower animated link is self-animated");
      Check(emissive->chain.front().animated,
            "the lower animated link animates");
      Check(!emissive->chain.back().selfAnimated,
            "the upper static link is not self-animated");
      Check(emissive->chain.back().animated,
            "a static link over an animated base re-renders each tick");
      Check(emissive->animated, "the slot animates through its animated base");
    }
  }

  {
    const Recipe lower = StaticRecipe();
    const Recipe upper = StaticRecipe();
    const std::vector<PlacedRecipe> placed{
        PlacedRecipe{&lower, 5, {0}},
        PlacedRecipe{&upper, 10, {0}},
    };
    const GeometryPlan geometry = PlanGeometry(placed);
    const GeometryStackPlan stacks = PlanStacks(placed, geometry);
    const SlotStackPlan *emissive =
        SlotStackPlanOf(stacks, Surface::kMaterial, Slot::kEmissive);
    Check(emissive != nullptr, "an all-static geometry has an emissive stack");
    if (emissive) {
      Check(!emissive->chain.front().animated &&
                !emissive->chain.back().animated,
            "no link animates when nothing reads a signal or scroll");
      Check(!emissive->animated, "an all-static slot is static");
    }
  }

  {
    const Recipe low = AnimatedRecipe();
    const Recipe mid = StaticReplaceRecipe();
    const Recipe high = StaticRecipe();
    const std::vector<PlacedRecipe> placed{
        PlacedRecipe{&low, 1, {0}},
        PlacedRecipe{&mid, 2, {0}},
        PlacedRecipe{&high, 3, {0}},
    };
    const GeometryPlan geometry = PlanGeometry(placed);
    const GeometryStackPlan stacks = PlanStacks(placed, geometry);
    const SlotStackPlan *emissive =
        SlotStackPlanOf(stacks, Surface::kMaterial, Slot::kEmissive);
    Check(emissive != nullptr, "the replace scenario builds an emissive stack");
    if (emissive) {
      Check(emissive->chain.size() == 2,
            "the stack keeps only the replacer and everything above it");
      Check(emissive->chain.front().contribution ==
                SlotContribution{SlotContributor{1}, 0},
            "the replacing static contribution begins the surviving stack");
      Check(!emissive->chain.front().selfAnimated,
            "the replacer sees a fresh static base, not the replaced animated "
            "link");
      Check(!emissive->animated,
            "cutting the animated link below the replace leaves a static slot");
    }
  }

  {
    const Recipe lower = StaticRecipe();
    const Recipe upper = AnimatedRecipe();
    const std::vector<PlacedRecipe> placed{
        PlacedRecipe{&lower, 5, {0}},
        PlacedRecipe{&upper, 10, {0}},
    };
    const GeometryPlan geometry = PlanGeometry(placed);
    Check(ChainIndexOf(geometry, SlotContribution{SlotContributor{0}, 0}) ==
              std::optional<std::size_t>{0},
          "the lower contribution sits at chain index 0");
    Check(ChainIndexOf(geometry, SlotContribution{SlotContributor{1}, 0}) ==
              std::optional<std::size_t>{1},
          "the higher contribution sits at chain index 1");
    Check(ChainIndexOf(geometry, SlotContribution{SlotContributor{2}, 0}) ==
              std::nullopt,
          "a contribution no chain holds has no index");
  }

  return test::Finish("planners stackplan");
}
