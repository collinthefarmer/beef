// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/MaterialRegions.h"
#include "test_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
constexpr std::uint32_t kSide = 64;
constexpr std::uint16_t kMetalChart = 0;
constexpr std::uint16_t kLeatherChart = 1;
constexpr std::uint16_t kTinyChart = 2;

class Jitter {
public:
  float Next() {
    state_ = state_ * 1664525u + 1013904223u;
    return (static_cast<float>(state_ >> 8) / 16777216.0f - 0.5f) * 0.06f;
  }

private:
  std::uint32_t state_ = 3;
};

MaterialTexel Metal(Jitter &a_j) {
  MaterialTexel t;
  t.roughness = 0.25f + a_j.Next();
  t.metallic = 0.95f;
  t.occlusion = 0.8f + a_j.Next();
  t.reflectance = 0.7f + a_j.Next();
  t.diffuse = Vec3{0.6f + a_j.Next(), 0.6f + a_j.Next(), 0.62f + a_j.Next()};
  t.luma = 0.6f;
  t.detail = 0.2f + a_j.Next();
  return t;
}

MaterialTexel Leather(Jitter &a_j) {
  MaterialTexel t;
  t.roughness = 0.8f + a_j.Next();
  t.metallic = 0.02f;
  t.occlusion = 0.6f + a_j.Next();
  t.reflectance = 0.2f + a_j.Next();
  t.diffuse = Vec3{0.42f + a_j.Next(), 0.27f + a_j.Next(), 0.16f + a_j.Next()};
  t.luma = 0.3f;
  t.detail = 0.5f + a_j.Next();
  return t;
}

bool IsTiny(std::uint32_t a_y, std::uint32_t a_x) {
  return a_y >= 60 && a_y < 63 && a_x >= 60 && a_x < 63;
}

MaterialRaster TwoMaterials() {
  Jitter jitter;
  std::vector<MaterialTexel> texels;
  std::vector<std::uint16_t> charts;
  for (std::uint32_t y = 0; y < kSide; ++y) {
    for (std::uint32_t x = 0; x < kSide; ++x) {
      if (IsTiny(y, x)) {
        texels.push_back(Metal(jitter));
        charts.push_back(kTinyChart);
      } else if (y < 4 || y >= 56) {
        texels.push_back(MaterialTexel{});
        charts.push_back(kNoChart);
      } else if (x < kSide / 2) {
        texels.push_back(Metal(jitter));
        charts.push_back(kMetalChart);
      } else {
        texels.push_back(Leather(jitter));
        charts.push_back(kLeatherChart);
      }
    }
  }
  return *RasterOf(kSide, std::move(texels), std::move(charts));
}

std::map<std::uint16_t, std::set<std::int32_t>>
RegionsByChart(const MaterialRaster &a_raster, const Regions &a_regions) {
  std::map<std::uint16_t, std::set<std::int32_t>> out;
  for (std::size_t i = 0; i < a_raster.charts.size(); ++i) {
    if (a_raster.charts[i] != kNoChart) {
      out[a_raster.charts[i]].insert(a_regions.ofTexel[i]);
    }
  }
  return out;
}

double SidePurity(const MaterialRaster &a_raster,
                  const std::vector<std::int16_t> &a_labels) {
  std::map<std::uint16_t, std::map<std::int16_t, std::size_t>> votes;
  std::size_t total = 0;
  for (std::size_t i = 0; i < a_labels.size(); ++i) {
    const std::uint16_t chart = a_raster.charts[i];
    if (chart == kMetalChart || chart == kLeatherChart) {
      ++votes[chart][a_labels[i]];
      ++total;
    }
  }
  std::size_t agree = 0;
  std::int16_t metalGroup = kNoGroup;
  for (const auto &[chart, byGroup] : votes) {
    std::pair<std::int16_t, std::size_t> best{kNoGroup, 0};
    for (const auto &[group, count] : byGroup) {
      if (count > best.second) {
        best = {group, count};
      }
    }
    if (chart == kMetalChart) {
      metalGroup = best.first;
    } else if (best.first == metalGroup) {
      return 0.0;
    }
    agree += best.second;
  }
  return static_cast<double>(agree) / static_cast<double>(total);
}
}

