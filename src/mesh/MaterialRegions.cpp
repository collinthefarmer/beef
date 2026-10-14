// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/MaterialRegions.h"

#include "Core.h"
#include "mesh/GaussianMixture.h"
#include "mesh/MaterialClusters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
constexpr std::size_t kIterations = 10;
constexpr double kMinSizeFactor = 0.5;
constexpr float kChannelScale = 100.0f;
constexpr double kRegularisation = 1e-2;
constexpr double kMinSpread = 1e-3;
constexpr std::size_t kMergeSample = 40000;
constexpr std::size_t kMinGroupTexels = 16;
constexpr std::size_t kDescriptorAxes = kTexelAxes + 1;
constexpr std::size_t kLumaAxis = 4;
constexpr std::size_t kMinRegionsForGroups = 4;

struct Grid {
  std::uint32_t side = 0;

  [[nodiscard]] std::size_t At(std::uint32_t a_y,
                               std::uint32_t a_x) const noexcept {
    return static_cast<std::size_t>(a_y) * side + a_x;
  }
  [[nodiscard]] std::size_t Size() const noexcept {
    return static_cast<std::size_t>(side) * side;
  }
};

template <typename Visit>
void ForEachNeighbour(const Grid &a_grid, std::size_t a_index, Visit a_visit) {
  const auto y = static_cast<std::uint32_t>(a_index / a_grid.side);
  const auto x = static_cast<std::uint32_t>(a_index % a_grid.side);
  if (y > 0) {
    a_visit(a_grid.At(y - 1, x));
  }
  if (y + 1 < a_grid.side) {
    a_visit(a_grid.At(y + 1, x));
  }
  if (x > 0) {
    a_visit(a_grid.At(y, x - 1));
  }
  if (x + 1 < a_grid.side) {
    a_visit(a_grid.At(y, x + 1));
  }
}

[[nodiscard]] bool IsCovered(const MaterialRaster &a_raster,
                             std::size_t a_index) noexcept {
  return a_raster.charts[a_index] != kNoChart;
}

[[nodiscard]] float Linear(float a_srgb) noexcept {
  return a_srgb <= 0.04045f ? a_srgb / 12.92f
                            : std::pow((a_srgb + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] float LabCurve(float a_t) noexcept {
  return a_t > 0.008856f ? std::cbrt(a_t) : 7.787f * a_t + 16.0f / 116.0f;
}

[[nodiscard]] Vec3 LabOf(const Vec3 &a_srgb) noexcept {
  const float r = Linear(a_srgb.x);
  const float g = Linear(a_srgb.y);
  const float b = Linear(a_srgb.z);
  const float x = (0.412453f * r + 0.357580f * g + 0.180423f * b) / 0.950456f;
  const float y = 0.212671f * r + 0.715160f * g + 0.072169f * b;
  const float z = (0.019334f * r + 0.119193f * g + 0.950227f * b) / 1.088754f;
  const float fx = LabCurve(x);
  const float fy = LabCurve(y);
  const float fz = LabCurve(z);
  return Vec3{116.0f * fy - 16.0f, 500.0f * (fx - fy), 200.0f * (fy - fz)};
}

struct FeatureStack {
  std::size_t channels = 0;
  std::vector<float> values;

  [[nodiscard]] std::span<const float> At(std::size_t a_index) const noexcept {
    return std::span<const float>(values).subspan(a_index * channels, channels);
  }
};

struct WeightedChannel {
  float weight = 0.0f;
  float MaterialTexel::*value = nullptr;
};

[[nodiscard]] std::pair<float, float>
DiffuseRange(const MaterialRaster &a_raster) noexcept {
  float low = 1.0f;
  float high = 0.0f;
  for (std::size_t i = 0; i < a_raster.texels.size(); ++i) {
    if (!IsCovered(a_raster, i)) {
      continue;
    }
    const Vec3 &d = a_raster.texels[i].diffuse;
    low = std::min({low, d.x, d.y, d.z});
    high = std::max({high, d.x, d.y, d.z});
  }
  return {low, std::max(high - low, 1e-6f)};
}

[[nodiscard]] FeatureStack FeaturesOf(const MaterialRaster &a_raster,
                                      const SegmentWeights &a_weights) {
  const WeightedChannel scalars[]{
      {a_weights.roughness, &MaterialTexel::roughness},
      {a_weights.metallic, &MaterialTexel::metallic},
      {a_weights.occlusion, &MaterialTexel::occlusion},
      {a_weights.reflectance, &MaterialTexel::reflectance},
      {a_weights.detail, &MaterialTexel::detail},
  };
  const bool anyWeight =
      a_weights.luma > 0.0f || a_weights.chroma > 0.0f ||
      std::ranges::any_of(scalars, [](const WeightedChannel &a_c) {
        return a_c.weight > 0.0f;
      });
  const float luma = anyWeight ? a_weights.luma : 1.0f;
  const float chroma = anyWeight ? a_weights.chroma : 1.0f;
  const auto [low, span] = DiffuseRange(a_raster);

  FeatureStack stack;
  stack.channels = (luma > 0.0f ? 1 : 0) + (chroma > 0.0f ? 2 : 0) +
                   static_cast<std::size_t>(std::ranges::count_if(
                       scalars, [](const WeightedChannel &a_c) {
                         return a_c.weight > 0.0f;
                       }));
  stack.values.reserve(a_raster.texels.size() * stack.channels);
  for (const MaterialTexel &texel : a_raster.texels) {
    const Vec3 lab = LabOf(Vec3{Clamp01((texel.diffuse.x - low) / span),
                                Clamp01((texel.diffuse.y - low) / span),
                                Clamp01((texel.diffuse.z - low) / span)});
    if (luma > 0.0f) {
      stack.values.push_back(lab.x * luma);
    }
    if (chroma > 0.0f) {
      stack.values.push_back(lab.y * chroma);
      stack.values.push_back(lab.z * chroma);
    }
    for (const WeightedChannel &channel : scalars) {
      if (channel.weight > 0.0f) {
        stack.values.push_back(texel.*channel.value * kChannelScale *
                               channel.weight);
      }
    }
  }
  return stack;
}

[[nodiscard]] std::vector<float> GradientOf(const FeatureStack &a_stack,
                                            const Grid &a_grid) {
  std::vector<float> out(a_grid.Size(), 0.0f);
  const auto clampTo = [&](std::int64_t a_v) {
    return static_cast<std::uint32_t>(
        std::clamp<std::int64_t>(a_v, 0, a_grid.side - 1));
  };
  for (std::uint32_t y = 0; y < a_grid.side; ++y) {
    for (std::uint32_t x = 0; x < a_grid.side; ++x) {
      const std::uint32_t up = clampTo(std::int64_t{y} - 1);
      const std::uint32_t down = clampTo(std::int64_t{y} + 1);
      const std::uint32_t left = clampTo(std::int64_t{x} - 1);
      const std::uint32_t right = clampTo(std::int64_t{x} + 1);
      float sum = 0.0f;
      for (std::size_t c = 0; c < a_stack.channels; ++c) {
        const auto v = [&](std::uint32_t a_y, std::uint32_t a_x) {
          return a_stack.At(a_grid.At(a_y, a_x))[c];
        };
        const float gx = (v(up, right) + 2.0f * v(y, right) + v(down, right)) -
                         (v(up, left) + 2.0f * v(y, left) + v(down, left));
        const float gy = (v(down, left) + 2.0f * v(down, x) + v(down, right)) -
                         (v(up, left) + 2.0f * v(up, x) + v(up, right));
        sum += gx * gx + gy * gy;
      }
      out[a_grid.At(y, x)] = sum;
    }
  }
  return out;
}

struct Seed {
  std::uint32_t y = 0;
  std::uint32_t x = 0;
  std::uint16_t chart = kNoChart;
};

[[nodiscard]] std::vector<Seed> SeedsOf(const MaterialRaster &a_raster,
                                        const Grid &a_grid, double a_step) {
  std::map<std::uint16_t, std::vector<Seed>> byChart;
  for (double gy = a_step / 2.0; gy < a_grid.side; gy += a_step) {
    for (double gx = a_step / 2.0; gx < a_grid.side; gx += a_step) {
      const auto y = static_cast<std::uint32_t>(gy);
      const auto x = static_cast<std::uint32_t>(gx);
      const std::uint16_t chart = a_raster.charts[a_grid.At(y, x)];
      if (chart != kNoChart) {
        byChart[chart].push_back(Seed{y, x, chart});
      }
    }
  }
  std::map<std::uint16_t, std::pair<double, double>> centre;
  std::map<std::uint16_t, double> count;
  for (std::size_t i = 0; i < a_grid.Size(); ++i) {
    if (IsCovered(a_raster, i)) {
      auto &c = centre[a_raster.charts[i]];
      c.first += static_cast<double>(i / a_grid.side);
      c.second += static_cast<double>(i % a_grid.side);
      count[a_raster.charts[i]] += 1.0;
    }
  }
  for (auto &[chart, sums] : centre) {
    if (byChart.contains(chart)) {
      continue;
    }
    const double cy = sums.first / count[chart];
    const double cx = sums.second / count[chart];
    std::optional<std::pair<double, std::size_t>> nearest;
    for (std::size_t i = 0; i < a_grid.Size(); ++i) {
      if (a_raster.charts[i] != chart) {
        continue;
      }
      const double dy = static_cast<double>(i / a_grid.side) - cy;
      const double dx = static_cast<double>(i % a_grid.side) - cx;
      const double d = dy * dy + dx * dx;
      if (!nearest || d < nearest->first) {
        nearest.emplace(d, i);
      }
    }
    if (nearest) {
      byChart[chart].push_back(Seed{
          static_cast<std::uint32_t>(nearest->second / a_grid.side),
          static_cast<std::uint32_t>(nearest->second % a_grid.side), chart});
    }
  }
  std::vector<Seed> out;
  for (auto &[chart, seeds] : byChart) {
    out.insert(out.end(), seeds.begin(), seeds.end());
  }
  return out;
}

[[nodiscard]] Seed SmoothestNear(const Seed &a_seed,
                                 const MaterialRaster &a_raster,
                                 const Grid &a_grid,
                                 const std::vector<float> &a_gradient) {
  Seed best = a_seed;
  float lowest = a_gradient[a_grid.At(a_seed.y, a_seed.x)];
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      const std::int64_t y = std::int64_t{a_seed.y} + dy;
      const std::int64_t x = std::int64_t{a_seed.x} + dx;
      if (y < 0 || x < 0 || y >= a_grid.side || x >= a_grid.side) {
        continue;
      }
      const std::size_t i = a_grid.At(static_cast<std::uint32_t>(y),
                                      static_cast<std::uint32_t>(x));
      if (a_raster.charts[i] == a_seed.chart && a_gradient[i] < lowest) {
        lowest = a_gradient[i];
        best = Seed{static_cast<std::uint32_t>(y),
                    static_cast<std::uint32_t>(x), a_seed.chart};
      }
    }
  }
  return best;
}

