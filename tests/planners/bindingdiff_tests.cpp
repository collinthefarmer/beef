#include "planners/BindingDiff.h"
#include "test_support.h"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
SlotContribution Contribution(std::size_t a_placed, std::size_t a_output) {
  return SlotContribution{static_cast<SlotSource>(a_placed), a_output};
}

SlotPlan MakeSlot(Surface a_surface, Slot a_slot,
                  std::vector<SlotContribution> a_chain) {
  SlotPlan plan;
  plan.surface = a_surface;
  plan.slot = a_slot;
  plan.chain = std::move(a_chain);
  return plan;
}
}

int main() {
  {
    const GeometryPlan plan;
    const BindingDiff diff = PlanBinding({}, plan, {}, {});
    Check(diff == BindingDiff{},
          "an empty plan with nothing written yields an empty diff");
    Check(!diff.material && !diff.shell, "an empty plan needs neither surface");
  }

  {
    const std::vector<PlacedRecipe> placed(1);
    GeometryPlan plan;
    plan.slots.push_back(
        MakeSlot(Surface::kMaterial, Slot::kEmissive, {Contribution(0, 0)}));
    plan.slots.push_back(
        MakeSlot(Surface::kMaterial, Slot::kRmaos, {Contribution(0, 1)}));
    const BindingDiff diff = PlanBinding(placed, plan, {}, {});
    Check(diff.material && !diff.shell,
          "a material-only plan needs the material surface only");
    Check(diff.materialSlots ==
              (std::vector<Slot>{Slot::kEmissive, Slot::kRmaos}),
          "install lists the planned material slots in plan order");
    Check(diff.shellSlots.empty(),
          "a material-only plan writes no shell slots");
    Check(diff.restoreMaterial.empty() && diff.restoreShell.empty(),
          "with nothing written before, nothing is restored");
    Check(!diff.shellOwner.has_value(),
          "a material-only plan names no shell owner");
  }

  {
    std::vector<PlacedRecipe> placed(2);
    placed[0].priority = 3;
    placed[1].priority = 7;
    GeometryPlan plan;
    plan.slots.push_back(
        MakeSlot(Surface::kShell, Slot::kEmissive, {Contribution(0, 0)}));
    plan.slots.push_back(
        MakeSlot(Surface::kShell, Slot::kDiffuse, {Contribution(1, 0)}));
    const BindingDiff diff = PlanBinding(placed, plan, {}, {});
    Check(diff.shell && !diff.material,
          "a shell-only plan needs the shell surface only");
    Check(diff.shellSlots ==
              (std::vector<Slot>{Slot::kEmissive, Slot::kDiffuse}),
          "install lists the planned shell slots");
    Check(diff.shellOwner ==
              std::optional<SlotSource>{static_cast<SlotSource>(1)},
          "shell owner is the highest-priority shell contribution");
  }

  {
    std::vector<PlacedRecipe> placed(2);
    placed[0].priority = 9;
    placed[1].priority = 1;
    GeometryPlan plan;
    plan.slots.push_back(MakeSlot(Surface::kShell, Slot::kEmissive,
                                  {Contribution(1, 0), Contribution(0, 0)}));
    const BindingDiff diff = PlanBinding(placed, plan, {}, {});
    Check(diff.shellOwner ==
              std::optional<SlotSource>{static_cast<SlotSource>(0)},
          "shell owner reads the top (last) contribution of a chain");
  }

  {
    const std::vector<PlacedRecipe> placed(1);
    GeometryPlan plan;
    plan.slots.push_back(
        MakeSlot(Surface::kMaterial, Slot::kEmissive, {Contribution(0, 0)}));
    const std::vector<Slot> wroteMaterial{Slot::kEmissive, Slot::kRmaos,
                                          Slot::kNormal};
    const std::vector<Slot> wroteShell{Slot::kDiffuse};
    const BindingDiff diff =
        PlanBinding(placed, plan, wroteMaterial, wroteShell);
    Check(diff.restoreMaterial ==
              (std::vector<Slot>{Slot::kRmaos, Slot::kNormal}),
          "material slots written before but absent now are restored");
    Check(diff.restoreShell == (std::vector<Slot>{Slot::kDiffuse}),
          "a dropped shell surface restores its written slots");
    Check(diff.materialSlots == (std::vector<Slot>{Slot::kEmissive}),
          "install still lists the still-planned slot");
  }

  {
    const std::vector<PlacedRecipe> placed(1);
    GeometryPlan plan;
    plan.slots.push_back(
        MakeSlot(Surface::kMaterial, Slot::kEmissive, {Contribution(0, 0)}));
    plan.slots.push_back(
        MakeSlot(Surface::kMaterial, Slot::kRmaos, {Contribution(0, 0)}));
    const std::vector<Slot> wrote{Slot::kEmissive, Slot::kRmaos};
    const BindingDiff diff = PlanBinding(placed, plan, wrote, {});
    Check(diff.restoreMaterial.empty(),
          "identical planned and written slots restore nothing");
    Check(diff.materialSlots == wrote,
          "install re-lists every planned slot for an idempotent rewrite");
  }

  {
    GeometryPlan plan;
    plan.slots.push_back(MakeSlot(Surface::kMaterial, Slot::kEmissive, {}));
    plan.slots.push_back(
        MakeSlot(Surface::kShell, Slot::kDiffuse, {Contribution(0, 0)}));
    const BindingDiff diff = PlanBinding({}, plan, {}, {});
    Check(!diff.material && diff.materialSlots.empty(),
          "a slot with an empty chain writes nothing and needs no surface");
    Check(diff.shell && diff.shellSlots == (std::vector<Slot>{Slot::kDiffuse}),
          "a shell slot with a chain is still planned");
    Check(!diff.shellOwner.has_value(),
          "an out-of-range contribution names no shell owner");
  }

  return test::Finish("planners bindingdiff");
}
