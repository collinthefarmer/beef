// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/TextureSize.h"
#include "planners/RenderPlan.h"

#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
struct PlanSizes {
  std::vector<TextureSize> sizes;
  [[nodiscard]] bool operator==(const PlanSizes &) const = default;
};

[[nodiscard]] std::expected<PlanSizes, std::string>
SizesFor(const RenderPlan &a_plan, std::vector<TextureSize> a_sizes);
[[nodiscard]] TextureSize SizeOf(const PlanSizes &a_sizes,
                                 SideRef a_side) noexcept;
[[nodiscard]] std::optional<std::string> SharedKeyOf(const RenderStep &a_step,
                                                     const PlanSizes &a_sizes);
}
