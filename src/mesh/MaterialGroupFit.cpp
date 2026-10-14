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
constexpr double kRegularisation = 1e-2;
constexpr double kMinSpread = 1e-3;
constexpr std::size_t kMergeSample = 40000;
constexpr std::size_t kMinGroupTexels = 16;
constexpr std::size_t kDescriptorAxes = kTexelAxes + 1;
constexpr std::size_t kLumaAxis = 4;
constexpr std::size_t kMinRegionsForGroups = 4;

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
  const std::size_t limit = std::max<std::size_t>(a_limit, 1);
  const std::size_t stride =
      std::max<std::size_t>(1, (covered + limit - 1) / limit);
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

TexelAxes AxesOfTexel(const MaterialTexel &a_texel) noexcept {
  const Chroma chroma = ChromaOf(a_texel.diffuse);
  return TexelAxes{a_texel.roughness,   a_texel.metallic, a_texel.occlusion,
                   a_texel.reflectance, a_texel.luma,     chroma.orangeBlue,
                   chroma.greenMagenta, a_texel.detail};
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
  if (std::ranges::any_of(a_regions.ofTexel, [&](std::int32_t a_region) {
        return a_region >= a_regions.count;
      })) {
    return std::unexpected("a texel names a region past the region count");
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

TexelAxes Standardised(const Standardisation &a_s,
                       const MaterialTexel &a_texel) noexcept {
  const TexelAxes axes = AxesOfTexel(a_texel);
  std::array<double, kTexelAxes> out{};
  for (std::size_t a = 0; a < kTexelAxes; ++a) {
    out[a] = (axes[a] - a_s.centre[a]) / a_s.spread[a];
  }
  return out;
}

std::vector<PreparedGaussian>
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
}
