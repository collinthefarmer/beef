// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ResolutionLevel.h"
#include "test_support.h"

#include <variant>

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

ActorSight Seen(float a_fraction) {
  return {a_fraction, true, ActorRole::kOther};
}

ActorSight Unseen(float a_fraction) {
  return {a_fraction, false, ActorRole::kOther};
}

constexpr std::uint32_t kNow = 100000;

void InViewStepsUpAtOnce() {
  Check(NextLevelStep(ResolutionLevel::kEighth, Seen(0.26f), std::nullopt,
                      kNow) == LevelStep{StepUpTo{ResolutionLevel::kHalf}},
        "a larger screen size steps up at once, skipping the levels between");
  Check(NextLevelStep(ResolutionLevel::kHalf, Seen(0.24f), std::nullopt,
                      kNow) == LevelStep{KeepLevel{}},
        "just under a threshold an actor keeps its level");
  Check(NextLevelStep(ResolutionLevel::kHalf, Seen(0.3f), std::nullopt, kNow) ==
            LevelStep{KeepLevel{}},
        "an actor at its level asks for nothing");
}

void OutOfViewNeverStepsUp() {
  Check(WantedLevel(Unseen(0.9f)) == ResolutionLevel::kEighth,
        "an actor out of view wants the coarsest level");
  Check(NextLevelStep(ResolutionLevel::kEighth, Unseen(0.9f), std::nullopt,
                      kNow) == LevelStep{KeepLevel{}},
        "an actor out of view never steps up");
  Check(NextLevelStep(ResolutionLevel::kFull, Unseen(0.9f), std::nullopt,
                      kNow) == LevelStep{KeepLevel{}},
        "a large actor out of view keeps its level");
}

void SmallActorsStepDownAfterTheHold() {
  Check(!SmallFor(0.41f, ResolutionLevel::kFull) &&
            SmallFor(0.39f, ResolutionLevel::kFull),
        "full is small below 0.8 of its threshold of 0.5");
  Check(!SmallFor(0.21f, ResolutionLevel::kHalf) &&
            SmallFor(0.19f, ResolutionLevel::kHalf) &&
            SmallFor(0.09f, ResolutionLevel::kQuarter),
        "each level is small below 0.8 of its own threshold");
  Check(!SmallFor(0.0f, ResolutionLevel::kEighth), "eighth is never small");
  const std::optional<std::uint32_t> since =
      SmallSince(ResolutionLevel::kFull, 0.3f, std::nullopt, kNow);
  Check(since == kNow, "the hold starts when the actor becomes small");
  Check(SmallSince(ResolutionLevel::kFull, 0.3f, since, kNow + 500) == kNow,
        "the hold keeps its start while the actor stays small");
  Check(!SmallSince(ResolutionLevel::kFull, 0.45f, since, kNow + 500),
        "the hold clears when the actor grows past the band");
  Check(NextLevelStep(ResolutionLevel::kFull, Seen(0.3f), since,
                      kNow + kStepDownHoldMS - 1) == LevelStep{KeepLevel{}},
        "no step down before the hold ends");
  Check(NextLevelStep(ResolutionLevel::kFull, Seen(0.05f), since,
                      kNow + kStepDownHoldMS) ==
            LevelStep{StepDownTo{ResolutionLevel::kHalf}},
        "after the hold an actor steps down one level, however small");
  Check(NextLevelStep(ResolutionLevel::kFull, Unseen(0.3f), since,
                      kNow + kStepDownHoldMS) ==
            LevelStep{StepDownTo{ResolutionLevel::kHalf}},
        "an actor out of view steps down by its size alone");
}

void WalkingAwayAndBackSettles() {
  ResolutionLevel level = ResolutionLevel::kQuarter;
  std::optional<std::uint32_t> since;
  std::uint32_t now = kNow;
  std::size_t changes = 0;
  for (const float fraction :
       {0.26f, 0.24f, 0.51f, 0.45f, 0.42f, 0.41f, 0.52f, 0.45f, 0.41f}) {
    for (int tick = 0; tick < 30; ++tick, now += 100) {
      since = SmallSince(level, fraction, since, now);
      const LevelStep step = NextLevelStep(level, Seen(fraction), since, now);
      if (const auto *up = std::get_if<StepUpTo>(&step)) {
        level = up->level;
        since.reset();
        ++changes;
      } else if (const auto *down = std::get_if<StepDownTo>(&step)) {
        level = down->level;
        since.reset();
        ++changes;
      }
    }
  }
  Check(changes == 2 && level == ResolutionLevel::kFull,
        "moving inside the band changes nothing");
}

void UnmeasuredSizeKeepsTheLevel() {
  const ActorSight unmeasured{std::nullopt, true, ActorRole::kOther};
  Check(!WantedLevel(unmeasured), "an unmeasured actor wants nothing");
  Check(!SmallSince(ResolutionLevel::kFull, std::nullopt, std::nullopt, kNow),
        "an unmeasured actor never starts the hold");
  Check(NextLevelStep(ResolutionLevel::kEighth, unmeasured, std::nullopt,
                      kNow) == LevelStep{KeepLevel{}},
        "an unmeasured actor keeps its level");
}

void PlayerStaysFull() {
  const ActorSight player{0.01f, true, ActorRole::kPlayer};
  Check(NextLevelStep(ResolutionLevel::kFull, player, kNow,
                      kNow + 10 * kStepDownHoldMS) == LevelStep{KeepLevel{}},
        "the player stays full however small");
  Check(NextLevelStep(ResolutionLevel::kQuarter,
                      {std::nullopt, false, ActorRole::kPlayer}, std::nullopt,
                      kNow) == LevelStep{StepUpTo{ResolutionLevel::kFull}},
        "a player below full returns to full");
}

void LargerOnScreenPutsUnmeasuredLast() {
  Check(LargerOnScreen(0.3f, 0.2f) && !LargerOnScreen(0.2f, 0.3f),
        "the larger fraction comes first");
  Check(LargerOnScreen(0.0f, std::nullopt) &&
            !LargerOnScreen(std::nullopt, 0.0f) &&
            !LargerOnScreen(std::nullopt, std::nullopt),
        "an unmeasured actor comes after every measured one");
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
  InViewStepsUpAtOnce();
  OutOfViewNeverStepsUp();
  SmallActorsStepDownAfterTheHold();
  WalkingAwayAndBackSettles();
  UnmeasuredSizeKeepsTheLevel();
  PlayerStaysFull();
  LargerOnScreenPutsUnmeasuredLast();
  SidesScaleAndStopAtTheMinimum();
  CoarserLevelsStepOnce();
  return test::Finish("planners_resolutionlevel");
}
