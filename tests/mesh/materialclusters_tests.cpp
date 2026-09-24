// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/MaterialClusters.h"
#include "test_support.h"

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

MaterialSample TwoGroups() {
  MaterialSample sample;
  sample.width = 10;
  sample.height = 10;
  for (std::size_t i = 0; i < 50; ++i) {
    const float jitter = static_cast<float>(i) * 0.001f;
    sample.texels.push_back(
        Texel(0.12f + jitter, 0.10f, 0.30f, 0.20f, 0.15f + jitter));
    sample.texels.push_back(
        Texel(0.88f - jitter, 0.90f, 0.70f, 0.80f, 0.85f - jitter));
  }
  return sample;
}

float ShareSum(const MaterialAnalysis &a_analysis) {
  float sum = 0.0f;
  for (const MaterialCluster &cluster : a_analysis.clusters) {
    sum += cluster.share;
  }
  return sum;
}
}

int main() {
  Check(kMaxMaterialClusters == 8, "at most eight clusters");
  Check(kMaxSampleTexels == 64 * 64, "the sample caps at 64 by 64 texels");

  ClusterSettings settings;
  settings.clusters = 2;
  settings.seed = 1;

  const MaterialSample sample = TwoGroups();
  const MaterialAnalysis analysis = ClusterMaterial(sample, settings);

  Check(analysis.clusters.size() == 2,
        "two separated groups yield two clusters");
  Check(Near(ShareSum(analysis), 1.0f),
        "every texel is owned, so the shares sum to one");
  Check(analysis.settings == settings,
        "the analysis echoes the settings it ran under");

  const MaterialTexel low = Texel(0.12f, 0.10f, 0.30f, 0.20f, 0.15f);
  const MaterialTexel high = Texel(0.88f, 0.90f, 0.70f, 0.80f, 0.85f);
  const std::uint8_t lowId = NearestCluster(low, analysis);
  const std::uint8_t highId = NearestCluster(high, analysis);
  Check(lowId != highId,
        "a low-material and a high-material texel fall in different clusters");
  Check(lowId < analysis.clusters.size() && highId < analysis.clusters.size(),
        "nearest ids index a real cluster");

  const MaterialAnalysis again = ClusterMaterial(sample, settings);
  Check(again == analysis, "the same seed reproduces the clustering exactly");

  ClusterSettings otherSeed = settings;
  otherSeed.seed = 999;
  const MaterialAnalysis third = ClusterMaterial(sample, otherSeed);
  Check(third.clusters.size() == 2,
        "a different seed still resolves the two groups");
  Check(Near(ShareSum(third), 1.0f),
        "the shares still sum to one under another seed");

  ClusterSettings zero = settings;
  zero.clusters = 0;
  const MaterialAnalysis clampedLow = ClusterMaterial(sample, zero);
  Check(clampedLow.clusters.size() == 1,
        "a request for zero clusters is clamped up to one");

  ClusterSettings tooMany = settings;
  tooMany.clusters = 200;
  const MaterialAnalysis clampedHigh = ClusterMaterial(sample, tooMany);
  Check(clampedHigh.clusters.size() <= kMaxMaterialClusters,
        "a request beyond the cap is clamped down");

  MaterialSample empty;
  const MaterialAnalysis emptyAnalysis = ClusterMaterial(empty, settings);
  Check(emptyAnalysis.clusters.empty(), "an empty sample produces no clusters");
  Check(NearestCluster(low, emptyAnalysis) == 0,
        "nearest of an empty analysis is a defined zero");

  MaterialSample oversize;
  oversize.width = 128;
  oversize.height = 128;
  for (std::size_t i = 0; i < kMaxSampleTexels + 37; ++i) {
    oversize.texels.push_back(Texel(0.2f, 0.2f, 0.2f, 0.2f, 0.2f));
  }
  const MaterialAnalysis oversizeAnalysis = ClusterMaterial(oversize, settings);
  Check(!oversizeAnalysis.clusters.empty(),
        "a sample larger than the cap still clusters");
  Check(Near(ShareSum(oversizeAnalysis), 1.0f),
        "the share denominator is the capped texel count");

  ClusterSettings runaway = settings;
  runaway.iterations = 1000000;
  const MaterialAnalysis bounded = ClusterMaterial(sample, runaway);
  Check(bounded.clusters.size() == 2,
        "an unbounded iteration count is capped and still terminates");

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

  const MaterialClustersSource source{ClusterSettings{
      5, ChannelWeights{2.0f, 0.25f, 0.75f, 0.5f, 1.5f}, 7, 64}};
  Check(source.settings ==
            ClusterSettings{5, ChannelWeights{2.0f, 0.25f, 0.75f, 0.5f, 1.5f},
                            7, 64},
        "the source carries its analysis settings directly");

  return test::Finish("materialclusters");
}
