// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/ActorBudget.h"

#include "engine/ActorResize.h"
#include "render/RenderInstance.h"

#include <algorithm>
#include <memory>

namespace BetterEnchantmentEffects {
std::optional<LevelCost> LevelCostOf(const RenderPlan &a_plan,
                                     const PlanSizes &a_full,
                                     ResolutionLevel a_level) {
  const std::expected<PlanSizes, std::string> sizes =
      SizesFor(a_plan, AtLevel(a_full, a_level).sizes);
  if (!sizes) {
    return std::nullopt;
  }
  const PlanTargets targets = TargetsOf(a_plan, *sizes);
  return LevelCost{CostOf(targets.withIntermediates).bytes,
                   CostOf(targets.whenReleased).bytes};
}

InstallLevelChoice ChooseInstallLevel(const RenderPlan &a_plan,
                                      const PlanSizes &a_full,
                                      const InstallBound &a_bound) {
  if (!a_bound.headroom) {
    return {a_bound.finest, 0};
  }
  LevelBytes bytes;
  for (std::optional<ResolutionLevel> level = a_bound.finest; level;
       level = CoarserLevel(*level)) {
    if (const std::optional<LevelCost> cost =
            LevelCostOf(a_plan, a_full, *level)) {
      bytes.At(*level) = cost->withIntermediates;
    }
  }
  const ResolutionLevel chosen =
      FittingLevel(a_bound.finest, bytes, a_bound.headroom);
  return {chosen, bytes.At(chosen).value_or(0)};
}

std::optional<LevelCost> CachedLevelCost(LiveActor &a_state,
                                         ResolutionLevel a_level) {
  std::optional<LevelCost> &cached = a_state.levelCosts.At(a_level);
  if (cached) {
    return cached;
  }
  const std::shared_ptr<RenderInstance> render = AttachedRender(a_state);
  if (!render) {
    return std::nullopt;
  }
  cached = LevelCostOf(render->Plan(), a_state.fullSizes, a_level);
  return cached;
}

namespace {
std::uint64_t SurplusBytesOf(LiveActor &a_state, const BudgetActor &a_actor) {
  const std::optional<ResolutionLevel> wanted =
      WantedLevel({a_actor.screenFraction, a_actor.inView, a_actor.role});
  if (!wanted || LevelGap(a_actor.level, *wanted) <= 0) {
    return 0;
  }
  const std::optional<LevelCost> cost = CachedLevelCost(a_state, *wanted);
  return cost ? Less(a_actor.plannedBytes,
                     PlannedBytes(*cost, a_state.intermediates))
              : 0;
}
}

BudgetActor BudgetActorOf(LiveActor &a_state, std::uint32_t a_id,
                          ActorRole a_role, std::optional<float> a_fraction,
                          std::optional<float> a_distance) {
  BudgetActor actor{a_id,
                    a_role,
                    a_state.inView,
                    !Is<ResizeIdle>(a_state.resize),
                    a_state.appliedResolution,
                    a_fraction,
                    0,
                    std::nullopt,
                    0,
                    a_distance};
  const std::optional<LevelCost> current =
      CachedLevelCost(a_state, a_state.appliedResolution);
  if (!current) {
    return actor;
  }
  actor.plannedBytes = PlannedBytes(*current, a_state.intermediates);
  if (a_state.intermediates == IntermediateHolding::kHeld) {
    actor.releasable = IntermediateBytes(*current);
  }
  if (const std::optional<ResolutionLevel> coarser =
          CoarserLevel(a_state.appliedResolution)) {
    actor.stepDown =
        StepDownOf(a_state.appliedResolution, *current,
                   CachedLevelCost(a_state, *coarser), a_state.intermediates);
  }
  actor.surplusBytes = SurplusBytesOf(a_state, actor);
  return actor;
}

const std::vector<HeldTarget> &HeldNow(const LiveActor &a_state,
                                       const PlanTargets &a_targets) noexcept {
  return a_state.intermediates == IntermediateHolding::kReleased
             ? a_targets.whenReleased
             : a_targets.withIntermediates;
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

std::optional<std::uint64_t>
ReturnBytesOf(LiveActor &a_state, std::optional<ResolutionLevel> a_target) {
  const std::optional<LevelCost> cost =
      CachedLevelCost(a_state, a_target.value_or(a_state.appliedResolution));
  const std::uint64_t bytes =
      cost ? cost->withIntermediates : PlannedBytesOf(a_state);
  if (bytes == 0) {
    return std::nullopt;
  }
  return bytes;
}

std::size_t ReleaseIntermediates(LiveActor &a_state) {
  const std::shared_ptr<RenderInstance> render = AttachedRender(a_state);
  if (!render) {
    return 0;
  }
  a_state.intermediates = IntermediateHolding::kReleased;
  a_state.holding = IntermediateHolding::kReleased;
  return render->ReleaseIntermediates();
}
}
