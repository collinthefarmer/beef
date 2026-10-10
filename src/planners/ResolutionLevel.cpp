// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ResolutionLevel.h"

namespace BetterEnchantmentEffects {
namespace {
constexpr float kFullLevelFraction = 0.5f;

constexpr unsigned Halvings(ResolutionLevel a_level) noexcept {
  return static_cast<unsigned>(LevelIndex(a_level));
}

float LowerBound(ResolutionLevel a_level) noexcept {
  float bound = kFullLevelFraction;
  for (unsigned i = 0; i < Halvings(a_level); ++i) {
    bound /= 2.0f;
  }
  return bound;
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

std::optional<ResolutionLevel> WantedLevel(const ActorSight &a_sight) noexcept {
  if (a_sight.role == ActorRole::kPlayer) {
    return ResolutionLevel::kFull;
  }
  if (!a_sight.screenFraction) {
    return std::nullopt;
  }
  if (!a_sight.inView) {
    return ResolutionLevel::kEighth;
  }
  return IdealLevel(*a_sight.screenFraction);
}

bool SmallFor(std::optional<float> a_screenFraction,
              ResolutionLevel a_current) noexcept {
  if (!a_screenFraction || a_current == ResolutionLevel::kEighth) {
    return false;
  }
  return *a_screenFraction < kStepDownBand * LowerBound(a_current);
}

LevelStep LevelStepOf(ResolutionLevel a_current, const ActorSight &a_sight,
                      bool a_stepDownDue) noexcept {
  if (const std::optional<ResolutionLevel> wanted = WantedLevel(a_sight);
      wanted && Halvings(*wanted) < Halvings(a_current)) {
    return StepUpTo{*wanted};
  }
  if (a_sight.role == ActorRole::kPlayer || !a_stepDownDue) {
    return KeepLevel{};
  }
  if (const std::optional<ResolutionLevel> coarser = CoarserLevel(a_current)) {
    return StepDownTo{*coarser};
  }
  return KeepLevel{};
}

bool LargerOnScreen(std::optional<float> a_left,
                    std::optional<float> a_right) noexcept {
  if (!a_left || !a_right) {
    return a_left.has_value() && !a_right.has_value();
  }
  return *a_left > *a_right;
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
