// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/TextureSize.h"
#include "planners/PlanSides.h"
#include "planners/RenderPlan.h"
#include "planners/TextureDemand.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
struct HeldTarget {
  TextureSize side;
  TextureFormat format = TextureFormat::kRgba8;
  std::optional<std::string> sharedKey;
  [[nodiscard]] bool operator==(const HeldTarget &) const = default;
};

struct PlanTargets {
  std::vector<HeldTarget> held;
  std::vector<HeldTarget> stacks;
  [[nodiscard]] bool operator==(const PlanTargets &) const = default;
};

struct TargetCost {
  std::size_t targets = 0;
  std::uint64_t bytes = 0;
  std::map<std::uint32_t, std::size_t> targetsBySide;
  [[nodiscard]] bool operator==(const TargetCost &) const = default;
};

[[nodiscard]] std::uint64_t TargetBytes(TextureSize a_side,
                                        TextureFormat a_format) noexcept;
[[nodiscard]] PlanTargets TargetsOf(const RenderPlan &a_plan,
                                    const PlanSides &a_sides);
[[nodiscard]] TargetCost CostOf(std::span<const HeldTarget> a_targets);
}
