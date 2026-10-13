// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/MaterialRegions.h"
#include "test_support.h"

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

  return test::Finish("materialregions");
}
