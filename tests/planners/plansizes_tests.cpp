// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/PlanSizes.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
RenderPlan PlanReadingSide(SideRef a_side) {
  RenderPlan plan;
  RenderStep step{ComposeVectorStep{{}, {a_side}}, "compose"};
  step.contentKey = "compose";
  plan.steps.push_back(step);
  return plan;
}

void SizesMustCoverThePlan() {
  const RenderPlan plan = PlanReadingSide(SideRef{1});
  Check(!SizesFor(plan, {TextureSize{512}}),
        "a plan that names a side the list lacks is refused");
  Check(SizesFor(plan, {TextureSize{512}, TextureSize{256}}).has_value(),
        "a list that covers every named side is accepted");
}

void MissingSideFallsToTheMinimum() {
  const PlanSizes sizes{{TextureSize{512}}};
  Check(SizeOf(sizes, SideRef{0}) == TextureSize{512},
        "a side resolves to its size");
  Check(SizeOf(sizes, SideRef{7}) == TextureSize{TextureSize::kMin},
        "an unknown side resolves to the minimum");
}

void SharedKeysSeparateSizes() {
  const RenderPlan plan = PlanReadingSide(SideRef{0});
  const RenderStep &step = plan.steps.front();
  const PlanSizes full{{TextureSize{1024}}};
  const PlanSizes half{{TextureSize{512}}};
  Check(SharedKeyOf(step, full) == SharedKeyOf(step, full),
        "two instances at one size share a key");
  Check(SharedKeyOf(step, full) != SharedKeyOf(step, half),
        "two instances of one plan at different sizes never share an output");
  RenderStep unkeyed = step;
  unkeyed.contentKey.reset();
  Check(!SharedKeyOf(unkeyed, full), "a step without a content key is private");
}
}

int main() {
  SizesMustCoverThePlan();
  MissingSideFallsToTheMinimum();
  SharedKeysSeparateSizes();
  return test::Finish("planners_plansizes");
}
