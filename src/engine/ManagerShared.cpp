#include "engine/ManagerShared.h"

namespace BetterEnchantmentEffects {
std::uint32_t NowMS() { return RE::GetDurationOfApplicationRunTime(); }

SlotTarget *TargetFor(LiveGeometry &a_bound, Surface a_surface) {
  if (a_surface == Surface::kShell) {
    return a_bound.shell.get();
  }
  return a_bound.material.get();
}

PlacedOutput *OutputAt(LivePlacement &a_placement, std::size_t a_index) {
  for (PlacedOutput &output : a_placement.outputs) {
    if (output.index == a_index) {
      return &output;
    }
  }
  return nullptr;
}
}
