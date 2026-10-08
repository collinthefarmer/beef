// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ResolutionLevel.h"

namespace BetterEnchantmentEffects {
namespace {
constexpr float kFullLevelFraction = 0.5f;

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

std::optional<ResolutionLevel> LevelChange(ResolutionLevel a_current,
                                           float a_screenFraction,
                                           ActorRole a_role) noexcept {
  if (a_role != ActorRole::kPlayer && !(a_screenFraction > 0.0f)) {
    return std::nullopt;
  }
  const ResolutionLevel wanted = a_role == ActorRole::kPlayer
                                     ? ResolutionLevel::kFull
                                     : IdealLevel(a_screenFraction);
  if (Step(wanted) >= Step(a_current)) {
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
