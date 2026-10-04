// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "actors/Actor.h"

#include <cstdint>
#include <optional>

namespace BetterEnchantmentEffects::Actors {
inline constexpr std::uint32_t kModelWaitMs = 1000;
inline constexpr std::uint32_t kPendingRecheckMs = 100;

enum class Recheck { kNone, kSettling, kStuck };

struct AdvanceRequest {
  const Phase &phase;
  const ActorFacts &facts;
  const ActorPlan &plan;
  std::uint32_t nowMs = 0;
  float evictDistance = 0.0f;
};

[[nodiscard]] Advanced Advance(const AdvanceRequest &a_request);
[[nodiscard]] bool InRange(const Phase &a_phase,
                           std::optional<float> a_distance,
                           float a_evictDistance);
[[nodiscard]] Recheck RecheckOf(const Phase &a_phase, std::uint32_t a_nowMs);
}
