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
}
