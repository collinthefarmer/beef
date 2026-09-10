#pragma once

#include "recipe/Merge.h"
#include "recipe/Recipe.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects {
struct StackLink {
  SlotContribution contribution;
  bool selfAnimated = false;
  bool animated = false;
  [[nodiscard]] bool operator==(const StackLink &) const = default;
};

struct SlotStackPlan {
  Surface surface = Surface::kMaterial;
  Slot slot = Slot::kEmissive;
  std::vector<StackLink> chain;
  bool animated = false;
  [[nodiscard]] bool operator==(const SlotStackPlan &) const = default;
};

struct GeometryStackPlan {
  std::vector<SlotStackPlan> slots;
  [[nodiscard]] bool operator==(const GeometryStackPlan &) const = default;
};

[[nodiscard]] GeometryStackPlan
PlanStacks(std::span<const PlacedRecipe> a_placed, const GeometryPlan &a_plan);
[[nodiscard]] const SlotStackPlan *
SlotStackPlanOf(const GeometryStackPlan &a_plan, Surface a_surface,
                Slot a_slot) noexcept;
[[nodiscard]] std::optional<std::size_t>
ChainIndexOf(const GeometryPlan &a_plan,
             SlotContribution a_contribution) noexcept;
}
