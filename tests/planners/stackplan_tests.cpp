#include "planners/StackPlan.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  StackLink link;
  link.contribution = SlotContribution{SlotSource{0}, 0};
  link.selfAnimated = false;
  link.animated = false;

  SlotStackPlan slot;
  slot.surface = Surface::kMaterial;
  slot.slot = Slot::kEmissive;
  slot.chain.push_back(link);

  GeometryStackPlan plan;
  plan.slots.push_back(slot);

  Check(plan.slots.size() == 1,
        "a geometry stack plan holds a slot stack plan");
  Check(plan.slots.front().chain.size() == 1,
        "a slot stack plan holds a chain of links");

  Check(
      true,
      "TODO(planners fill): cover PlanStacks static-vs-animated "
      "classification, chained-base re-render, SlotStackPlanOf, ChainIndexOf");
  return test::Finish("planners stackplan");
}
