// GPL-3.0-only with the additional permission in COPYING.md.
#include "menu/PreviewWindows.h"

#include "menu/MenuWidgets.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImVec2;

namespace BetterEnchantmentEffects::Menu {
namespace {
constexpr float kFirstWindowInset = 60.0f;
constexpr float kWindowCascade = 28.0f;
constexpr int kPreviewWindowFlags =
    ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize |
    ImGuiMCP::ImGuiWindowFlags_NoSavedSettings;

[[nodiscard]] std::string PinScope(const Studio::PreviewSubject &a_subject) {
  return Match(
      a_subject,
      [](const Studio::SourceSubject &a_source) {
        return std::format("pin-source:{}", a_source.name);
      },
      [](const Studio::MaskSubject &a_mask) {
        return std::format("pin-mask:{}", a_mask.name);
      },
      [](const Studio::OutputSubject &a_output) {
        return std::format("pin-output:{}", a_output.output);
      },
      [](const Studio::LayerSubject &a_layer) {
        return std::format("pin-layer:{}:{}", a_layer.output, a_layer.layer);
      });
}

void PlaceWindow(const Studio::PreviewWindow &a_window, std::size_t a_slot,
                 float a_scale) {
  if (a_window.position) {
    ImGui::SetNextWindowPos(ImVec2{a_window.position->x, a_window.position->y},
                            ImGuiMCP::ImGuiCond_Appearing);
    return;
  }
  const float inset =
      (kFirstWindowInset + kWindowCascade * static_cast<float>(a_slot)) *
      a_scale;
  ImGui::SetNextWindowPos(ImVec2{inset, inset}, ImGuiMCP::ImGuiCond_Appearing);
}

[[nodiscard]] std::optional<std::string>
PickName(const char *a_key, const std::string &a_current,
         const std::vector<std::string> &a_names, float a_scale) {
  return ChoiceCombo(a_key, a_current, a_names,
                     {Studio::Width::Px(WidestOf(a_names)), a_scale});
}

void PickTier(Studio::PreviewWindow &a_window, float a_scale) {
  std::vector<std::string> names;
  for (const Studio::PreviewTier tier : Studio::kPreviewTiers) {
    names.emplace_back(Studio::TierName(tier));
  }
  const std::optional<std::string> chosen = PickName(
      "tier", std::string{Studio::TierName(a_window.tier)}, names, a_scale);
  for (const Studio::PreviewTier tier : Studio::kPreviewTiers) {
    if (chosen && *chosen == Studio::TierName(tier)) {
      a_window.tier = tier;
    }
  }
}

void PickChannel(Studio::PreviewWindow &a_window, ShaderChannel a_shown,
                 float a_scale) {
  std::vector<std::string> names;
  for (const ShaderChannel channel : Studio::kPreviewChannels) {
    names.emplace_back(Studio::PreviewChannelName(channel));
  }
  const std::optional<std::string> chosen =
      PickName("channel", std::string{Studio::PreviewChannelName(a_shown)},
               names, a_scale);
  for (const ShaderChannel channel : Studio::kPreviewChannels) {
    if (chosen && *chosen == Studio::PreviewChannelName(channel)) {
      a_window.channel = channel;
    }
  }
}

void PickGeometry(Studio::PreviewWindow &a_window,
                  const std::vector<std::string> &a_names, float a_scale) {
  if (const std::optional<std::string> chosen =
          PickName("geometry", a_window.target.geometry, a_names, a_scale)) {
    a_window.target.geometry = *chosen;
  }
}

void DrawPicture(Studio::PreviewWindow &a_window,
                 const Studio::PreviewPicture &a_picture,
                 const Studio::Snapshot &a_snapshot, float a_scale) {
  const ShaderChannel shown = a_window.channel.value_or(a_picture.channel);
  PickTier(a_window, a_scale);
  ImGui::SameLine();
  PickChannel(a_window, shown, a_scale);
  const std::vector<std::string> geometries =
      Studio::PreviewGeometries(a_snapshot, a_window.target);
  if (geometries.size() > 1) {
    ImGui::SameLine();
    PickGeometry(a_window, geometries, a_scale);
  }
  const std::uint32_t pixels = Studio::PixelsOf(a_window.tier);
  Thumbnail({.texture = a_picture.texture,
             .channel = shown,
             .dynamic = a_picture.animated,
             .size = static_cast<float>(pixels) * a_scale,
             .pixels = pixels});
  if (!a_picture.problem.empty()) {
    Problem(a_picture.problem);
  }
}

void DrawContent(Studio::PreviewWindow &a_window,
                 const Studio::PreviewContent &a_content,
                 const Studio::Snapshot &a_snapshot, float a_scale) {
  Match(
      a_content,
      [&](const Studio::PreviewPicture &a_picture) {
        DrawPicture(a_window, a_picture, a_snapshot, a_scale);
      },
      [](const Studio::PieceNotViewed &) {
        Dim("Select this piece in the studio to show its live texture.");
      });
}

[[nodiscard]] std::optional<Studio::PreviewWindow>
DrawPreviewWindow(Studio::PreviewWindow a_window, std::size_t a_slot,
                  const Studio::Snapshot &a_snapshot, float a_scale) {
  const std::optional<Studio::PreviewContent> content =
      Studio::ResolvePreview(a_snapshot, a_window.target);
  if (!content) {
    return a_window;
  }
  PlaceWindow(a_window, a_slot, a_scale);
  const std::string title = std::format(
      "{}###preview-window-{}",
      Studio::PreviewLabel(a_snapshot, a_window.target), a_window.id);
  bool open = true;
  if (ImGui::Begin(title.c_str(), &open, kPreviewWindowFlags)) {
    DrawContent(a_window, *content, a_snapshot, a_scale);
  }
  const ImVec2 at = ImGui::GetWindowPos();
  ImGui::End();
  a_window.position = Studio::ScreenPoint{at.x, at.y};
  if (!open) {
    return std::nullopt;
  }
  return a_window;
}
}

void PinPreviewButton(const Studio::PreviewSubject &a_subject,
                      const std::optional<Studio::PreviewTarget> &a_target,
                      Studio::MenuState &a_state) {
  const bool pinned = a_target && Studio::IsPinned(a_state.previews, *a_target);
  const bool blocked =
      !a_target || (!pinned && Studio::IsFull(a_state.previews));
  ImGui::PushID(PinScope(a_subject).c_str());
  Disabled(blocked, [&] {
    bool shown = pinned;
    if (SquareToggle("P", shown, "") && a_target) {
      static_cast<void>(Studio::TogglePin(a_state.previews, *a_target));
    }
  });
  ImGui::PopID();
}

void PinSubjectButton(const Frame &a_frame, Studio::PreviewSubject a_subject) {
  if (!a_frame.state) {
    return;
  }
  const std::optional<Studio::PreviewTarget> target = Studio::PreviewTargetFor(
      SelectionOf(a_frame), a_frame.recipe, a_frame.geometry, a_subject);
  PinPreviewButton(a_subject, target, *a_frame.state);
}

void DrawPreviewLimit(const Studio::MenuState &a_state) {
  if (Studio::IsFull(a_state.previews)) {
    Warn(std::format("{} previews pinned: close one to pin another",
                     Studio::kMaxPreviewWindows));
  }
}

void DrawPreviewWindows(const Studio::Snapshot &a_snapshot,
                        Studio::MenuState &a_state, float a_scale) {
  const std::vector<Studio::PreviewWindow> drawn = a_state.previews.open;
  for (std::size_t slot = 0; slot < drawn.size(); ++slot) {
    const std::uint64_t id = drawn[slot].id;
    std::optional<Studio::PreviewWindow> kept =
        DrawPreviewWindow(drawn[slot], slot, a_snapshot, a_scale);
    Studio::PreviewWindow *live = Studio::FindPreview(a_state.previews, id);
    if (!live) {
      continue;
    }
    if (!kept) {
      Studio::ClosePreview(a_state.previews, id);
      continue;
    }
    *live = std::move(*kept);
  }
}
}
