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
void DrawSeparation(const Studio::ClusterChart &a_chart, ChartFocus a_focus);
void DrawClusterTable(const MaterialAnalysis &a_analysis,
                      std::optional<std::uint8_t> a_picked);
void DrawClusterRow(const MaterialCluster &a_cluster, bool a_picked,
                    Table &a_table);
ImGui::ImVec4 InkColour(std::uint32_t a_ink);
std::string DumpWrittenText(const Studio::MaterialDumpWritten &a_written);
}

std::uint32_t ClusterInk(std::uint8_t a_cluster,
                         std::uint8_t a_alpha) noexcept {
  const Vec3 &colour = kClusterPalette[a_cluster % kClusterPalette.size()];
  const auto byte = [](float a_channel) {
    return static_cast<std::uint32_t>(std::clamp(a_channel, 0.0f, 1.0f) *
                                      255.0f);
  };
  return byte(colour.x) | (byte(colour.y) << 8) | (byte(colour.z) << 16) |
         (static_cast<std::uint32_t>(a_alpha) << 24);
}

void DrawMaterialDump(const Frame &a_frame) {
  if (!a_frame.piece || !a_frame.geometry || !a_frame.intents ||
      !a_frame.snapshot) {
    return;
  }
  ImGui::PushID("material-dump");
  if (ImGui::Button("Dump material")) {
    Studio::Post(*a_frame.intents,
                 Studio::DumpMaterial{a_frame.piece->ref.actorID,
                                      a_frame.geometry->name});
  }
  ImGui::PopID();
  const std::optional<Studio::MaterialDumpResult> &result =
      a_frame.snapshot->materialDump;
  if (!result || result->geometry != a_frame.geometry->name) {
    return;
  }
  if (!result->outcome) {
    Warn(std::format("The dump failed: {}", result->outcome.error()));
    return;
  }
  Ok(DumpWrittenText(*result->outcome));
}

