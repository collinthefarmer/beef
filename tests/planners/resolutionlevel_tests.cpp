// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ResolutionLevel.h"
#include "test_support.h"

#include <cstddef>
#include <cstdint>
#include <optional>
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

void InViewStepsUpAtOnce() {
  Check(LevelStepOf(ResolutionLevel::kEighth, Seen(0.26f), false) ==
            LevelStep{StepUpTo{ResolutionLevel::kHalf}},
        "a larger screen size steps up at once, skipping the levels between");
  Check(LevelStepOf(ResolutionLevel::kHalf, Seen(0.24f), false) ==
            LevelStep{KeepLevel{}},
        "just under a threshold an actor keeps its level");
  Check(LevelStepOf(ResolutionLevel::kHalf, Seen(0.3f), false) ==
            LevelStep{KeepLevel{}},
        "an actor at its level asks for nothing");
}

void OutOfViewNeverStepsUp() {
  Check(WantedLevel(Unseen(0.9f)) == ResolutionLevel::kEighth,
        "an actor out of view wants the coarsest level");
  Check(LevelStepOf(ResolutionLevel::kEighth, Unseen(0.9f), false) ==
            LevelStep{KeepLevel{}},
        "an actor out of view never steps up");
  Check(LevelStepOf(ResolutionLevel::kFull, Unseen(0.9f), false) ==
            LevelStep{KeepLevel{}},
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
  Check(LevelStepOf(ResolutionLevel::kFull, Seen(0.3f), false) ==
            LevelStep{KeepLevel{}},
        "no step down before the hold ends");
  Check(LevelStepOf(ResolutionLevel::kFull, Seen(0.05f), true) ==
            LevelStep{StepDownTo{ResolutionLevel::kHalf}},
        "after the hold an actor steps down one level, however small");
  Check(LevelStepOf(ResolutionLevel::kFull, Unseen(0.3f), true) ==
            LevelStep{StepDownTo{ResolutionLevel::kHalf}},
        "an actor out of view steps down by its size alone");
}

std::optional<std::uint32_t> SmallSinceNow(ResolutionLevel a_level,
                                           float a_fraction,
                                           std::optional<std::uint32_t> a_since,
                                           std::uint32_t a_now) {
  if (!SmallFor(a_fraction, a_level)) {
    return std::nullopt;
  }
  return a_since.value_or(a_now);
}

void WalkingAwayAndBackSettles() {
  ResolutionLevel level = ResolutionLevel::kQuarter;
  std::optional<std::uint32_t> since;
  std::uint32_t now = 100000;
  std::size_t changes = 0;
  for (const float fraction :
       {0.26f, 0.24f, 0.51f, 0.45f, 0.42f, 0.41f, 0.52f, 0.45f, 0.41f}) {
    for (int tick = 0; tick < 30; ++tick, now += 100) {
      since = SmallSinceNow(level, fraction, since, now);
      const LevelStep step = LevelStepOf(
          level, Seen(fraction), since && now - *since >= kStepDownHoldMS);
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
  Check(!SmallFor(std::nullopt, ResolutionLevel::kFull),
        "an unmeasured actor is never small");
  Check(LevelStepOf(ResolutionLevel::kEighth, unmeasured, false) ==
            LevelStep{KeepLevel{}},
        "an unmeasured actor keeps its level");
}

void PlayerStaysFull() {
  const ActorSight player{0.01f, true, ActorRole::kPlayer};
  Check(LevelStepOf(ResolutionLevel::kFull, player, true) ==
            LevelStep{KeepLevel{}},
        "the player stays full however small");
  Check(LevelStepOf(ResolutionLevel::kQuarter,
                    {std::nullopt, false, ActorRole::kPlayer},
                    false) == LevelStep{StepUpTo{ResolutionLevel::kFull}},
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

void ADueStepDownStepsOneLevel() {
  Check(LevelStepOf(ResolutionLevel::kFull, Seen(0.05f), true) ==
            LevelStep{StepDownTo{ResolutionLevel::kHalf}},
        "a due step-down steps one level, however small the actor");
  Check(LevelStepOf(ResolutionLevel::kFull, Seen(0.05f), false) ==
            LevelStep{KeepLevel{}},
        "a step-down that is not due keeps the level");
  Check(LevelStepOf(ResolutionLevel::kEighth, Seen(0.05f), true) ==
            LevelStep{KeepLevel{}},
        "the coarsest level does not step down");
  Check(LevelStepOf(ResolutionLevel::kQuarter, Seen(0.6f), true) ==
            LevelStep{StepUpTo{ResolutionLevel::kFull}},
        "a larger wanted level steps up even when a step-down is due");
  Check(LevelStepOf(ResolutionLevel::kHalf, {0.05f, true, ActorRole::kPlayer},
                    true) == LevelStep{StepUpTo{ResolutionLevel::kFull}},
        "the player steps up to full and never down");
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
  ADueStepDownStepsOneLevel();
  return test::Finish("planners_resolutionlevel");
}
