// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/PlanSides.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace BetterEnchantmentEffects {
enum class ResolutionLevel : std::uint8_t { kFull, kHalf, kQuarter, kEighth };
inline constexpr std::size_t kResolutionLevelCount = 4;

enum class ActorRole { kPlayer, kOther };

[[nodiscard]] ResolutionLevel IdealLevel(float a_screenFraction) noexcept;
[[nodiscard]] std::optional<ResolutionLevel>
LevelChange(ResolutionLevel a_current, float a_screenFraction,
            ActorRole a_role) noexcept;
[[nodiscard]] std::optional<ResolutionLevel>
CoarserLevel(ResolutionLevel a_level) noexcept;
[[nodiscard]] int LevelGap(ResolutionLevel a_finer,
                           ResolutionLevel a_coarser) noexcept;
[[nodiscard]] PlanSides AtLevel(const PlanSides &a_sides,
                                ResolutionLevel a_level);
[[nodiscard]] std::string_view LevelName(ResolutionLevel a_level) noexcept;
}
