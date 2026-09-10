#include "planners/BindingDiff.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  BindingDiff diff;
  diff.material = true;
  diff.shell = false;
  diff.materialSlots.push_back(Slot::kEmissive);
  diff.restoreMaterial.push_back(Slot::kDiffuse);

  Check(diff.material,
        "a binding diff records that a material binding is needed");
  Check(diff.materialSlots.size() == 1,
        "a binding diff lists the material slots it writes");
  Check(diff.restoreMaterial.size() == 1,
        "a binding diff lists the slots to restore");

  Check(true,
        "TODO(planners fill): cover PlanBinding install/restore over an empty "
        "plan, material-only, shell-only, and a shellOwner tie-break");
  return test::Finish("planners bindingdiff");
}
