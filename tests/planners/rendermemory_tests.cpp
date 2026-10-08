// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderMemory.h"
#include "test_support.h"

#include <optional>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
constexpr std::uint64_t kGiB = kHoldingStepBytes;

void LevelsRiseAndFallWithHysteresis() {
  Check(NextHoldingStep(0, kGiB / 2) == HoldingStep{0, Crossing::kNone},
        "below the first step nothing is reported");
  Check(NextHoldingStep(0, kGiB) == HoldingStep{1, Crossing::kRose},
        "reaching a step rises");
  Check(NextHoldingStep(1, 3 * kGiB + 1) == HoldingStep{3, Crossing::kRose},
        "a jump rises to the level reached");
  Check(NextHoldingStep(3, 3 * kGiB - kGiB / 4) ==
            HoldingStep{3, Crossing::kNone},
        "a small dip below the step does not fall");
  Check(NextHoldingStep(3, 2 * kGiB) == HoldingStep{2, Crossing::kFell},
        "half a step below the level falls");
  Check(NextHoldingStep(1, 0) == HoldingStep{0, Crossing::kFell},
        "an empty pool falls to zero");
}

void BurstsNeedAQuarterGibibyte() {
  Check(!IsBurst({10, kBurstBytes - 1, 0}) && IsBurst({10, kBurstBytes, 0}),
        "a burst starts at its byte threshold");
}

void SpreadsSummariseScreenSizes() {
  Check(SpreadOf({}) == ScreenSpread{}, "no actor gives an empty spread");
  Check(SpreadOf({0.3f, 0.1f, 0.2f}) == ScreenSpread{0.1f, 0.2f, 0.3f},
        "the spread is the least, the median and the most");
}

void LargestHoldingsComeFirst() {
  const std::vector<ActorHolding> holdings{
      {1, "A", 10, 0, 0.1f, {}}, {2, "B", 50, 0, 0.2f, {}},
      {3, "C", 30, 0, 0.3f, {}}, {4, "D", 20, 0, 0.4f, {}},
      {5, "E", 40, 0, 0.5f, {}}, {6, "F", 60, 0, 0.6f, {}}};
  const std::vector<ActorHolding> largest =
      LargestHoldings(holdings, kReportedHoldings);
  Equal(largest.size(), kReportedHoldings, "the report keeps five actors");
  Check(largest.front().name == "F" && largest.back().name == "D",
        "the report orders actors by bytes");
}

void SharedTargetsAreNamed() {
  const TextureSize size{512};
  const TextureFormat format = TextureFormat::kRgba8;
  const std::uint64_t one = TargetBytes(size, format);
  const std::vector<ActorTargets> actors{
      {1,
       "A",
       0.1f,
       {},
       {{size, format, "mask"}, {size, format, std::nullopt}}},
      {2, "B", 0.2f, {}, {{size, format, "mask"}, {size, format, "own"}}}};
  const std::vector<ActorHolding> holdings = HoldingsOf(actors);
  Check(holdings.size() == 2 && holdings[0].ownBytes == one &&
            holdings[0].sharedBytes == one && holdings[1].ownBytes == one &&
            holdings[1].sharedBytes == one,
        "a key that two actors hold counts as shared in both, not as own");
}

void OwnBytesAddUpToThePlannedTotal() {
  const TextureSize size{512};
  const TextureFormat format = TextureFormat::kRgba8;
  const std::vector<ActorTargets> actors{
      {1,
       "A",
       0.1f,
       {},
       {{size, format, "mask"}, {size, format, std::nullopt}}},
      {2, "B", 0.2f, {}, {{size, format, "mask"}, {size, format, "own"}}}};
  std::vector<HeldTarget> all;
  for (const ActorTargets &actor : actors) {
    all.insert(all.end(), actor.targets.begin(), actor.targets.end());
  }
  std::uint64_t own = 0;
  for (const ActorHolding &holding : HoldingsOf(actors)) {
    own += holding.ownBytes;
  }
  Equal(own + TargetBytes(size, format), CostOf(all).bytes,
        "the actors' own bytes and each shared target once make the total");
}

void LinesNameActorsAndRecipes() {
  const std::vector<ActorHolding> largest{
      {0x1A2B3,
       "Bandit",
       std::uint64_t{1100} << 20,
       std::uint64_t{300} << 20,
       0.12f,
       {"EnchArmorFireFXS"}},
      {0xFF001BBA, "", std::uint64_t{10} << 20, 0, 0.5f, {}}};
  Equal(HoldingLine({278, std::uint64_t{3400} << 20},
                    {250, std::uint64_t{3000} << 20}, largest),
        std::string{"render targets: 3400 MiB in 278 targets; plans hold "
                    "3000 MiB in 250 targets; Bandit (0001A2B3) 1100 MiB own "
                    "and 300 MiB shared, 0.12 of the screen height, recipes "
                    "EnchArmorFireFXS; FF001BBA 10 MiB own and 0 MiB shared, "
                    "0.50 of the screen height, recipes none"},
        "the holding line names each actor, its bytes, size and recipes");
  Equal(
      BurstLine({101, std::uint64_t{880} << 20, std::uint64_t{480} << 20, 1004},
                "reinstalled 1 (Bandit)"),
      std::string{"render targets: created 880 MiB in 101 targets and "
                  "destroyed 480 MiB within 1004 ms; syncs: reinstalled 1 "
                  "(Bandit)"},
      "the burst line names the bytes, the interval and the syncs");
  Check(BurstLine({}, "").ends_with("syncs: none"),
        "a burst with no syncs says so");
}
}

int main() {
  LevelsRiseAndFallWithHysteresis();
  BurstsNeedAQuarterGibibyte();
  SpreadsSummariseScreenSizes();
  LargestHoldingsComeFirst();
  SharedTargetsAreNamed();
  OwnBytesAddUpToThePlannedTotal();
  LinesNameActorsAndRecipes();
  return test::Finish("render memory");
}
