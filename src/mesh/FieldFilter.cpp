// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/FieldFilter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace BetterEnchantmentEffects {
namespace {
struct Axis {
  std::int32_t dx = 0;
  std::int32_t dy = 0;
};

std::vector<FilterPass> PassAlong(std::span<const FilterPass> a_input,
                                  std::span<const std::uint16_t> a_charts,
                                  std::uint32_t a_side, Axis a_axis,
                                  std::int32_t a_taps);
float Finish(const FilterPass &a_pass, FilterOperation a_operation) noexcept;
}

std::int32_t FilterTaps(float a_radius) noexcept {
  if (!std::isfinite(a_radius)) {
    return 0;
  }
  return std::clamp(static_cast<std::int32_t>(std::floor(a_radius + 0.5f)), 0,
                    kMaxFilterTaps);
}

float FilterWeight(std::int32_t a_offset, std::int32_t a_taps) noexcept {
  const float sigma = std::max(static_cast<float>(a_taps) * 0.5f, 0.5f);
  const auto k = static_cast<float>(a_offset);
  return std::exp(-(k * k) / (2.0f * sigma * sigma));
}

std::vector<float> FilteredField(std::span<const float> a_values,
                                 std::span<const std::uint16_t> a_charts,
                                 std::uint32_t a_side,
                                 FilterOperation a_operation, float a_radius) {
  const std::size_t texels = static_cast<std::size_t>(a_side) * a_side;
  if (a_values.size() != texels ||
      (!a_charts.empty() && a_charts.size() != texels)) {
    return {};
  }
  std::vector<FilterPass> start(texels);
  for (std::size_t i = 0; i < texels; ++i) {
    start[i] = FilterPass{a_values[i], a_values[i], a_values[i]};
  }
  const std::int32_t taps = FilterTaps(a_radius);
  const auto across = PassAlong(start, a_charts, a_side, Axis{1, 0}, taps);
  const auto down = PassAlong(across, a_charts, a_side, Axis{0, 1}, taps);
  std::vector<float> out(texels);
  for (std::size_t i = 0; i < texels; ++i) {
    out[i] = Finish(down[i], a_operation);
  }
  return out;
}

namespace {
std::vector<FilterPass> PassAlong(std::span<const FilterPass> a_input,
                                  std::span<const std::uint16_t> a_charts,
                                  std::uint32_t a_side, Axis a_axis,
                                  std::int32_t a_taps) {
  const auto side = static_cast<std::int32_t>(a_side);
  std::vector<FilterPass> out(a_input.size());
  for (std::int32_t y = 0; y < side; ++y) {
    for (std::int32_t x = 0; x < side; ++x) {
      const std::size_t at =
          static_cast<std::size_t>(y) * a_side + static_cast<std::size_t>(x);
      const bool seams = !a_charts.empty();
      const std::uint16_t chart = seams ? a_charts[at] : 0;
      float sum = 0.0f;
      float weight = 0.0f;
      float lowest = a_input[at].lowest;
      float highest = a_input[at].highest;
      for (std::int32_t k = -a_taps; k <= a_taps; ++k) {
        const std::int32_t px = std::clamp(x + a_axis.dx * k, 0, side - 1);
        const std::int32_t py = std::clamp(y + a_axis.dy * k, 0, side - 1);
        const std::size_t near = static_cast<std::size_t>(py) * a_side +
                                 static_cast<std::size_t>(px);
        if (seams && a_charts[near] != chart) {
          continue;
        }
        const float w = FilterWeight(k, a_taps);
        sum += a_input[near].blurred * w;
        weight += w;
        lowest = std::min(lowest, a_input[near].lowest);
        highest = std::max(highest, a_input[near].highest);
      }
      out[at] = FilterPass{weight > 0.0f ? sum / weight : a_input[at].blurred,
                           lowest, highest};
    }
  }
  return out;
}

float Finish(const FilterPass &a_pass, FilterOperation a_operation) noexcept {
  switch (a_operation) {
  case FilterOperation::kGrow:
    return a_pass.highest;
  case FilterOperation::kShrink:
    return a_pass.lowest;
  case FilterOperation::kEdge:
    return a_pass.highest - a_pass.lowest;
  case FilterOperation::kBlur:
    break;
  }
  return a_pass.blurred;
}
}
}
