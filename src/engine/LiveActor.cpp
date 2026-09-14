#include "engine/LiveActor.h"

namespace BetterEnchantmentEffects {
void RetireGeometry(LiveGeometry &a_geometry) {
  a_geometry.lost = true;
  a_geometry.shell.reset();
  a_geometry.material.reset();
  a_geometry.shellOwner.reset();
  a_geometry.plan = {};
  a_geometry.stackPlan = {};
  a_geometry.binding = {};
}

void RetireActorEffects(LiveActor &a_actor) {
  a_actor.applications.clear();
  for (auto &instance : a_actor.instances)
    instance.light.reset();
  for (auto &piece : a_actor.pieces) {
    for (auto &geometry : piece.geometries)
      RetireGeometry(geometry);
  }
  a_actor.placements.clear();
  a_actor.instances.clear();
  a_actor.pieces.clear();
}

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
