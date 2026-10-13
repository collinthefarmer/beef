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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
float ClusterWeight(float a_share, float a_bias) noexcept {
  return std::isfinite(a_share) && a_share > 0.0f ? std::pow(a_share, a_bias)
                                                  : 0.0f;
}

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
  const Chroma chroma = ChromaOf(a_texel.diffuse);
  return Axes{a_texel.roughness,   a_texel.metallic, a_texel.occlusion,
              a_texel.reflectance, a_texel.luma,     chroma.orangeBlue,
              chroma.greenMagenta, a_texel.detail};
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
                            Finite01(a_texel.diffuse.z)},
                       Finite01(a_texel.detail)};
}

[[nodiscard]] Axes ScalesOf(const ChannelWeights &a_weights) noexcept {
  const Axes raw{a_weights.roughness,   a_weights.metallic, a_weights.occlusion,
                 a_weights.reflectance, a_weights.luma,     a_weights.chroma,
                 a_weights.chroma,      a_weights.detail};
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

struct Clustering {
  std::vector<Axes> centroids;
  std::vector<std::size_t> owner;
};

[[nodiscard]] std::vector<Axes> TexelAxesOf(const MaterialSample &a_sample) {
  std::vector<Axes> texels;
  texels.reserve(std::min(a_sample.texels.size(), kMaxSampleTexels));
  for (const MaterialTexel &texel : a_sample.texels) {
    if (texels.size() >= kMaxSampleTexels) {
      break;
    }
    texels.push_back(AxesOf(Sanitised(texel)));
  }
  return texels;
}

[[nodiscard]] Clustering ClusterTexels(const std::vector<Axes> &a_texels,
                                       const ClusterSettings &a_settings) {
  const Axes scales = ScalesOf(a_settings.weights);
  const std::size_t wanted = std::min<std::size_t>(
      std::clamp<std::size_t>(a_settings.clusters, 1, kMaxMaterialClusters),
      a_texels.size());
  Lcg random(a_settings.seed);
  Clustering out;
  out.centroids = SeedCentroids(a_texels, wanted, scales, random);
  if (out.centroids.empty()) {
    return out;
  }
  out.owner.assign(a_texels.size(), 0);
  Assign(a_texels, out.centroids, scales, out.owner);
  const std::uint32_t passes =
      std::min(a_settings.iterations, kMaxClusterIterations);
  for (std::uint32_t pass = 0; pass < passes; ++pass) {
    Recentre(a_texels, out.owner, out.centroids);
    if (!Assign(a_texels, out.centroids, scales, out.owner)) {
      break;
    }
  }
  return out;
}

[[nodiscard]] std::vector<MaterialTexel>
MeanTexelsOf(const MaterialSample &a_sample, const Clustering &a_clustering) {
  std::vector<MaterialTexel> sums(a_clustering.centroids.size());
  std::vector<float> counts(a_clustering.centroids.size(), 0.0f);
  const std::size_t texels = std::min(
      {a_sample.texels.size(), a_clustering.owner.size(), kMaxSampleTexels});
  for (std::size_t i = 0; i < texels; ++i) {
    const std::size_t owner = a_clustering.owner[i];
    if (owner >= sums.size()) {
      continue;
    }
    const MaterialTexel texel = Sanitised(a_sample.texels[i]);
    MaterialTexel &sum = sums[owner];
    sum.roughness += texel.roughness;
    sum.metallic += texel.metallic;
    sum.occlusion += texel.occlusion;
    sum.reflectance += texel.reflectance;
    sum.luma += texel.luma;
    sum.diffuse =
        Vec3{sum.diffuse.x + texel.diffuse.x, sum.diffuse.y + texel.diffuse.y,
             sum.diffuse.z + texel.diffuse.z};
    sum.detail += texel.detail;
    counts[owner] += 1.0f;
  }
  for (std::size_t c = 0; c < sums.size(); ++c) {
    if (counts[c] <= 0.0f) {
      continue;
    }
    const float n = counts[c];
    MaterialTexel &mean = sums[c];
    mean.roughness /= n;
    mean.metallic /= n;
    mean.occlusion /= n;
    mean.reflectance /= n;
    mean.luma /= n;
    mean.diffuse =
        Vec3{mean.diffuse.x / n, mean.diffuse.y / n, mean.diffuse.z / n};
    mean.detail /= n;
  }
  return sums;
}

[[nodiscard]] std::vector<ClusterCount>
RankedClusters(const Clustering &a_clustering) {
  std::vector<ClusterCount> counts(a_clustering.centroids.size());
  for (std::size_t c = 0; c < counts.size(); ++c) {
    counts[c].centroid = c;
  }
  for (const std::size_t c : a_clustering.owner) {
    if (c < counts.size()) {
      ++counts[c].members;
    }
  }
  std::stable_sort(counts.begin(), counts.end(),
                   [](const ClusterCount &a, const ClusterCount &b) {
                     return a.members > b.members;
                   });
  return counts;
}
}

