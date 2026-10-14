// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/MaterialRegions.h"

#include "mesh/GaussianMixture.h"
#include "mesh/MaterialClusters.h"
#include "mesh/RasterGrid.h"

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
}
