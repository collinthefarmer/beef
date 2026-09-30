// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/RenderPlan.h"

namespace BetterEnchantmentEffects {
struct ProgramStep {
  InterpreterProgram program;
  std::vector<RenderValueRef> inputs;
  std::vector<RenderValueRef> lookups;
  TextureRequirements requirements;
};
struct FusedPlan {
  RenderPlan plan;
  std::size_t inlined = 0;
};
[[nodiscard]] std::optional<ProgramStep> AsProgram(const RenderPlan &plan,
                                                   const RenderStepKind &step);
[[nodiscard]] std::vector<bool> ChangingSteps(const RenderPlan &plan);
[[nodiscard]] std::vector<std::size_t> LiveConsumers(const RenderPlan &plan);
[[nodiscard]] FusedPlan FusePrograms(RenderPlan plan);
}
