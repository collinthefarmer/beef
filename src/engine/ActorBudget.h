// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "engine/LiveActor.h"
#include "planners/TargetCost.h"

#include <cstddef>
#include <vector>

namespace BetterEnchantmentEffects {
[[nodiscard]] const std::vector<HeldTarget> &
HeldNow(const LiveActor &a_state, const PlanTargets &a_targets) noexcept;
[[nodiscard]] std::vector<const RenderInstance *>
InstancesOf(const LiveActor &a_state);
std::size_t ReleaseIntermediates(LiveActor &a_state);
}
