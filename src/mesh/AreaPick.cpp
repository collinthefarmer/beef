// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/AreaPick.h"

#include "recipe/ExpressionMath.h"

#include <algorithm>
#include <cmath>

namespace BetterEnchantmentEffects {
namespace {
constexpr float kGroupSalt = 1.0f;
constexpr float kChartSalt = 4096.0f;
constexpr auto kMostShuffled = static_cast<std::size_t>(kMaxShuffleTotal);

std::size_t CountOf(float a_wanted, std::size_t a_available) noexcept;
std::vector<ChartCover> CoversOfGroup(std::span<const ChartCover> a_covers,
                                      std::uint8_t a_group);
float ChartSeedOf(float a_seed, GeometryPlace a_place, std::uint8_t a_group);
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
  const std::optional<std::uint8_t> group =
      PickCluster(a_analysis, groupSeed, a_settings.areaBias);
  if (!group) {
    return std::nullopt;
  }
  const std::vector<std::uint16_t> candidates =
      CandidateCharts(a_covers, *group, a_settings.minCover);
  if (candidates.empty()) {
    return std::nullopt;
  }
  const std::size_t lit = CountOf(a_settings.charts, candidates.size());
  const float chartSeed = ChartSeedOf(a_settings.seed, a_place, *group);
  const auto total = static_cast<float>(candidates.size());
  PickedArea picked;
  picked.group = *group;
  for (std::size_t k = 0; k < candidates.size(); ++k) {
    const float position =
        ShufflePosition(static_cast<float>(k), total, chartSeed);
    if (position < static_cast<float>(lit) &&
        candidates[k] < picked.charts.size()) {
      picked.charts[candidates[k]] = true;
    }
  }
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
                         a_settings.seed) < static_cast<float>(lit);
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

float ChartSeedOf(float a_seed, GeometryPlace a_place, std::uint8_t a_group) {
  const float salt = kChartSalt + static_cast<float>(a_place.index) * 16.0f +
                     static_cast<float>(a_group);
  return HashUnit(a_seed, salt);
}
}
}
