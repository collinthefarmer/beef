// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/MaterialClusters.h"
#include "test_support.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Near;

namespace {
MaterialTexel Texel(float a_roughness, float a_metallic, float a_occlusion,
                    float a_reflectance, float a_luma) {
  MaterialTexel texel;
  texel.roughness = a_roughness;
  texel.metallic = a_metallic;
  texel.occlusion = a_occlusion;
  texel.reflectance = a_reflectance;
  texel.luma = a_luma;
  return texel;
}
}

int main() {
  Check(kMaxMaterialClusters == 8, "at most eight clusters");
  Check(kMaxSampleTexels == 256 * 256, "the sample caps at 256 by 256 texels");

  Check(DescribeTexel(Texel(0.1f, 0.1f, 0.3f, 0.2f, 0.1f)) ==
            "polished dark non-metal",
        "a smooth dark dielectric reads as polished dark non-metal");
  Check(DescribeTexel(Texel(0.9f, 0.9f, 0.7f, 0.8f, 0.9f)) ==
            "rough bright metal",
        "a rough bright conductor reads as rough bright metal");
  Check(
      DescribeTexel(Texel(0.5f, 0.5f, 0.5f, 0.5f, 0.5f)) ==
          "matte mid non-metal",
      "the mid bands and the inclusive metal cut land on matte mid non-metal");
  Check(DescribeTexel(Texel(2.0f, -1.0f, 0.5f, 0.5f, 0.5f)) ==
            "rough mid non-metal",
        "out-of-range channels are clamped to [0,1] before description");

  MaterialAnalysis ranked;
  for (std::uint8_t id = 0; id < 3; ++id) {
    MaterialCluster cluster;
    cluster.id = id;
    cluster.share = id == 0 ? 0.7f : (id == 1 ? 0.2f : 0.1f);
    ranked.clusters.push_back(cluster);
  }
  Check(PickCluster(ranked, 0.0f, 1.0f) == 0 &&
            PickCluster(ranked, 0.69f, 1.0f) == 0 &&
            PickCluster(ranked, 0.71f, 1.0f) == 1 &&
            PickCluster(ranked, 0.95f, 1.0f) == 2,
        "an area bias of one picks in proportion to share");
  Check(PickCluster(ranked, 0.32f, 0.0f) == 0 &&
            PickCluster(ranked, 0.34f, 0.0f) == 1 &&
            PickCluster(ranked, 0.99f, 0.0f) == 2,
        "an area bias of zero picks every cluster equally");
  Check(PickCluster(ranked, 0.95f, 8.0f) == 0,
        "a high area bias favours the largest cluster");
  Check(PickCluster(ranked, 1.0f, 1.0f) == 2 &&
            PickCluster(ranked, -3.0f, 1.0f) == 0,
        "a pick outside zero to one is clamped");
  Check(PickCluster(ranked, std::nanf(""), 1.0f) == 0 &&
            PickCluster(ranked, 0.95f, std::nanf("")) == 2 &&
            PickCluster(ranked, 0.95f, -2.0f) == 2,
        "a non-finite pick takes the largest cluster; a bad bias counts as "
        "one or zero");
  Check(!PickCluster(MaterialAnalysis{}, 0.5f, 1.0f).has_value(),
        "an analysis without clusters picks nothing");
  for (int step = 0; step <= 100; ++step) {
    const float pick = static_cast<float>(step) / 100.0f;
    const auto id = PickCluster(ranked, pick, 1.0f);
    Check(id.has_value() && *id < ranked.clusters.size() &&
              id == PickCluster(ranked, pick, 1.0f),
          "every pick lands on an existing cluster and is stable");
  }

  for (const MaterialCluster &cluster : ranked.clusters) {
    for (const float bias : {0.0f, 1.0f, 2.5f}) {
      const std::optional<float> pick = PickValueOf(ranked, cluster.id, bias);
      Check(pick && PickCluster(ranked, *pick, bias) == cluster.id,
            "the pick value of a cluster picks that cluster");
    }
  }
  Check(!PickValueOf(ranked, 9, 1.0f).has_value(),
        "a cluster the analysis lacks has no pick value");
  Check(!PickValueOf(MaterialAnalysis{}, 0, 1.0f).has_value(),
        "an empty analysis has no pick value");

  MaterialTexel brightRed;
  brightRed.diffuse = Vec3{0.9f, 0.2f, 0.2f};
  MaterialTexel grey;
  grey.diffuse = Vec3{0.5f, 0.5f, 0.5f};
  const Chroma bright = ChromaOf(brightRed.diffuse);
  const Chroma neutral = ChromaOf(grey.diffuse);
  Check(neutral.orangeBlue == 0.5f && neutral.greenMagenta == 0.5f &&
            bright.orangeBlue > 0.5f && bright.greenMagenta < 0.5f,
        "grey has neutral chroma and red leans orange and magenta");
  const Chroma darkGrey = ChromaOf(Vec3{0.1f, 0.1f, 0.1f});
  const Chroma lightGrey = ChromaOf(Vec3{0.9f, 0.9f, 0.9f});
  Check(darkGrey.orangeBlue == lightGrey.orangeBlue &&
            darkGrey.greenMagenta == lightGrey.greenMagenta,
        "greys of any brightness share one chroma, so brightness is counted "
        "only by luma");

  return test::Finish("materialclusters");
}
