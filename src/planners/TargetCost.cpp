// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/TargetCost.h"

#include <set>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
std::vector<bool> ReachedSteps(const RenderPlan &a_plan) {
  std::vector<bool> reached(a_plan.steps.size(), false);
  std::vector<RenderStepId> pending = RenderedRoots(a_plan);
  while (!pending.empty()) {
    const RenderStepId step = pending.back();
    pending.pop_back();
    if (step >= a_plan.steps.size() || reached[step]) {
      continue;
    }
    reached[step] = true;
    for (const RenderValueRef operand : StepDependencies(a_plan, step)) {
      if (const auto *output = Get<StepOutputRef>(operand)) {
        pending.push_back(output->step);
      }
    }
  }
  return reached;
}

std::vector<bool> ReadByAnimatedSteps(const RenderPlan &a_plan,
                                      const std::vector<bool> &a_reached,
                                      const std::vector<bool> &a_animated) {
  std::vector<bool> read(a_plan.steps.size(), false);
  for (RenderStepId step = 0; step < a_plan.steps.size(); ++step) {
    if (!a_reached[step] || step >= a_animated.size() || !a_animated[step]) {
      continue;
    }
    for (const RenderValueRef operand : StepDependencies(a_plan, step)) {
      const auto *output = Get<StepOutputRef>(operand);
      if (output && output->step < read.size()) {
        read[output->step] = true;
      }
    }
  }
  return read;
}

void Add(TargetCost &a_cost, const TextureRequirements &a_requirements) {
  ++a_cost.targets;
  a_cost.bytes += TargetBytes(a_requirements);
  ++a_cost.targetsBySide[a_requirements.size.Pixels()];
}
}

std::uint64_t TargetBytes(const TextureRequirements &a_requirements) noexcept {
  const auto side = static_cast<std::uint64_t>(a_requirements.size.Pixels());
  const std::uint64_t mipped = side * side * 4 * 4 / 3;
  return a_requirements.format == TextureFormat::kRgba32Float ? mipped * 4
                                                              : mipped;
}

PlanTargets TargetsOf(const RenderPlan &a_plan) {
  const std::vector<bool> reached = ReachedSteps(a_plan);
  const std::vector<bool> animated = AnimatedSteps(a_plan);
  const std::vector<bool> read = ReadByAnimatedSteps(a_plan, reached, animated);
  PlanTargets targets;
  for (RenderStepId step = 0; step < a_plan.steps.size(); ++step) {
    const RenderStep &planned = a_plan.steps[step];
    const std::optional<TextureRequirements> requirements =
        RequirementsOf(planned.kind);
    if (!reached[step] || !requirements || !Draws(planned.kind)) {
      continue;
    }
    const HeldTarget held{*requirements, planned.contentKey};
    const bool stack = Is<CompositeStackStep>(planned.kind);
    const bool live = step < animated.size() && animated[step];
    targets.firstRender.push_back(held);
    if (stack || live || read[step]) {
      targets.rendering.push_back(held);
    }
  }
  return targets;
}

TargetCost CostOf(std::span<const HeldTarget> a_targets) {
  std::set<std::string> counted;
  TargetCost cost;
  for (const HeldTarget &target : a_targets) {
    if (!target.contentKey || counted.insert(*target.contentKey).second) {
      Add(cost, target.requirements);
    }
  }
  return cost;
}
}
