// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderBudget.h"
#include "test_support.h"

#include <limits>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
constexpr std::uint64_t kGiB = 1024 * kMebibyte;

BudgetActor Npc(ResolutionLevel a_level, float a_fraction,
                std::uint64_t a_freed) {
  BudgetActor actor;
  actor.level = a_level;
  actor.screenFraction = a_fraction;
  if (const std::optional<ResolutionLevel> coarser = CoarserLevel(a_level)) {
    actor.stepDown = StepDownOption{*coarser, a_freed};
  }
  return actor;
}

BudgetActor Hidden(std::uint64_t a_releasable) {
  BudgetActor actor = Npc(ResolutionLevel::kFull, 0.5f, a_releasable);
  actor.inView = false;
  actor.releasable = a_releasable;
  return actor;
}

void AllowanceFollowsHeadroom() {
  const BudgetSample sample{VideoMemory{8 * kGiB, 6 * kGiB}, 1 * kGiB, 0};
  Equal(MarginOf(8 * kGiB), kMinBudgetMargin,
        "a small budget keeps the 512 MiB margin");
  Equal(MarginOf(20 * kGiB), 1 * kGiB, "a large budget keeps 5 % as margin");
  Check(AllowanceOf(sample) == 1 * kGiB + 2 * kGiB - kMinBudgetMargin,
        "the allowance is the plugin's bytes plus headroom less the margin");
  BudgetSample capped = sample;
  capped.capBytes = 1 * kGiB;
  Check(AllowanceOf(capped) == 1 * kGiB, "the cap lowers the allowance");
  capped.capBytes = 100 * kGiB;
  Check(AllowanceOf(capped) == AllowanceOf(sample),
        "a cap above the allowance changes nothing");
}

void UnknownBudgetDoesNothing() {
  const BudgetSample unknown{std::nullopt, 4 * kGiB, 1 * kGiB};
  Check(!AllowanceOf(unknown), "no DXGI reading gives no allowance");
  const BudgetState state = NextBudget({}, AllowanceOf(unknown), 9 * kGiB);
  Check(state.pressure == Pressure::kNone, "an unknown budget never presses");
  Check(AdmitGrowth(state, 9 * kGiB) == Admission::kAdmitted,
        "an unknown budget admits every step up");
  const BudgetState wasOver{2 * kGiB, 3 * kGiB, Pressure::kOverWarned};
  Check(NextBudget(wasOver, std::nullopt, 3 * kGiB).pressure == Pressure::kNone,
        "a budget that becomes unknown ends the pressure");
}

void GarbageReadingsStayBounded() {
  Check(!AllowanceOf({VideoMemory{0, 0}, 1 * kGiB, 0}),
        "a zero budget is unknown");
  Check(AllowanceOf({VideoMemory{2 * kGiB, 5 * kGiB}, 0, 0}) ==
            std::optional<std::uint64_t>{0},
        "usage over the budget leaves no allowance");
  Check(AllowanceOf({VideoMemory{8 * kGiB, 1 * kGiB}, 6 * kGiB, 0}) ==
            8 * kGiB - kMinBudgetMargin,
        "plugin bytes above the usage count only up to the usage");
  constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
  Check(AllowanceOf({VideoMemory{kMax, 0}, kMax, 0}).has_value(),
        "huge values do not overflow");
  Equal(BandOf(0), std::uint64_t{0}, "a zero allowance has no band");
  Equal(BandOf(200 * kMebibyte), 100 * kMebibyte,
        "a small allowance keeps half of itself below the band");
  const BudgetState none = NextBudget({}, 0, 0);
  Check(none.pressure == Pressure::kNone, "nothing planned is not over zero");
  Check(NextBudget({}, 0, 1).pressure == Pressure::kOver,
        "any plan is over a zero allowance");
  Check(AdmitGrowth(none, kMax) == Admission::kOverAllowance,
        "growth that overflows is refused");
}