struct Centre {
  double y = 0.0;
  double x = 0.0;
  std::uint16_t chart = kNoChart;
  std::vector<double> feature;
};

[[nodiscard]] std::vector<std::int32_t>
Assign(const MaterialRaster &a_raster, const Grid &a_grid,
       const FeatureStack &a_stack, const std::vector<Centre> &a_centres,
       double a_step, double a_compactness) {
  std::vector<std::int32_t> labels(a_grid.Size(), kNoRegion);
  std::vector<double> best(a_grid.Size(),
                           std::numeric_limits<double>::infinity());
  const auto radius = static_cast<std::int64_t>(std::ceil(a_step));
  const double colourScale = 1.0 / (a_compactness * a_compactness);
  const double spaceScale = 1.0 / (a_step * a_step);
  for (std::size_t k = 0; k < a_centres.size(); ++k) {
    const Centre &centre = a_centres[k];
    const auto cy = static_cast<std::int64_t>(centre.y);
    const auto cx = static_cast<std::int64_t>(centre.x);
    const std::int64_t y0 = std::max<std::int64_t>(0, cy - radius);
    const std::int64_t y1 =
        std::min<std::int64_t>(a_grid.side, cy + radius + 1);
    const std::int64_t x0 = std::max<std::int64_t>(0, cx - radius);
    const std::int64_t x1 =
        std::min<std::int64_t>(a_grid.side, cx + radius + 1);
    for (std::int64_t y = y0; y < y1; ++y) {
      for (std::int64_t x = x0; x < x1; ++x) {
        const std::size_t i = a_grid.At(static_cast<std::uint32_t>(y),
                                        static_cast<std::uint32_t>(x));
        if (a_raster.charts[i] != centre.chart) {
          continue;
        }
        const std::span<const float> feature = a_stack.At(i);
        double colour = 0.0;
        for (std::size_t c = 0; c < a_stack.channels; ++c) {
          const double d = feature[c] - centre.feature[c];
          colour += d * d;
        }
        const double dy = static_cast<double>(y) - centre.y;
        const double dx = static_cast<double>(x) - centre.x;
        const double distance =
            colour * colourScale + (dy * dy + dx * dx) * spaceScale;
        if (distance < best[i]) {
          best[i] = distance;
          labels[i] = static_cast<std::int32_t>(k);
        }
      }
    }
  }
  return labels;
}

