// GPL-3.0-only with the additional permission in COPYING.md.
#include "menu/ClusterChart.h"

#include "menu/MenuWidgets.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
namespace {
constexpr float kChartHeight = 220.0f;
constexpr float kSidePadding = 24.0f;
constexpr float kHoverRadius = 8.0f;
constexpr float kHistogramReach = 0.42f;
constexpr std::uint32_t kAxisInk = 0x80A0A0A0U;
constexpr std::uint32_t kLabelInk = 0xFFD0D0D0U;
constexpr std::uint32_t kWeightInk = 0xFF909090U;
constexpr std::array<std::uint32_t, 8> kPalette{
    0x00E8A33DU, 0x003DC9E8U, 0x0059D46BU, 0x00D45ED4U,
    0x005E8CF5U, 0x00F5E05EU, 0x00B0B0B0U, 0x004F4FE8U};

struct ChartArea {
  ImGui::ImVec2 origin;
  float width = 0.0f;
  float top = 0.0f;
  float bottom = 0.0f;
  float scale = 1.0f;
};

struct Emphasis {
  std::uint8_t bandAlpha;
  std::uint8_t lineAlpha;
  float extraThickness;
};

float AxisX(const ChartArea &a_area, std::size_t a_axis);
float ValueY(const ChartArea &a_area, float a_value);
void DrawAxes(ImGui::ImDrawList *a_draw, const ChartArea &a_area,
              const Studio::ClusterChart &a_chart);
void DrawCluster(ImGui::ImDrawList *a_draw, const ChartArea &a_area,
                 const Studio::ClusterProfile &a_cluster, Emphasis a_emphasis);
void DrawHistograms(ImGui::ImDrawList *a_draw, const ChartArea &a_area,
                    const Studio::ClusterProfile &a_cluster);
std::optional<std::uint8_t> HoveredCluster(const ChartArea &a_area,
                                           const Studio::ClusterChart &a_chart);
Emphasis EmphasisOf(std::uint8_t a_cluster, ChartFocus a_focus);
}

std::uint32_t ClusterInk(std::uint8_t a_cluster,
                         std::uint8_t a_alpha) noexcept {
  return kPalette[a_cluster % kPalette.size()] |
         (static_cast<std::uint32_t>(a_alpha) << 24);
}

std::optional<ChartClick> DrawClusterChart(const Studio::ClusterChart &a_chart,
                                           ChartFocus a_focus, float a_scale) {
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = kChartHeight * a_scale;
  ImGui::InvisibleButton("cluster-chart", ImGui::ImVec2{width, height});
  ImGui::ImDrawList *draw = ImGui::GetWindowDrawList();
  if (!draw || width <= 2.0f * kSidePadding * a_scale) {
    return std::nullopt;
  }
  const float line = ImGui::GetTextLineHeight();
  const ImGui::ImVec2 origin = ImGui::GetItemRectMin();
  const ChartArea area{origin, width, origin.y + line * 1.5f,
                       origin.y + height - line * 1.5f, a_scale};

  DrawAxes(draw, area, a_chart);
  for (const Studio::ClusterProfile &cluster : a_chart.clusters) {
    if (cluster.id != a_focus.picked && cluster.id != a_focus.compared) {
      DrawCluster(draw, area, cluster, EmphasisOf(cluster.id, a_focus));
    }
  }
  for (const std::uint8_t focused :
       {a_focus.compared.value_or(a_focus.picked), a_focus.picked}) {
    if (const Studio::ClusterProfile *cluster =
            Studio::ProfileOf(a_chart, focused)) {
      DrawCluster(draw, area, *cluster, EmphasisOf(focused, a_focus));
    }
  }
  if (const Studio::ClusterProfile *picked =
          Studio::ProfileOf(a_chart, a_focus.picked)) {
    DrawHistograms(draw, area, *picked);
  }

  if (!ImGui::IsItemHovered()) {
    return std::nullopt;
  }
  const std::optional<std::uint8_t> hovered = HoveredCluster(area, a_chart);
  if (!hovered) {
    return std::nullopt;
  }
  Tooltip(std::format("cluster {}: click to pick, ctrl-click to compare",
                      *hovered));
  if (!ImGui::IsItemClicked()) {
    return std::nullopt;
  }
  const auto *io = ImGui::GetIO();
  const bool compare = io && io->KeyCtrl;
  return ChartClick{*hovered,
                    compare ? ChartAction::kCompare : ChartAction::kPick};
}

