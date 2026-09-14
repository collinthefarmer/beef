#include "planners/BindingPlan.h"

#include <cstddef>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
[[nodiscard]] std::optional<int>
PriorityOf(std::span<const PlacedRecipe> a_placed,
           SlotContributor a_source) noexcept {
  const std::size_t index = static_cast<std::size_t>(a_source);
  if (index >= a_placed.size()) {
    return std::nullopt;
  }
  return a_placed[index].priority;
}

[[nodiscard]] std::optional<SlotContribution>
ShellTop(std::span<const PlacedRecipe> a_placed, const GeometryPlan &a_plan) {
  std::optional<SlotContribution> top;
  std::optional<int> topPriority;
  for (const SlotPlan &slot : a_plan.slots) {
    if (slot.surface != Surface::kShell || slot.chain.empty()) {
      continue;
    }
    const SlotContribution candidate = slot.chain.back();
    const std::optional<int> priority = PriorityOf(a_placed, candidate.placed);
    if (!priority) {
      continue;
    }
    if (!top || *priority > *topPriority) {
      top = candidate;
      topPriority = priority;
    }
  }
  return top;
}
}

BindingPlan PlanBinding(std::span<const PlacedRecipe> a_placed,
                        const GeometryPlan &a_plan) {
  BindingPlan plan;
  for (const SlotPlan &slot : a_plan.slots) {
    if (slot.chain.empty()) {
      continue;
    }
    if (slot.surface == Surface::kMaterial) {
      plan.material = true;
    } else {
      plan.shell = true;
    }
  }
  const std::optional<SlotContribution> top = ShellTop(a_placed, a_plan);
  if (top) {
    plan.shellOwner = top->placed;
  }
  return plan;
}
}
