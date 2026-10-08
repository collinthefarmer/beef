// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "engine/LiveActor.h"
#include "planners/RenderBudget.h"
#include "planners/ResolutionLevel.h"
#include "planners/TargetCost.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace BetterEnchantmentEffects {
struct InstallLevelChoice {
  ResolutionLevel level = ResolutionLevel::kFull;
  std::uint64_t chargedBytes = 0;
};

[[nodiscard]] std::optional<LevelCost> LevelCostOf(const RenderPlan &a_plan,
                                                   const PlanSizes &a_full,
                                                   ResolutionLevel a_level);
[[nodiscard]] InstallLevelChoice
ChooseInstallLevel(const RenderPlan &a_plan, const PlanSizes &a_full,
                   const InstallBound &a_bound);
[[nodiscard]] std::optional<LevelCost> CachedLevelCost(LiveActor &a_state,
                                                       ResolutionLevel a_level);
[[nodiscard]] BudgetActor BudgetActorOf(LiveActor &a_state, std::uint32_t a_id,
                                        ActorRole a_role, float a_fraction,
                                        std::optional<float> a_distance);
[[nodiscard]] const std::vector<HeldTarget> &
HeldNow(const LiveActor &a_state, const PlanTargets &a_targets) noexcept;
[[nodiscard]] std::vector<const RenderInstance *>
InstancesOf(const LiveActor &a_state);
[[nodiscard]] std::uint64_t PlannedBytesOf(const LiveActor &a_state);
std::size_t ReleaseIntermediates(LiveActor &a_state);
}
