// GPL-3.0-only with the additional permission in COPYING.md.
#include "Core.h"
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

std::vector<BudgetActor> Numbered(std::vector<BudgetActor> a_actors) {
  for (std::size_t i = 0; i < a_actors.size(); ++i) {
    a_actors[i].actor = static_cast<std::uint32_t>(i + 1);
  }
  return a_actors;
}

std::vector<std::uint32_t> ReleasedActors(const BudgetResponse &a_response) {
  std::vector<std::uint32_t> actors;
  for (const BudgetRelease &release : a_response.release) {
    actors.push_back(release.actor);
  }
  return actors;
}

BudgetActor Hidden(std::uint64_t a_releasable) {
  BudgetActor actor = Npc(ResolutionLevel::kFull, 0.5f, a_releasable);
  actor.inView = false;
  actor.releasable = a_releasable;
  return actor;
}

void AllowanceIsFreeVideoMemoryLessMargin() {
  const BudgetSample sample{VideoMemory{8 * kGiB, 6 * kGiB}, 1 * kGiB, 0};
  Equal(MarginOf(8 * kGiB), kMinBudgetMargin,
        "a small budget keeps the 512 MiB margin");
  Equal(MarginOf(20 * kGiB), 1 * kGiB, "a large budget keeps 5 % as margin");
  Check(AllowanceOf(sample) == 1 * kGiB + 2 * kGiB - kMinBudgetMargin,
        "the allowance is the plugin's bytes plus free video memory less the "
        "margin");
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

void GrowthMustFitTheAllowance() {
  const BudgetState state{3 * kGiB, 2 * kGiB, Pressure::kNone};
  Check(AdmitGrowth(state, 1 * kGiB) == Admission::kAdmitted,
        "growth that fits on top of the planned bytes is admitted");
  Check(AdmitGrowth(state, 1 * kGiB + 1) == Admission::kOverAllowance,
        "growth that needs more than the headroom waits");
  const BudgetState after = Charged(state, 1 * kGiB);
  Equal(after.planned, 3 * kGiB, "admitted growth is charged at once");
  Check(AdmitGrowth(after, 1) == Admission::kOverAllowance,
        "a second step up in the same interval sees the first");
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
        "a step down frees the difference of the planned bytes");
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
  const std::vector<BudgetActor> actors =
      Numbered({Npc(ResolutionLevel::kFull, 0.1f, 500 * kMebibyte), Hidden(100),
                Hidden(300)});
  const BudgetResponse small = RespondToPressure(1000, 800, actors, false);
  Check(small.release == std::vector<BudgetRelease>{{3, 300}},
        "the largest hidden holding releases first and enough stops");
  Check(Is<Enough>(small.next), "a release that fits needs no step down");
  const BudgetResponse large = RespondToPressure(1000, 500, actors, false);
  Check(ReleasedActors(large) == std::vector<std::uint32_t>{3, 2},
        "every hidden actor releases before a step down");
  const StepDown *stepDown = Get<StepDown>(large.next);
  Check(stepDown && stepDown->actor == 1,
        "a step down follows when the releases do not fit");
  Check(RespondToPressure(400, 500, actors, false).release.empty(),
        "nothing happens below the goal");
}

void WidestGapThenMostFreedStepsDownFirst() {
  BudgetActor player = Npc(ResolutionLevel::kFull, 0.01f, 9 * kGiB);
  player.role = ActorRole::kPlayer;
  BudgetActor resizing = Npc(ResolutionLevel::kFull, 0.01f, 9 * kGiB);
  resizing.resizing = true;
  const std::vector<BudgetActor> actors =
      Numbered({player, resizing, Npc(ResolutionLevel::kFull, 0.6f, 4 * kGiB),
                Npc(ResolutionLevel::kFull, 0.2f, 1 * kGiB),
                Npc(ResolutionLevel::kHalf, 0.05f, 2 * kGiB),
                Npc(ResolutionLevel::kEighth, 0.01f, 0)});
  const BudgetResponse response =
      RespondToPressure(10 * kGiB, 1 * kGiB, actors, false);
  const StepDown *stepDown = Get<StepDown>(response.next);
  Check(stepDown && stepDown->actor == 5,
        "the NPC finest against its screen need steps down, bytes break the "
        "tie");
  Check(stepDown && stepDown->option.to == ResolutionLevel::kQuarter,
        "a step down goes one level coarser");
  Check(Is<AwaitResize>(
            RespondToPressure(10 * kGiB, 1 * kGiB, actors, true).next),
        "no step down starts while a resize is in flight, and that is not "
        "exhaustion");
}

