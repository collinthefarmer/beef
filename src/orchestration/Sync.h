// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "actors/Actor.h"
#include "orchestration/Facts.h"
#include "orchestration/World.h"
#include "planners/RenderBudget.h"

#include <cstdint>
#include <optional>

namespace BetterEnchantmentEffects::Orchestration {
[[nodiscard]] std::uint32_t PlannerMs(Millis a_now) noexcept;
[[nodiscard]] std::optional<std::uint64_t>
ReturnBytesOf(const ActorRecord &a_record) noexcept;
[[nodiscard]] bool RangeChanged(const Actors::ActorState &a_state,
                                std::optional<float> a_distance,
                                float a_evictDistance);
}
