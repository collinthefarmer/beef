// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/RenderBudget.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects {
inline constexpr float kRadiusShrinkFactor = 0.99f;
inline constexpr std::uint32_t kRadiusDwellMs = 10000;

struct BudgetRadius {
  float distance = 0.0f;
  std::uint32_t sinceMs = 0;
  [[nodiscard]] bool operator==(const BudgetRadius &) const = default;
};

struct RadiusShrink {
  std::size_t actor = 0;
  BudgetRadius radius;
  std::uint64_t freed = 0;
  [[nodiscard]] bool operator==(const RadiusShrink &) const = default;
};

enum class RadiusChange : std::uint8_t { kHeld, kGrown, kCleared };

struct RadiusGrowth {
  std::optional<BudgetRadius> radius;
  RadiusChange change = RadiusChange::kHeld;
  [[nodiscard]] bool operator==(const RadiusGrowth &) const = default;
};

[[nodiscard]] bool LimitsDistance(float a_evictDistance) noexcept;
[[nodiscard]] float
EffectiveEvictDistance(float a_evictDistance,
                       const std::optional<BudgetRadius> &a_radius) noexcept;
[[nodiscard]] std::optional<RadiusShrink>
ShrinkRadius(std::span<const BudgetActor> a_actors,
             const std::optional<BudgetRadius> &a_current,
             std::uint32_t a_nowMs) noexcept;
[[nodiscard]] RadiusGrowth GrowRadius(const BudgetRadius &a_radius,
                                      Pressure a_pressure,
                                      std::uint32_t a_nowMs,
                                      float a_evictDistance,
                                      std::optional<float> a_farthest) noexcept;
[[nodiscard]] std::string RadiusShrinkLine(float a_radius,
                                           std::uint32_t a_actor,
                                           std::string_view a_name,
                                           std::uint64_t a_freed);
[[nodiscard]] std::string RadiusGrowLine(float a_radius);
[[nodiscard]] std::string RadiusClearLine();
}
