#include "menu/Menu.h"

#include "PCH.h"
#include "Settings.h"
#include "SettingsFile.h"
#include "engine/Manager.h"
#include "menu/MenuWidgets.h"
#include "studio/Intent.h"
#include "studio/Selection.h"
#include "studio/Snapshot.h"
#include "studio/Widgets.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImVec2;

namespace BetterEnchantmentEffects::Menu {
namespace {
bool g_needsReapply = false;
bool g_autoReapply = true;
Settings g_savedSettings{};
bool g_savedKnown = false;

constexpr Studio::TableStyle kGridStyle{.borders = Studio::TableBorders::kAll,
                                        .stretch = true,
                                        .headers = true,
                                        .rowBackground = true};

constexpr const char *kSetupSections[]{"General"};
constexpr const char *kVerboseKey = "VerboseLogging";

void MarkReapply(bool a_changed) {
  if (!a_changed) {
    return;
  }
  g_needsReapply = true;
  if (g_autoReapply) {
    if (Manager *manager = Manager::GetSingleton()) {
      manager->ReapplyAll();
    }
    g_needsReapply = false;
  }
}

[[nodiscard]] bool Shown(const SettingDesc &a_desc) noexcept {
  return std::ranges::any_of(kSetupSections, [&](const char *a_section) {
    return std::string_view{a_desc.section} == a_section;
  });
}

[[nodiscard]] const SettingDesc *FindSetting(std::string_view a_key) noexcept {
  const auto table = SettingTable();
  const auto it = std::ranges::find_if(table, [&](const SettingDesc &a_desc) {
    return std::string_view{a_desc.key} == a_key;
  });
  return it == table.end() ? nullptr : &*it;
}

[[nodiscard]] bool TextureScaleWidget(TextureScale &a_value,
                                      const SettingDesc &a_desc,
                                      const char *a_label) {
  bool changed = false;
  const int current = static_cast<int>(a_value);
  const std::string preview =
      current >= 0 && static_cast<std::size_t>(current) < a_desc.items.size()
          ? std::string{a_desc.items[static_cast<std::size_t>(current)]}
          : std::string{};
  if (ImGui::BeginCombo(a_label, preview.c_str())) {
    for (std::size_t i = 0; i < a_desc.items.size(); ++i) {
      const std::string item{a_desc.items[i]};
      if (ImGui::Selectable(item.c_str(),
                            static_cast<std::size_t>(current) == i)) {
        a_value = static_cast<TextureScale>(i);
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

void Widget(Settings &a_settings, const SettingDesc &a_desc,
            const char *a_label) {
  bool changed = false;
  Match(a_desc.member, [&](auto a_member) {
    using T = std::remove_cvref_t<decltype(a_settings.*a_member)>;
    if constexpr (std::is_same_v<T, bool>) {
      changed = ImGui::Checkbox(a_label, &(a_settings.*a_member));
    } else if constexpr (std::is_same_v<T, float>) {
      changed = ImGui::SliderFloat(
          a_label, &(a_settings.*a_member), a_desc.min, a_desc.max,
          a_desc.max - a_desc.min > 10 ? "%.2f" : "%.3f", 0);
    } else if constexpr (std::is_same_v<T, TextureScale>) {
      changed = TextureScaleWidget(a_settings.*a_member, a_desc, a_label);
    } else {
      int v = static_cast<int>(a_settings.*a_member);
      if (ImGui::SliderInt(a_label, &v, static_cast<int>(a_desc.min),
                           static_cast<int>(a_desc.max))) {
        a_settings.*a_member = static_cast<std::uint32_t>(v);
        changed = true;
      }
    }
  });
  if (changed) {
    a_settings = NormalizeSettings(a_settings);
    SetSettings(a_settings);
    MarkReapply(a_desc.reapply);
  }
}

void DrawSaveBar(Settings &a_settings) {
  Manager *manager = Manager::GetSingleton();
  if (!manager) {
    return;
  }
  if (!g_savedKnown) {
    g_savedSettings = a_settings;
    g_savedKnown = true;
  }
  constexpr Studio::TableStyle style{.borders = Studio::TableBorders::kAll,
                                     .stretch = false,
                                     .headers = false,
                                     .rowBackground = false};
  auto table = Table::Begin("save-bar",
                            {{"", Studio::Width::Fit()},
                             {"", Studio::Width::Fit()},
                             {"", Studio::Width::Fit()},
                             {"", Studio::Width::Fit()},
                             {"", Studio::Width::Fill()}},
                            style);
  if (!table.Open()) {
    return;
  }
  table.Cell();
  if (ImGui::Button("Save INI")) {
    if (SaveSettingsToDisk(a_settings)) {
      g_savedSettings = a_settings;
    }
  }
  table.Cell();
  if (ImGui::Button("Reload INI")) {
    SetSettings(LoadSettingsFromDisk());
    a_settings = GetSettings();
    g_savedSettings = a_settings;
    g_needsReapply = false;
    manager->ReapplyAll();
  }
  table.Cell();
  if (ImGui::Button("Re-apply")) {
    g_needsReapply = false;
    manager->ReapplyAll();
  }
  table.Cell();
  Toggle("auto", g_autoReapply, "");
  HelpMarker("Re-apply automatically when a setting that is read at apply time "
             "changes.");
  table.Cell();
  if (SettingsDiffer(a_settings, g_savedSettings)) {
    Warn("unsaved changes");
    ImGui::SameLine();
  }
  if (g_needsReapply) {
    Warn("re-apply needed");
  }
  table.End();
}

void DrawValueTable(Settings &a_settings) {
  auto table = Table::Begin(
      "values",
      {{"setting", Studio::Width::Fit()}, {"value", Studio::Width::Fill()}},
      kGridStyle);
  if (!table.Open()) {
    return;
  }
  for (const SettingDesc &desc : SettingTable()) {
    if (!Shown(desc) || std::string_view{desc.key} == kVerboseKey) {
      continue;
    }
    ImGui::PushID(desc.key);
    table.Cell();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(desc.label);
    HelpMarker(desc.help);
    table.Cell();
    NextItemWidth(Studio::Width::Fill());
    Widget(a_settings, desc, "##value");
    ImGui::PopID();
  }
  table.End();
}

void DrawLog(Settings &a_settings) {
  static char filter[64]{};
  static bool autoScroll = true;
  NextItemWidth(Studio::Width::Px(240.0f));
  ImGui::InputText("filter", filter, sizeof(filter));
  ImGui::SameLine();
  Toggle("auto-scroll", autoScroll, "");
  if (const SettingDesc *verbose = FindSetting(kVerboseKey)) {
    ImGui::SameLine();
    Widget(a_settings, *verbose, verbose->label);
    HelpMarker(verbose->help);
  }
  if (!g_logRing) {
    ImGui::Text("no log buffer");
    return;
  }
  ImGui::BeginChild("log", ImVec2{0, 0}, 1, 0);
  for (const std::string &line : g_logRing->last_formatted(300)) {
    if (filter[0] && line.find(filter) == std::string::npos) {
      continue;
    }
    const bool warn = line.find("[warning]") != std::string::npos ||
                      line.find("[error]") != std::string::npos;
    if (warn) {
      Warn(line);
    } else {
      ImGui::TextUnformatted(line.c_str());
    }
  }
  if (autoScroll) {
    ImGui::SetScrollHereY(1.0f);
  }
  ImGui::EndChild();
}
}

void __stdcall RenderSetup() {
  Manager *manager = Manager::GetSingleton();
  if (!manager) {
    return;
  }
  Settings settings = GetSettings();
  manager->Watch(Studio::RequestOf(Studio::State().selection));
  const std::shared_ptr<const Studio::Snapshot> held =
      manager->LatestSnapshot();
  if (!held) {
    return;
  }
  RenderHeader(*held);
  const float half = ImGui::GetContentRegionAvail().y * 0.5f;
  if (ImGui::BeginChild("settings", ImVec2{0.0f, half}, 0, 0)) {
    DrawSaveBar(settings);
    DrawValueTable(settings);
  }
  ImGui::EndChild();
  Rule();
  DrawLog(settings);
}
}
