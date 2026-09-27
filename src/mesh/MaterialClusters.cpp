// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/MaterialClusters.h"

#include "Core.h"
#include "recipe/Recipe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
struct TexelBand {
  float MaterialTexel::*axis = nullptr;
  std::span<const float> cuts;
  std::span<const std::string_view> words;
  bool belowInclusive = false;
};

constexpr float kRoughnessCuts[]{0.35f, 0.65f};
constexpr std::string_view kRoughnessWords[]{"polished", "matte", "rough"};
static_assert(std::size(kRoughnessWords) == std::size(kRoughnessCuts) + 1);
constexpr float kLumaCuts[]{0.35f, 0.65f};
constexpr std::string_view kLumaWords[]{"dark", "mid", "bright"};
static_assert(std::size(kLumaWords) == std::size(kLumaCuts) + 1);
constexpr float kMetallicCuts[]{0.5f};
constexpr std::string_view kMetallicWords[]{"non-metal", "metal"};
static_assert(std::size(kMetallicWords) == std::size(kMetallicCuts) + 1);

constexpr TexelBand kTexelBands[]{
    {&MaterialTexel::roughness, kRoughnessCuts, kRoughnessWords, false},
    {&MaterialTexel::luma, kLumaCuts, kLumaWords, false},
    {&MaterialTexel::metallic, kMetallicCuts, kMetallicWords, true},
};

[[nodiscard]] std::string_view BandWord(const TexelBand &a_band,
                                        float a_value) noexcept {
  for (std::size_t i = 0; i < a_band.cuts.size(); ++i) {
    const bool below = a_band.belowInclusive ? a_value <= a_band.cuts[i]
                                             : a_value < a_band.cuts[i];
    if (below) {
      return a_band.words[i];
    }
  }
  return a_band.words.back();
}

constexpr std::size_t kAxes = 8;
using Axes = std::array<float, kAxes>;

[[nodiscard]] Axes AxesOf(const MaterialTexel &a_texel) noexcept {
  return Axes{a_texel.roughness,   a_texel.metallic, a_texel.occlusion,
              a_texel.reflectance, a_texel.luma,     a_texel.diffuse.x,
              a_texel.diffuse.y,   a_texel.diffuse.z};
}

[[nodiscard]] MaterialTexel TexelOf(const Axes &a_axes) noexcept {
  return MaterialTexel{a_axes[0], a_axes[1],
                       a_axes[2], a_axes[3],
                       a_axes[4], Vec3{a_axes[5], a_axes[6], a_axes[7]}};
}

[[nodiscard]] float Finite01(float a_value) noexcept {
  return std::isfinite(a_value) ? Clamp01(a_value) : 0.0f;
}

[[nodiscard]] MaterialTexel Sanitised(const MaterialTexel &a_texel) noexcept {
  return MaterialTexel{Finite01(a_texel.roughness),
                       Finite01(a_texel.metallic),
                       Finite01(a_texel.occlusion),
                       Finite01(a_texel.reflectance),
                       Finite01(a_texel.luma),
                       Vec3{Finite01(a_texel.diffuse.x),
                            Finite01(a_texel.diffuse.y),
                            Finite01(a_texel.diffuse.z)}};
}

[[nodiscard]] Axes ScalesOf(const ChannelWeights &a_weights) noexcept {
  const Axes raw{a_weights.roughness,    a_weights.metallic,
                 a_weights.occlusion,    a_weights.reflectance,
                 a_weights.luma,         a_weights.color / 3.0f,
                 a_weights.color / 3.0f, a_weights.color / 3.0f};
  Axes scales{};
  for (std::size_t i = 0; i < kAxes; ++i) {
    scales[i] = std::isfinite(raw[i]) && raw[i] > 0.0f ? raw[i] : 0.0f;
  }
  return scales;
}

[[nodiscard]] float Distance(const Axes &a_left, const Axes &a_right,
                             const Axes &a_scales) noexcept {
  float sum = 0.0f;
  for (std::size_t i = 0; i < kAxes; ++i) {
    const float d = a_left[i] - a_right[i];
    sum += a_scales[i] * d * d;
  }
  return sum;
}

class Lcg {
public:
  explicit Lcg(std::uint32_t a_seed) noexcept : state_(a_seed) {}

  [[nodiscard]] float Unit() noexcept {
    state_ = state_ * 1664525u + 1013904223u;
    return static_cast<float>(state_ >> 8) / 16777216.0f;
  }

private:
  std::uint32_t state_;
};

[[nodiscard]] std::vector<Axes> SeedCentroids(const std::vector<Axes> &a_texels,
                                              std::size_t a_count,
                                              const Axes &a_scales,
                                              Lcg &a_random) {
  std::vector<Axes> centroids;
  if (a_texels.empty() || a_count == 0) {
    return centroids;
  }
  const std::size_t first = std::min<std::size_t>(
      static_cast<std::size_t>(a_random.Unit() *
                               static_cast<float>(a_texels.size())),
      a_texels.size() - 1);
  centroids.push_back(a_texels[first]);
  std::vector<float> nearest(a_texels.size(), 0.0f);
  while (centroids.size() < a_count) {
    float total = 0.0f;
    for (std::size_t i = 0; i < a_texels.size(); ++i) {
      float best = Distance(a_texels[i], centroids[0], a_scales);
      for (std::size_t c = 1; c < centroids.size(); ++c) {
        best = std::min(best, Distance(a_texels[i], centroids[c], a_scales));
      }
      nearest[i] = best;
      total += best;
    }
    if (!(total > 0.0f)) {
      break;
    }
    const float threshold = a_random.Unit() * total;
    float running = 0.0f;
    std::size_t pick = a_texels.size();
    for (std::size_t i = 0; i < a_texels.size(); ++i) {
      if (nearest[i] <= 0.0f) {
        continue;
      }
      running += nearest[i];
      pick = i;
      if (running > threshold) {
        break;
      }
    }
    if (pick >= a_texels.size()) {
      break;
    }
    centroids.push_back(a_texels[pick]);
  }
  return centroids;
}

