// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/ActorMatches.h"
#include "planners/IdentityFacts.h"
#include "planners/RenderPlan.h"
#include "planners/TextureDemand.h"
#include "recipe/Recipe.h"
#include "recipe/RecipeGraph.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
struct PlanStackRequest {
  std::shared_ptr<const RecipeGraph> graph;
  std::shared_ptr<const SurfaceOutput> output;
  std::string recipe;
  std::size_t outputIndex = 0;
  PlacementId placement{};
  GeometryId geometry{};
  std::size_t context = 0;
  SideRef side;
  std::shared_ptr<const IdentityFacts> identity;
};

struct PlanBuildInput {
  std::vector<PlanStackRequest> requests;
  std::vector<SlotChainRequest> chains;
};

struct PlanBuildPhases {
  std::uint64_t demandUs = 0;
  std::uint64_t lowerUs = 0;
  std::size_t demands = 0;
};

struct PlanBuildOutput {
  std::expected<RenderPlan, std::string> plan;
  std::vector<std::string> warnings;
  PlanBuildPhases phases;
};

[[nodiscard]] PlanBuildOutput BuildPlanFrom(const PlanBuildInput &a_input);
}
