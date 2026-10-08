// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/TargetCost.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects {
inline constexpr std::uint64_t kHoldingStepBytes = std::uint64_t{1} << 30;
inline constexpr std::uint64_t kBurstBytes = std::uint64_t{256} << 20;
inline constexpr std::size_t kReportedHoldings = 5;

enum class Crossing : std::uint8_t { kNone, kRose, kFell };

struct HoldingLevel {
  std::uint32_t level = 0;
  Crossing crossing = Crossing::kNone;
  [[nodiscard]] bool operator==(const HoldingLevel &) const = default;
};

struct ActorHolding {
  std::uint32_t actor = 0;
  std::string name;
  std::uint64_t bytes = 0;
  std::uint64_t sharedBytes = 0;
  float screenFraction = 0.0f;
  std::vector<std::string> recipes;
};

struct ActorTargets {
  std::uint32_t actor = 0;
  std::string name;
  float screenFraction = 0.0f;
  std::vector<std::string> recipes;
  std::vector<HeldTarget> held;
};

struct TargetTotals {
  std::uint64_t targets = 0;
  std::uint64_t bytes = 0;
};

struct ScreenSpread {
  float least = 0.0f;
  float median = 0.0f;
  float most = 0.0f;
  [[nodiscard]] bool operator==(const ScreenSpread &) const = default;
};

struct TargetBurst {
  std::uint64_t created = 0;
  std::uint64_t createdBytes = 0;
  std::uint64_t destroyedBytes = 0;
  std::uint32_t intervalMs = 0;
};

[[nodiscard]] HoldingLevel NextHoldingLevel(std::uint32_t a_level,
                                            std::uint64_t a_bytes) noexcept;
[[nodiscard]] bool IsBurst(const TargetBurst &a_burst) noexcept;
[[nodiscard]] ScreenSpread SpreadOf(std::vector<float> a_fractions);
[[nodiscard]] std::vector<ActorHolding>
HoldingsOf(std::span<const ActorTargets> a_actors);
[[nodiscard]] std::vector<ActorHolding>
LargestHoldings(std::vector<ActorHolding> a_holdings, std::size_t a_count);
[[nodiscard]] std::string HoldingLine(const TargetTotals &a_measured,
                                      const TargetTotals &a_planned,
                                      std::span<const ActorHolding> a_largest);
[[nodiscard]] std::string BurstLine(const TargetBurst &a_burst,
                                    std::string_view a_syncs);
[[nodiscard]] std::string Mebibytes(std::uint64_t a_bytes);
}
