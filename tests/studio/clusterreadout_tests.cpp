// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/ClusterReadout.h"
#include "test_support.h"

#include <memory>

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
  const MaterialAnalysis *first = ClustersOf(memo, sample, settings);
  Check(first && *first == ClusterMaterial(*sample, settings),
        "the readout shows the clustering the term's settings produce");

  const MaterialAnalysis *again = ClustersOf(memo, sample, settings);
  Check(again == first && memo.settings == settings,
        "the same settings reuse the remembered clustering");

  ClusterSettings one = settings;
  one.clusters = 1;
  const MaterialAnalysis *changed = ClustersOf(memo, sample, one);
  Check(changed && changed->clusters.size() == 1,
        "changed settings cluster the sample again");

  const MaterialAnalysis *two = ClustersOf(memo, sample, settings);
  const auto rows = LargestFirst(*two);
  Check(rows.size() == 2 && rows[0]->share > rows[1]->share &&
            rows[0]->centroid.metallic > 0.5f,
        "the largest cluster is listed first");

  return test::Finish("studio_clusterreadout");
}
