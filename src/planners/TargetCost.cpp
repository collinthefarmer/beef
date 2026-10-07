// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/TargetCost.h"

#include <optional>
#include <set>
#include <string>

namespace BetterEnchantmentEffects {
namespace {
std::optional<TextureRequirements> TargetOf(const RenderStep &a_step) {
  if (const auto *sample = Get<SampleFieldStep>(a_step.kind);
      sample && sample->direct) {
    return std::nullopt;
  }
  if (OutputType(a_step.kind) !=
      RenderValueType{RenderResourceType::kTexture}) {
    return std::nullopt;
  }
  return RequirementsOf(a_step.kind);
}

void Add(TargetCost &a_cost, const TextureRequirements &a_requirements) {
  ++a_cost.targets;
  a_cost.bytes += TargetBytes(a_requirements);
  ++a_cost.targetsBySide[a_requirements.size.Pixels()];
}

std::set<std::string> ContentKeysOf(const RenderPlan &a_plan) {
  std::set<std::string> keys;
  for (const RenderStep &step : a_plan.steps) {
    if (step.contentKey && TargetOf(step)) {
      keys.insert(*step.contentKey);
    }
  }
  return keys;
}
}

std::uint64_t TargetBytes(const TextureRequirements &a_requirements) noexcept {
  const auto side = static_cast<std::uint64_t>(a_requirements.size.Pixels());
  const std::uint64_t mipped = side * side * 4 * 4 / 3;
  return a_requirements.format == TextureFormat::kRgba32Float ? mipped * 4
                                                              : mipped;
}

TargetCost TargetCostOf(const RenderPlan &a_plan) {
  TargetCost cost;
  for (const RenderStep &step : a_plan.steps) {
    if (const std::optional<TextureRequirements> target = TargetOf(step)) {
      Add(cost, *target);
    }
  }
  return cost;
}

TargetCost SharedTargetCostOf(const RenderPlan &a_first,
                              const RenderPlan &a_second) {
  const std::set<std::string> second = ContentKeysOf(a_second);
  std::set<std::string> counted;
  TargetCost cost;
  for (const RenderStep &step : a_first.steps) {
    const std::optional<TextureRequirements> target = TargetOf(step);
    if (target && step.contentKey && second.contains(*step.contentKey) &&
        counted.insert(*step.contentKey).second) {
      Add(cost, *target);
    }
  }
  return cost;
}
}
