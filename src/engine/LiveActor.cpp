// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/LiveActor.h"

#include <algorithm>

namespace BetterEnchantmentEffects {
std::size_t LiveInstanceCount(const LiveActor &a_actor) noexcept {
  return static_cast<std::size_t>(std::ranges::count_if(
      a_actor.instances, [](const LiveInstance &a_instance) {
        return a_instance.recipe != nullptr;
      }));
}

void RetireGeometry(LiveGeometry &a_geometry) {
  a_geometry.lost = true;
  a_geometry.shell.reset();
  a_geometry.material.reset();
  a_geometry.shellOwner.reset();
  a_geometry.plan = {};
  a_geometry.stackPlan = {};
  a_geometry.binding = {};
  a_geometry.inputs.masks.reset();
  a_geometry.inputs.ripples.reset();
  a_geometry.inputs.derived.reset();
  a_geometry.inputs.material = {};
  a_geometry.inputs.geometry.reset();
  a_geometry.inputs.root.reset();
  a_geometry.geometry.reset();
  a_geometry.property.reset();
}

void RetireGeometry(LiveActor &a_actor, LiveGeometry &a_geometry) {
  RetireGeometry(a_geometry);
  for (const PlacementId id : a_geometry.placements) {
    const auto resolved = ResolvePlacement(a_actor, id);
    if (!resolved) {
      continue;
    }
    for (PlacedOutput &output :
         a_actor.placements[resolved->placement].outputs) {
      output.stack.reset();
      output.rendered = false;
      output.renderFailed = true;
      if (output.problem.empty()) {
        output.problem = "the geometry was retired after ownership changed";
      }
    }
  }
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

std::optional<ResolvedPlacement>
ResolvePlacement(const LiveActor &a_state, PlacementId a_placement) noexcept {
  const std::size_t placement = IndexOf(a_placement);
  if (placement >= a_state.plan.placements.size() ||
      placement >= a_state.placements.size()) {
    return std::nullopt;
  }
  const std::size_t instance =
      IndexOf(a_state.plan.placements[placement].instance);
  if (instance >= a_state.instances.size()) {
    return std::nullopt;
  }
  return ResolvedPlacement{placement, instance};
}

std::optional<ResolvedPlacement>
ResolvePlacement(const LiveActor &a_state, const LiveGeometry &a_bound,
                 std::size_t a_placed) noexcept {
  if (a_placed >= a_bound.placements.size()) {
    return std::nullopt;
  }
  return ResolvePlacement(a_state, a_bound.placements[a_placed]);
}
}
