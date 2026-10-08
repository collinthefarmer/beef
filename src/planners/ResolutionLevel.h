// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/PlanSides.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace BetterEnchantmentEffects {
enum class ResolutionLevel : std::uint8_t { kFull, kHalf, kQuarter, kEighth };
inline constexpr std::size_t kResolutionLevelCount = 4;

struct LevelHistory {
  ResolutionLevel level = ResolutionLevel::kFull;
  std::optional<std::uint64_t> changedMS;
  [[nodiscard]] bool operator==(const LevelHistory &) const = default;
};

enum class ActorRole { kPlayer, kOther };

[[nodiscard]] ResolutionLevel IdealLevel(float a_screenFraction) noexcept;
[[nodiscard]] ResolutionLevel SteadyLevel(float a_screenFraction,
                                          ResolutionLevel a_current) noexcept;
[[nodiscard]] std::optional<ResolutionLevel>
LevelChange(const LevelHistory &a_history, float a_screenFraction,
            ActorRole a_role, std::uint64_t a_nowMS) noexcept;
[[nodiscard]] PlanSides AtLevel(const PlanSides &a_sides,
                                ResolutionLevel a_level);
[[nodiscard]] std::string_view LevelName(ResolutionLevel a_level) noexcept;
}
