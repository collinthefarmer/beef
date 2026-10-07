// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/RenderPlan.h"
#include "planners/TextureDemand.h"

#include <cstddef>
#include <cstdint>
#include <map>

namespace BetterEnchantmentEffects {
struct TargetCost {
  std::size_t targets = 0;
  std::uint64_t bytes = 0;
  std::map<std::uint32_t, std::size_t> targetsBySide;
  [[nodiscard]] bool operator==(const TargetCost &) const = default;
};

[[nodiscard]] std::uint64_t
TargetBytes(const TextureRequirements &a_requirements) noexcept;
[[nodiscard]] TargetCost TargetCostOf(const RenderPlan &a_plan);
[[nodiscard]] TargetCost SharedTargetCostOf(const RenderPlan &a_first,
                                            const RenderPlan &a_second);
}
