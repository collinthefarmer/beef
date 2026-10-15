// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/FieldFilter.h"
#include "test_support.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
constexpr std::uint32_t kSide = 16;

std::vector<float> Dot() {
  std::vector<float> values(kSide * kSide, 0.0f);
  values[8 * kSide + 8] = 1.0f;
  return values;
}

float At(const std::vector<float> &a_values, std::uint32_t a_x,
         std::uint32_t a_y) {
  return a_values[a_y * kSide + a_x];
}

std::vector<std::uint16_t> TwoCharts() {
  std::vector<std::uint16_t> charts(kSide * kSide, 0);
  for (std::uint32_t y = 0; y < kSide; ++y)
    for (std::uint32_t x = 9; x < kSide; ++x)
      charts[y * kSide + x] = 1;
  return charts;
}
}

int main() {
  const std::vector<float> dot = Dot();

  const auto blurred = FilteredField(dot, {}, kSide, FilterOperation::kBlur, 2);
  float total = 0.0f;
  for (const float v : blurred)
    total += v;
  Check(At(blurred, 8, 8) < 1.0f && At(blurred, 9, 8) > 0.0f &&
            At(blurred, 11, 8) == 0.0f,
        "blur spreads a dot over its radius and no further");
  Check(std::abs(total - 1.0f) < 0.001f,
        "blur keeps the total away from the texture border");
  Check(test::Near(At(blurred, 7, 8), At(blurred, 9, 8)) &&
            test::Near(At(blurred, 8, 7), At(blurred, 8, 9)),
        "blur is symmetric");

  const auto grown = FilteredField(dot, {}, kSide, FilterOperation::kGrow, 2);
  Check(At(grown, 10, 10) == 1.0f && At(grown, 11, 8) == 0.0f,
        "grow fills the square of the radius around a dot");
  const auto shrunk =
      FilteredField(grown, {}, kSide, FilterOperation::kShrink, 2);
  Check(shrunk == dot, "shrink after grow at one radius restores a dot");
  const auto edge = FilteredField(grown, {}, kSide, FilterOperation::kEdge, 1);
  Check(At(edge, 8, 8) == 0.0f && At(edge, 10, 8) == 1.0f &&
            At(edge, 11, 8) == 1.0f && At(edge, 12, 8) == 0.0f,
        "edge marks both sides of a boundary and leaves flat areas at 0");

  const auto none = FilteredField(dot, {}, kSide, FilterOperation::kBlur, 0);
  Check(none == dot, "radius 0 leaves the field unchanged");

  const auto charts = TwoCharts();
  const auto kept =
      FilteredField(dot, charts, kSide, FilterOperation::kBlur, 3);
  const auto crossed = FilteredField(dot, {}, kSide, FilterOperation::kBlur, 3);
  Check(At(kept, 9, 8) == 0.0f && At(crossed, 9, 8) > 0.0f,
        "a chart seam stops the blur; without charts it crosses");
  const auto grownKept =
      FilteredField(dot, charts, kSide, FilterOperation::kGrow, 3);
  Check(At(grownKept, 8, 8) == 1.0f && At(grownKept, 9, 8) == 0.0f,
        "a chart seam stops grow");

  Check(
      FilterTaps(100.0f) == kMaxFilterTaps && FilterTaps(-3.0f) == 0 &&
          FilterTaps(std::numeric_limits<float>::quiet_NaN()) == 0 &&
          FilterTaps(std::numeric_limits<float>::infinity()) == 0,
      "the radius is clamped to 0..16 and a non-finite radius filters nothing");
  Check(FilteredField(dot, {}, kSide + 1, FilterOperation::kBlur, 2).empty() &&
            FilteredField(dot, std::vector<std::uint16_t>(3), kSide,
                          FilterOperation::kBlur, 2)
                .empty(),
        "a field or chart list of the wrong size is refused");
  return test::Finish("field filter");
}
