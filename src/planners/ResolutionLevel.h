// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/ActorRole.h"
#include "planners/PlanSizes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace BetterEnchantmentEffects {
enum class ResolutionLevel : std::uint8_t { kFull, kHalf, kQuarter, kEighth };
inline constexpr std::array<ResolutionLevel, 4> kResolutionLevels{
    ResolutionLevel::kFull, ResolutionLevel::kHalf, ResolutionLevel::kQuarter,
    ResolutionLevel::kEighth};

[[nodiscard]] constexpr std::size_t
LevelIndex(ResolutionLevel a_level) noexcept {
  switch (a_level) {
  case ResolutionLevel::kFull:
    return 0;
  case ResolutionLevel::kHalf:
    return 1;
  case ResolutionLevel::kQuarter:
    return 2;
  case ResolutionLevel::kEighth:
    return 3;
  }
  return 0;
}

template <class T> struct PerLevel {
  std::array<T, kResolutionLevels.size()> values{};
  [[nodiscard]] T &At(ResolutionLevel a_level) noexcept {
    return values[LevelIndex(a_level)];
  }
  [[nodiscard]] const T &At(ResolutionLevel a_level) const noexcept {
    return values[LevelIndex(a_level)];
  }
  [[nodiscard]] bool operator==(const PerLevel &) const = default;
};

inline constexpr float kStepDownBand = 0.8f;
inline constexpr std::uint32_t kStepDownHoldMS = 2000;

struct ActorSight {
  std::optional<float> screenFraction;
  bool inView = true;
  ActorRole role = ActorRole::kOther;
};

struct KeepLevel {
  [[nodiscard]] bool operator==(const KeepLevel &) const = default;
};

struct StepUpTo {
  ResolutionLevel level = ResolutionLevel::kFull;
  [[nodiscard]] bool operator==(const StepUpTo &) const = default;
};

struct StepDownTo {
  ResolutionLevel level = ResolutionLevel::kEighth;
  [[nodiscard]] bool operator==(const StepDownTo &) const = default;
};

using LevelStep = std::variant<KeepLevel, StepUpTo, StepDownTo>;

[[nodiscard]] ResolutionLevel IdealLevel(float a_screenFraction) noexcept;
[[nodiscard]] std::optional<ResolutionLevel>
WantedLevel(const ActorSight &a_sight) noexcept;
[[nodiscard]] bool SmallFor(std::optional<float> a_screenFraction,
                            ResolutionLevel a_current) noexcept;
[[nodiscard]] LevelStep LevelStepOf(ResolutionLevel a_current,
                                    const ActorSight &a_sight,
                                    bool a_stepDownDue) noexcept;
[[nodiscard]] bool LargerOnScreen(std::optional<float> a_left,
                                  std::optional<float> a_right) noexcept;
[[nodiscard]] std::optional<ResolutionLevel>
CoarserLevel(ResolutionLevel a_level) noexcept;
[[nodiscard]] int LevelGap(ResolutionLevel a_finer,
                           ResolutionLevel a_coarser) noexcept;
[[nodiscard]] PlanSizes AtLevel(const PlanSizes &a_sizes,
                                ResolutionLevel a_level);
[[nodiscard]] std::string_view LevelName(ResolutionLevel a_level) noexcept;
}