void PressureHasABand() {
  const std::uint64_t allowance = 4 * kGiB;
  const std::uint64_t band = BandOf(allowance);
  Equal(band, 4 * kGiB / 10, "the band is 10 % of a large allowance");
  Equal(BandOf(1 * kGiB), kMinPressureBand, "the band is at least 256 MiB");
  const BudgetState calm = NextBudget({}, allowance, allowance);
  Check(calm.pressure == Pressure::kNone,
        "planned bytes equal to the allowance do not press");
  const BudgetState over = NextBudget(calm, allowance, allowance + 1);
  Check(over.pressure == Pressure::kOver,
        "planned bytes over the allowance start pressure");
  Check(NextBudget(over, allowance, allowance - 1).pressure == Pressure::kOver,
        "falling just under the allowance keeps the pressure");
  Check(NextBudget(over, allowance, allowance - band).pressure ==
            Pressure::kOver,
        "the band edge keeps the pressure");
  Check(NextBudget(over, allowance, allowance - band - 1).pressure ==
            Pressure::kNone,
        "falling below the band ends the pressure");
  const BudgetState warned{allowance, allowance + 1, Pressure::kOverWarned};
  Check(NextBudget(warned, allowance, allowance + 1).pressure ==
            Pressure::kOverWarned,
        "a warned episode stays warned");
  Equal(PressureGoal(allowance), allowance - band,
        "the goal is the allowance less the band");
}

void ResizeNeedsOldAndNewBytes() {
  const BudgetState state{3 * kGiB, 2 * kGiB, Pressure::kNone};
  Check(AdmitGrowth(state, 1 * kGiB) == Admission::kAdmitted,
        "a build that fits on top of the holding is admitted");
  Check(AdmitGrowth(state, 1 * kGiB + 1) == Admission::kOverAllowance,
        "a build that needs more than the headroom waits");
  const BudgetState after = Admitted(state, 1 * kGiB);
  Equal(after.planned, 3 * kGiB, "an admitted build counts at once");
  Check(AdmitGrowth(after, 1) == Admission::kOverAllowance,
        "a second build in the same interval sees the first");
  const BudgetState pressed{3 * kGiB, 1 * kGiB, Pressure::kOver};
  Check(AdmitGrowth(pressed, 1) == Admission::kUnderPressure,
        "no step up is admitted under pressure");
}

void StepDownPricesBothLevels() {
  const LevelCost full{800 * kMebibyte, 200 * kMebibyte};
  const LevelCost half{200 * kMebibyte, 50 * kMebibyte};
  Check(StepDownOf(ResolutionLevel::kFull, full, half,
                   IntermediateHolding::kHeld) ==
            StepDownOption{ResolutionLevel::kHalf, 600 * kMebibyte},
        "a step down frees the difference of the held bytes");
  Check(StepDownOf(ResolutionLevel::kFull, full, half,
                   IntermediateHolding::kReleased) ==
            StepDownOption{ResolutionLevel::kHalf, 150 * kMebibyte},
        "a released actor frees only the difference of its stacks");
  Check(!StepDownOf(ResolutionLevel::kEighth, full, half,
                    IntermediateHolding::kHeld),
        "the coarsest level has no step down");
  Check(!StepDownOf(ResolutionLevel::kFull, full, std::nullopt,
                    IntermediateHolding::kHeld),
        "an unpriced level has no step down");
  Check(
      StepDownOf(ResolutionLevel::kFull, half, full, IntermediateHolding::kHeld)
              ->freed == 0,
      "a coarser level that costs more frees nothing");
}

void HiddenActorsReleaseFirst() {
  const std::vector<BudgetActor> actors{
      Npc(ResolutionLevel::kFull, 0.1f, 500 * kMebibyte), Hidden(100),
      Hidden(300)};
  const BudgetResponse small = RespondToPressure(1000, 800, actors, false);
  Check(small.release == std::vector<std::size_t>{2},
        "the largest hidden holding releases first and enough stops");
  Check(!small.stepDown, "a release that fits needs no step down");
  const BudgetResponse large = RespondToPressure(1000, 500, actors, false);
  Check(large.release == std::vector<std::size_t>{2, 1},
        "every hidden actor releases before a step down");
  Check(large.stepDown && large.stepDown->actor == 0,
        "a step down follows when the releases do not fit");
  Check(RespondToPressure(400, 500, actors, false).release.empty(),
        "nothing happens below the goal");
}

