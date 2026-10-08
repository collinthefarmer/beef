// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/PlanSizes.h"

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

std::expected<PlanSizes, std::string>
SizesFor(const RenderPlan &a_plan, std::vector<TextureSize> a_sizes) {
  for (const RenderStep &step : a_plan.steps) {
    for (const SideRef side : SidesNamedBy(step)) {
      if (side.index >= a_sizes.size()) {
        return std::unexpected(std::format("{} names side {} of {}",
                                           step.displayName, side.index,
                                           a_sizes.size()));
      }
    }
  }
  return PlanSizes{std::move(a_sizes)};
}

TextureSize SizeOf(const PlanSizes &a_sizes, SideRef a_side) noexcept {
  return a_side.index < a_sizes.sizes.size() ? a_sizes.sizes[a_side.index]
                                             : TextureSize{TextureSize::kMin};
}

std::optional<std::string> SharedKeyOf(const RenderStep &a_step,
                                       const PlanSizes &a_sizes) {
  if (!a_step.contentKey) {
    return std::nullopt;
  }
  std::string key;
  for (const SideRef side : SidesNamedBy(a_step)) {
    key += std::format("{}|", SizeOf(a_sizes, side).Pixels());
  }
  key += *a_step.contentKey;
  if (key.size() > kMaxCacheKeyBytes) {
    return std::nullopt;
  }
  return key;
}
}
