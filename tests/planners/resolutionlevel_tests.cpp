// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ResolutionLevel.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
void IdealLevelHalvesWithScreenSize() {
  Check(IdealLevel(1.0f) == ResolutionLevel::kFull &&
            IdealLevel(0.5f) == ResolutionLevel::kFull,
        "an actor of half the screen or more is full");
  Check(IdealLevel(0.3f) == ResolutionLevel::kHalf &&
            IdealLevel(0.2f) == ResolutionLevel::kQuarter &&
            IdealLevel(0.1f) == ResolutionLevel::kEighth &&
            IdealLevel(0.0f) == ResolutionLevel::kEighth,
        "each halving of the screen size halves the side, down to an eighth");
}

void LevelsOnlyStepUp() {
  Check(StepUpFor(ResolutionLevel::kEighth, 0.26f, ActorRole::kOther) ==
            ResolutionLevel::kHalf,
        "a larger screen size steps up at once, skipping the levels between");
  Check(!StepUpFor(ResolutionLevel::kHalf, 0.24f, ActorRole::kOther),
        "just under a threshold an actor keeps its level");
  Check(!StepUpFor(ResolutionLevel::kFull, 0.05f, ActorRole::kOther),
        "a smaller screen size never steps down");
  Check(!StepUpFor(ResolutionLevel::kHalf, 0.3f, ActorRole::kOther),
        "an actor at its level asks for nothing");
}

void BackAndForthChangesOnce() {
  ResolutionLevel level = ResolutionLevel::kQuarter;
  std::size_t changes = 0;
  for (const float fraction : {0.26f, 0.24f, 0.51f, 0.39f, 0.52f, 0.1f, 0.6f}) {
    if (const std::optional<ResolutionLevel> next =
            StepUpFor(level, fraction, ActorRole::kOther)) {
      level = *next;
      ++changes;
    }
  }
  Check(changes == 2 && level == ResolutionLevel::kFull,
        "walking to and from an actor steps up once per boundary");
}

void UnknownSizeKeepsTheLevel() {
  Check(!StepUpFor(ResolutionLevel::kEighth, 0.0f, ActorRole::kOther),
        "a screen fraction of zero changes nothing");
}

void PlayerStaysFull() {
  Check(!StepUpFor(ResolutionLevel::kFull, 0.01f, ActorRole::kPlayer),
        "the player stays full however small");
  Check(StepUpFor(ResolutionLevel::kQuarter, 0.0f, ActorRole::kPlayer) ==
            ResolutionLevel::kFull,
        "a player below full returns to full");
}

void SidesScaleAndStopAtTheMinimum() {
  const PlanSizes sides{{TextureSize{2048}, TextureSize{512}}};
  const PlanSizes half = AtLevel(sides, ResolutionLevel::kHalf);
  Check(half.sizes == std::vector{TextureSize{1024}, TextureSize{256}},
        "a level divides every side");
  const PlanSizes eighth = AtLevel(sides, ResolutionLevel::kEighth);
  Check(eighth.sizes == std::vector{TextureSize{256}, TextureSize{64}},
        "a side never falls below the minimum");
  Check(AtLevel(sides, ResolutionLevel::kFull) == sides,
        "full keeps the sides");
}
}

void CoarserLevelsStepOnce() {
  Check(CoarserLevel(ResolutionLevel::kFull) == ResolutionLevel::kHalf,
        "full steps down to half");
  Check(CoarserLevel(ResolutionLevel::kQuarter) == ResolutionLevel::kEighth,
        "quarter steps down to eighth");
  Check(!CoarserLevel(ResolutionLevel::kEighth), "eighth has no coarser level");
  Check(LevelGap(ResolutionLevel::kFull, ResolutionLevel::kQuarter) == 2,
        "full is two steps finer than quarter");
  Check(LevelGap(ResolutionLevel::kHalf, ResolutionLevel::kFull) == -1,
        "half is one step coarser than full");
}

int main() {
  IdealLevelHalvesWithScreenSize();
  LevelsOnlyStepUp();
  BackAndForthChangesOnce();
  UnknownSizeKeepsTheLevel();
  PlayerStaysFull();
  SidesScaleAndStopAtTheMinimum();
  CoarserLevelsStepOnce();
  return test::Finish("planners_resolutionlevel");
}
