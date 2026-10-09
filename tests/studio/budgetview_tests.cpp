// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/BudgetView.h"
#include "test_support.h"

#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;
using test::Equal;

namespace {
constexpr std::uint64_t kGiB = 1024 * kMebibyte;

PerLevel<std::size_t> Levels() {
  PerLevel<std::size_t> levels;
  levels.At(ResolutionLevel::kFull) = 3;
  levels.At(ResolutionLevel::kHalf) = 1;
  levels.At(ResolutionLevel::kEighth) = 2;
  return levels;
}

std::string ValueOf(const std::vector<ReadoutLine> &a_lines,
                    std::string_view a_label) {
  for (const ReadoutLine &line : a_lines) {
    if (line.label == a_label) {
      return line.value;
    }
  }
  return "(missing)";
}

void KnownMemoryWithCap() {
  const BudgetSample sample{VideoMemory{8 * kGiB, 6 * kGiB}, 900 * kMebibyte,
                            1 * kGiB};
  const BudgetState state{1 * kGiB, 1200 * kMebibyte, Pressure::kOver};
  const BudgetView view =
      BudgetViewOf(sample, state, BudgetRadius{1500.0f, 0, false}, Levels());
  const std::vector<ReadoutLine> lines = BudgetReadout(view);
  Equal(ValueOf(lines, "video memory"),
        std::string{"budget 8192 MiB, usage 6144 MiB"},
        "the DXGI budget and usage show in MiB");
  Equal(ValueOf(lines, "free video memory"), std::string{"2048 MiB"},
        "free video memory is the budget less the usage");
  Equal(ValueOf(lines, "allowance"), std::string{"1024 MiB (the cap)"},
        "a cap under the free video memory names the cap");
  Equal(ValueOf(lines, "planned"), std::string{"1200 MiB"},
        "the planned bytes come from the budget state");
  Equal(ValueOf(lines, "headroom"), std::string{"0 MiB"},
        "planned bytes over the allowance leave no headroom");
  Equal(ValueOf(lines, "measured targets"), std::string{"900 MiB"},
        "the measured bytes are the plugin's targets less scratch");
  Equal(ValueOf(lines, "pressure"), std::string{"over"},
        "pressure over shows as over");
  Equal(ValueOf(lines, "budget radius"), std::string{"1500 units"},
        "a budget radius shows its distance");
  Equal(ValueOf(lines, "actors by resolution"),
        std::string{"full 3, half 1, quarter 0, eighth 2"},
        "the level counts show in level order");
}

void FreeVideoMemorySetsTheAllowance() {
  const BudgetSample sample{VideoMemory{8 * kGiB, 6 * kGiB}, 1 * kGiB, 0};
  const BudgetState state{3 * kGiB - kMinBudgetMargin, 2 * kGiB,
                          Pressure::kNone};
  const std::vector<ReadoutLine> lines =
      BudgetReadout(BudgetViewOf(sample, state, std::nullopt, {}));
  Equal(ValueOf(lines, "allowance"),
        std::string{"2560 MiB (free video memory less the 512 MiB margin)"},
        "without a cap the allowance names free video memory and the margin");
  Equal(ValueOf(lines, "headroom"), std::string{"512 MiB"},
        "headroom is the allowance less the planned bytes");
  Equal(ValueOf(lines, "budget radius"), std::string{"off"},
        "no budget radius shows off");
  Equal(ValueOf(lines, "pressure"), std::string{"none"},
        "no pressure shows none");
}

void UnknownBudget() {
  const BudgetSample sample{std::nullopt, 1 * kGiB, 1 * kGiB};
  const BudgetState state{std::nullopt, 1 * kGiB, Pressure::kNone};
  const BudgetView view = BudgetViewOf(sample, state, std::nullopt, {});
  Check(!view.reading, "a failed probe gives no memory reading");
  const std::vector<ReadoutLine> lines = BudgetReadout(view);
  Equal(ValueOf(lines, "video memory"), std::string{"unknown"},
        "a failed probe shows unknown video memory");
  Equal(ValueOf(lines, "free video memory"), std::string{"unknown"},
        "a failed probe shows unknown free video memory");
  Equal(ValueOf(lines, "allowance"),
        std::string{"none: video memory is unknown"},
        "a failed probe sets no allowance, even with a cap");
  Equal(ValueOf(lines, "headroom"), std::string{"unknown"},
        "no allowance gives no headroom");
  const BudgetView zero =
      BudgetViewOf({VideoMemory{0, 0}, 0, 0}, {}, std::nullopt, {});
  Equal(ValueOf(BudgetReadout(zero), "allowance"),
        std::string{"none: video memory is unknown"},
        "a DXGI budget of 0 counts as unknown");
}

void PressureNames() {
  Check(PressureText(Pressure::kNone) == "none", "none");
  Check(PressureText(Pressure::kOver) == "over", "over");
  Check(PressureText(Pressure::kOverWarned) == "over and exhausted",
        "a warned episode shows as exhausted");
}

void CapControlKeepsTheStoredValue() {
  Check(PresetValueText(kBudgetCapControl, 0) == "Automatic",
        "0 shows as automatic");
  Check(PresetValueText(kBudgetCapControl, 65536) == "65536 MiB",
        "an INI value over the slider range shows as stored");
  Check(PresetValueText(kBudgetCapControl, 64) == "64 MiB",
        "an INI value under the slider range shows as stored");
  Check(!PresetIndexOf(kBudgetCapControl, 64),
        "a value between presets is custom");
}
}

int main() {
  KnownMemoryWithCap();
  FreeVideoMemorySetsTheAllowance();
  UnknownBudget();
  PressureNames();
  CapControlKeepsTheStoredValue();
  return test::Finish("studio_budgetview");
}
