// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/TextureSize.h"
#include "planners/PlanSizes.h"
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
  TextureSize size;
  TextureFormat format = TextureFormat::kRgba8;
  std::optional<std::string> sharedKey;
  MipPolicy mips = MipPolicy::kGenerate;
  [[nodiscard]] bool operator==(const HeldTarget &) const = default;
};

struct PlanTargets {
  std::vector<HeldTarget> withIntermediates;
  std::vector<HeldTarget> stacks;
  [[nodiscard]] bool operator==(const PlanTargets &) const = default;
};

struct TargetCost {
  std::size_t targets = 0;
  std::uint64_t bytes = 0;
  std::map<std::uint32_t, std::size_t> targetsBySize;
  [[nodiscard]] bool operator==(const TargetCost &) const = default;
};

[[nodiscard]] std::uint64_t TargetBytes(TextureSize a_size,
                                        TextureFormat a_format,
                                        MipPolicy a_mips) noexcept;
[[nodiscard]] std::vector<RenderStepId> HeldSteps(const RenderPlan &a_plan);
[[nodiscard]] PlanTargets TargetsOf(const RenderPlan &a_plan,
                                    const PlanSizes &a_sizes);
[[nodiscard]] TargetCost CostOf(std::span<const HeldTarget> a_targets);
}
