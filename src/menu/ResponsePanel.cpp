#include "menu/ResponsePanel.h"

#include "studio/ResponseGraph.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
namespace {
[[nodiscard]] std::pair<float, float>
PlotBounds(const Studio::ResponseGraph &a_graph) {
  const auto bounds =
      std::minmax_element(a_graph.values.begin(), a_graph.values.end());
  float low = *bounds.first;
  float high = *bounds.second;
  if (a_graph.live) {
    low = (std::min)(low, *a_graph.live);
    high = (std::max)(high, *a_graph.live);
  }
  if (low == high) {
    const float padding = (std::max)(0.01f, std::abs(low) * 0.05f);
    low -= padding;
    high += padding;
  }
  return {low, high};
}

void LiveMarker(float a_value, float a_low, float a_high, float a_scale) {
  ImGui::ImDrawList *draw = ImGui::GetWindowDrawList();
  if (!draw) {
    return;
  }
  const ImGui::ImVec2 low = ImGui::GetItemRectMin();
  const ImGui::ImVec2 high = ImGui::GetItemRectMax();
  const float padding = 4.0f * a_scale;
  const float fraction = (a_value - a_low) / (a_high - a_low);
  const float y =
      high.y - padding - fraction * (high.y - low.y - 2.0f * padding);
  ImGui::ImDrawListManager::AddLine(draw, {low.x + padding, y},
                                    {high.x - padding, y}, 0xFF60D0FFU, 1.0f);
}
}

void DrawResponse(const Studio::SignalRow &a_signal, const Frame &a_frame) {
  if (!a_frame.recipe) {
    return;
  }
  const auto response = Studio::BuildResponseGraph(a_signal, *a_frame.recipe);
  if (!response) {
    ImGui::TextWrapped("%s", response.error().c_str());
    return;
  }
  ImGui::PushID(a_signal.name.c_str());
  const auto [low, high] = PlotBounds(*response);
  const std::string overlay = std::format("Output {:.3g} to {:.3g}", low, high);
  ImGui::PlotLines("##response", response->values.data(),
                   static_cast<int>(response->values.size()), 0,
                   overlay.c_str(), low, high, {0.0f, 125.0f * a_frame.scale});
  if (response->live) {
    LiveMarker(*response->live, low, high, a_frame.scale);
    ImGui::Text("Live output: %.4g (horizontal marker)",
                static_cast<double>(*response->live));
  }
  ImGui::Text("Elapsed seconds: 0 to %.4g",
              static_cast<double>(response->seconds));
  ImGui::TextWrapped("%s", response->caption.c_str());
  ImGui::PopID();
}
}
