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
constexpr double kGuidedEpsilon = 1e-3;
constexpr float kOddsFloor = 1.0f / 512.0f;

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
      : side_(a_side),
        sums_((std::size_t{a_side} + 1) * (std::size_t{a_side} + 1), 0.0) {
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

template <class Values>
[[nodiscard]] std::vector<BoxSums>
BoxSumsEach(std::uint32_t a_side, std::size_t a_count, const Values &a_values) {
  std::vector<BoxSums> sums;
  sums.reserve(a_count);
  for (std::size_t c = 0; c < a_count; ++c) {
    sums.emplace_back(a_side, a_values(c));
  }
  return sums;
}

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
  const std::vector<BoxSums> means =
      BoxSumsEach(a_side, 3, [&](std::size_t a_c) {
        return Channel(a_guide, a_c, std::nullopt);
      });
  std::vector<BoxSums> products;
  products.reserve(6);
  for (std::size_t a = 0; a < 3; ++a) {
    for (std::size_t b = a; b < 3; ++b) {
      products.emplace_back(a_side, Channel(a_guide, a, b));
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
        m[c] = means[c].Mean(y, x, a_r);
      }
      std::array<double, 9> covariance{};
      std::size_t product = 0;
      for (std::size_t a = 0; a < 3; ++a) {
        for (std::size_t b = a; b < 3; ++b) {
          const double value = products[product++].Mean(y, x, a_r) -
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
  const std::vector<BoxSums> meanIp =
      BoxSumsEach(a_side, 3, [&](std::size_t a_c) {
        std::vector<double> product(a_p.size());
        for (std::size_t i = 0; i < product.size(); ++i) {
          product[i] = a_guide[i][a_c] * a_p[i];
        }
        return product;
      });
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
        covariance[c] = meanIp[c].Mean(y, x, a_r) - a_stats.mean[i][c] * mp;
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
  const std::vector<BoxSums> meanA =
      BoxSumsEach(a_side, 3, [&](std::size_t a_c) { return slope[a_c]; });
  std::vector<double> out(a_p.size());
  for (std::uint32_t y = 0; y < a_side; ++y) {
    for (std::uint32_t x = 0; x < a_side; ++x) {
      const std::size_t i = static_cast<std::size_t>(y) * a_side + x;
      double q = meanB.Mean(y, x, a_r);
      for (std::size_t c = 0; c < 3; ++c) {
        q += meanA[c].Mean(y, x, a_r) * a_guide[i][c];
      }
      out[i] = std::clamp(q, 0.0, 1.0);
    }
  }
  return out;
}
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
    GroupScoring scoring{group.id,
                         {},
                         *whitening,
                         static_cast<float>(prepared->logScale),
                         static_cast<float>(std::log(group.gaussian.weight))};
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

float OddsWeightOf(const ClusterSettings &a_settings) noexcept {
  return a_settings.smoothing > 0 ? 1.0f : 0.0f;
}

std::vector<std::int16_t>
LabelWithOdds(const MaterialRaster &a_raster, const MaterialGroups &a_groups,
              const std::vector<GroupScoring> &a_scoring,
              const GroupProbabilities &a_odds, float a_oddsWeight) {
  std::vector<std::int16_t> out(a_raster.texels.size(), kNoGroup);
  const bool useOdds =
      a_oddsWeight > 0.0f && a_odds.ofTexel.size() == out.size();
  for (std::size_t i = 0; i < out.size() && i < a_raster.charts.size(); ++i) {
    if (!IsCovered(a_raster, i)) {
      continue;
    }
    float best = -std::numeric_limits<float>::infinity();
    for (const GroupScoring &group : a_scoring) {
      float score =
          ScoreOf(group, a_groups.standardisation, a_raster.texels[i]);
      const auto id = static_cast<std::size_t>(group.id);
      if (useOdds && id < kMaxGroups) {
        const float odds = std::max(a_odds.ofTexel[i][id] / 255.0f, kOddsFloor);
        score += a_oddsWeight * (std::log(odds) - group.logShare);
      }
      if (score > best) {
        best = score;
        out[i] = group.id;
      }
    }
  }
  return out;
}
}
