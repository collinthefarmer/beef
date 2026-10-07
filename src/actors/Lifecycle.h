// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "actors/Actor.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects::Actors {
inline constexpr std::uint32_t kModelWaitMs = 1000;
inline constexpr std::uint32_t kPendingRecheckMs = 100;
inline constexpr std::uint32_t kRecheckMs = 1000;

enum class Recheck { kNone, kWaiting, kStuck };

struct AdvanceRequest {
  const std::optional<ActorState> &state;
  const ActorFacts &facts;
  const ActorPlan &plan;
  std::uint32_t nowMs = 0;
  float evictDistance = 0.0f;
};

[[nodiscard]] Transition Advance(const AdvanceRequest &a_request);
[[nodiscard]] bool InRange(bool a_installed, std::optional<float> a_distance,
                           float a_evictDistance);
[[nodiscard]] bool
AnythingInstalled(const std::optional<ActorState> &a_state) noexcept;
[[nodiscard]] Recheck RecheckOf(const std::optional<ActorState> &a_state,
                                std::uint32_t a_nowMs);
[[nodiscard]] std::vector<Claim> LiveClaims(std::span<const Claim> a_claims,
                                            const ActorFacts &a_facts);
[[nodiscard]] ActorState WithClaim(ActorState a_state, Claim a_claim);
}
