// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/PlanSides.h"

#include <format>

namespace BetterEnchantmentEffects {
namespace {
std::vector<SideRef> SidesNamedBy(const RenderStep &a_step) {
  std::vector<SideRef> sides;
  if (const std::optional<TextureRequirements> requirements =
          RequirementsOf(a_step.kind)) {
    sides.push_back(requirements->side);
  }
  if (const auto *reduction = Get<SubmitReductionStep>(a_step.kind)) {
    sides.push_back(reduction->domain);
  }
  if (a_step.producedKey) {
    sides.push_back(a_step.producedKey->requirements.side);
  }
  return sides;
}
}

std::expected<PlanSides, std::string>
SidesFor(const RenderPlan &a_plan, std::vector<TextureSize> a_pixels) {
  for (const RenderStep &step : a_plan.steps) {
    for (const SideRef side : SidesNamedBy(step)) {
      if (side.index >= a_pixels.size()) {
        return std::unexpected(std::format("{} names side {} of {}",
                                           step.displayName, side.index,
                                           a_pixels.size()));
      }
    }
  }
  return PlanSides{std::move(a_pixels)};
}

TextureSize SideOf(const PlanSides &a_sides, SideRef a_side) noexcept {
  return a_side.index < a_sides.pixels.size() ? a_sides.pixels[a_side.index]
                                              : TextureSize{TextureSize::kMin};
}

std::optional<std::string> SharedKeyOf(const RenderStep &a_step,
                                       const PlanSides &a_sides) {
  if (!a_step.contentKey) {
    return std::nullopt;
  }
  std::string key;
  for (const SideRef side : SidesNamedBy(a_step)) {
    key += std::format("{}|", SideOf(a_sides, side).Pixels());
  }
  key += *a_step.contentKey;
  if (key.size() > kMaxCacheKeyBytes) {
    return std::nullopt;
  }
  return key;
}
}
