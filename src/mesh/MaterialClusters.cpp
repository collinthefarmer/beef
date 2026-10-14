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
}

Chroma ChromaOf(const Vec3 &a_diffuse) noexcept {
  const float red = Finite01(a_diffuse.x);
  const float green = Finite01(a_diffuse.y);
  const float blue = Finite01(a_diffuse.z);
  return Chroma{Clamp01(0.5f + red - blue),
                Clamp01(0.5f + green - 0.5f * (red + blue))};
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
