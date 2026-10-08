// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ResolutionLevel.h"

namespace BetterEnchantmentEffects {
namespace {
constexpr float kFullLevelFraction = 0.5f;
constexpr float kCoarserBand = 0.8f;
constexpr std::uint64_t kLevelHoldMS = 2000;

constexpr unsigned Step(ResolutionLevel a_level) noexcept {
  return static_cast<unsigned>(a_level);
}
}

ResolutionLevel IdealLevel(float a_screenFraction) noexcept {
  float threshold = kFullLevelFraction;
  for (const ResolutionLevel level :
       {ResolutionLevel::kFull, ResolutionLevel::kHalf,
        ResolutionLevel::kQuarter}) {
    if (a_screenFraction >= threshold) {
      return level;
    }
    threshold /= 2.0f;
  }
  return ResolutionLevel::kEighth;
}

ResolutionLevel SteadyLevel(float a_screenFraction,
                            ResolutionLevel a_current) noexcept {
  const ResolutionLevel finer = IdealLevel(a_screenFraction);
  if (Step(finer) < Step(a_current)) {
    return finer;
  }
  const ResolutionLevel coarser = IdealLevel(a_screenFraction / kCoarserBand);
  return Step(coarser) > Step(a_current) ? coarser : a_current;
}

std::optional<ResolutionLevel> LevelChange(const LevelHistory &a_history,
                                           float a_screenFraction,
                                           ActorRole a_role,
                                           std::uint64_t a_nowMS) noexcept {
  if (a_role != ActorRole::kPlayer && !(a_screenFraction > 0.0f)) {
    return std::nullopt;
  }
  const ResolutionLevel wanted =
      a_role == ActorRole::kPlayer
          ? ResolutionLevel::kFull
          : SteadyLevel(a_screenFraction, a_history.level);
  if (wanted == a_history.level) {
    return std::nullopt;
  }
  if (a_history.changedMS && a_nowMS >= *a_history.changedMS &&
      a_nowMS - *a_history.changedMS < kLevelHoldMS) {
    return std::nullopt;
  }
  return wanted;
}

PlanSides AtLevel(const PlanSides &a_sides, ResolutionLevel a_level) {
  PlanSides leveled;
  leveled.pixels.reserve(a_sides.pixels.size());
  for (const TextureSize side : a_sides.pixels) {
    leveled.pixels.emplace_back(side.Pixels() >> Step(a_level));
  }
  return leveled;
}

std::string_view LevelName(ResolutionLevel a_level) noexcept {
  switch (a_level) {
  case ResolutionLevel::kFull:
    return "full";
  case ResolutionLevel::kHalf:
    return "half";
  case ResolutionLevel::kQuarter:
    return "quarter";
  case ResolutionLevel::kEighth:
    return "eighth";
  }
  return "full";
}
}
