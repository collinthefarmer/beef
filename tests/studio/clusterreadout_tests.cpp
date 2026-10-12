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
MaterialTexel Texel(float a_roughness, float a_metallic) {
  MaterialTexel texel;
  texel.roughness = a_roughness;
  texel.metallic = a_metallic;
  texel.luma = a_roughness;
  return texel;
}

std::shared_ptr<const MaterialSample> ThreeToOne() {
  MaterialSample sample;
  sample.width = 8;
  sample.height = 8;
  for (std::size_t i = 0; i < 64; ++i) {
    sample.texels.push_back(i % 4 == 0 ? Texel(0.9f, 0.0f) : Texel(0.1f, 1.0f));
  }
  return std::make_shared<const MaterialSample>(std::move(sample));
}
}

int main() {
  ClusterMemo memo;
  ClusterSettings settings;
  settings.clusters = 2;

  Check(ClustersOf(memo, nullptr, settings) == nullptr,
        "a geometry with no material sample has no clusters to show");

  const auto sample = ThreeToOne();
  const ClusterView *first = ClustersOf(memo, sample, settings);
  Check(first && first->analysis == ClusterMaterial(*sample, settings),
        "the readout shows the clustering the term's settings produce");

  const ClusterView *again = ClustersOf(memo, sample, settings);
  Check(again == first && memo.settings == settings,
        "the same settings reuse the remembered clustering");

  ClusterSettings one = settings;
  one.clusters = 1;
  memo.compared = 0;
  const ClusterView *changed = ClustersOf(memo, sample, one);
  Check(changed && changed->analysis.clusters.size() == 1 &&
            !memo.compared.has_value(),
        "changed settings cluster the sample again");

  const ClusterView *two = ClustersOf(memo, sample, settings);
  const auto rows = LargestFirst(two->analysis);
  Check(rows.size() == 2 && rows[0]->share > rows[1]->share &&
            rows[0]->centroid.metallic > 0.5f,
        "the largest cluster is listed first");

  const ClusterChart &chart = two->chart;
  const std::uint8_t metal = rows[0]->id;
  const std::uint8_t rough = rows[1]->id;
  const ClusterProfile *metalProfile = ProfileOf(chart, metal);
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
        "an id the clustering did not produce has no profile");
  Check(NearestOther(chart, metal) == std::optional<std::uint8_t>{rough},
        "with two clusters, each is the other's nearest");

  const auto separation = SeparationOf(chart, metal, rough);
  Check(separation.size() == kClusterAxisCount &&
            separation.front().axis == ClusterAxis::kMetallic &&
            separation.back().separation == 0.0f,
        "the axis where two clusters differ most is ranked first");
  const auto occlusion = std::ranges::find(separation, ClusterAxis::kOcclusion,
                                           &AxisSeparation::axis);
  Check(occlusion != separation.end() &&
            occlusion->advice == WeightAdvice::kLower,
        "a weighted axis that separates nothing is advised lower");
  Check(CanSeparate(separation) &&
            !CanSeparate(std::span<const AxisSeparation>{}),
        "two clusters apart on some axis can be separated");

  ClusterSettings unweighted = settings;
  unweighted.weights.metallic = 0.0f;
  const ClusterView *blind = ClustersOf(memo, sample, unweighted);
  const auto blindRows = LargestFirst(blind->analysis);
  const auto blindSeparation =
      SeparationOf(blind->chart, blindRows[0]->id, blindRows[1]->id);
  const auto metallic = std::ranges::find(
      blindSeparation, ClusterAxis::kMetallic, &AxisSeparation::axis);
  Check(metallic != blindSeparation.end() &&
            metallic->advice == WeightAdvice::kRaise,
        "an unweighted axis that separates two clusters is advised higher");

  return test::Finish("studio_clusterreadout");
}