[[nodiscard]] std::size_t NearestOf(const Axes &a_texel,
                                    const std::vector<Axes> &a_centroids,
                                    const Axes &a_scales) noexcept {
  std::size_t nearest = 0;
  float best = 0.0f;
  for (std::size_t c = 0; c < a_centroids.size(); ++c) {
    const float d = Distance(a_texel, a_centroids[c], a_scales);
    if (c == 0 || d < best) {
      best = d;
      nearest = c;
    }
  }
  return nearest;
}

bool Assign(const std::vector<Axes> &a_texels,
            const std::vector<Axes> &a_centroids, const Axes &a_scales,
            std::vector<std::size_t> &a_owner) noexcept {
  bool changed = false;
  for (std::size_t i = 0; i < a_texels.size() && i < a_owner.size(); ++i) {
    const std::size_t nearest = NearestOf(a_texels[i], a_centroids, a_scales);
    changed = changed || nearest != a_owner[i];
    a_owner[i] = nearest;
  }
  return changed;
}

void Recentre(const std::vector<Axes> &a_texels,
              const std::vector<std::size_t> &a_owner,
              std::vector<Axes> &a_centroids) {
  std::vector<Axes> sums(a_centroids.size(), Axes{});
  std::vector<std::size_t> counts(a_centroids.size(), 0);
  for (std::size_t i = 0; i < a_texels.size() && i < a_owner.size(); ++i) {
    const std::size_t c = a_owner[i];
    if (c >= sums.size()) {
      continue;
    }
    for (std::size_t axis = 0; axis < kAxes; ++axis) {
      sums[c][axis] += a_texels[i][axis];
    }
    ++counts[c];
  }
  for (std::size_t c = 0; c < a_centroids.size(); ++c) {
    if (counts[c] == 0) {
      continue;
    }
    for (std::size_t axis = 0; axis < kAxes; ++axis) {
      a_centroids[c][axis] = sums[c][axis] / static_cast<float>(counts[c]);
    }
  }
}

struct ClusterCount {
  std::size_t centroid = 0;
  std::size_t members = 0;
};
}

MaterialAnalysis ClusterMaterial(const MaterialSample &a_sample,
                                 const ClusterSettings &a_settings) {
  MaterialAnalysis out;
  out.settings = a_settings;
  if (a_sample.texels.empty()) {
    return out;
  }
  std::vector<Axes> texels;
  texels.reserve(std::min(a_sample.texels.size(), kMaxSampleTexels));
  for (const MaterialTexel &texel : a_sample.texels) {
    if (texels.size() >= kMaxSampleTexels) {
      break;
    }
    texels.push_back(AxesOf(Sanitised(texel)));
  }
  const Axes scales = ScalesOf(a_settings.weights);
  const std::size_t wanted = std::min<std::size_t>(
      std::clamp<std::size_t>(a_settings.clusters, 1, kMaxMaterialClusters),
      texels.size());
  Lcg random(a_settings.seed);
  std::vector<Axes> centroids = SeedCentroids(texels, wanted, scales, random);
  if (centroids.empty()) {
    return out;
  }

  std::vector<std::size_t> owner(texels.size(), 0);
  Assign(texels, centroids, scales, owner);
  const std::uint32_t passes =
      std::min(a_settings.iterations, kMaxClusterIterations);
  for (std::uint32_t pass = 0; pass < passes; ++pass) {
    Recentre(texels, owner, centroids);
    if (!Assign(texels, centroids, scales, owner)) {
      break;
    }
  }

  std::vector<ClusterCount> counts(centroids.size());
  for (std::size_t c = 0; c < centroids.size(); ++c) {
    counts[c].centroid = c;
  }
  for (const std::size_t c : owner) {
    if (c < counts.size()) {
      ++counts[c].members;
    }
  }
  std::stable_sort(counts.begin(), counts.end(),
                   [](const ClusterCount &a, const ClusterCount &b) {
                     return a.members > b.members;
                   });
  for (const ClusterCount &count : counts) {
    if (count.members == 0) {
      continue;
    }
    MaterialCluster cluster;
    cluster.id = static_cast<std::uint8_t>(out.clusters.size());
    cluster.centroid = TexelOf(centroids[count.centroid]);
    cluster.share =
        static_cast<float>(count.members) / static_cast<float>(texels.size());
    cluster.description = DescribeTexel(cluster.centroid);
    out.clusters.push_back(std::move(cluster));
  }
  return out;
}

std::uint8_t NearestCluster(const MaterialTexel &a_texel,
                            const MaterialAnalysis &a_analysis) noexcept {
  const Axes texel = AxesOf(Sanitised(a_texel));
  const Axes scales = ScalesOf(a_analysis.settings.weights);
  std::uint8_t nearest = 0;
  float best = 0.0f;
  bool first = true;
  for (const MaterialCluster &cluster : a_analysis.clusters) {
    const float d = Distance(texel, AxesOf(cluster.centroid), scales);
    if (first || d < best) {
      best = d;
      nearest = cluster.id;
      first = false;
    }
  }
  return nearest;
}

std::string DescribeTexel(const MaterialTexel &a_texel) {
  const MaterialTexel texel = Sanitised(a_texel);
  std::string out;
  for (const TexelBand &band : kTexelBands) {
    if (!out.empty()) {
      out += " ";
    }
    out += BandWord(band, texel.*band.axis);
  }
  return out;
}

}