Chroma ChromaOf(const Vec3 &a_diffuse) noexcept {
  const float red = Finite01(a_diffuse.x);
  const float green = Finite01(a_diffuse.y);
  const float blue = Finite01(a_diffuse.z);
  return Chroma{Clamp01(0.5f + red - blue),
                Clamp01(0.5f + green - 0.5f * (red + blue))};
}

MaterialAnalysis ClusterMaterial(const MaterialSample &a_sample,
                                 const ClusterSettings &a_settings) {
  MaterialAnalysis out;
  out.settings = a_settings;
  if (a_sample.texels.empty()) {
    return out;
  }
  const std::vector<Axes> texels = TexelAxesOf(a_sample);
  const Clustering clustering = ClusterTexels(texels, a_settings);
  const std::vector<MaterialTexel> centres = MeanTexelsOf(a_sample, clustering);
  for (const ClusterCount &count : RankedClusters(clustering)) {
    if (count.members == 0 || count.centroid >= centres.size()) {
      continue;
    }
    MaterialCluster cluster;
    cluster.id = static_cast<std::uint8_t>(out.clusters.size());
    cluster.centroid = centres[count.centroid];
    cluster.share =
        static_cast<float>(count.members) / static_cast<float>(texels.size());
    cluster.description = DescribeTexel(cluster.centroid);
    out.clusters.push_back(std::move(cluster));
  }
  return out;
}

namespace {
constexpr std::size_t kSplitBins = 64;
constexpr float kFlatSpread = 0.0004f;
constexpr float kUnimodalSplit = 0.8f;
constexpr float kMinimumGap = 0.1f;
constexpr std::uint8_t kFewestGuessed = 2;
constexpr std::uint32_t kGuessSeeds = 3;
constexpr float kFitTolerance = 0.02f;

struct AxisSplit {
  float explained = 0.0f;
  float gap = 0.0f;
};

AxisSplit BestSplit(const std::vector<Axes> &a_texels, std::size_t a_axis);
float SplitWeight(const AxisSplit &a_split) noexcept;
ChannelWeights GuessedWeights(const std::vector<Axes> &a_texels);
float Inertia(const std::vector<Axes> &a_texels, const Clustering &a_clustering,
              const Axes &a_scales) noexcept;
float Fit(const std::vector<Axes> &a_texels, const Clustering &a_clustering,
          const Axes &a_scales) noexcept;
}

ClusterSettings GuessClusterSettings(const MaterialSample &a_sample) {
  ClusterSettings guess;
  const std::vector<Axes> texels = TexelAxesOf(a_sample);
  if (texels.size() < kFewestGuessed) {
    return guess;
  }
  guess.weights = GuessedWeights(texels);
  const Axes scales = ScalesOf(guess.weights);
  std::optional<ClusterSettings> best;
  float bestFit = 0.0f;
  for (std::uint8_t count = kFewestGuessed; count <= kMaxMaterialClusters;
       ++count) {
    ClusterSettings tried = guess;
    tried.clusters = count;
    float tightest = 0.0f;
    std::optional<Clustering> kept;
    for (std::uint32_t seed = 1; seed <= kGuessSeeds; ++seed) {
      ClusterSettings seeded = tried;
      seeded.seed = seed;
      Clustering clustering = ClusterTexels(texels, seeded);
      const float inertia = Inertia(texels, clustering, scales);
      if (!kept || inertia < tightest) {
        tightest = inertia;
        kept = std::move(clustering);
        tried.seed = seed;
      }
    }
    const float fit = Fit(texels, *kept, scales);
    if (!best || fit > bestFit + kFitTolerance) {
      best = tried;
      bestFit = fit;
    }
  }
  return best ? *best : guess;
}

