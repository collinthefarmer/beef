#pragma once

#include "recipe/Merge.h"
#include "recipe/Recipe.h"

#include <optional>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects {
struct BindingDiff {
  bool material = false;
  bool shell = false;
  std::optional<SlotSource> shellOwner;
  std::vector<Slot> materialSlots;
  std::vector<Slot> shellSlots;
  std::vector<Slot> restoreMaterial;
  std::vector<Slot> restoreShell;
  [[nodiscard]] bool operator==(const BindingDiff &) const = default;
};

[[nodiscard]] BindingDiff PlanBinding(std::span<const PlacedRecipe> a_placed,
                                      const GeometryPlan &a_plan,
                                      std::span<const Slot> a_wroteMaterial,
                                      std::span<const Slot> a_wroteShell);
}
