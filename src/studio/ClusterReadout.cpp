// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/ClusterReadout.h"

#include <algorithm>
#include <cmath>

namespace BetterEnchantmentEffects::Studio {
namespace {
constexpr float kLowPercentile = 0.1f;
constexpr float kHighPercentile = 0.9f;
constexpr float kSpreadFloor = 0.02f;
constexpr float kClearSeparation = 1.0f;
constexpr float kWeakSeparation = 0.3f;
constexpr float kLowWeight = 0.5f;

using AxisValues = std::array<float, kClusterAxisCount>;

AxisValues AxisValuesOf(const MaterialTexel &a_texel) noexcept;
float Finite01(float a_value) noexcept;
ClusterProfile ProfileFrom(const MaterialCluster &a_cluster,
                           std::vector<AxisValues> a_members);
AxisBand BandOf(std::vector<float> &a_values, float a_centre);
AxisHistogram HistogramOf(const std::vector<float> &a_values);
WeightAdvice AdviceFor(float a_separation, float a_weight) noexcept;
}

const ClusterView *
ClustersOf(ClusterMemo &a_memo,
           const std::shared_ptr<const MaterialSample> &a_sample,
           const ClusterSettings &a_settings) {
  if (!a_sample) {
    return nullptr;
  }
  if (a_memo.sample != a_sample || a_memo.settings != a_settings) {
    MaterialAnalysis analysis = ClusterMaterial(*a_sample, a_settings);
    ClusterChart chart = ChartOf(*a_sample, analysis);
    a_memo = {a_sample,
              a_settings,
              {std::move(analysis), std::move(chart)},
              std::nullopt};
  }
  return &a_memo.view;
}

std::vector<const MaterialCluster *>
LargestFirst(const MaterialAnalysis &a_analysis) {
  std::vector<const MaterialCluster *> rows;
  rows.reserve(a_analysis.clusters.size());
  for (const MaterialCluster &cluster : a_analysis.clusters) {
    rows.push_back(&cluster);
  }
  std::ranges::stable_sort(
      rows, [](const MaterialCluster *a_left, const MaterialCluster *a_right) {
        return a_left->share > a_right->share;
      });
  return rows;
}

ClusterChart ChartOf(const MaterialSample &a_sample,
                     const MaterialAnalysis &a_analysis) {
  ClusterChart chart;
  for (std::size_t i = 0; i < kClusterAxisCount; ++i) {
    const float weight = a_analysis.settings.weights.*kClusterAxes[i].weight;
    chart.weights[i] = std::isfinite(weight) ? std::max(weight, 0.0f) : 0.0f;
  }
  std::vector<std::vector<AxisValues>> members(a_analysis.clusters.size());
  const std::size_t count = std::min(a_sample.texels.size(), kMaxSampleTexels);
  for (std::size_t t = 0; t < count; ++t) {
    const MaterialTexel &texel = a_sample.texels[t];
    const std::uint8_t id = NearestCluster(texel, a_analysis);
    for (std::size_t c = 0; c < a_analysis.clusters.size(); ++c) {
      if (a_analysis.clusters[c].id == id) {
        members[c].push_back(AxisValuesOf(texel));
        break;
      }
    }
  }
  for (std::size_t c = 0; c < a_analysis.clusters.size(); ++c) {
    chart.clusters.push_back(
        ProfileFrom(a_analysis.clusters[c], std::move(members[c])));
  }
  return chart;
}

const ClusterProfile *ProfileOf(const ClusterChart &a_chart,
                                std::uint8_t a_id) noexcept {
  const auto found =
      std::ranges::find(a_chart.clusters, a_id, &ClusterProfile::id);
  return found != a_chart.clusters.end() ? &*found : nullptr;
}

std::optional<std::uint8_t> NearestOther(const ClusterChart &a_chart,
                                         std::uint8_t a_id) noexcept {
  const ClusterProfile *from = ProfileOf(a_chart, a_id);
  if (!from) {
    return std::nullopt;
  }
  std::optional<std::uint8_t> nearest;
  float best = 0.0f;
  for (const ClusterProfile &other : a_chart.clusters) {
    if (other.id == a_id) {
      continue;
    }
    float distance = 0.0f;
    for (std::size_t i = 0; i < kClusterAxisCount; ++i) {
      const float d = from->bands[i].centre - other.bands[i].centre;
      distance += a_chart.weights[i] * d * d;
    }
    if (!nearest || distance < best) {
      nearest = other.id;
      best = distance;
    }
  }
  return nearest;
}

std::vector<AxisSeparation> SeparationOf(const ClusterChart &a_chart,
                                         std::uint8_t a_left,
                                         std::uint8_t a_right) {
  const ClusterProfile *left = ProfileOf(a_chart, a_left);
  const ClusterProfile *right = ProfileOf(a_chart, a_right);
  std::vector<AxisSeparation> axes;
  if (!left || !right) {
    return axes;
  }
  for (std::size_t i = 0; i < kClusterAxisCount; ++i) {
    const AxisBand &l = left->bands[i];
    const AxisBand &r = right->bands[i];
    const float spread =
        (l.high - l.low) / 2.0f + (r.high - r.low) / 2.0f + kSpreadFloor;
    const float separation = std::abs(l.centre - r.centre) / spread;
    axes.push_back({kClusterAxes[i].axis, separation, a_chart.weights[i],
                    AdviceFor(separation, a_chart.weights[i])});
  }
  std::ranges::stable_sort(
      axes, [](const AxisSeparation &a_first, const AxisSeparation &a_second) {
        return a_first.separation > a_second.separation;
      });
  return axes;
}

bool CanSeparate(std::span<const AxisSeparation> a_axes) noexcept {
  return std::ranges::any_of(a_axes, [](const AxisSeparation &a_axis) {
    return a_axis.separation >= kClearSeparation;
  });
}

std::string_view AdviceText(WeightAdvice a_advice) noexcept {
  switch (a_advice) {
  case WeightAdvice::kRaise:
    return "raise weight";
  case WeightAdvice::kLower:
    return "lower weight";
  case WeightAdvice::kKeep:
    return "";
  }
  return "";
}

namespace {
AxisValues AxisValuesOf(const MaterialTexel &a_texel) noexcept {
  const Chroma chroma = ChromaOf(a_texel.diffuse);
  return {Finite01(a_texel.roughness), Finite01(a_texel.metallic),
          Finite01(a_texel.occlusion), Finite01(a_texel.reflectance),
          Finite01(a_texel.luma),      chroma.orangeBlue,
          chroma.greenMagenta,         Finite01(a_texel.detail)};
}

float Finite01(float a_value) noexcept {
  return std::isfinite(a_value) ? std::clamp(a_value, 0.0f, 1.0f) : 0.0f;
}

ClusterProfile ProfileFrom(const MaterialCluster &a_cluster,
                           std::vector<AxisValues> a_members) {
  ClusterProfile profile;
  profile.id = a_cluster.id;
  profile.share = a_cluster.share;
  profile.colour = a_cluster.centroid.diffuse;
  const AxisValues centre = AxisValuesOf(a_cluster.centroid);
  std::vector<float> values;
  values.reserve(a_members.size());
  for (std::size_t i = 0; i < kClusterAxisCount; ++i) {
    values.clear();
    for (const AxisValues &member : a_members) {
      values.push_back(member[i]);
    }
    profile.histograms[i] = HistogramOf(values);
    profile.bands[i] = BandOf(values, centre[i]);
  }
  return profile;
}

AxisBand BandOf(std::vector<float> &a_values, float a_centre) {
  if (a_values.empty()) {
    return {a_centre, a_centre, a_centre};
  }
  std::ranges::sort(a_values);
  const auto at = [&](float a_fraction) {
    const auto last = static_cast<float>(a_values.size() - 1);
    return a_values[static_cast<std::size_t>(std::floor(a_fraction * last))];
  };
  return {at(kLowPercentile), a_centre, at(kHighPercentile)};
}

AxisHistogram HistogramOf(const std::vector<float> &a_values) {
  AxisHistogram histogram{};
  if (a_values.empty()) {
    return histogram;
  }
  for (const float value : a_values) {
    const auto bin =
        static_cast<std::size_t>(value * static_cast<float>(kHistogramBins));
    ++histogram[std::min(bin, kHistogramBins - 1)];
  }
  const auto total = static_cast<float>(a_values.size());
  for (float &bin : histogram) {
    bin /= total;
  }
  return histogram;
}

WeightAdvice AdviceFor(float a_separation, float a_weight) noexcept {
  if (a_separation >= kClearSeparation && a_weight < kLowWeight) {
    return WeightAdvice::kRaise;
  }
  if (a_separation < kWeakSeparation && a_weight >= kLowWeight) {
    return WeightAdvice::kLower;
  }
  return WeightAdvice::kKeep;
}
}
}
