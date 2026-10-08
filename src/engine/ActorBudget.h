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
[[nodiscard]] std::optional<LevelCost> LevelCostOf(LiveActor &a_state,
                                                   ResolutionLevel a_level);
[[nodiscard]] BudgetActor BudgetActorOf(LiveActor &a_state, std::uint32_t a_id,
                                        ActorRole a_role, float a_fraction);
[[nodiscard]] const std::vector<HeldTarget> &
HeldNow(const LiveActor &a_state, const PlanTargets &a_targets) noexcept;
std::size_t ReleaseIntermediates(LiveActor &a_state);
}
