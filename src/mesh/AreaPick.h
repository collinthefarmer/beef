// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/MaterialRegions.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects {
inline constexpr std::size_t kChartSlots = 256;

struct ChartChoice {
  float count = 1.0f;
  float minCover = kDefaultMinCover;
  float minShare = 0.0f;
};

struct AreaPickSettings {
  float seed = 0.0f;
  float geometries = kEveryGeometryCount;
  float areaBias = 1.0f;
  std::optional<ChartChoice> charts;
};

using LitCharts = std::array<bool, kChartSlots>;

struct GeometryPlace {
  std::uint32_t index = 0;
  std::uint32_t count = 1;
};

struct PickedArea {
  std::uint8_t group = 0;
  std::optional<LitCharts> charts;
  std::size_t litCharts = 0;
  float share = 0.0f;
};

[[nodiscard]] bool IsGeometryPicked(const AreaPickSettings &a_settings,
                                    GeometryPlace a_place) noexcept;
[[nodiscard]] std::vector<std::uint16_t>
CandidateCharts(std::span<const ChartCover> a_covers, std::uint8_t a_group,
                float a_minCover);
[[nodiscard]] std::optional<PickedArea>
PickArea(const AreaPickSettings &a_settings, GeometryPlace a_place,
         const MaterialAnalysis &a_analysis,
         std::span<const ChartCover> a_covers);
}