void MoveCentres(const FeatureStack &a_stack, const Grid &a_grid,
                 const std::vector<std::int32_t> &a_labels,
                 std::vector<Centre> &a_centres) {
  const std::size_t n = a_centres.size();
  std::vector<double> count(n, 0.0);
  std::vector<double> sumY(n, 0.0);
  std::vector<double> sumX(n, 0.0);
  std::vector<double> sumFeature(n * a_stack.channels, 0.0);
  for (std::size_t i = 0; i < a_labels.size(); ++i) {
    if (a_labels[i] < 0) {
      continue;
    }
    const auto k = static_cast<std::size_t>(a_labels[i]);
    count[k] += 1.0;
    sumY[k] += static_cast<double>(i / a_grid.side);
    sumX[k] += static_cast<double>(i % a_grid.side);
    const std::span<const float> feature = a_stack.At(i);
    for (std::size_t c = 0; c < a_stack.channels; ++c) {
      sumFeature[k * a_stack.channels + c] += feature[c];
    }
  }
  for (std::size_t k = 0; k < n; ++k) {
    if (count[k] == 0.0) {
      continue;
    }
    a_centres[k].y = sumY[k] / count[k];
    a_centres[k].x = sumX[k] / count[k];
    for (std::size_t c = 0; c < a_stack.channels; ++c) {
      a_centres[k].feature[c] = sumFeature[k * a_stack.channels + c] / count[k];
    }
  }
}

void FillWithinCharts(const MaterialRaster &a_raster, const Grid &a_grid,
                      std::vector<std::int32_t> &a_labels) {
  std::deque<std::size_t> frontier;
  for (std::size_t i = 0; i < a_labels.size(); ++i) {
    if (a_labels[i] >= 0) {
      frontier.push_back(i);
    }
  }
  while (!frontier.empty()) {
    const std::size_t i = frontier.front();
    frontier.pop_front();
    ForEachNeighbour(a_grid, i, [&](std::size_t a_next) {
      if (a_labels[a_next] < 0 && IsCovered(a_raster, a_next) &&
          a_raster.charts[a_next] == a_raster.charts[i]) {
        a_labels[a_next] = a_labels[i];
        frontier.push_back(a_next);
      }
    });
  }
}

[[nodiscard]] std::vector<std::vector<std::size_t>>
PiecesOf(const Grid &a_grid, const std::vector<std::int32_t> &a_labels,
         std::span<const std::size_t> a_texels) {
  std::vector<std::vector<std::size_t>> pieces;
  std::vector<bool> seen(a_grid.Size(), false);
  for (const std::size_t start : a_texels) {
    if (seen[start]) {
      continue;
    }
    std::vector<std::size_t> piece{start};
    seen[start] = true;
    for (std::size_t head = 0; head < piece.size(); ++head) {
      ForEachNeighbour(a_grid, piece[head], [&](std::size_t a_next) {
        if (!seen[a_next] && a_labels[a_next] == a_labels[start]) {
          seen[a_next] = true;
          piece.push_back(a_next);
        }
      });
    }
    pieces.push_back(std::move(piece));
  }
  return pieces;
}

[[nodiscard]] std::vector<std::vector<std::size_t>>
TexelsByLabel(const std::vector<std::int32_t> &a_labels) {
  std::int32_t most = -1;
  for (const std::int32_t label : a_labels) {
    most = std::max(most, label);
  }
  std::vector<std::vector<std::size_t>> out(static_cast<std::size_t>(most + 1));
  for (std::size_t i = 0; i < a_labels.size(); ++i) {
    if (a_labels[i] >= 0) {
      out[static_cast<std::size_t>(a_labels[i])].push_back(i);
    }
  }
  return out;
}

void KeepLargestPieces(const Grid &a_grid,
                       std::vector<std::int32_t> &a_labels) {
  for (const std::vector<std::size_t> &texels : TexelsByLabel(a_labels)) {
    std::vector<std::vector<std::size_t>> pieces =
        PiecesOf(a_grid, a_labels, texels);
    if (pieces.size() < 2) {
      continue;
    }
    const auto largest = std::ranges::max_element(
        pieces, {},
        [](const std::vector<std::size_t> &a_p) { return a_p.size(); });
    for (auto it = pieces.begin(); it != pieces.end(); ++it) {
      if (it == largest) {
        continue;
      }
      for (const std::size_t i : *it) {
        a_labels[i] = kNoRegion;
      }
    }
  }
}

void AbsorbSmall(const MaterialRaster &a_raster, const Grid &a_grid,
                 std::size_t a_minSize, std::vector<std::int32_t> &a_labels) {
  for (const std::vector<std::size_t> &texels : TexelsByLabel(a_labels)) {
    if (texels.empty() || texels.size() >= a_minSize) {
      continue;
    }
    const std::int32_t self = a_labels[texels.front()];
    const std::uint16_t chart = a_raster.charts[texels.front()];
    std::map<std::int32_t, std::size_t> votes;
    for (const std::size_t i : texels) {
      ForEachNeighbour(a_grid, i, [&](std::size_t a_next) {
        if (a_labels[a_next] >= 0 && a_labels[a_next] != self &&
            a_raster.charts[a_next] == chart) {
          ++votes[a_labels[a_next]];
        }
      });
    }
    if (votes.empty()) {
      continue;
    }
    const auto winner = std::ranges::max_element(
        votes, {}, [](const auto &a_vote) { return a_vote.second; });
    for (const std::size_t i : texels) {
      a_labels[i] = winner->first;
    }
  }
}

void LabelLeftovers(const MaterialRaster &a_raster, const Grid &a_grid,
                    std::vector<std::int32_t> &a_labels) {
  std::int32_t next = 0;
  for (const std::int32_t label : a_labels) {
    next = std::max(next, label + 1);
  }
  for (std::size_t start = 0; start < a_labels.size(); ++start) {
    if (a_labels[start] >= 0 || !IsCovered(a_raster, start)) {
      continue;
    }
    std::vector<std::size_t> piece{start};
    a_labels[start] = next;
    for (std::size_t head = 0; head < piece.size(); ++head) {
      ForEachNeighbour(a_grid, piece[head], [&](std::size_t a_n) {
        if (a_labels[a_n] < 0 && IsCovered(a_raster, a_n) &&
            a_raster.charts[a_n] == a_raster.charts[start]) {
          a_labels[a_n] = next;
          piece.push_back(a_n);
        }
      });
    }
    ++next;
  }
}

[[nodiscard]] Regions Compacted(std::uint32_t a_side,
                                std::vector<std::int32_t> a_labels) {
  std::map<std::int32_t, std::int32_t> renumber;
  for (std::int32_t &label : a_labels) {
    if (label < 0) {
      continue;
    }
    label =
        renumber.try_emplace(label, static_cast<std::int32_t>(renumber.size()))
            .first->second;
  }
  return Regions{a_side, std::move(a_labels),
                 static_cast<std::int32_t>(renumber.size())};
}

[[nodiscard]] std::size_t CoveredCount(const MaterialRaster &a_raster) {
  return static_cast<std::size_t>(std::ranges::count_if(
      a_raster.charts, [](std::uint16_t a_c) { return a_c != kNoChart; }));
}

[[nodiscard]] Standardisation
StandardisationOf(const MaterialRaster &a_raster) {
  Standardisation out;
  TexelAxes squares{};
  double n = 0.0;
  for (std::size_t i = 0; i < a_raster.texels.size(); ++i) {
    if (!IsCovered(a_raster, i)) {
      continue;
    }
    const TexelAxes axes = AxesOfTexel(a_raster.texels[i]);
    for (std::size_t a = 0; a < kTexelAxes; ++a) {
      out.centre[a] += axes[a];
      squares[a] += axes[a] * axes[a];
    }
    n += 1.0;
  }
  for (std::size_t a = 0; a < kTexelAxes; ++a) {
    out.centre[a] /= std::max(n, 1.0);
    const double variance =
        squares[a] / std::max(n, 1.0) - out.centre[a] * out.centre[a];
    out.spread[a] = std::max(std::sqrt(std::max(variance, 0.0)), kMinSpread);
  }
  return out;
}

