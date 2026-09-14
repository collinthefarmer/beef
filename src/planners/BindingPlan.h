#pragma once

#include "recipe/Merge.h"
#include "recipe/Recipe.h"

#include <optional>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects {
struct BindingPlan {
  bool material = false;
  bool shell = false;
  std::optional<SlotContributor> shellOwner;
  [[nodiscard]] bool operator==(const BindingPlan &) const = default;
};

[[nodiscard]] BindingPlan PlanBinding(std::span<const PlacedRecipe> a_placed,
                                      const GeometryPlan &a_plan);
}