namespace {
AxisSplit BestSplit(const std::vector<Axes> &a_texels, std::size_t a_axis) {
  std::array<float, kSplitBins> counts{};
  std::array<float, kSplitBins> sums{};
  float total = 0.0f;
  float mean = 0.0f;
  for (const Axes &texel : a_texels) {
    const float value = texel[a_axis];
    const std::size_t bin = std::min(
        static_cast<std::size_t>(value * static_cast<float>(kSplitBins)),
        kSplitBins - 1);
    counts[bin] += 1.0f;
    sums[bin] += value;
    mean += value;
    total += 1.0f;
  }
  mean /= total;
  float spread = 0.0f;
  for (const Axes &texel : a_texels) {
    const float d = texel[a_axis] - mean;
    spread += d * d;
  }
  spread /= total;
  if (spread < kFlatSpread) {
    return {};
  }
  AxisSplit best;
  float lowCount = 0.0f;
  float lowSum = 0.0f;
  for (std::size_t bin = 0; bin + 1 < kSplitBins; ++bin) {
    lowCount += counts[bin];
    lowSum += sums[bin];
    const float highCount = total - lowCount;
    if (lowCount <= 0.0f || highCount <= 0.0f) {
      continue;
    }
    const float lowMean = lowSum / lowCount;
    const float highMean = (mean * total - lowSum) / highCount;
    const float between = lowCount * highCount * (lowMean - highMean) *
                          (lowMean - highMean) / (total * total);
    const float explained = between / spread;
    if (explained > best.explained) {
      best = {explained, highMean - lowMean};
    }
  }
  return best;
}

float SplitWeight(const AxisSplit &a_split) noexcept {
  const float bimodal =
      std::clamp((a_split.explained - kUnimodalSplit) / (1.0f - kUnimodalSplit),
                 0.0f, 1.0f);
  if (a_split.gap < kMinimumGap) {
    return 0.0f;
  }
  return std::clamp(bimodal / (a_split.gap * a_split.gap), 0.0f,
                    kMaxChannelWeight);
}

ChannelWeights GuessedWeights(const std::vector<Axes> &a_texels) {
  Axes weights{};
  for (std::size_t axis = 0; axis < kAxes; ++axis) {
    weights[axis] = SplitWeight(BestSplit(a_texels, axis));
  }
  if (std::ranges::all_of(weights,
                          [](float a_weight) { return a_weight <= 0.0f; })) {
    return ChannelWeights{};
  }
  return ChannelWeights{weights[0], weights[1],
                        weights[2], weights[3],
                        weights[4], std::max(weights[5], weights[6]),
                        weights[7]};
}

float Inertia(const std::vector<Axes> &a_texels, const Clustering &a_clustering,
              const Axes &a_scales) noexcept {
  float sum = 0.0f;
  for (std::size_t i = 0; i < a_texels.size() && i < a_clustering.owner.size();
       ++i) {
    const std::size_t owner = a_clustering.owner[i];
    if (owner < a_clustering.centroids.size()) {
      sum += Distance(a_texels[i], a_clustering.centroids[owner], a_scales);
    }
  }
  return sum;
}

float Fit(const std::vector<Axes> &a_texels, const Clustering &a_clustering,
          const Axes &a_scales) noexcept {
  if (a_clustering.centroids.size() < 2 || a_texels.empty()) {
    return 0.0f;
  }
  float sum = 0.0f;
  for (std::size_t i = 0; i < a_texels.size() && i < a_clustering.owner.size();
       ++i) {
    const std::size_t owner = a_clustering.owner[i];
    float own = 0.0f;
    float other = -1.0f;
    for (std::size_t c = 0; c < a_clustering.centroids.size(); ++c) {
      const float d =
          std::sqrt(Distance(a_texels[i], a_clustering.centroids[c], a_scales));
      if (c == owner) {
        own = d;
      } else if (other < 0.0f || d < other) {
        other = d;
      }
    }
    const float larger = std::max(own, other);
    sum += larger > 0.0f ? (other - own) / larger : 0.0f;
  }
  return sum / static_cast<float>(a_texels.size());
}
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

std::optional<std::uint8_t> PickCluster(const MaterialAnalysis &a_analysis,
                                        float a_pick,
                                        float a_areaBias) noexcept {
  if (a_analysis.clusters.empty()) {
    return std::nullopt;
  }
  const float bias = std::isfinite(a_areaBias)
                         ? std::clamp(a_areaBias, 0.0f, kMaxAreaBias)
                         : 1.0f;
  const float pick =
      std::isfinite(a_pick) ? std::clamp(a_pick, 0.0f, 1.0f) : 0.0f;
  float total = 0.0f;
  for (const MaterialCluster &cluster : a_analysis.clusters) {
    total += ClusterWeight(cluster.share, bias);
  }
  if (!(total > 0.0f) || !std::isfinite(total)) {
    return a_analysis.clusters.front().id;
  }
  const float target = pick * total;
  float reached = 0.0f;
  for (const MaterialCluster &cluster : a_analysis.clusters) {
    reached += ClusterWeight(cluster.share, bias);
    if (target < reached) {
      return cluster.id;
    }
  }
  return a_analysis.clusters.back().id;
}

std::optional<float> PickValueOf(const MaterialAnalysis &a_analysis,
                                 std::uint8_t a_cluster,
                                 float a_areaBias) noexcept {
  const float bias = std::isfinite(a_areaBias)
                         ? std::clamp(a_areaBias, 0.0f, kMaxAreaBias)
                         : 1.0f;
  float total = 0.0f;
  for (const MaterialCluster &cluster : a_analysis.clusters) {
    total += ClusterWeight(cluster.share, bias);
  }
  if (!(total > 0.0f) || !std::isfinite(total)) {
    return std::nullopt;
  }
  float reached = 0.0f;
  for (const MaterialCluster &cluster : a_analysis.clusters) {
    const float weight = ClusterWeight(cluster.share, bias);
    if (cluster.id == a_cluster) {
      return weight > 0.0f
                 ? std::optional<float>{(reached + weight / 2.0f) / total}
                 : std::nullopt;
    }
    reached += weight;
  }
  return std::nullopt;
}

}