[[nodiscard]] std::array<double, kTexelAxes>
Standardised(const Standardisation &a_s,
             const MaterialTexel &a_texel) noexcept {
  const TexelAxes axes = AxesOfTexel(a_texel);
  std::array<double, kTexelAxes> out{};
  for (std::size_t a = 0; a < kTexelAxes; ++a) {
    out[a] = (axes[a] - a_s.centre[a]) / a_s.spread[a];
  }
  return out;
}

[[nodiscard]] std::expected<Points, std::string>
DescriptorsOf(const MaterialRaster &a_raster, const Regions &a_regions) {
  const auto count = static_cast<std::size_t>(a_regions.count);
  std::vector<double> sums(count * kTexelAxes, 0.0);
  std::vector<double> lumaSquares(count, 0.0);
  std::vector<double> sizes(count, 0.0);
  for (std::size_t i = 0; i < a_regions.ofTexel.size(); ++i) {
    const std::int32_t region = a_regions.ofTexel[i];
    if (region < 0 || !IsCovered(a_raster, i)) {
      continue;
    }
    const auto r = static_cast<std::size_t>(region);
    const TexelAxes axes = AxesOfTexel(a_raster.texels[i]);
    for (std::size_t a = 0; a < kTexelAxes; ++a) {
      sums[r * kTexelAxes + a] += axes[a];
    }
    lumaSquares[r] += axes[kLumaAxis] * axes[kLumaAxis];
    sizes[r] += 1.0;
  }
  std::vector<double> values;
  for (std::size_t r = 0; r < count; ++r) {
    const double n = std::max(sizes[r], 1.0);
    for (std::size_t a = 0; a < kTexelAxes; ++a) {
      values.push_back(sums[r * kTexelAxes + a] / n);
    }
    const double mean = sums[r * kTexelAxes + kLumaAxis] / n;
    values.push_back(
        std::sqrt(std::max(lumaSquares[r] / n - mean * mean, 0.0)));
  }
  for (std::size_t a = 0; a < kDescriptorAxes; ++a) {
    double mean = 0.0;
    double square = 0.0;
    for (std::size_t r = 0; r < count; ++r) {
      mean += values[r * kDescriptorAxes + a];
      square +=
          values[r * kDescriptorAxes + a] * values[r * kDescriptorAxes + a];
    }
    mean /= static_cast<double>(count);
    const double spread =
        std::max(std::sqrt(std::max(
                     square / static_cast<double>(count) - mean * mean, 0.0)),
                 kMinSpread);
    for (std::size_t r = 0; r < count; ++r) {
      values[r * kDescriptorAxes + a] =
          (values[r * kDescriptorAxes + a] - mean) / spread;
    }
  }
  return PointsOf(kDescriptorAxes, std::move(values));
}

[[nodiscard]] std::vector<std::int16_t>
GroupOfTexel(const Regions &a_regions,
             const std::vector<std::size_t> &a_groupOfRegion) {
  std::vector<std::int16_t> out(a_regions.ofTexel.size(), kNoGroup);
  for (std::size_t i = 0; i < out.size(); ++i) {
    const std::int32_t region = a_regions.ofTexel[i];
    if (region >= 0 &&
        static_cast<std::size_t>(region) < a_groupOfRegion.size()) {
      out[i] = static_cast<std::int16_t>(
          a_groupOfRegion[static_cast<std::size_t>(region)]);
    }
  }
  return out;
}

struct Sample {
  Points points;
  std::vector<std::int16_t> groups;
};

[[nodiscard]] std::expected<Sample, std::string>
SampleOf(const MaterialRaster &a_raster, const Standardisation &a_s,
         const std::vector<std::int16_t> &a_groups, std::size_t a_limit) {
  const std::size_t covered = CoveredCount(a_raster);
  const std::size_t stride =
      std::max<std::size_t>(1, covered / std::max<std::size_t>(a_limit, 1));
  std::vector<double> values;
  Sample sample;
  std::size_t seen = 0;
  for (std::size_t i = 0; i < a_raster.texels.size(); ++i) {
    if (!IsCovered(a_raster, i) || a_groups[i] == kNoGroup) {
      continue;
    }
    if (seen++ % stride != 0) {
      continue;
    }
    const auto z = Standardised(a_s, a_raster.texels[i]);
    values.insert(values.end(), z.begin(), z.end());
    sample.groups.push_back(a_groups[i]);
  }
  auto points = PointsOf(kTexelAxes, std::move(values));
  if (!points) {
    return std::unexpected(points.error());
  }
  sample.points = std::move(*points);
  return sample;
}

[[nodiscard]] std::vector<MaterialGroup> GroupsOf(const Sample &a_sample) {
  std::map<std::int16_t, std::vector<std::size_t>> members;
  for (std::size_t i = 0; i < a_sample.groups.size(); ++i) {
    members[a_sample.groups[i]].push_back(i);
  }
  const auto total = static_cast<double>(a_sample.groups.size());
  std::vector<MaterialGroup> out;
  for (const auto &[id, indices] : members) {
    if (indices.size() < kMinGroupTexels) {
      continue;
    }
    const double share = static_cast<double>(indices.size()) / total;
    auto gaussian =
        GaussianOf(a_sample.points, indices, share, kRegularisation);
    if (gaussian) {
      out.push_back(
          MaterialGroup{id, std::move(*gaussian), static_cast<float>(share)});
    }
  }
  return out;
}

[[nodiscard]] std::vector<PreparedGaussian>
PreparedOf(const std::vector<MaterialGroup> &a_groups) {
  std::vector<PreparedGaussian> out;
  for (const MaterialGroup &group : a_groups) {
    auto prepared = Prepare(group.gaussian);
    if (!prepared) {
      return {};
    }
    out.push_back(std::move(*prepared));
  }
  return out;
}

struct Confusion {
  double share = 0.0;
  std::size_t left = 0;
  std::size_t right = 0;
};

[[nodiscard]] std::optional<Confusion>
MostConfused(const Sample &a_sample,
             const std::vector<MaterialGroup> &a_groups) {
  const std::vector<PreparedGaussian> prepared = PreparedOf(a_groups);
  const std::size_t k = prepared.size();
  if (k < 2) {
    return std::nullopt;
  }
  std::vector<double> mass(k, 0.0);
  std::vector<double> lost(k * k, 0.0);
  for (std::size_t i = 0; i < CountOf(a_sample.points); ++i) {
    const std::vector<double> p =
        Posteriors(prepared, PointAt(a_sample.points, i));
    for (std::size_t a = 0; a < k; ++a) {
      mass[a] += p[a];
      for (std::size_t b = 0; b < k; ++b) {
        if (a != b && p[b] > p[a]) {
          lost[a * k + b] += p[a];
        }
      }
    }
  }
  std::optional<Confusion> worst;
  for (std::size_t a = 0; a < k; ++a) {
    for (std::size_t b = a + 1; b < k; ++b) {
      const double share = std::max(lost[a * k + b] / std::max(mass[a], 1e-9),
                                    lost[b * k + a] / std::max(mass[b], 1e-9));
      if (!worst || share > worst->share) {
        worst = Confusion{share, a, b};
      }
    }
  }
  return worst;
}

