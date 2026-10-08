// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/TextureSize.h"
#include "planners/RenderPlan.h"

#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
struct PlanSides {
  std::vector<TextureSize> pixels;
  [[nodiscard]] bool operator==(const PlanSides &) const = default;
};

[[nodiscard]] std::expected<PlanSides, std::string>
SidesFor(const RenderPlan &a_plan, std::vector<TextureSize> a_pixels);
[[nodiscard]] TextureSize SideOf(const PlanSides &a_sides,
                                 SideRef a_side) noexcept;
[[nodiscard]] std::optional<std::string> SharedKeyOf(const RenderStep &a_step,
                                                     const PlanSides &a_sides);
}