std::optional<std::uint8_t> DrawClusterExplorer(const ClusterSubject &a_subject,
                                                Studio::ClusterMemo &a_memo,
                                                float a_scale) {
  const Studio::ClusterView *view =
      Studio::ClustersOf(a_memo, a_subject.geometry.grouping);
  if (!view) {
    Dim("The material of this geometry has not been grouped yet.");
    return std::nullopt;
  }
  Dim(std::format("{} clusters on {}", view->analysis.clusters.size(),
                  a_subject.geometry.name));
  if (a_subject.settings != ClusterSettings{}) {
    Dim("The chart shows the default merge and smoothing; the piece shows "
        "this source's.");
  }
  const std::optional<std::uint8_t> nearest =
      a_subject.picked ? Studio::NearestOther(view->chart, *a_subject.picked)
                       : std::nullopt;
  const ChartFocus focus{a_subject.picked,
                         a_memo.compared ? a_memo.compared : nearest};
  std::optional<std::uint8_t> picked;
  if (const std::optional<ChartClick> click =
          DrawClusterChart(view->chart, focus, a_scale)) {
    if (click->action == ChartAction::kCompare) {
      a_memo.compared = click->cluster;
    } else {
      picked = click->cluster;
      if (a_memo.compared == click->cluster) {
        a_memo.compared.reset();
      }
    }
  }
  DrawSeparation(view->chart, focus);
  DrawClusterTable(view->analysis, a_subject.picked);
  return picked;
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
  for (const std::optional<std::uint8_t> focused :
       {a_focus.compared, a_focus.picked}) {
    const Studio::ClusterProfile *cluster =
        focused ? Studio::ProfileOf(a_chart, *focused) : nullptr;
    if (cluster) {
      DrawCluster(draw, area, *cluster, EmphasisOf(cluster->id, a_focus));
    }
  }
  const Studio::ClusterProfile *picked =
      a_focus.picked ? Studio::ProfileOf(a_chart, *a_focus.picked) : nullptr;
  if (picked) {
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
    const float nameWidth = ImGui::CalcTextSize(name.c_str()).x;
    ImGui::ImDrawListManager::AddText(a_draw,
                                      {x - nameWidth / 2.0f, a_area.origin.y},
                                      kLabelInk, name.c_str());
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

void DrawSeparation(const Studio::ClusterChart &a_chart, ChartFocus a_focus) {
  if (!a_focus.picked || !a_focus.compared ||
      *a_focus.compared == *a_focus.picked) {
    return;
  }
  const std::vector<Studio::AxisSeparation> axes =
      Studio::SeparationOf(a_chart, *a_focus.picked, *a_focus.compared);
  Dim(std::format("What separates cluster {} from cluster {}", *a_focus.picked,
                  *a_focus.compared));
  if (!Studio::CanSeparate(axes)) {
    Warn("These two overlap on every axis. A part, bone or chart term may "
         "separate them where the material cannot.");
  }
  auto table = Table::Begin("cluster-separation",
                            {{"axis", Studio::Width::Fit("rough")},
                             {"separation", Studio::Width::Fill()}},
                            Studio::kColumnsTable);
  if (!table.Open()) {
    return;
  }
  for (const Studio::AxisSeparation &axis : axes) {
    table.Cell();
    ImGui::TextUnformatted(std::string{
        Studio::kClusterAxes[static_cast<std::size_t>(axis.axis)].name}
                               .c_str());
    table.Cell();
    ImGui::TextUnformatted(std::format("{:.1f}", axis.separation).c_str());
  }
  table.End();
}

void DrawClusterTable(const MaterialAnalysis &a_analysis,
                      std::optional<std::uint8_t> a_picked) {
  auto table = Table::Begin("cluster-readout",
                            {{"id", Studio::Width::Fit("0000")},
                             {"colour", Studio::Width::Fit("0000")},
                             {"looks", Studio::Width::Fill()},
                             {"rough", Studio::Width::Fit("0.00")},
                             {"metal", Studio::Width::Fit("0.00")},
                             {"occl", Studio::Width::Fit("0.00")},
                             {"refl", Studio::Width::Fit("0.00")},
                             {"cover", Studio::Width::Fit("100%")}},
                            Studio::kColumnsTable);
  if (!table.Open()) {
    return;
  }
  for (const MaterialCluster *cluster : Studio::LargestFirst(a_analysis)) {
    DrawClusterRow(*cluster, cluster->id == a_picked, table);
  }
  table.End();
}

void DrawClusterRow(const MaterialCluster &a_cluster, bool a_picked,
                    Table &a_table) {
  const MaterialTexel &centre = a_cluster.centroid;
  ImGui::PushID(a_cluster.id);
  a_table.Cell();
  ImGui::ColorButton("##ink", InkColour(ClusterInk(a_cluster.id, 255)), 0,
                     ImGui::ImVec2{10.0f, 10.0f});
  ImGui::SameLine();
  const std::string id = std::to_string(a_cluster.id);
  if (a_picked) {
    Ok(id);
  } else {
    ImGui::TextUnformatted(id.c_str());
  }
  a_table.Cell();
  ImGui::ColorButton(
      "##centre",
      ImGui::ImVec4{centre.diffuse.x, centre.diffuse.y, centre.diffuse.z, 1.0f},
      0, ImGui::ImVec2{16.0f, 16.0f});
  a_table.Cell();
  ImGui::TextUnformatted(a_cluster.description.c_str());
  for (const float channel : {centre.roughness, centre.metallic,
                              centre.occlusion, centre.reflectance}) {
    a_table.Cell();
    ImGui::TextUnformatted(std::format("{:.2f}", channel).c_str());
  }
  a_table.Cell();
  ImGui::TextUnformatted(
      std::format("{:.0f}%", a_cluster.share * 100.0f).c_str());
  ImGui::PopID();
}

ImGui::ImVec4 InkColour(std::uint32_t a_ink) {
  const auto channel = [&](int a_shift) {
    return static_cast<float>((a_ink >> a_shift) & 0xFFU) / 255.0f;
  };
  return ImGui::ImVec4{channel(0), channel(8), channel(16), channel(24)};
}

std::string DumpWrittenText(const Studio::MaterialDumpWritten &a_written) {
  if (a_written.skipped == 0) {
    return std::format("Dumped to {}", a_written.folder);
  }
  return std::format("Dumped to {} with {} items skipped; meta.json says why",
                     a_written.folder, a_written.skipped);
}
}
}
