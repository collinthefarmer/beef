// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/StackPlan.h"

#include "Core.h"
#include "recipe/Recipe.h"

namespace BetterEnchantmentEffects {
namespace {
bool LinkSelfAnimated(std::span<const PlacedRecipe> a_placed,
                      SlotContribution a_contribution) {
  const std::size_t placed = IndexOf(a_contribution.placed);
  if (placed >= a_placed.size()) {
    return false;
  }
  const Recipe *recipe = a_placed[placed].recipe;
  if (!recipe || a_contribution.output >= recipe->outputs.size()) {
    return false;
  }
  const SurfaceOutput *output =
      Get<SurfaceOutput>(recipe->outputs[a_contribution.output]);
  if (!output) {
    return false;
  }
  return IsAnimated(*recipe, Output{*output});
}

SlotStackPlan PlanSlot(std::span<const PlacedRecipe> a_placed,
                       const SlotPlan &a_slot) {
  SlotStackPlan plan;
  plan.surface = a_slot.surface;
  plan.slot = a_slot.slot;
  plan.chain.reserve(a_slot.chain.size());
  bool baseAnimated = false;
  for (const SlotContribution &contribution : a_slot.chain) {
    StackLink link;
    link.contribution = contribution;
    link.selfAnimated = LinkSelfAnimated(a_placed, contribution);
    link.animated = link.selfAnimated || baseAnimated;
    baseAnimated = link.animated;
    plan.chain.push_back(link);
  }
  plan.animated = baseAnimated;
  return plan;
}
}

GeometryStackPlan PlanStacks(std::span<const PlacedRecipe> a_placed,
                             const GeometryPlan &a_plan) {
  GeometryStackPlan plan;
  plan.slots.reserve(a_plan.slots.size());
  for (const SlotPlan &slot : a_plan.slots) {
    plan.slots.push_back(PlanSlot(a_placed, slot));
  }
  return plan;
}

const SlotStackPlan *SlotStackPlanOf(const GeometryStackPlan &a_plan,
                                     Surface a_surface, Slot a_slot) noexcept {
  for (const SlotStackPlan &slot : a_plan.slots) {
    if (slot.surface == a_surface && slot.slot == a_slot) {
      return &slot;
    }
  }
  return nullptr;
}

std::optional<std::size_t>
ChainIndexOf(const GeometryPlan &a_plan,
             SlotContribution a_contribution) noexcept {
  for (const SlotPlan &slot : a_plan.slots) {
    for (std::size_t i = 0; i < slot.chain.size(); ++i) {
      if (slot.chain[i] == a_contribution) {
        return i;
      }
    }
  }
  return std::nullopt;
}
}
