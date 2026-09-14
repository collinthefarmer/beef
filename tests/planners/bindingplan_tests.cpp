#include "planners/BindingPlan.h"
#include "test_support.h"
using namespace BetterEnchantmentEffects;
using test::Check;
int main() {
  std::vector<PlacedRecipe> placed(2);
  placed[0].priority = 3;
  placed[1].priority = 7;
  GeometryPlan geometry;
  Check(PlanBinding(placed, geometry) == BindingPlan{},
        "empty geometry needs no surfaces");
  SlotPlan material;
  material.surface = Surface::kMaterial;
  material.slot = Slot::kEmissive;
  material.chain = {{SlotContributor{0}, 0}};
  geometry.slots.push_back(material);
  auto plan = PlanBinding(placed, geometry);
  Check(plan.material && !plan.shell && !plan.shellOwner,
        "material contribution needs only a material binding");
  SlotPlan shell = material;
  shell.surface = Surface::kShell;
  geometry.slots.push_back(shell);
  shell.slot = Slot::kDiffuse;
  shell.chain = {{SlotContributor{1}, 0}};
  geometry.slots.push_back(shell);
  plan = PlanBinding(placed, geometry);
  Check(plan.material && plan.shell && plan.shellOwner == SlotContributor{1},
        "highest priority shell contribution controls shell settings");
  geometry.slots.back().chain.push_back({SlotContributor{0}, 1});
  Check(PlanBinding(placed, geometry).shellOwner == SlotContributor{0},
        "owner is selected from the top of each chain");
  geometry.slots.clear();
  material.chain.clear();
  geometry.slots.push_back(material);
  Check(PlanBinding(placed, geometry) == BindingPlan{},
        "empty chains need no surface");
  geometry.slots.push_back(shell);
  Check(!PlanBinding({}, geometry).shellOwner,
        "invalid contributions do not invent an owner");
  return test::Finish("binding plan");
}
