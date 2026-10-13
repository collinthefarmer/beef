// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"
#include "mesh/MaterialClusters.h"
#include "recipe/Recipe.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
enum class ClusterAxis : std::uint8_t {
  kRoughness,
  kMetallic,
  kOcclusion,
  kReflectance,
  kLuma,
  kOrangeBlue,
  kGreenMagenta,
  kDetail,
};
inline constexpr std::size_t kClusterAxisCount = 8;

struct ClusterAxisSpec {
  ClusterAxis axis;
  std::string_view name;
  float ChannelWeights::*weight;
};
inline constexpr ClusterAxisSpec kClusterAxes[]{
    {ClusterAxis::kRoughness, "rough", &ChannelWeights::roughness},
    {ClusterAxis::kMetallic, "metal", &ChannelWeights::metallic},
    {ClusterAxis::kOcclusion, "occl", &ChannelWeights::occlusion},
    {ClusterAxis::kReflectance, "refl", &ChannelWeights::reflectance},
    {ClusterAxis::kLuma, "luma", &ChannelWeights::luma},
    {ClusterAxis::kOrangeBlue, "warm", &ChannelWeights::chroma},
    {ClusterAxis::kGreenMagenta, "green", &ChannelWeights::chroma},
    {ClusterAxis::kDetail, "detail", &ChannelWeights::detail},
};
static_assert(std::size(kClusterAxes) == kClusterAxisCount);

inline constexpr std::size_t kHistogramBins = 16;
using AxisHistogram = std::array<float, kHistogramBins>;

struct AxisBand {
  float low = 0.0f;
  float centre = 0.0f;
  float high = 0.0f;
};

struct ClusterProfile {
  std::uint8_t id = 0;
  float share = 0.0f;
  Vec3 colour{};
  std::array<AxisBand, kClusterAxisCount> bands{};
  std::array<AxisHistogram, kClusterAxisCount> histograms{};
};

struct ClusterChart {
  std::array<float, kClusterAxisCount> weights{};
  std::vector<ClusterProfile> clusters;
};

enum class WeightAdvice : std::uint8_t { kKeep, kRaise, kLower };

struct AxisSeparation {
  ClusterAxis axis = ClusterAxis::kRoughness;
  float separation = 0.0f;
  float weight = 0.0f;
  WeightAdvice advice = WeightAdvice::kKeep;
};

struct ClusterView {
  MaterialAnalysis analysis;
  ClusterChart chart;
};

struct ClusterMemo {
  std::shared_ptr<const MaterialSample> sample;
  ClusterSettings settings;
  ClusterView view;
  std::optional<std::uint8_t> compared;
};

[[nodiscard]] const ClusterView *
ClustersOf(ClusterMemo &a_memo,
           const std::shared_ptr<const MaterialSample> &a_sample,
           const ClusterSettings &a_settings);
[[nodiscard]] std::vector<const MaterialCluster *>
LargestFirst(const MaterialAnalysis &a_analysis);

[[nodiscard]] ClusterChart ChartOf(const MaterialSample &a_sample,
                                   const MaterialAnalysis &a_analysis);
[[nodiscard]] const ClusterProfile *ProfileOf(const ClusterChart &a_chart,
                                              std::uint8_t a_id) noexcept;
[[nodiscard]] std::optional<std::uint8_t>
NearestOther(const ClusterChart &a_chart, std::uint8_t a_id) noexcept;
[[nodiscard]] std::vector<AxisSeparation>
SeparationOf(const ClusterChart &a_chart, std::uint8_t a_left,
             std::uint8_t a_right);
[[nodiscard]] bool CanSeparate(std::span<const AxisSeparation> a_axes) noexcept;
[[nodiscard]] std::string_view AdviceText(WeightAdvice a_advice) noexcept;
}
