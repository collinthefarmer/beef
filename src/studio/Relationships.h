#pragma once

#include "recipe/Recipe.h"
#include "recipe/Visit.h"

#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct Relationship {
  PropertyLocation consumer;
  ResourceRef driver;
  [[nodiscard]] bool operator==(const Relationship &) const = default;
};
[[nodiscard]] std::vector<Relationship> RelationshipsOf(const Recipe &a_recipe);

struct CascadePlan {
  std::vector<ResourceRef> resources;
  std::vector<LayerOwner> layers;
  std::vector<Relationship> blocked;
};
[[nodiscard]] CascadePlan PlanCascade(const Recipe &a_recipe,
                                      const ResourceRef &a_seed);
}