void PlayerIsNeverPicked() {
  BudgetActor player = Npc(ResolutionLevel::kFull, 0.0f, 9 * kGiB);
  player.role = ActorRole::kPlayer;
  const std::vector<BudgetActor> actors{player,
                                        Npc(ResolutionLevel::kEighth, 0.0f, 0)};
  const BudgetResponse response =
      RespondToPressure(10 * kGiB, 1 * kGiB, actors, false);
  Check(!Is<StepDown>(response.next), "the player never steps down");
  Check(Is<Exhausted>(response.next),
        "pressure with only the player and coarsest NPCs is exhausted");
}

void HeadroomFollowsTheState() {
  Check(!HeadroomOf(BudgetState{}, 0).has_value(),
        "an unknown allowance gives no headroom bound");
  Check(HeadroomOf({1 * kGiB, 400 * kMebibyte, Pressure::kNone}, 0) ==
            624 * kMebibyte,
        "the headroom is the allowance less the planned bytes");
  Check(HeadroomOf({1 * kGiB, 2 * kGiB, Pressure::kNone}, 0) == 0,
        "planned bytes over the allowance leave no headroom");
  Check(HeadroomOf({1 * kGiB, 100 * kMebibyte, Pressure::kOver}, 0) == 0,
        "pressure leaves no headroom");
  Check(HeadroomOf({1 * kGiB, 100 * kMebibyte, Pressure::kOverWarned}, 0) == 0,
        "warned pressure leaves no headroom");
}

void InstallTakesTheFinestFittingLevel() {
  const LevelBytes vigilant{1600 * kMebibyte, 600 * kMebibyte, 150 * kMebibyte,
                            40 * kMebibyte};
  Equal(FittingLevel(ResolutionLevel::kFull, vigilant, std::nullopt),
        ResolutionLevel::kFull, "no headroom bound keeps the finest level");
  Equal(FittingLevel(ResolutionLevel::kQuarter, vigilant, std::nullopt),
        ResolutionLevel::kQuarter, "no headroom bound keeps an ideal quarter");
  Equal(FittingLevel(ResolutionLevel::kFull, vigilant, 2 * kGiB),
        ResolutionLevel::kFull, "full fits a large headroom");
  Equal(FittingLevel(ResolutionLevel::kFull, vigilant, 1600 * kMebibyte),
        ResolutionLevel::kFull, "a level that fits exactly is taken");
  Equal(FittingLevel(ResolutionLevel::kFull, vigilant, 1 * kGiB),
        ResolutionLevel::kHalf, "1 GiB of headroom takes half");
  Equal(FittingLevel(ResolutionLevel::kFull, vigilant, 200 * kMebibyte),
        ResolutionLevel::kQuarter, "200 MiB of headroom takes quarter");
  Equal(FittingLevel(ResolutionLevel::kQuarter, vigilant, 2 * kGiB),
        ResolutionLevel::kQuarter, "the ideal level bounds the install");
  Equal(FittingLevel(ResolutionLevel::kFull, vigilant, 0),
        ResolutionLevel::kEighth, "no headroom takes eighth");
  Equal(FittingLevel(ResolutionLevel::kFull, vigilant, 10 * kMebibyte),
        ResolutionLevel::kEighth, "eighth is taken even when it does not fit");
  const LevelBytes unpriced{std::nullopt, std::nullopt, 150 * kMebibyte,
                            std::nullopt};
  Equal(FittingLevel(ResolutionLevel::kFull, unpriced, 1 * kGiB),
        ResolutionLevel::kQuarter, "an unpriced level never fits");
  Equal(FittingLevel(ResolutionLevel::kFull, LevelBytes{}, 1 * kGiB),
        ResolutionLevel::kEighth, "no priced level takes eighth");
  const std::uint64_t most = std::numeric_limits<std::uint64_t>::max();
  Equal(FittingLevel(ResolutionLevel::kFull, {most, most, most, most}, most),
        ResolutionLevel::kFull, "the largest values compare without overflow");
}

