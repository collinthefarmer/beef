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

void CoarserLevelsWaitForTheBand() {
  Check(SteadyLevel(0.24f, ResolutionLevel::kHalf) == ResolutionLevel::kHalf,
        "just under a threshold an actor keeps its finer level");
  Check(SteadyLevel(0.19f, ResolutionLevel::kHalf) == ResolutionLevel::kQuarter,
        "below the band an actor steps down");
  Check(SteadyLevel(0.26f, ResolutionLevel::kQuarter) == ResolutionLevel::kHalf,
        "crossing a threshold upward steps up at once");
  Check(SteadyLevel(0.05f, ResolutionLevel::kFull) == ResolutionLevel::kEighth,
        "a large drop skips the levels between");
}

void ChangesAreHeldApart() {
  const LevelHistory fresh{ResolutionLevel::kFull, std::nullopt};
  Check(LevelChange(fresh, 0.05f, ActorRole::kOther, 0) ==
            ResolutionLevel::kEighth,
        "an actor with no change yet moves at once");
  const LevelHistory recent{ResolutionLevel::kEighth, 1000};
  Check(!LevelChange(recent, 0.9f, ActorRole::kOther, 2500),
        "a second change within the hold waits");
  Check(LevelChange(recent, 0.9f, ActorRole::kOther, 3000) ==
            ResolutionLevel::kFull,
        "after the hold the change goes ahead");
  Check(!LevelChange(fresh, 0.6f, ActorRole::kOther, 0),
        "an actor at its level asks for nothing");
}

void UnknownSizeKeepsTheLevel() {
  const LevelHistory half{ResolutionLevel::kHalf, std::nullopt};
  Check(!LevelChange(half, 0.0f, ActorRole::kOther, 0),
        "a screen fraction of zero changes nothing");
}

void PlayerStaysFull() {
  const LevelHistory full{ResolutionLevel::kFull, std::nullopt};
  Check(!LevelChange(full, 0.01f, ActorRole::kPlayer, 0),
        "the player stays full however small");
  const LevelHistory reduced{ResolutionLevel::kQuarter, std::nullopt};
  Check(LevelChange(reduced, 0.0f, ActorRole::kPlayer, 0) ==
            ResolutionLevel::kFull,
        "a player below full returns to full");
}

void SidesScaleAndStopAtTheMinimum() {
  const PlanSides sides{{TextureSize{2048}, TextureSize{512}}};
  const PlanSides half = AtLevel(sides, ResolutionLevel::kHalf);
  Check(half.pixels == std::vector{TextureSize{1024}, TextureSize{256}},
        "a level divides every side");
  const PlanSides eighth = AtLevel(sides, ResolutionLevel::kEighth);
  Check(eighth.pixels == std::vector{TextureSize{256}, TextureSize{64}},
        "a side never falls below the minimum");
  Check(AtLevel(sides, ResolutionLevel::kFull) == sides,
        "full keeps the sides");
}
}

int main() {
  IdealLevelHalvesWithScreenSize();
  CoarserLevelsWaitForTheBand();
  ChangesAreHeldApart();
  UnknownSizeKeepsTheLevel();
  PlayerStaysFull();
  SidesScaleAndStopAtTheMinimum();
  return test::Finish("planners_resolutionlevel");
}
