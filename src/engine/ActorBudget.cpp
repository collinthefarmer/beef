// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/ActorBudget.h"

#include "engine/ActorResize.h"
#include "render/RenderInstance.h"

#include <algorithm>
#include <memory>

namespace BetterEnchantmentEffects {
std::optional<LevelCost> PlanLevelCost(const RenderPlan &a_plan,
                                       const PlanSides &a_full,
                                       ResolutionLevel a_level) {
  const std::expected<PlanSides, std::string> sides =
      SidesFor(a_plan, AtLevel(a_full, a_level).pixels);
  if (!sides) {
    return std::nullopt;
  }
  const PlanTargets targets = TargetsOf(a_plan, *sides);
  return LevelCost{CostOf(targets.held).bytes, CostOf(targets.stacks).bytes};
}

InstallLevelChoice ChooseInstallLevel(const RenderPlan &a_plan,
                                      const PlanSides &a_full,
                                      const InstallBound &a_bound) {
  if (!a_bound.headroom) {
    return {a_bound.finest, 0};
  }
  LevelBytes bytes;
  for (std::optional<ResolutionLevel> level = a_bound.finest; level;
       level = CoarserLevel(*level)) {
    const auto index = static_cast<std::size_t>(*level);
    const std::optional<LevelCost> cost = PlanLevelCost(a_plan, a_full, *level);
    if (index < bytes.size() && cost) {
      bytes[index] = cost->held;
    }
  }
  const ResolutionLevel chosen =
      FittingLevel(a_bound.finest, bytes, a_bound.headroom);
  const auto index = static_cast<std::size_t>(chosen);
  return {chosen, index < bytes.size() ? bytes[index].value_or(0) : 0};
}

std::optional<LevelCost> LevelCostOf(LiveActor &a_state,
                                     ResolutionLevel a_level) {
  const auto index = static_cast<std::size_t>(a_level);
  if (index >= a_state.levelCosts.size()) {
    return std::nullopt;
  }
  std::optional<LevelCost> &cached = a_state.levelCosts[index];
  if (cached) {
    return cached;
  }
  const std::shared_ptr<RenderInstance> render = AttachedRender(a_state);
  if (!render) {
    return std::nullopt;
  }
  cached = PlanLevelCost(render->Plan(), a_state.fullSides, a_level);
  return cached;
}

BudgetActor BudgetActorOf(LiveActor &a_state, std::uint32_t a_id,
                          ActorRole a_role, float a_fraction,
                          std::optional<float> a_distance) {
  BudgetActor actor{a_id,
                    a_role,
                    a_state.inView,
                    !Is<ResizeIdle>(a_state.resize),
                    a_state.resolution,
                    a_fraction,
                    0,
                    std::nullopt,
                    0,
                    a_distance};
  const std::optional<LevelCost> current =
      LevelCostOf(a_state, a_state.resolution);
  if (!current) {
    return actor;
  }
  actor.held = HeldBytes(*current, a_state.intermediates);
  if (a_state.intermediates == IntermediateHolding::kHeld) {
    actor.releasable =
        current->held > current->stacks ? current->held - current->stacks : 0;
  }
  if (const std::optional<ResolutionLevel> coarser =
          CoarserLevel(a_state.resolution)) {
    actor.stepDown =
        StepDownOf(a_state.resolution, *current, LevelCostOf(a_state, *coarser),
                   a_state.intermediates);
  }
  return actor;
}

const std::vector<HeldTarget> &HeldNow(const LiveActor &a_state,
                                       const PlanTargets &a_targets) noexcept {
  return a_state.intermediates == IntermediateHolding::kReleased
             ? a_targets.stacks
             : a_targets.held;
}

std::vector<const RenderInstance *> InstancesOf(const LiveActor &a_state) {
  std::vector<const RenderInstance *> instances;
  for (const LivePiece &piece : a_state.pieces) {
    for (const LiveGeometry &geometry : piece.geometries) {
      if (geometry.inputs.render &&
          !std::ranges::contains(instances, geometry.inputs.render.get())) {
        instances.push_back(geometry.inputs.render.get());
      }
    }
  }
  if (const RenderInstance *resizing = ResizingInstance(a_state)) {
    instances.push_back(resizing);
  }
  return instances;
}

std::uint64_t PlannedBytesOf(const LiveActor &a_state) {
  if (a_state.renderedFrame == 0) {
    return 0;
  }
  std::vector<HeldTarget> held;
  for (const RenderInstance *render : InstancesOf(a_state)) {
    const std::vector<HeldTarget> &now = HeldNow(a_state, render->Targets());
    held.insert(held.end(), now.begin(), now.end());
  }
  return CostOf(held).bytes;
}

std::size_t ReleaseIntermediates(LiveActor &a_state) {
  const std::shared_ptr<RenderInstance> render = AttachedRender(a_state);
  if (!render) {
    return 0;
  }
  a_state.intermediates = IntermediateHolding::kReleased;
  return render->ReleaseIntermediates();
}
}
