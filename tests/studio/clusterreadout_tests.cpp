// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/ClusterReadout.h"
#include "test_support.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <span>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
constexpr std::int16_t kMetal = 3;
constexpr std::int16_t kRough = 5;

MaterialTexel Texel(float a_roughness, float a_metallic) {
  MaterialTexel texel;
  texel.roughness = a_roughness;
  texel.metallic = a_metallic;
  texel.luma = a_roughness;
  return texel;
}

std::shared_ptr<const MaterialGrouping> ThreeToOne() {
  MaterialGrouping grouping;
  grouping.raster.side = 8;
  for (std::size_t i = 0; i < 64; ++i) {
    const bool rough = i % 4 == 0;
    grouping.raster.texels.push_back(rough ? Texel(0.9f, 0.0f)
                                           : Texel(0.1f, 1.0f));
    grouping.raster.charts.push_back(0);
    grouping.labels.push_back(rough ? kRough : kMetal);
  }
  grouping.labels[1] = kNoGroup;
  grouping.analysis = AnalysisOf(grouping.raster, grouping.labels);
  return std::make_shared<const MaterialGrouping>(std::move(grouping));
}
}

int main() {
  ClusterMemo memo;
  Check(ClustersOf(memo, nullptr) == nullptr,
        "a geometry with no grouping has no clusters to show");

  const auto grouping = ThreeToOne();
  const ClusterView *first = ClustersOf(memo, grouping);
  Check(first && first->analysis == grouping->analysis,
        "the readout shows the grouping's analysis");
  memo.compared = 0;
  const ClusterView *again = ClustersOf(memo, grouping);
  Check(again == first && memo.compared.has_value(),
        "the same grouping reuses the remembered chart");
  const ClusterView *fresh = ClustersOf(memo, ThreeToOne());
  Check(fresh && !memo.compared.has_value(),
        "a new grouping rebuilds the chart and forgets the comparison");

  const auto rows = LargestFirst(fresh->analysis);
  Check(rows.size() == 2 && rows[0]->share > rows[1]->share &&
            rows[0]->centroid.metallic > 0.5f,
        "the largest cluster is listed first");

  const ClusterChart &chart = fresh->chart;
  const ClusterProfile *metalProfile = ProfileOf(chart, kMetal);
  const auto metalAxis = static_cast<std::size_t>(ClusterAxis::kMetallic);
  float histogramTotal = 0.0f;
  for (const float bin : metalProfile->histograms[metalAxis]) {
    histogramTotal += bin;
  }
  Check(metalProfile && metalProfile->bands[metalAxis].low == 1.0f &&
            metalProfile->bands[metalAxis].high == 1.0f &&
            metalProfile->histograms[metalAxis].back() == 1.0f &&
            histogramTotal == 1.0f,
        "a cluster's band and histogram describe its own texels");
  Check(ProfileOf(chart, 7) == nullptr,
        "an id the grouping did not produce has no profile");
  Check(NearestOther(chart, kMetal) == std::optional<std::uint8_t>{kRough},
        "with two clusters, each is the other's nearest");

  const auto separation = SeparationOf(chart, kMetal, kRough);
  Check(separation.size() == kClusterAxisCount &&
            separation.front().axis == ClusterAxis::kMetallic &&
            separation.back().separation == 0.0f,
        "the axis where two clusters differ most is ranked first");
  Check(CanSeparate(separation) &&
            !CanSeparate(std::span<const AxisSeparation>{}),
        "two clusters apart on some axis can be separated");

  return test::Finish("studio_clusterreadout");
}
