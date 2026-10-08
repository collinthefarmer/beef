// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/ActorBudget.h"

#include "engine/ActorResize.h"
#include "render/RenderInstance.h"

#include <memory>

namespace BetterEnchantmentEffects {
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
  const std::expected<PlanSides, std::string> sides =
      SidesFor(render->Plan(), AtLevel(a_state.fullSides, a_level).pixels);
  if (!sides) {
    return std::nullopt;
  }
  const PlanTargets targets = TargetsOf(render->Plan(), *sides);
  cached = LevelCost{CostOf(targets.held).bytes, CostOf(targets.stacks).bytes};
  return cached;
}

BudgetActor BudgetActorOf(LiveActor &a_state, std::uint32_t a_id,
                          ActorRole a_role, float a_fraction) {
  BudgetActor actor{a_id,
                    a_role,
                    a_state.inView,
                    !Is<ResizeIdle>(a_state.resize),
                    a_state.resolution,
                    a_fraction,
                    0,
                    std::nullopt};
  const std::optional<LevelCost> current =
      LevelCostOf(a_state, a_state.resolution);
  if (!current) {
    return actor;
  }
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

std::size_t ReleaseIntermediates(LiveActor &a_state) {
  const std::shared_ptr<RenderInstance> render = AttachedRender(a_state);
  if (!render) {
    return 0;
  }
  a_state.intermediates = IntermediateHolding::kReleased;
  return render->ReleaseIntermediates();
}
}
