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

[[nodiscard]] LabelledTexels
LabelledOf(const MaterialRaster &a_raster,
           const std::vector<std::int16_t> &a_labels) {
  LabelledTexels out;
  for (std::uint32_t y = 0; y < a_raster.side; y += kLabelledStride) {
    for (std::uint32_t x = 0; x < a_raster.side; x += kLabelledStride) {
      const std::size_t i = static_cast<std::size_t>(y) * a_raster.side + x;
      if (i < a_labels.size() && i < a_raster.texels.size()) {
        out.texels.push_back(a_raster.texels[i]);
        out.labels.push_back(a_labels[i]);
      }
    }
  }
  return out;
}
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
  out.scoring = ScoringOf(*groups);
  out.smoothed = SmoothedProbabilities(*raster, *groups, a_settings.smoothing);
  const std::vector<std::int16_t> labels = LabelWithOdds(
      *raster, *groups, out.scoring, out.smoothed, OddsWeightOf(a_settings));
  out.analysis = AnalysisOf(*raster, labels);
  out.covers = ChartCoversOf(*raster, labels);
  out.labelled = LabelledOf(*raster, labels);
  out.groups = std::move(*groups);
  out.groups.ofTexel.clear();
  if (out.scoring.empty() || out.analysis.clusters.empty()) {
    return std::unexpected("the material has no groups");
  }
  return out;
}

std::vector<ChartCover>
ChartCoversOf(const MaterialRaster &a_raster,
              const std::vector<std::int16_t> &a_labels) {
  constexpr std::size_t kCharts = kMaxPickChart + 1;
  std::vector<std::size_t> totals(kCharts, 0);
  std::vector<std::array<std::size_t, kMaxGroups>> counts(kCharts);
  const std::size_t texels = std::min(a_raster.charts.size(), a_labels.size());
  for (std::size_t i = 0; i < texels; ++i) {
    const std::uint16_t chart = a_raster.charts[i];
    const std::int16_t group = a_labels[i];
    if (chart >= kCharts || group < 0 ||
        static_cast<std::size_t>(group) >= kMaxGroups) {
      continue;
    }
    ++totals[chart];
    ++counts[chart][static_cast<std::size_t>(group)];
  }
  std::size_t covered = 0;
  for (const std::size_t total : totals) {
    covered += total;
  }
  std::vector<ChartCover> covers;
  for (std::size_t chart = 0; chart < kCharts; ++chart) {
    for (std::size_t group = 0; group < kMaxGroups; ++group) {
      if (counts[chart][group] == 0) {
        continue;
      }
      covers.push_back(
          {static_cast<std::uint8_t>(group), static_cast<std::uint16_t>(chart),
           static_cast<float>(counts[chart][group]) /
               static_cast<float>(totals[chart]),
           static_cast<float>(totals[chart]) / static_cast<float>(covered)});
    }
  }
  return covers;
}
}