[[nodiscard]] std::vector<GroupMerge> MergeConfused(Sample &a_sample,
                                                    float a_threshold) {
  std::vector<GroupMerge> merges;
  while (true) {
    const std::vector<MaterialGroup> groups = GroupsOf(a_sample);
    const std::optional<Confusion> worst = MostConfused(a_sample, groups);
    if (!worst || worst->share <= a_threshold) {
      break;
    }
    const MaterialGroup &a = groups[worst->left];
    const MaterialGroup &b = groups[worst->right];
    const bool aIsLarger = a.share >= b.share;
    const std::int16_t kept = aIsLarger ? a.id : b.id;
    const std::int16_t absorbed = aIsLarger ? b.id : a.id;
    merges.push_back(
        GroupMerge{static_cast<float>(worst->share), kept, absorbed});
    std::ranges::replace(a_sample.groups, absorbed, kept);
  }
  return merges;
}

[[nodiscard]] MaterialGroups SingleGroup(const MaterialRaster &a_raster,
                                         Standardisation a_s,
                                         std::vector<std::int16_t> a_ofTexel) {
  for (std::size_t i = 0; i < a_ofTexel.size(); ++i) {
    a_ofTexel[i] = IsCovered(a_raster, i) ? 0 : kNoGroup;
  }
  MaterialGroups out{std::move(a_s), 1, {}, {}, std::move(a_ofTexel)};
  auto sample =
      SampleOf(a_raster, out.standardisation, out.ofTexel, kMergeSample);
  if (sample) {
    out.groups = GroupsOf(*sample);
  }
  return out;
}
}

std::expected<MaterialRaster, std::string>
RasterOf(std::uint32_t a_side, std::vector<MaterialTexel> a_texels,
         std::vector<std::uint16_t> a_charts) {
  if (a_side < kMinRasterSide || a_side > kMaxRasterSide) {
    return std::unexpected(std::format("raster side {} is outside {}..{}",
                                       a_side, kMinRasterSide, kMaxRasterSide));
  }
  const std::size_t size = static_cast<std::size_t>(a_side) * a_side;
  if (a_texels.size() != size || a_charts.size() != size) {
    return std::unexpected(std::format(
        "raster of side {} needs {} texels and charts, has {} and {}", a_side,
        size, a_texels.size(), a_charts.size()));
  }
  const auto finite01 = [](float a_v) {
    return std::isfinite(a_v) ? Clamp01(a_v) : 0.0f;
  };
  for (MaterialTexel &t : a_texels) {
    t = MaterialTexel{finite01(t.roughness),
                      finite01(t.metallic),
                      finite01(t.occlusion),
                      finite01(t.reflectance),
                      finite01(t.luma),
                      Vec3{finite01(t.diffuse.x), finite01(t.diffuse.y),
                           finite01(t.diffuse.z)},
                      finite01(t.detail)};
  }
  return MaterialRaster{a_side, std::move(a_texels), std::move(a_charts)};
}

TexelAxes AxesOfTexel(const MaterialTexel &a_texel) noexcept {
  const Chroma chroma = ChromaOf(a_texel.diffuse);
  return TexelAxes{a_texel.roughness,   a_texel.metallic, a_texel.occlusion,
                   a_texel.reflectance, a_texel.luma,     chroma.orangeBlue,
                   chroma.greenMagenta, a_texel.detail};
}

Regions SegmentRaster(const MaterialRaster &a_raster,
                      const SegmentSettings &a_settings) {
  const Grid grid{a_raster.side};
  const std::size_t covered = CoveredCount(a_raster);
  if (covered == 0 || a_raster.charts.size() != grid.Size() ||
      a_raster.texels.size() != grid.Size()) {
    return Regions{a_raster.side,
                   std::vector<std::int32_t>(grid.Size(), kNoRegion), 0};
  }
  const double step =
      std::sqrt(static_cast<double>(covered) /
                std::max<std::uint32_t>(a_settings.segments, 1));
  const double compactness =
      std::isfinite(a_settings.compactness) && a_settings.compactness > 0.0f
          ? a_settings.compactness
          : 10.0;
  const FeatureStack stack = FeaturesOf(a_raster, a_settings.weights);
  const std::vector<float> gradient = GradientOf(stack, grid);

  std::vector<Centre> centres;
  for (const Seed &seed : SeedsOf(a_raster, grid, step)) {
    const Seed moved = SmoothestNear(seed, a_raster, grid, gradient);
    const std::span<const float> feature = stack.At(grid.At(moved.y, moved.x));
    centres.push_back(Centre{
        static_cast<double>(moved.y), static_cast<double>(moved.x), moved.chart,
        std::vector<double>(feature.begin(), feature.end())});
  }

  std::vector<std::int32_t> labels;
  for (std::size_t round = 0; round < kIterations; ++round) {
    labels = Assign(a_raster, grid, stack, centres, step, compactness);
    MoveCentres(stack, grid, labels, centres);
  }
  FillWithinCharts(a_raster, grid, labels);
  KeepLargestPieces(grid, labels);
  FillWithinCharts(a_raster, grid, labels);
  AbsorbSmall(a_raster, grid,
              static_cast<std::size_t>(kMinSizeFactor * step * step), labels);
  LabelLeftovers(a_raster, grid, labels);
  return Compacted(a_raster.side, std::move(labels));
}