namespace {
float AxisX(const ChartArea &a_area, std::size_t a_axis) {
  const float padding = kSidePadding * a_area.scale;
  const float span = a_area.width - 2.0f * padding;
  return a_area.origin.x + padding +
         span * static_cast<float>(a_axis) /
             static_cast<float>(Studio::kClusterAxisCount - 1);
}

float ValueY(const ChartArea &a_area, float a_value) {
  return a_area.bottom -
         std::clamp(a_value, 0.0f, 1.0f) * (a_area.bottom - a_area.top);
}

void DrawAxes(ImGui::ImDrawList *a_draw, const ChartArea &a_area,
              const Studio::ClusterChart &a_chart) {
  const float line = ImGui::GetTextLineHeight();
  for (std::size_t i = 0; i < Studio::kClusterAxisCount; ++i) {
    const float x = AxisX(a_area, i);
    ImGui::ImDrawListManager::AddLine(a_draw, {x, a_area.top},
                                      {x, a_area.bottom}, kAxisInk, 1.0f);
    const std::string name{Studio::kClusterAxes[i].name};
    const std::string weight = std::format("w{:.1f}", a_chart.weights[i]);
    const float nameWidth = ImGui::CalcTextSize(name.c_str()).x;
    const float weightWidth = ImGui::CalcTextSize(weight.c_str()).x;
    ImGui::ImDrawListManager::AddText(a_draw,
                                      {x - nameWidth / 2.0f, a_area.origin.y},
                                      kLabelInk, name.c_str());
    ImGui::ImDrawListManager::AddText(
        a_draw, {x - weightWidth / 2.0f, a_area.bottom + line * 0.4f},
        kWeightInk, weight.c_str());
  }
}

void DrawCluster(ImGui::ImDrawList *a_draw, const ChartArea &a_area,
                 const Studio::ClusterProfile &a_cluster, Emphasis a_emphasis) {
  const std::uint32_t band = ClusterInk(a_cluster.id, a_emphasis.bandAlpha);
  const std::uint32_t ink = ClusterInk(a_cluster.id, a_emphasis.lineAlpha);
  const float thickness =
      (1.0f + 5.0f * a_cluster.share + a_emphasis.extraThickness) *
      a_area.scale;
  for (std::size_t i = 0; i + 1 < Studio::kClusterAxisCount; ++i) {
    const Studio::AxisBand &from = a_cluster.bands[i];
    const Studio::AxisBand &to = a_cluster.bands[i + 1];
    const float x0 = AxisX(a_area, i);
    const float x1 = AxisX(a_area, i + 1);
    ImGui::ImDrawListManager::AddQuadFilled(
        a_draw, {x0, ValueY(a_area, from.high)}, {x1, ValueY(a_area, to.high)},
        {x1, ValueY(a_area, to.low)}, {x0, ValueY(a_area, from.low)}, band);
    ImGui::ImDrawListManager::AddLine(a_draw, {x0, ValueY(a_area, from.centre)},
                                      {x1, ValueY(a_area, to.centre)}, ink,
                                      thickness);
  }
}

void DrawHistograms(ImGui::ImDrawList *a_draw, const ChartArea &a_area,
                    const Studio::ClusterProfile &a_cluster) {
  const float reach = kHistogramReach * (AxisX(a_area, 1) - AxisX(a_area, 0));
  const float binHeight =
      (a_area.bottom - a_area.top) / static_cast<float>(Studio::kHistogramBins);
  const std::uint32_t ink = ClusterInk(a_cluster.id, 150);
  for (std::size_t i = 0; i < Studio::kClusterAxisCount; ++i) {
    const Studio::AxisHistogram &histogram = a_cluster.histograms[i];
    const float tallest = *std::ranges::max_element(histogram);
    if (tallest <= 0.0f) {
      continue;
    }
    const float x = AxisX(a_area, i);
    for (std::size_t bin = 0; bin < Studio::kHistogramBins; ++bin) {
      const float length = reach * histogram[bin] / tallest;
      const float bottom = a_area.bottom - binHeight * static_cast<float>(bin);
      ImGui::ImDrawListManager::AddRectFilled(
          a_draw, {x, bottom - binHeight + 1.0f}, {x + length, bottom}, ink,
          0.0f, 0);
    }
  }
}

std::optional<std::uint8_t>
HoveredCluster(const ChartArea &a_area, const Studio::ClusterChart &a_chart) {
  const auto *io = ImGui::GetIO();
  if (!io) {
    return std::nullopt;
  }
  const float radius = kHoverRadius * a_area.scale;
  std::optional<std::uint8_t> nearest;
  float best = radius * radius;
  for (const Studio::ClusterProfile &cluster : a_chart.clusters) {
    for (std::size_t i = 0; i < Studio::kClusterAxisCount; ++i) {
      const float dx = io->MousePos.x - AxisX(a_area, i);
      const float dy = io->MousePos.y - ValueY(a_area, cluster.bands[i].centre);
      const float distance = dx * dx + dy * dy;
      if (distance <= best) {
        best = distance;
        nearest = cluster.id;
      }
    }
  }
  return nearest;
}

Emphasis EmphasisOf(std::uint8_t a_cluster, ChartFocus a_focus) {
  if (a_cluster == a_focus.picked) {
    return {70, 255, 2.0f};
  }
  if (a_cluster == a_focus.compared) {
    return {55, 230, 1.0f};
  }
  return {25, 110, 0.0f};
}
}
}
