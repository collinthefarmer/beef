// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/AreaPick.h"

#include "recipe/ExpressionMath.h"

#include <algorithm>
#include <cmath>

namespace BetterEnchantmentEffects {
namespace {
constexpr float kGeometrySalt = 0.5f;
constexpr float kGroupSalt = 1.0f;
constexpr float kChartSalt = 4096.0f;
constexpr auto kMostShuffled = static_cast<std::size_t>(kMaxShuffleTotal);

std::size_t CountOf(float a_wanted, std::size_t a_available) noexcept;
std::vector<ChartCover> CoversOfGroup(std::span<const ChartCover> a_covers,
                                      std::uint8_t a_group);
float ChartSeedOf(float a_seed, GeometryPlace a_place, std::uint8_t a_group);
float LitShareOf(std::span<const ChartCover> a_covers,
                 const PickedArea &a_picked);
MaterialAnalysis GroupsWithCharts(const MaterialAnalysis &a_analysis,
                                  std::span<const ChartCover> a_covers);
std::optional<LitCharts>
PickCharts(const ChartChoice &a_choice, float a_seed,
           const std::vector<std::uint16_t> &a_candidates,
           std::span<const ChartCover> a_groupCovers);
std::vector<std::uint16_t>
InShuffleOrder(const std::vector<std::uint16_t> &a_candidates, float a_seed);
float AreaOf(std::span<const ChartCover> a_groupCovers, std::uint16_t a_chart);
float Unit(float a_value) noexcept;
}

std::optional<PickedArea> PickArea(const AreaPickSettings &a_settings,
                                   GeometryPlace a_place,
                                   const MaterialAnalysis &a_analysis,
                                   std::span<const ChartCover> a_covers) {
  if (!IsGeometryPicked(a_settings, a_place)) {
    return std::nullopt;
  }
  const float groupSeed =
      HashUnit(a_settings.seed, kGroupSalt + static_cast<float>(a_place.index));
  const std::optional<std::uint8_t> group = PickCluster(
      a_settings.charts ? GroupsWithCharts(a_analysis, a_covers) : a_analysis,
      groupSeed, a_settings.areaBias);
  if (!group) {
    return std::nullopt;
  }
  PickedArea picked;
  picked.group = *group;
  if (a_settings.charts) {
    std::optional<LitCharts> charts = PickCharts(
        *a_settings.charts, ChartSeedOf(a_settings.seed, a_place, *group),
        CandidateCharts(a_covers, *group, a_settings.charts->minCover),
        CoversOfGroup(a_covers, *group));
    if (!charts) {
      return std::nullopt;
    }
    picked.charts = *charts;
    picked.litCharts =
        static_cast<std::size_t>(std::ranges::count(*picked.charts, true));
  }
  picked.share = LitShareOf(a_covers, picked);
  return picked;
}

bool IsGeometryPicked(const AreaPickSettings &a_settings,
                      GeometryPlace a_place) noexcept {
  const std::size_t count = std::min<std::size_t>(
      std::max<std::uint32_t>(a_place.count, 1), kMostShuffled);
  if (a_place.index >= count) {
    return false;
  }
  const std::size_t lit = CountOf(a_settings.geometries, count);
  return ShufflePosition(static_cast<float>(a_place.index),
                         static_cast<float>(count),
                         HashUnit(a_settings.seed, kGeometrySalt)) <
         static_cast<float>(lit);
}

std::vector<std::uint16_t> CandidateCharts(std::span<const ChartCover> a_covers,
                                           std::uint8_t a_group,
                                           float a_minCover) {
  std::vector<ChartCover> covers = CoversOfGroup(a_covers, a_group);
  if (covers.empty()) {
    return {};
  }
  std::ranges::stable_sort(
      covers, [](const ChartCover &a_left, const ChartCover &a_right) {
        return a_left.cover > a_right.cover;
      });
  const float minCover =
      std::isfinite(a_minCover) ? std::clamp(a_minCover, 0.0f, 1.0f) : 0.0f;
  const auto above =
      std::ranges::count_if(covers, [&](const ChartCover &a_cover) {
        return a_cover.cover >= minCover;
      });
  const std::size_t kept =
      std::min<std::size_t>(std::max<std::ptrdiff_t>(above, 1), kMostShuffled);
  std::vector<std::uint16_t> charts;
  charts.reserve(kept);
  for (std::size_t k = 0; k < kept && k < covers.size(); ++k) {
    charts.push_back(covers[k].chart);
  }
  std::ranges::sort(charts);
  return charts;
}

namespace {
std::size_t CountOf(float a_wanted, std::size_t a_available) noexcept {
  if (a_available == 0) {
    return 0;
  }
  if (!std::isfinite(a_wanted)) {
    return 1;
  }
  const float rounded = std::round(a_wanted);
  return static_cast<std::size_t>(
      std::clamp(rounded, 1.0f, static_cast<float>(a_available)));
}

std::vector<ChartCover> CoversOfGroup(std::span<const ChartCover> a_covers,
                                      std::uint8_t a_group) {
  std::vector<ChartCover> covers;
  for (const ChartCover &cover : a_covers) {
    if (cover.group == a_group && cover.chart <= kMaxPickChart &&
        cover.cover > 0.0f) {
      covers.push_back(cover);
    }
  }
  return covers;
}

MaterialAnalysis GroupsWithCharts(const MaterialAnalysis &a_analysis,
                                  std::span<const ChartCover> a_covers) {
  MaterialAnalysis kept;
  for (const MaterialCluster &cluster : a_analysis.clusters) {
    if (!CoversOfGroup(a_covers, cluster.id).empty()) {
      kept.clusters.push_back(cluster);
    }
  }
  return kept;
}

float ChartSeedOf(float a_seed, GeometryPlace a_place, std::uint8_t a_group) {
  const float salt = kChartSalt + static_cast<float>(a_place.index) * 16.0f +
                     static_cast<float>(a_group);
  return HashUnit(a_seed, salt);
}

std::optional<LitCharts>
PickCharts(const ChartChoice &a_choice, float a_seed,
           const std::vector<std::uint16_t> &a_candidates,
           std::span<const ChartCover> a_groupCovers) {
  if (a_candidates.empty()) {
    return std::nullopt;
  }
  const std::size_t least = CountOf(a_choice.count, a_candidates.size());
  const float minShare = Unit(a_choice.minShare);
  LitCharts charts{};
  std::size_t lit = 0;
  float share = 0.0f;
  for (const std::uint16_t chart : InShuffleOrder(a_candidates, a_seed)) {
    if (lit >= least && share >= minShare) {
      break;
    }
    if (chart < charts.size()) {
      charts[chart] = true;
      ++lit;
      share += AreaOf(a_groupCovers, chart);
    }
  }
  return charts;
}

std::vector<std::uint16_t>
InShuffleOrder(const std::vector<std::uint16_t> &a_candidates, float a_seed) {
  const auto total = static_cast<float>(a_candidates.size());
  std::vector<std::pair<float, std::uint16_t>> ranked;
  ranked.reserve(a_candidates.size());
  for (std::size_t k = 0; k < a_candidates.size(); ++k) {
    ranked.emplace_back(ShufflePosition(static_cast<float>(k), total, a_seed),
                        a_candidates[k]);
  }
  std::ranges::sort(ranked);
  std::vector<std::uint16_t> ordered;
  ordered.reserve(ranked.size());
  for (const auto &[position, chart] : ranked) {
    ordered.push_back(chart);
  }
  return ordered;
}

float AreaOf(std::span<const ChartCover> a_groupCovers, std::uint16_t a_chart) {
  const auto found =
      std::ranges::find(a_groupCovers, a_chart, &ChartCover::chart);
  return found != a_groupCovers.end() ? found->cover * found->chartShare : 0.0f;
}

float Unit(float a_value) noexcept {
  return std::isfinite(a_value) ? std::clamp(a_value, 0.0f, 1.0f) : 0.0f;
}

float LitShareOf(std::span<const ChartCover> a_covers,
                 const PickedArea &a_picked) {
  float share = 0.0f;
  for (const ChartCover &cover : a_covers) {
    const bool lit =
        !a_picked.charts || (cover.chart < a_picked.charts->size() &&
                             (*a_picked.charts)[cover.chart]);
    if (cover.group == a_picked.group && lit) {
      share += cover.cover * cover.chartShare;
    }
  }
  return share;
}
}
}
