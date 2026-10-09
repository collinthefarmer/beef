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

void Add(TargetCost &a_cost, const HeldTarget &a_target) {
  ++a_cost.targets;
  a_cost.bytes += TargetBytes(a_target.size, a_target.format, a_target.mips);
  ++a_cost.targetsBySize[a_target.size.Pixels()];
}
}

bool KeptWhenReleased(const RenderStepKind &a_kind) noexcept {
  return Is<CompositeStackStep>(a_kind) || Is<BakeMeshStep>(a_kind);
}

std::uint64_t TargetBytes(TextureSize a_size, TextureFormat a_format,
                          MipPolicy a_mips) noexcept {
  const auto pixels = static_cast<std::uint64_t>(a_size.Pixels());
  const std::uint64_t level = pixels * pixels * BytesPerTexel(a_format);
  return a_mips == MipPolicy::kGenerate ? level * 4 / 3 : level;
}

std::vector<RenderStepId> HeldSteps(const RenderPlan &a_plan) {
  const std::vector<bool> reached = ReachedSteps(a_plan);
  std::vector<RenderStepId> held;
  for (RenderStepId step = 0; step < a_plan.steps.size(); ++step) {
    const RenderStepKind &kind = a_plan.steps[step].kind;
    if (reached[step] && RequirementsOf(kind) && Draws(kind)) {
      held.push_back(step);
    }
  }
  return held;
}

PlanTargets TargetsOf(const RenderPlan &a_plan, const PlanSizes &a_sizes) {
  PlanTargets targets;
  for (const RenderStepId step : HeldSteps(a_plan)) {
    const RenderStep &planned = a_plan.steps[step];
    const std::optional<TextureRequirements> requirements =
        RequirementsOf(planned.kind);
    if (!requirements) {
      continue;
    }
    const HeldTarget target{SizeOf(a_sizes, requirements->side),
                            requirements->format, SharedKeyOf(planned, a_sizes),
                            requirements->mipPolicy};
    targets.withIntermediates.push_back(target);
    if (Is<CompositeStackStep>(planned.kind)) {
      targets.stacks.push_back(target);
    }
    if (KeptWhenReleased(planned.kind)) {
      targets.whenReleased.push_back(target);
    }
  }
  return targets;
}

TargetCost CostOf(std::span<const HeldTarget> a_targets) {
  std::set<std::string> counted;
  TargetCost cost;
  for (const HeldTarget &target : a_targets) {
    if (!target.sharedKey || counted.insert(*target.sharedKey).second) {
      Add(cost, target);
    }
  }
  return cost;
}
}