int main() {
  Check(!RasterOf(4, {}, {}), "a raster below the minimum side is refused");
  Check(!RasterOf(kMaxRasterSide + 1, {}, {}),
        "a raster past the maximum side is refused");
  Check(!RasterOf(8, std::vector<MaterialTexel>(63),
                  std::vector<std::uint16_t>(64, 0)),
        "a raster with too few texels is refused");
  MaterialTexel broken;
  broken.roughness = std::numeric_limits<float>::quiet_NaN();
  broken.metallic = 7.0f;
  const auto cleaned = RasterOf(8, std::vector<MaterialTexel>(64, broken),
                                std::vector<std::uint16_t>(64, 0));
  Check(cleaned && cleaned->texels[0].roughness == 0.0f &&
            cleaned->texels[0].metallic == 1.0f,
        "non-finite and out-of-range channels are clamped");

  const MaterialRaster raster = TwoMaterials();
  SegmentSettings segments;
  segments.segments = 60;
  const Regions regions = SegmentRaster(raster, segments);
  Check(regions.count > 10, "the raster splits into many regions");

  bool coveredLabelled = true;
  bool uncoveredEmpty = true;
  for (std::size_t i = 0; i < raster.charts.size(); ++i) {
    const bool covered = raster.charts[i] != kNoChart;
    coveredLabelled = coveredLabelled && (!covered || regions.ofTexel[i] >= 0);
    uncoveredEmpty =
        uncoveredEmpty && (covered || regions.ofTexel[i] == kNoRegion);
  }
  Check(coveredLabelled, "every covered texel belongs to a region");
  Check(uncoveredEmpty, "no uncovered texel belongs to a region");

  const auto byChart = RegionsByChart(raster, regions);
  std::map<std::int32_t, std::size_t> chartsOfRegion;
  for (const auto &[chart, ids] : byChart) {
    for (const std::int32_t id : ids) {
      ++chartsOfRegion[id];
    }
  }
  bool insideOneChart = true;
  for (const auto &[id, count] : chartsOfRegion) {
    insideOneChart = insideOneChart && count == 1;
  }
  Check(insideOneChart, "no region crosses a chart border");
  Equal(byChart.size(), std::size_t{3}, "all three charts hold regions");
  Equal(byChart.at(kTinyChart).size(), std::size_t{1},
        "a chart smaller than a region still gets one region of its own");

  const Regions again = SegmentRaster(raster, segments);
  Check(again.ofTexel == regions.ofTexel, "segmenting is deterministic");

  const auto groups = GroupRegions(raster, regions, GroupSettings{});
  Check(groups.has_value(), "the two-material raster groups");
  Check(groups->groups.size() >= 2,
        "metal and leather fall into separate groups");
  Check(SidePurity(raster, LabelTexels(raster, *groups)) > 0.95,
        "texel labels follow the metal and leather halves");

  GroupSettings keepAll;
  keepAll.merge = 1.0f;
  GroupSettings mergeAny;
  mergeAny.merge = 0.0f;
  const auto kept = GroupRegions(raster, regions, keepAll);
  const auto merged = GroupRegions(raster, regions, mergeAny);
  Check(kept->merges.empty(), "a merge share of one merges nothing");
  Check(merged->groups.size() <= kept->groups.size(),
        "a lower merge share never leaves more groups");

  const auto empty =
      RasterOf(kSide, std::vector<MaterialTexel>(kSide * kSide),
               std::vector<std::uint16_t>(kSide * kSide, kNoChart));
  const Regions none = SegmentRaster(*empty, segments);
  Equal(none.count, std::int32_t{0}, "an uncovered raster has no regions");
  Check(!GroupRegions(*empty, none, GroupSettings{}),
        "an uncovered raster cannot be grouped");

  Regions mismatched = regions;
  mismatched.ofTexel.pop_back();
  Check(!GroupRegions(raster, mismatched, GroupSettings{}),
        "regions that do not match the raster are refused");
  Check(LabelTexels(raster, MaterialGroups{}).size() == raster.texels.size(),
        "labelling with no groups still returns one label per texel");

  MeshData mesh;
  MeshPartition quad;
  const auto vertex = [](float a_u, float a_v) {
    MeshVertex v;
    v.uv = Vec2{a_u, a_v};
    return v;
  };
  quad.vertices = {vertex(0.0f, 0.0f),   vertex(0.5f, 0.0f),
                   vertex(0.5f, 1.0f),   vertex(0.0f, 1.0f),
                   vertex(0.75f, 0.25f), vertex(1.0f, 0.25f),
                   vertex(1.0f, 0.5f)};
  quad.triangles = {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {0, 1, 99}};
  mesh.partitions.push_back(quad);
  MeshAnalysis analysis;
  analysis.chartOf = {0, 0, 0, 0, 5, 5, 5};
  const std::vector<std::uint16_t> charts = ChartRasterOf(mesh, analysis, 16);
  Equal(charts.size(), std::size_t{256}, "a chart raster has one id per texel");
  Equal(charts[8 * 16 + 2], std::uint16_t{0},
        "the left half belongs to chart 0");
  Equal(charts[5 * 16 + 14], std::uint16_t{5},
        "the small triangle belongs to chart 5");
  Equal(charts[12 * 16 + 12], kNoChart,
        "texels outside every triangle have no chart");
  Check(ChartRasterOf(mesh, analysis, 4).empty(),
        "a raster side below the minimum is refused");
  MeshAnalysis shortAnalysis;
  Check(std::ranges::all_of(ChartRasterOf(mesh, shortAnalysis, 16),
                            [](std::uint16_t a_c) { return a_c == kNoChart; }),
        "vertices without a chart leave the raster empty");

  const std::vector<GroupScoring> scoring = ScoringOf(*groups);
  Equal(scoring.size(), groups->groups.size(),
        "every group gets shader scoring");
  bool scoresAgree = true;
  for (std::size_t g = 0; g < scoring.size(); ++g) {
    const PreparedGaussian prepared = *Prepare(groups->groups[g].gaussian);
    for (std::size_t i = 0; i < raster.texels.size(); i += 97) {
      const TexelAxes axes = AxesOfTexel(raster.texels[i]);
      std::array<double, kTexelAxes> z{};
      for (std::size_t a = 0; a < kTexelAxes; ++a) {
        z[a] = (axes[a] - groups->standardisation.centre[a]) /
               groups->standardisation.spread[a];
      }
      const double exact = LogDensity(prepared, z);
      const float packed =
          ScoreOf(scoring[g], groups->standardisation, raster.texels[i]);
      scoresAgree = scoresAgree &&
                    std::abs(exact - packed) <= 1e-3 * (1.0 + std::abs(exact));
    }
  }
  Check(scoresAgree, "the packed whitening scores as the exact density does");

  MaterialRaster speckled = raster;
  Jitter jitter;
  const std::size_t fleck = 20 * kSide + 10;
  MaterialTexel fleckTexel = Leather(jitter);
  fleckTexel.diffuse = speckled.texels[fleck].diffuse;
  fleckTexel.luma = speckled.texels[fleck].luma;
  speckled.texels[fleck] = fleckTexel;
  const auto speckledGroups = GroupRegions(speckled, regions, GroupSettings{});
  const auto argmaxAt = [](const GroupProbabilities &a_p, std::size_t a_i) {
    const auto &row = a_p.ofTexel[a_i];
    return static_cast<std::size_t>(std::max_element(row.begin(), row.end()) -
                                    row.begin());
  };
  const GroupProbabilities raw =
      SmoothedProbabilities(speckled, *speckledGroups, 0);
  const GroupProbabilities smooth =
      SmoothedProbabilities(speckled, *speckledGroups, 4);
  Check(argmaxAt(raw, fleck) != argmaxAt(raw, fleck + 3),
        "without smoothing a lone leather-like texel keeps its own group");
  Check(
      argmaxAt(smooth, fleck) == argmaxAt(smooth, fleck + 3),
      "smoothing gives a lone texel of the same colour its neighbours' group");
  int total = 0;
  for (std::uint8_t v : smooth.ofTexel[fleck]) {
    total += v;
  }
  Check(total >= 250 && total <= 260,
        "smoothed probabilities sum to about 255");
  Check(SmoothedProbabilities(speckled, MaterialGroups{}, 4).ofTexel.empty(),
        "no groups give no probabilities");

  const std::vector<std::int16_t> density = LabelTexels(raster, *groups);
  const std::vector<std::int16_t> unweighted = LabelWithOdds(
      raster, *groups, ScoringOf(*groups), GroupProbabilities{}, 0.0f);
  std::size_t agree = 0;
  for (std::size_t i = 0; i < density.size(); ++i) {
    agree += density[i] == unweighted[i] ? 1 : 0;
  }
  Check(agree * 100 >= density.size() * 99,
        "with no odds weight the packed scoring labels as the exact density "
        "does");

  MaterialSample sample{kSide, kSide, raster.texels};
  const auto grouping = GroupMaterial(sample, raster.charts, ClusterSettings{});
  Check(grouping.has_value(), "the two-material sample groups end to end");
  const std::size_t stride = kLabelledStride;
  Check(grouping &&
            grouping->labelled.texels.size() ==
                (kSide / stride) * (kSide / stride) &&
            grouping->labelled.labels.size() ==
                grouping->labelled.texels.size(),
        "the grouping keeps a strided subset of labelled texels for the chart");
  Check(grouping && grouping->groups.ofTexel.empty(),
        "the grouping drops its per-texel region groups");
  Check(grouping && grouping->smoothed.ofTexel.size() == raster.texels.size(),
        "the grouping keeps the smoothed odds for the draw");
  Check(grouping && std::ranges::all_of(grouping->scoring,
                                        [](const GroupScoring &a_g) {
                                          return a_g.logShare < 0.0f;
                                        }),
        "each group carries the log of its share");

  Regions overflowing = regions;
  overflowing.ofTexel[100] = regions.count + 5;
  Check(!GroupRegions(raster, overflowing, GroupSettings{}),
        "a texel naming a region past the count is refused");

  return test::Finish("materialregions");
}
