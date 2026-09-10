#include "planners/BindingDiff.h"

#include <cstddef>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
[[nodiscard]] bool SlotListed(std::span<const Slot> a_slots,
                              Slot a_slot) noexcept {
  for (const Slot slot : a_slots) {
    if (slot == a_slot) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::optional<int>
PriorityOf(std::span<const PlacedRecipe> a_placed,
           SlotSource a_source) noexcept {
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

BindingDiff PlanBinding(std::span<const PlacedRecipe> a_placed,
                        const GeometryPlan &a_plan,
                        std::span<const Slot> a_wroteMaterial,
                        std::span<const Slot> a_wroteShell) {
  BindingDiff diff;
  for (const SlotPlan &slot : a_plan.slots) {
    if (slot.chain.empty()) {
      continue;
    }
    if (slot.surface == Surface::kMaterial) {
      diff.material = true;
      diff.materialSlots.push_back(slot.slot);
    } else {
      diff.shell = true;
      diff.shellSlots.push_back(slot.slot);
    }
  }
  const std::optional<SlotContribution> top = ShellTop(a_placed, a_plan);
  if (top) {
    diff.shellOwner = top->placed;
  }
  for (const Slot slot : a_wroteMaterial) {
    if (!SlotListed(diff.materialSlots, slot)) {
      diff.restoreMaterial.push_back(slot);
    }
  }
  for (const Slot slot : a_wroteShell) {
    if (!SlotListed(diff.shellSlots, slot)) {
      diff.restoreShell.push_back(slot);
    }
  }
  return diff;
}
}