std::expected<MaterialGroups, std::string>
GroupRegions(const MaterialRaster &a_raster, const Regions &a_regions,
             const GroupSettings &a_settings) {
  const Grid grid{a_raster.side};
  if (a_regions.side != a_raster.side ||
      a_regions.ofTexel.size() != grid.Size() ||
      a_raster.charts.size() != grid.Size() ||
      a_raster.texels.size() != grid.Size()) {
    return std::unexpected("the regions do not match the raster");
  }
  if (CoveredCount(a_raster) < kMinCoveredTexels) {
    return std::unexpected(
        std::format("fewer than {} covered texels", kMinCoveredTexels));
  }
  Standardisation standardisation = StandardisationOf(a_raster);
  std::vector<std::int16_t> unset(grid.Size(), kNoGroup);
  if (a_regions.count < static_cast<std::int32_t>(kMinRegionsForGroups)) {
    return SingleGroup(a_raster, std::move(standardisation), std::move(unset));
  }
  auto descriptors = DescriptorsOf(a_raster, a_regions);
  if (!descriptors) {
    return std::unexpected(descriptors.error());
  }
  MixtureSearch search;
  search.seed = a_settings.seed;
  search.regularisation = kRegularisation;
  auto mixture = ChooseMixture(*descriptors, search);
  if (!mixture) {
    return SingleGroup(a_raster, std::move(standardisation), std::move(unset));
  }

  MaterialGroups out;
  out.standardisation = std::move(standardisation);
  out.fitted = mixture->components.size();
  out.ofTexel =
      GroupOfTexel(a_regions, MostLikelyComponents(*mixture, *descriptors));

  auto sample =
      SampleOf(a_raster, out.standardisation, out.ofTexel, kMergeSample);
  if (!sample) {
    return std::unexpected(sample.error());
  }
  const float threshold =
      std::isfinite(a_settings.merge) ? Clamp01(a_settings.merge) : 1.0f;
  out.merges = MergeConfused(*sample, threshold);
  for (const GroupMerge &merge : out.merges) {
    std::ranges::replace(out.ofTexel, merge.absorbed, merge.kept);
  }

  auto everyTexel = SampleOf(a_raster, out.standardisation, out.ofTexel,
                             std::numeric_limits<std::size_t>::max());
  if (!everyTexel) {
    return std::unexpected(everyTexel.error());
  }
  out.groups = GroupsOf(*everyTexel);
  return out;
}

std::vector<std::int16_t> LabelTexels(const MaterialRaster &a_raster,
                                      const MaterialGroups &a_groups) {
  std::vector<std::int16_t> out(a_raster.texels.size(), kNoGroup);
  const std::vector<PreparedGaussian> prepared = PreparedOf(a_groups.groups);
  if (prepared.empty() || a_raster.charts.size() != a_raster.texels.size()) {
    return out;
  }
  for (std::size_t i = 0; i < out.size(); ++i) {
    if (!IsCovered(a_raster, i)) {
      continue;
    }
    const auto z = Standardised(a_groups.standardisation, a_raster.texels[i]);
    double best = -std::numeric_limits<double>::infinity();
    for (std::size_t g = 0; g < prepared.size(); ++g) {
      const double value = LogDensity(prepared[g], z);
      if (value > best) {
        best = value;
        out[i] = a_groups.groups[g].id;
      }
    }
  }
  return out;
}
}

namespace BetterEnchantmentEffects {
namespace {
constexpr double kGuidedEpsilon = 1e-3;
constexpr double kTexelCentre = 0.5;

[[nodiscard]] double EdgeSide(const Vec2 &a_a, const Vec2 &a_b, double a_x,
                              double a_y) noexcept {
  return (static_cast<double>(a_b.x) - a_a.x) * (a_y - a_a.y) -
         (static_cast<double>(a_b.y) - a_a.y) * (a_x - a_a.x);
}

void FillTriangle(std::array<Vec2, 3> a_uv, std::uint16_t a_chart,
                  std::uint32_t a_side, std::vector<std::uint16_t> &a_out) {
  for (Vec2 &uv : a_uv) {
    uv = Vec2{uv.x * static_cast<float>(a_side),
              uv.y * static_cast<float>(a_side)};
  }
  const double area = EdgeSide(a_uv[0], a_uv[1], a_uv[2].x, a_uv[2].y);
  if (!std::isfinite(area) || area == 0.0) {
    return;
  }
  const auto bound = [a_side](double a_v) {
    return static_cast<std::int64_t>(
        std::clamp(std::floor(a_v), 0.0, static_cast<double>(a_side - 1)));
  };
  const double minX = std::min({a_uv[0].x, a_uv[1].x, a_uv[2].x});
  const double maxX = std::max({a_uv[0].x, a_uv[1].x, a_uv[2].x});
  const double minY = std::min({a_uv[0].y, a_uv[1].y, a_uv[2].y});
  const double maxY = std::max({a_uv[0].y, a_uv[1].y, a_uv[2].y});
  if (maxX < 0.0 || maxY < 0.0 || minX > a_side || minY > a_side) {
    return;
  }
  for (std::int64_t y = bound(minY); y <= bound(maxY); ++y) {
    for (std::int64_t x = bound(minX); x <= bound(maxX); ++x) {
      const double px = static_cast<double>(x) + kTexelCentre;
      const double py = static_cast<double>(y) + kTexelCentre;
      const double w0 = EdgeSide(a_uv[1], a_uv[2], px, py) / area;
      const double w1 = EdgeSide(a_uv[2], a_uv[0], px, py) / area;
      const double w2 = EdgeSide(a_uv[0], a_uv[1], px, py) / area;
      if (w0 >= 0.0 && w1 >= 0.0 && w2 >= 0.0) {
        a_out[static_cast<std::size_t>(y) * a_side +
              static_cast<std::size_t>(x)] = a_chart;
      }
    }
  }
}

[[nodiscard]] std::optional<std::array<float, kWhiteningFloats>>
WhiteningOf(const PreparedGaussian &a_gaussian) {
  if (a_gaussian.mean.size() != kTexelAxes ||
      a_gaussian.lower.size() != kTexelAxes * kTexelAxes) {
    return std::nullopt;
  }
  std::array<std::array<double, kTexelAxes>, kTexelAxes> inverse{};
  for (std::size_t column = 0; column < kTexelAxes; ++column) {
    for (std::size_t i = column; i < kTexelAxes; ++i) {
      double value = i == column ? 1.0 : 0.0;
      for (std::size_t k = column; k < i; ++k) {
        value -= a_gaussian.lower[i * kTexelAxes + k] * inverse[k][column];
      }
      inverse[i][column] = value / a_gaussian.lower[i * kTexelAxes + i];
    }
  }
  std::array<float, kWhiteningFloats> packed{};
  std::size_t at = 0;
  for (std::size_t i = 0; i < kTexelAxes; ++i) {
    for (std::size_t j = 0; j <= i; ++j) {
      packed[at++] = static_cast<float>(inverse[i][j]);
    }
  }
  return packed;
}

class BoxSums {
public:
  BoxSums(std::uint32_t a_side, const std::vector<double> &a_values)
      : side_(a_side), sums_((a_side + 1) * (a_side + 1), 0.0) {
    for (std::uint32_t y = 0; y < a_side; ++y) {
      double row = 0.0;
      for (std::uint32_t x = 0; x < a_side; ++x) {
        row += a_values[static_cast<std::size_t>(y) * a_side + x];
        sums_[(y + 1) * (side_ + 1) + x + 1] =
            sums_[y * (side_ + 1) + x + 1] + row;
      }
    }
  }

