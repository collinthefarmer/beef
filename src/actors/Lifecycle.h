// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "actors/Actor.h"

#include <cstdint>

namespace BetterEnchantmentEffects::Actors {
inline constexpr std::uint32_t kModelWaitMs = 1000;

struct AdvanceRequest {
  const Phase &phase;
  const ActorFacts &facts;
  const ActorPlan &plan;
  std::uint32_t nowMs = 0;
  float evictDistance = 0.0f;
};

[[nodiscard]] Advanced Advance(const AdvanceRequest &a_request);
}
