// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/AreaPick.h"
#include "test_support.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
constexpr std::uint8_t kMetal = 0;
constexpr std::uint8_t kLeather = 1;

MaterialAnalysis TwoGroups(float a_metalShare) {
  MaterialAnalysis analysis;
  analysis.clusters.push_back({kMetal, {}, a_metalShare, {}});
  analysis.clusters.push_back({kLeather, {}, 1.0f - a_metalShare, {}});
  return analysis;
}

std::vector<ChartCover> Covers() {
  return {{kMetal, 0, 0.9f, 0.4f},    {kMetal, 1, 0.5f, 0.2f},
          {kMetal, 2, 0.02f, 0.3f},   {kMetal, 7, 0.3f, 0.1f},
          {kLeather, 0, 0.1f, 0.4f},  {kLeather, 1, 0.5f, 0.2f},
          {kLeather, 2, 0.98f, 0.3f}, {kMetal, 300, 1.0f, 0.0f}};
}

std::size_t LitCount(const PickedArea &a_area) {
  return static_cast<std::size_t>(std::ranges::count(a_area.charts, true));
}

std::size_t PickedGeometries(const AreaPickSettings &a_settings,
                             std::uint32_t a_count) {
  std::size_t picked = 0;
  for (std::uint32_t index = 0; index < a_count; ++index) {
    picked += IsGeometryPicked(a_settings, {index, a_count}) ? 1 : 0;
  }
  return picked;
}

void ChartCoversCountEachChartsTexels() {
  MaterialRaster raster;
  raster.side = 2;
  raster.texels.resize(6);
  raster.charts = {0, 0, 0, 1, 1, kNoChart};
  const std::vector<std::int16_t> labels{0, 0, 1, 1, kNoGroup, 0};
  const std::vector<ChartCover> covers = ChartCoversOf(raster, labels);
  const auto coverOf = [&](std::uint8_t a_group, std::uint16_t a_chart) {
    const auto found = std::ranges::find_if(covers, [&](const ChartCover &c) {
      return c.group == a_group && c.chart == a_chart;
    });
    return found != covers.end() ? found->cover : -1.0f;
  };
  Check(covers.size() == 3, "only charted, labelled texels make a cover");
  Check(std::ranges::all_of(covers,
                            [](const ChartCover &c) {
                              return test::Near(c.chartShare,
                                                c.chart == 0 ? 0.75f : 0.25f);
                            }),
        "a chart's share is its part of the charted, labelled texels");
  Check(test::Near(coverOf(0, 0), 2.0f / 3.0f) &&
            test::Near(coverOf(1, 0), 1.0f / 3.0f) &&
            test::Near(coverOf(1, 1), 1.0f),
        "a cover is the group's share of the chart's labelled texels");
}

void CandidatesKeepCoveredCharts() {
  const std::vector<ChartCover> covers = Covers();
  Check(CandidateCharts(covers, kMetal, 0.05f) ==
            std::vector<std::uint16_t>{0, 1, 7},
        "a chart the group covers below minCover is not a candidate");
  Check(CandidateCharts(covers, kMetal, 0.95f) == std::vector<std::uint16_t>{0},
        "with no chart above minCover, the group's largest chart remains");
  Check(CandidateCharts(covers, 5, 0.05f).empty(),
        "a group with no texels has no candidate chart");
  Check(CandidateCharts(covers, kMetal,
                        std::numeric_limits<float>::quiet_NaN()) ==
            std::vector<std::uint16_t>{0, 1, 2, 7},
        "a non-finite minCover keeps every covered chart");
}

void GeometriesPickTheirCount() {
  AreaPickSettings settings;
  bool exact = true;
  for (int seed = 0; seed < 40; ++seed) {
    settings.seed = static_cast<float>(seed) / 40.0f;
    settings.geometries = 2.0f;
    exact = exact && PickedGeometries(settings, 5) == 2;
  }
  Check(exact, "the pick lights exactly the asked number of geometries");
  settings.geometries = 0.0f;
  Check(PickedGeometries(settings, 5) == 1,
        "the pick lights at least one geometry");
  settings.geometries = 9.0f;
  Check(PickedGeometries(settings, 3) == 3,
        "asking for more geometries than exist lights all of them");
  Check(!IsGeometryPicked(settings, {3, 3}),
        "an index past the count is never picked");
  settings.geometries = std::numeric_limits<float>::infinity();
  Check(PickedGeometries(settings, 4) == 1,
        "a non-finite count lights one geometry");
}

void PickAreaLightsChartsOfOneGroup() {
  const std::vector<ChartCover> covers = Covers();
  const MaterialAnalysis analysis = TwoGroups(0.5f);
  AreaPickSettings settings;
  settings.charts = 2.0f;
  bool consistent = true;
  bool varied = false;
  std::optional<std::uint8_t> firstGroup;
  for (int seed = 0; seed < 40; ++seed) {
    settings.seed = static_cast<float>(seed) / 40.0f;
    const std::optional<PickedArea> area =
        PickArea(settings, {0, 1}, analysis, covers);
    if (!area) {
      consistent = false;
      continue;
    }
    const std::vector<std::uint16_t> candidates =
        CandidateCharts(covers, area->group, settings.minCover);
    for (std::size_t chart = 0; chart < area->charts.size(); ++chart) {
      if (area->charts[chart] &&
          !std::ranges::contains(candidates,
                                 static_cast<std::uint16_t>(chart))) {
        consistent = false;
      }
    }
    consistent = consistent &&
                 LitCount(*area) == std::min<std::size_t>(2, candidates.size());
    firstGroup = firstGroup.value_or(area->group);
    varied = varied || area->group != *firstGroup;
  }
  Check(consistent, "a pick lights the asked charts among the group's "
                    "candidates, and never an empty area");
  Check(varied, "different seeds pick different groups");

  AreaPickSettings everyChart;
  everyChart.charts = 9.0f;
  everyChart.minCover = 0.0f;
  bool sums = true;
  for (int seed = 0; seed < 20; ++seed) {
    everyChart.seed = static_cast<float>(seed) / 20.0f;
    const auto area = PickArea(everyChart, {0, 1}, analysis, covers);
    const float expected =
        area && area->group == kMetal
            ? 0.9f * 0.4f + 0.5f * 0.2f + 0.02f * 0.3f + 0.3f * 0.1f
            : 0.1f * 0.4f + 0.5f * 0.2f + 0.98f * 0.3f;
    sums = sums && area && test::Near(area->share, expected);
  }
  Check(sums, "the lit share sums each lit chart's cover times its share");

  settings.seed = 0.25f;
  Check(PickArea(settings, {0, 1}, analysis, covers).has_value() &&
            PickArea(settings, {0, 1}, analysis, covers)->charts ==
                PickArea(settings, {0, 1}, analysis, covers)->charts,
        "the same seed picks the same area");
  Check(!PickArea(settings, {0, 1}, MaterialAnalysis{}, covers),
        "a material with no groups picks nothing");
  settings.geometries = 1.0f;
  std::size_t lit = 0;
  for (std::uint32_t index = 0; index < 4; ++index) {
    lit += PickArea(settings, {index, 4}, analysis, covers) ? 1 : 0;
  }
  Check(lit == 1, "only the picked geometry gets an area");
}
}

int main() {
  ChartCoversCountEachChartsTexels();
  CandidatesKeepCoveredCharts();
  GeometriesPickTheirCount();
  PickAreaLightsChartsOfOneGroup();
  return test::Finish("areapick");
}