  [[nodiscard]] double Mean(std::uint32_t a_y, std::uint32_t a_x,
                            std::uint32_t a_r) const noexcept {
    const std::uint32_t y0 = a_y > a_r ? a_y - a_r : 0;
    const std::uint32_t x0 = a_x > a_r ? a_x - a_r : 0;
    const std::uint32_t y1 = std::min(side_, a_y + a_r + 1);
    const std::uint32_t x1 = std::min(side_, a_x + a_r + 1);
    const double total =
        sums_[y1 * (side_ + 1) + x1] - sums_[y0 * (side_ + 1) + x1] -
        sums_[y1 * (side_ + 1) + x0] + sums_[y0 * (side_ + 1) + x0];
    return total / static_cast<double>((y1 - y0) * (x1 - x0));
  }

private:
  std::uint32_t side_;
  std::vector<double> sums_;
};

using Guide = std::array<double, 3>;

[[nodiscard]] std::vector<Guide> GuideOf(const MaterialRaster &a_raster) {
  std::vector<Guide> out;
  out.reserve(a_raster.texels.size());
  for (const MaterialTexel &t : a_raster.texels) {
    out.push_back(Guide{t.diffuse.x, t.diffuse.y, t.diffuse.z});
  }
  return out;
}

[[nodiscard]] std::vector<double> Channel(const std::vector<Guide> &a_guide,
                                          std::size_t a_a,
                                          std::optional<std::size_t> a_b) {
  std::vector<double> out(a_guide.size());
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = a_guide[i][a_a] * (a_b ? a_guide[i][*a_b] : 1.0);
  }
  return out;
}

struct GuideStatistics {
  std::vector<Guide> mean;
  std::vector<std::array<double, 9>> inverse;
};

[[nodiscard]] std::array<double, 9>
Inverse3(const std::array<double, 9> &a_m) noexcept {
  const double c00 = a_m[4] * a_m[8] - a_m[5] * a_m[7];
  const double c01 = a_m[5] * a_m[6] - a_m[3] * a_m[8];
  const double c02 = a_m[3] * a_m[7] - a_m[4] * a_m[6];
  const double det = a_m[0] * c00 + a_m[1] * c01 + a_m[2] * c02;
  if (!std::isfinite(det) || std::abs(det) < 1e-18) {
    return {};
  }
  const double inv = 1.0 / det;
  return {c00 * inv,
          (a_m[2] * a_m[7] - a_m[1] * a_m[8]) * inv,
          (a_m[1] * a_m[5] - a_m[2] * a_m[4]) * inv,
          c01 * inv,
          (a_m[0] * a_m[8] - a_m[2] * a_m[6]) * inv,
          (a_m[2] * a_m[3] - a_m[0] * a_m[5]) * inv,
          c02 * inv,
          (a_m[1] * a_m[6] - a_m[0] * a_m[7]) * inv,
          (a_m[0] * a_m[4] - a_m[1] * a_m[3]) * inv};
}

[[nodiscard]] GuideStatistics StatisticsOf(const std::vector<Guide> &a_guide,
                                           std::uint32_t a_side,
                                           std::uint32_t a_r) {
  std::array<std::optional<BoxSums>, 3> means;
  for (std::size_t c = 0; c < 3; ++c) {
    means[c].emplace(a_side, Channel(a_guide, c, std::nullopt));
  }
  std::array<std::optional<BoxSums>, 9> products;
  for (std::size_t a = 0; a < 3; ++a) {
    for (std::size_t b = a; b < 3; ++b) {
      products[a * 3 + b].emplace(a_side, Channel(a_guide, a, b));
    }
  }
  GuideStatistics out;
  out.mean.resize(a_guide.size());
  out.inverse.resize(a_guide.size());
  for (std::uint32_t y = 0; y < a_side; ++y) {
    for (std::uint32_t x = 0; x < a_side; ++x) {
      const std::size_t i = static_cast<std::size_t>(y) * a_side + x;
      Guide m{};
      for (std::size_t c = 0; c < 3; ++c) {
        m[c] = means[c]->Mean(y, x, a_r);
      }
      std::array<double, 9> covariance{};
      for (std::size_t a = 0; a < 3; ++a) {
        for (std::size_t b = a; b < 3; ++b) {
          const double value = products[a * 3 + b]->Mean(y, x, a_r) -
                               m[a] * m[b] + (a == b ? kGuidedEpsilon : 0.0);
          covariance[a * 3 + b] = value;
          covariance[b * 3 + a] = value;
        }
      }
      out.mean[i] = m;
      out.inverse[i] = Inverse3(covariance);
    }
  }
  return out;
}

[[nodiscard]] std::vector<double>
GuidedFilter(const std::vector<Guide> &a_guide, const GuideStatistics &a_stats,
             const std::vector<double> &a_p, std::uint32_t a_side,
             std::uint32_t a_r) {
  const BoxSums meanP(a_side, a_p);
  std::array<std::optional<BoxSums>, 3> meanIp;
  for (std::size_t c = 0; c < 3; ++c) {
    std::vector<double> product(a_p.size());
    for (std::size_t i = 0; i < product.size(); ++i) {
      product[i] = a_guide[i][c] * a_p[i];
    }
    meanIp[c].emplace(a_side, product);
  }
  std::array<std::vector<double>, 3> slope;
  for (auto &s : slope) {
    s.resize(a_p.size());
  }
  std::vector<double> offset(a_p.size());
  for (std::uint32_t y = 0; y < a_side; ++y) {
    for (std::uint32_t x = 0; x < a_side; ++x) {
      const std::size_t i = static_cast<std::size_t>(y) * a_side + x;
      const double mp = meanP.Mean(y, x, a_r);
      Guide covariance{};
      for (std::size_t c = 0; c < 3; ++c) {
        covariance[c] = meanIp[c]->Mean(y, x, a_r) - a_stats.mean[i][c] * mp;
      }
      const auto &inv = a_stats.inverse[i];
      double b = mp;
      for (std::size_t r = 0; r < 3; ++r) {
        const double a = inv[r * 3] * covariance[0] +
                         inv[r * 3 + 1] * covariance[1] +
                         inv[r * 3 + 2] * covariance[2];
        slope[r][i] = a;
        b -= a * a_stats.mean[i][r];
      }
      offset[i] = b;
    }
  }
  const BoxSums meanB(a_side, offset);
  std::array<std::optional<BoxSums>, 3> meanA;
  for (std::size_t c = 0; c < 3; ++c) {
    meanA[c].emplace(a_side, slope[c]);
  }
  std::vector<double> out(a_p.size());
  for (std::uint32_t y = 0; y < a_side; ++y) {
    for (std::uint32_t x = 0; x < a_side; ++x) {
      const std::size_t i = static_cast<std::size_t>(y) * a_side + x;
      double q = meanB.Mean(y, x, a_r);
      for (std::size_t c = 0; c < 3; ++c) {
        q += meanA[c]->Mean(y, x, a_r) * a_guide[i][c];
      }
      out[i] = std::clamp(q, 0.0, 1.0);
    }
  }
  return out;
}
}