void StepDownOrder() {
  BudgetActor player = Npc(ResolutionLevel::kFull, 0.01f, 9 * kGiB);
  player.role = ActorRole::kPlayer;
  BudgetActor resizing = Npc(ResolutionLevel::kFull, 0.01f, 9 * kGiB);
  resizing.resizing = true;
  const std::vector<BudgetActor> actors{
      player,
      resizing,
      Npc(ResolutionLevel::kFull, 0.6f, 4 * kGiB),
      Npc(ResolutionLevel::kFull, 0.2f, 1 * kGiB),
      Npc(ResolutionLevel::kHalf, 0.05f, 2 * kGiB),
      Npc(ResolutionLevel::kEighth, 0.01f, 0)};
  const BudgetResponse response =
      RespondToPressure(10 * kGiB, 1 * kGiB, actors, false);
  Check(response.stepDown && response.stepDown->actor == 4,
        "the NPC finest against its screen need steps down, bytes break the "
        "tie");
  Check(response.stepDown &&
            response.stepDown->option.to == ResolutionLevel::kQuarter,
        "a step down goes one level coarser");
  Check(!RespondToPressure(10 * kGiB, 1 * kGiB, actors, true).stepDown,
        "no step down starts while a resize is in flight");
  Check(!RespondToPressure(10 * kGiB, 1 * kGiB, actors, true).exhausted,
        "a resize in flight is not exhaustion");
}

void PlayerIsNeverPicked() {
  BudgetActor player = Npc(ResolutionLevel::kFull, 0.0f, 9 * kGiB);
  player.role = ActorRole::kPlayer;
  const std::vector<BudgetActor> actors{player,
                                        Npc(ResolutionLevel::kEighth, 0.0f, 0)};
  const BudgetResponse response =
      RespondToPressure(10 * kGiB, 1 * kGiB, actors, false);
  Check(!response.stepDown, "the player never steps down");
  Check(response.exhausted,
        "pressure with only the player and coarsest NPCs is exhausted");
}

void LinesCarryTheNumbers() {
  const BudgetState state{1 * kGiB, 2 * kGiB, Pressure::kOver};
  Equal(PressureStartLine({VideoMemory{8 * kGiB, 7 * kGiB}, 2 * kGiB, kGiB},
                          state),
        std::string{"render target budget: pressure starts: planned 2048 MiB "
                    "is over the allowance 1024 MiB (DXGI budget 8192 MiB, "
                    "usage 7168 MiB, plugin targets 2048 MiB, cap 1024 MiB)"},
        "the start line gives the numbers");
  Equal(PressureEndLine({1 * kGiB, 512 * kMebibyte, Pressure::kNone}),
        std::string{"render target budget: pressure ends: planned 512 MiB is "
                    "under the allowance 1024 MiB less the band 256 MiB"},
        "the end line gives the numbers");
  Equal(StepDownLine(0x1A2B, "Vigilant of Stendarr", ResolutionLevel::kFull,
                     {ResolutionLevel::kHalf, 600 * kMebibyte}),
        std::string{"actor 00001A2B (Vigilant of Stendarr) steps down from "
                    "full to half resolution: frees 600 MiB (render target "
                    "budget)"},
        "the step down line names the actor, the levels and the bytes");
  const std::vector<ActorHolding> largest{
      {0x14, "Prisoner", 300 * kMebibyte, 0, 0.2f, {}}};
  Equal(ExhaustedLine(state, largest),
        std::string{"render target budget: no release or step down is left: "
                    "planned 2048 MiB against the allowance 1024 MiB; the "
                    "largest holdings: Prisoner (00000014) 300 MiB"},
        "the exhausted line names the largest holdings");
}
}

int main() {
  AllowanceFollowsHeadroom();
  UnknownBudgetDoesNothing();
  GarbageReadingsStayBounded();
  PressureHasABand();
  ResizeNeedsOldAndNewBytes();
  StepDownPricesBothLevels();
  HiddenActorsReleaseFirst();
  StepDownOrder();
  PlayerIsNeverPicked();
  LinesCarryTheNumbers();
  return test::Finish("planners_renderbudget");
}
