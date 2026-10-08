// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ResolutionLevel.h"

namespace BetterEnchantmentEffects {
namespace {
constexpr float kFullLevelFraction = 0.5f;

constexpr unsigned Halvings(ResolutionLevel a_level) noexcept {
  return static_cast<unsigned>(LevelIndex(a_level));
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

std::optional<ResolutionLevel> StepUpFor(ResolutionLevel a_current,
                                         float a_screenFraction,
                                         ActorRole a_role) noexcept {
  if (a_role != ActorRole::kPlayer && !(a_screenFraction > 0.0f)) {
    return std::nullopt;
  }
  const ResolutionLevel wanted = a_role == ActorRole::kPlayer
                                     ? ResolutionLevel::kFull
                                     : IdealLevel(a_screenFraction);
  if (Halvings(wanted) >= Halvings(a_current)) {
    return std::nullopt;
  }
  return wanted;
}

std::optional<ResolutionLevel> CoarserLevel(ResolutionLevel a_level) noexcept {
  switch (a_level) {
  case ResolutionLevel::kFull:
    return ResolutionLevel::kHalf;
  case ResolutionLevel::kHalf:
    return ResolutionLevel::kQuarter;
  case ResolutionLevel::kQuarter:
    return ResolutionLevel::kEighth;
  case ResolutionLevel::kEighth:
    return std::nullopt;
  }
  return std::nullopt;
}

int LevelGap(ResolutionLevel a_finer, ResolutionLevel a_coarser) noexcept {
  return static_cast<int>(Halvings(a_coarser)) -
         static_cast<int>(Halvings(a_finer));
}

PlanSizes AtLevel(const PlanSizes &a_sizes, ResolutionLevel a_level) {
  PlanSizes leveled;
  leveled.sizes.reserve(a_sizes.sizes.size());
  for (const TextureSize size : a_sizes.sizes) {
    leveled.sizes.emplace_back(size.Pixels() >> Halvings(a_level));
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
