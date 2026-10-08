// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/RenderBudget.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects {
inline constexpr float kRadiusShrinkFactor = 0.99f;
inline constexpr std::uint32_t kRadiusDwellMs = 10000;
inline constexpr std::uint64_t kRingMarginDivisor = 2;
inline constexpr std::size_t kUnpricedPerGrowth = 1;
inline constexpr int kRadiusRoundingSteps = 4;

struct BudgetRadius {
  float distance = 0.0f;
  std::uint32_t sinceMs = 0;
  bool blockReported = false;
  [[nodiscard]] bool operator==(const BudgetRadius &) const = default;
};

struct EvictedActor {
  std::optional<float> distance;
  std::optional<std::uint64_t> returnBytes;
  [[nodiscard]] bool operator==(const EvictedActor &) const = default;
};

struct RingActor {
  float distance = 0.0f;
  std::optional<std::uint64_t> returnBytes;
  [[nodiscard]] bool operator==(const RingActor &) const = default;
};

struct RingRoom {
  std::uint64_t headroom = 0;
  std::uint64_t margin = 0;
  [[nodiscard]] bool operator==(const RingRoom &) const = default;
};

struct RingFit {
  std::size_t returning = 0;
  std::uint64_t bytes = 0;
  [[nodiscard]] bool operator==(const RingFit &) const = default;
};

struct RadiusShrink {
  std::uint32_t actor = 0;
  BudgetRadius radius;
  std::uint64_t freed = 0;
  [[nodiscard]] bool operator==(const RadiusShrink &) const = default;
};

struct RadiusGrowthRequest {
  BudgetRadius radius;
  BudgetState budget;
  std::uint32_t nowMs = 0;
  float evictDistance = 0.0f;
  std::optional<float> farthest;
  std::span<const EvictedActor> evicted;
  std::uint64_t unpricedBytes = 0;
};

struct RadiusUnchanged {
  BudgetRadius radius;
  [[nodiscard]] bool operator==(const RadiusUnchanged &) const = default;
};

struct RadiusGrown {
  BudgetRadius radius;
  std::uint64_t returningBytes = 0;
  [[nodiscard]] bool operator==(const RadiusGrown &) const = default;
};

struct RadiusBlocked {
  BudgetRadius radius;
  std::uint64_t ringBytes = 0;
  RingRoom room;
  bool firstInEpisode = false;
  [[nodiscard]] bool operator==(const RadiusBlocked &) const = default;
};

struct RadiusCleared {
  std::uint64_t returningBytes = 0;
  [[nodiscard]] bool operator==(const RadiusCleared &) const = default;
};

using RadiusGrowth =
    std::variant<RadiusUnchanged, RadiusGrown, RadiusBlocked, RadiusCleared>;

[[nodiscard]] bool LimitsDistance(float a_evictDistance) noexcept;
[[nodiscard]] float
EffectiveEvictDistance(float a_evictDistance,
                       const std::optional<BudgetRadius> &a_radius) noexcept;
[[nodiscard]] std::optional<RadiusShrink>
ShrinkRadius(std::span<const BudgetActor> a_actors,
             const std::optional<BudgetRadius> &a_current,
             std::uint32_t a_nowMs) noexcept;
[[nodiscard]] std::vector<RingActor>
RingOf(std::span<const EvictedActor> a_evicted, float a_evictNow,
       float a_evictAfter);
[[nodiscard]] std::uint64_t PriceOf(const RingActor &a_actor,
                                    std::uint64_t a_unpricedBytes) noexcept;
[[nodiscard]] std::uint64_t RingBytes(std::span<const RingActor> a_ring,
                                      std::uint64_t a_unpricedBytes) noexcept;
[[nodiscard]] std::optional<RingRoom>
RingRoomOf(const BudgetState &a_budget) noexcept;
[[nodiscard]] RingFit FitRing(std::span<const RingActor> a_ring,
                              std::uint64_t a_room,
                              std::uint64_t a_unpricedBytes) noexcept;
[[nodiscard]] float RadiusStoppingAt(float a_distance) noexcept;
[[nodiscard]] RadiusGrowth GrowRadius(const RadiusGrowthRequest &a_request);
[[nodiscard]] std::string RadiusShrinkLine(float a_radius,
                                           std::uint32_t a_actor,
                                           std::string_view a_name,
                                           std::uint64_t a_freed);
[[nodiscard]] std::string RadiusGrowLine(float a_radius);
[[nodiscard]] std::string RadiusBlockedLine(const RadiusBlocked &a_blocked);
[[nodiscard]] std::string RadiusClearLine();
}
