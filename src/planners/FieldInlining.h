// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/RenderPlan.h"

namespace BetterEnchantmentEffects {
struct ProgramLikeStep {
  FieldProgram program;
  std::vector<RenderValueRef> inputs;
  std::vector<RenderValueRef> lookups;
  TextureRequirements requirements;
};
struct InlinedPlan {
  RenderPlan plan;
  std::size_t programsInlined = 0;
  std::size_t layerFieldReads = 0;
};

[[nodiscard]] std::optional<ProgramLikeStep>
AsProgramLike(const RenderPlan &plan, const RenderStepKind &step);
[[nodiscard]] InlinedPlan InlineFields(RenderPlan plan);
}