std::vector<std::uint16_t> ChartRasterOf(const MeshData &a_mesh,
                                         const MeshAnalysis &a_analysis,
                                         std::uint32_t a_side) {
  if (a_side < kMinRasterSide || a_side > kMaxRasterSide) {
    return {};
  }
  std::vector<std::uint16_t> out(static_cast<std::size_t>(a_side) * a_side,
                                 kNoChart);
  std::size_t base = 0;
  for (const MeshPartition &partition : a_mesh.partitions) {
    for (const auto &triangle : partition.triangles) {
      if (std::ranges::any_of(triangle, [&](std::uint32_t a_v) {
            return a_v >= partition.vertices.size();
          })) {
        continue;
      }
      const std::size_t first = base + triangle[0];
      if (first >= a_analysis.chartOf.size() ||
          a_analysis.chartOf[first] == kNoIsland) {
        continue;
      }
      FillTriangle({partition.vertices[triangle[0]].uv,
                    partition.vertices[triangle[1]].uv,
                    partition.vertices[triangle[2]].uv},
                   a_analysis.chartOf[first], a_side, out);
    }
    base += partition.vertices.size();
  }
  return out;
}

std::vector<GroupScoring> ScoringOf(const MaterialGroups &a_groups) {
  std::vector<GroupScoring> out;
  for (const MaterialGroup &group : a_groups.groups) {
    auto prepared = Prepare(group.gaussian);
    if (!prepared) {
      continue;
    }
    auto whitening = WhiteningOf(*prepared);
    if (!whitening) {
      continue;
    }
    GroupScoring scoring{
        group.id, {}, *whitening, static_cast<float>(prepared->logScale)};
    for (std::size_t a = 0; a < kTexelAxes; ++a) {
      scoring.mean[a] = static_cast<float>(prepared->mean[a]);
    }
    out.push_back(scoring);
  }
  return out;
}

float ScoreOf(const GroupScoring &a_group,
              const Standardisation &a_standardisation,
              const MaterialTexel &a_texel) noexcept {
  const auto z = Standardised(a_standardisation, a_texel);
  double sum = 0.0;
  std::size_t at = 0;
  for (std::size_t i = 0; i < kTexelAxes; ++i) {
    double row = 0.0;
    for (std::size_t j = 0; j <= i; ++j) {
      row += a_group.whitening[at++] * (z[j] - a_group.mean[j]);
    }
    sum += row * row;
  }
  return static_cast<float>(a_group.logScale - 0.5 * sum);
}

GroupProbabilities SmoothedProbabilities(const MaterialRaster &a_raster,
                                         const MaterialGroups &a_groups,
                                         std::uint32_t a_radius) {
  const std::uint32_t side = a_raster.side;
  GroupProbabilities out{side, {}};
  const std::size_t size = static_cast<std::size_t>(side) * side;
  const std::vector<PreparedGaussian> prepared = PreparedOf(a_groups.groups);
  if (prepared.empty() || a_raster.texels.size() != size) {
    return out;
  }
  std::vector<std::vector<double>> probability(prepared.size(),
                                               std::vector<double>(size));
  for (std::size_t i = 0; i < size; ++i) {
    const auto z = Standardised(a_groups.standardisation, a_raster.texels[i]);
    const std::vector<double> p = Posteriors(prepared, z);
    for (std::size_t g = 0; g < prepared.size(); ++g) {
      probability[g][i] = p[g];
    }
  }
  const std::uint32_t radius = std::min(a_radius, kMaxClusterSmoothing);
  if (radius > 0) {
    const std::vector<Guide> guide = GuideOf(a_raster);
    const GuideStatistics stats = StatisticsOf(guide, side, radius);
    for (std::vector<double> &p : probability) {
      p = GuidedFilter(guide, stats, p, side, radius);
    }
  }
  out.ofTexel.assign(size, {});
  for (std::size_t i = 0; i < size; ++i) {
    double total = 0.0;
    for (const std::vector<double> &p : probability) {
      total += p[i];
    }
    for (std::size_t g = 0; g < prepared.size(); ++g) {
      const auto id = static_cast<std::size_t>(a_groups.groups[g].id);
      if (id >= kMaxGroups) {
        continue;
      }
      const double share = total > 0.0 ? probability[g][i] / total : 0.0;
      out.ofTexel[i][id] =
          static_cast<std::uint8_t>(std::lround(share * 255.0));
    }
  }
  return out;
}

MaterialAnalysis AnalysisOf(const MaterialRaster &a_raster,
                            const std::vector<std::int16_t> &a_labels) {
  struct Sum {
    std::size_t count = 0;
    MaterialTexel total;
  };
  std::map<std::int16_t, Sum> sums;
  std::size_t covered = 0;
  for (std::size_t i = 0; i < a_labels.size() && i < a_raster.texels.size();
       ++i) {
    if (a_labels[i] == kNoGroup) {
      continue;
    }
    ++covered;
    Sum &sum = sums[a_labels[i]];
    const MaterialTexel &t = a_raster.texels[i];
    ++sum.count;
    sum.total.roughness += t.roughness;
    sum.total.metallic += t.metallic;
    sum.total.occlusion += t.occlusion;
    sum.total.reflectance += t.reflectance;
    sum.total.luma += t.luma;
    sum.total.diffuse = Vec3{sum.total.diffuse.x + t.diffuse.x,
                             sum.total.diffuse.y + t.diffuse.y,
                             sum.total.diffuse.z + t.diffuse.z};
    sum.total.detail += t.detail;
  }
  MaterialAnalysis out;
  for (const auto &[id, sum] : sums) {
    const auto n = static_cast<float>(sum.count);
    const MaterialTexel &t = sum.total;
    const MaterialTexel centroid{
        t.roughness / n,
        t.metallic / n,
        t.occlusion / n,
        t.reflectance / n,
        t.luma / n,
        Vec3{t.diffuse.x / n, t.diffuse.y / n, t.diffuse.z / n},
        t.detail / n};
    out.clusters.push_back(MaterialCluster{
        static_cast<std::uint8_t>(id), centroid,
        static_cast<float>(sum.count) / static_cast<float>(covered),
        DescribeTexel(centroid)});
  }
  std::ranges::stable_sort(
      out.clusters, std::greater{},
      [](const MaterialCluster &a_c) { return a_c.share; });
  return out;
}

std::expected<MaterialGrouping, std::string>
GroupMaterial(const MaterialSample &a_sample,
              std::vector<std::uint16_t> a_charts,
              const ClusterSettings &a_settings) {
  if (a_sample.width != a_sample.height) {
    return std::unexpected("the material sample is not square");
  }
  auto raster = RasterOf(a_sample.width, a_sample.texels, std::move(a_charts));
  if (!raster) {
    return std::unexpected(raster.error());
  }
  const Regions regions = SegmentRaster(*raster, SegmentSettings{});
  GroupSettings grouping;
  grouping.merge = a_settings.merge;
  auto groups = GroupRegions(*raster, regions, grouping);
  if (!groups) {
    return std::unexpected(groups.error());
  }
  MaterialGrouping out;
  out.settings = a_settings;
  out.labels = LabelTexels(*raster, *groups);
  out.analysis = AnalysisOf(*raster, out.labels);
  out.scoring = ScoringOf(*groups);
  out.smoothed = SmoothedProbabilities(*raster, *groups, a_settings.smoothing);
  out.groups = std::move(*groups);
  out.raster = std::move(*raster);
  if (out.scoring.empty() || out.analysis.clusters.empty()) {
    return std::unexpected("the material has no groups");
  }
  return out;
}
}
