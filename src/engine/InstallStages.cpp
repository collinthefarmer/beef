// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/InstallStages.h"

#include <cstddef>

namespace BetterEnchantmentEffects {
namespace {
GeometryBinding BindingOf(const LiveGeometry &a_bound, GeometryId a_geometry) {
  GeometryBinding binding;
  binding.geometry = a_geometry;
  binding.placement.sources = a_bound.placements;
  binding.placement.plan = a_bound.plan;
  binding.stacks = a_bound.stackPlan;
  binding.binding = a_bound.binding;
  if (a_bound.shellOwner) {
    binding.shellOwner = InstanceId{*a_bound.shellOwner};
  }
  return binding;
}

PlacementOutputs OutputsOf(const LivePlacement &a_live,
                           PlacementId a_placement) {
  PlacementOutputs outputs{a_placement, a_live.geometry, {}};
  for (const PlacedOutput &output : a_live.outputs) {
    outputs.outputs.push_back({output.index, output.state});
  }
  return outputs;
}

std::vector<PlacedOutput> LiveOutputsOf(const PlacementOutputs &a_outputs) {
  std::vector<PlacedOutput> outputs;
  outputs.reserve(a_outputs.outputs.size());
  for (const PlacedOutputState &output : a_outputs.outputs) {
    outputs.push_back(PlacedOutput{output.output, nullptr, output.state});
  }
  return outputs;
}

std::vector<SlotTargetState> TargetsOf(LiveGeometry &a_bound) {
  std::vector<SlotTargetState> targets;
  for (const SlotPlan &slot : a_bound.plan.slots) {
    if (const SlotTarget *target = TargetFor(a_bound, slot.surface)) {
      targets.push_back(
          {slot.surface, slot.slot, ProblemText(target->Problem(slot.slot))});
    }
  }
  return targets;
}
}

std::optional<LocatedGeometry> LocateGeometry(LiveActor &a_state,
                                              GeometryId a_geometry) noexcept {
  std::size_t index = 0;
  for (LivePiece &piece : a_state.pieces) {
    for (LiveGeometry &geometry : piece.geometries) {
      if (GeometryId{index} == a_geometry) {
        return LocatedGeometry{piece, geometry, a_geometry};
      }
      ++index;
    }
  }
  return std::nullopt;
}

SurfacePlan SurfacePlanOf(const LiveActor &a_state) {
  SurfacePlan plan;
  std::size_t flat = 0;
  for (const LivePiece &piece : a_state.pieces) {
    for (const LiveGeometry &bound : piece.geometries) {
      plan.geometries.push_back(BindingOf(bound, GeometryId{flat}));
      for (const PlacementId source : bound.placements) {
        const std::size_t index = IndexOf(source);
        if (index < a_state.placements.size()) {
          plan.placements.push_back(
              OutputsOf(a_state.placements[index], source));
        }
      }
      ++flat;
    }
  }
  return plan;
}

void KeepSurfacePlan(LiveActor &a_state, const SurfacePlan &a_plan) {
  a_state.placements.clear();
  a_state.placements.resize(a_state.matches.placements.size());
  for (const GeometryBinding &geometry : a_plan.geometries) {
    const std::optional<LocatedGeometry> located =
        LocateGeometry(a_state, geometry.geometry);
    if (!located) {
      continue;
    }
    LiveGeometry &bound = located->bound;
    bound.placements = geometry.placement.sources;
    bound.plan = geometry.placement.plan;
    bound.stackPlan = geometry.stacks;
    bound.binding = geometry.binding;
  }
  for (const PlacementOutputs &outputs : a_plan.placements) {
    const std::size_t index = IndexOf(outputs.placement);
    if (index >= a_state.placements.size()) {
      continue;
    }
    LivePlacement &live = a_state.placements[index];
    live.geometry = outputs.geometry;
    live.outputs = LiveOutputsOf(outputs);
  }
}

void WriteOutputs(LiveActor &a_state,
                  std::span<const PlacementOutputs> a_outputs) {
  for (const PlacementOutputs &outputs : a_outputs) {
    const std::size_t index = IndexOf(outputs.placement);
    if (index >= a_state.placements.size()) {
      continue;
    }
    for (const PlacedOutputState &output : outputs.outputs) {
      if (PlacedOutput *live =
              OutputAt(a_state.placements[index], output.output)) {
        live->state = output.state;
      }
    }
  }
}

std::vector<InstanceId> CompiledInstancesOf(const LiveActor &a_state) {
  std::vector<InstanceId> compiled;
  for (std::size_t i = 0; i < a_state.instances.size(); ++i) {
    if (a_state.instances[i].recipe && a_state.instances[i].graph) {
      compiled.push_back(InstanceId{i});
    }
  }
  return compiled;
}

BoundSurfaces BoundSurfacesOf(LiveGeometry &a_bound, GeometryId a_geometry) {
  return {a_geometry, a_bound.material != nullptr, a_bound.shell != nullptr,
          TargetsOf(a_bound)};
}

std::vector<BoundSurfaces> BoundSurfacesOf(LiveActor &a_state) {
  std::vector<BoundSurfaces> bound;
  std::size_t flat = 0;
  for (LivePiece &piece : a_state.pieces) {
    for (LiveGeometry &geometry : piece.geometries) {
      bound.push_back(BoundSurfacesOf(geometry, GeometryId{flat}));
      ++flat;
    }
  }
  return bound;
}

Resolution ScaleResolutionOf(TextureScale a_scale) noexcept {
  switch (a_scale) {
  case TextureScale::kQuarter:
    return Resolution::kQuarter;
  case TextureScale::kHalf:
    return Resolution::kHalf;
  case TextureScale::kFull:
    return Resolution::kFull;
  }
  return Resolution::kFull;
}
}