void ReinstallFitsWithItsOwnHolding() {
  const LevelBytes vigilant{1600 * kMebibyte, 600 * kMebibyte, 150 * kMebibyte,
                            40 * kMebibyte};
  const BudgetState state{1 * kGiB, 900 * kMebibyte, Pressure::kNone};
  Check(HeadroomOf(state, 500 * kMebibyte) == 624 * kMebibyte,
        "a reinstall counts its own holding as freed");
  Check(HeadroomOf(state, 2 * kGiB) == 1 * kGiB,
        "a holding larger than the planned bytes frees them all");
  Equal(FittingLevel(ResolutionLevel::kHalf, vigilant,
                     HeadroomOf(state, 500 * kMebibyte)),
        ResolutionLevel::kHalf,
        "a reinstall that fits keeps its tracked level with its holding "
        "freed");
  Equal(FittingLevel(ResolutionLevel::kHalf, vigilant, HeadroomOf(state, 0)),
        ResolutionLevel::kEighth,
        "without the credit its own bytes would push it down");
  Equal(FittingLevel(ResolutionLevel::kHalf, vigilant,
                     HeadroomOf(state, 2 * kGiB)),
        ResolutionLevel::kHalf, "a reinstall never builds finer than tracked");
  const BudgetState amulet{256 * kMebibyte, 16 * kMebibyte, Pressure::kNone};
  Equal(FittingLevel(ResolutionLevel::kFull, vigilant,
                     HeadroomOf(amulet, 16 * kMebibyte)),
        ResolutionLevel::kQuarter,
        "a reinstall that grows past the headroom takes a coarser level");
  const BudgetState over{1 * kGiB, 1200 * kMebibyte, Pressure::kOver};
  Check(HeadroomOf(over, 600 * kMebibyte) == 600 * kMebibyte,
        "under pressure a reinstall may keep its own holding");
  Equal(FittingLevel(ResolutionLevel::kHalf, vigilant,
                     HeadroomOf(over, 600 * kMebibyte)),
        ResolutionLevel::kHalf, "under pressure an unchanged reinstall stays");
  Equal(FittingLevel(ResolutionLevel::kFull, vigilant,
                     HeadroomOf(over, 600 * kMebibyte)),
        ResolutionLevel::kHalf, "under pressure a reinstall does not grow");
  Check(!HeadroomOf(BudgetState{}, 500 * kMebibyte).has_value(),
        "an unknown allowance keeps the tracked level");
  Check(Released({1 * kGiB, 900 * kMebibyte, Pressure::kNone}, 500 * kMebibyte)
                .planned == 400 * kMebibyte,
        "a released holding leaves the planned bytes");
  Check(Released({1 * kGiB, 100 * kMebibyte, Pressure::kNone}, 1 * kGiB)
                .planned == 0,
        "a release never takes the planned bytes below 0");
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
  AllowanceIsFreeVideoMemoryLessMargin();
  UnknownBudgetDoesNothing();
  GarbageReadingsStayBounded();
  PressureHasABand();
  GrowthMustFitTheAllowance();
  StepDownPricesBothLevels();
  HiddenActorsReleaseFirst();
  WidestGapThenMostFreedStepsDownFirst();
  PlayerIsNeverPicked();
  HeadroomFollowsTheState();
  InstallTakesTheFinestFittingLevel();
  ReinstallFitsWithItsOwnHolding();
  LinesCarryTheNumbers();
  return test::Finish("planners_renderbudget");
}
