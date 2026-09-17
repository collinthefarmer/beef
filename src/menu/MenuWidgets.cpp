#include "menu/MenuWidgets.h"

#include "render/TextureLab.h"
#include "studio/Forms.h"
#include "studio/InputConnections.h"
#include "studio/MenuState.h"
#include "studio/Names.h"
#include "studio/View.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImGuiID;
using ImGuiMCP::ImTextureID;
using ImGuiMCP::ImVec2;
using ImGuiMCP::ImVec4;

static_assert(
    std::is_same_v<BetterEnchantmentEffects::Studio::FieldKey, ImGuiID>);

namespace BetterEnchantmentEffects::Menu {
namespace {
constexpr ImVec4 kWarn{1.0f, 0.8f, 0.3f, 1.0f};
constexpr ImVec4 kProblemFrame{0.45f, 0.12f, 0.12f, 1.0f};
constexpr ImVec4 kOk{0.5f, 0.9f, 0.5f, 1.0f};
constexpr ImVec4 kBad{1.0f, 0.4f, 0.4f, 1.0f};
constexpr ImVec4 kDim{0.6f, 0.6f, 0.6f, 1.0f};
constexpr ImVec4 kBadgeFrame{0.20f, 0.20f, 0.24f, 1.0f};

constexpr float kFillWidth = -(std::numeric_limits<float>::min)();

[[nodiscard]] const char *NonNull(const char *a_key) noexcept {
  return a_key ? a_key : "";
}

[[nodiscard]] std::string_view DisplayLabel(const char *a_title) noexcept {
  const std::string_view title{NonNull(a_title)};
  const std::size_t id = title.find("##");
  return id == std::string_view::npos ? title : title.substr(0, id);
}

[[nodiscard]] Studio::FieldKey KeyOf(const char *a_key) {
  return Studio::HashFieldKey(Studio::State().fieldScope, NonNull(a_key));
}

void TrackActive(Studio::FieldKey a_key) {
  auto &state = Studio::State();
  if (ImGui::IsItemActive()) {
    state.activeField = a_key;
  } else if (state.activeField == a_key) {
    state.activeField = Studio::kNoField;
  }
}

void Colored(const ImVec4 &a_color, std::string_view a_text) {
  ImGui::TextColored(a_color, "%.*s", static_cast<int>(a_text.size()),
                     a_text.data());
}

struct PreviewImage {
  ImTextureID texture = nullptr;
  TextureLab::PreviewDraw *draw = nullptr;
  explicit operator bool() const { return texture && draw; }
};

[[nodiscard]] RE::NiSourceTexture *TextureOf(Studio::TextureHandle a_handle) {
  return reinterpret_cast<RE::NiSourceTexture *>(
      static_cast<std::uintptr_t>(a_handle));
}

[[nodiscard]] PreviewImage PreviewOf(Studio::TextureHandle a_texture,
                                     ShaderChannel a_channel, bool a_dynamic) {
  RE::NiSourceTexture *texture = TextureOf(a_texture);
  if (!texture)
    return {};
  auto *lab = TextureLab::GetSingleton();
  const auto preview = lab->Preview(texture, a_channel, a_dynamic);
  if (!preview || !preview->View())
    return {};
  auto *draw = lab->RetainPreviewDraw(preview);
  return {reinterpret_cast<ImTextureID>(preview->View()), draw};
}

void FinishPreviewDraw(const PreviewImage &a_image) {
  ImGui::ImDrawListManager::AddCallback(
      ImGui::GetWindowDrawList(),
      [](const ImGuiMCP::ImDrawList *, const ImGuiMCP::ImDrawCmd *a_command) {
        static_cast<TextureLab::PreviewDraw *>(a_command->UserCallbackData)
            ->Consumed();
      },
      a_image.draw);
}

[[nodiscard]] std::optional<std::string>
ReferenceEntries(const std::string &a_current,
                 std::span<const std::string> a_names, bool a_allowEmpty,
                 std::span<const std::string> a_creators) {
  std::optional<std::string> chosen;
  if (a_allowEmpty && ImGui::Selectable("(none)", a_current.empty())) {
    chosen = std::string{};
  }
  for (const auto &name : a_names) {
    const auto text = Studio::ReferenceText(name);
    if (ImGui::Selectable(text.c_str(), text == a_current)) {
      chosen = text;
    }
  }
  if (!a_creators.empty()) {
    ImGui::Separator();
    for (const auto &creator : a_creators) {
      if (ImGui::Selectable(creator.c_str(), false)) {
        chosen = creator;
      }
    }
  }
  return chosen;
}

[[nodiscard]] std::optional<std::string>
SignalCombo(const Studio::FormField &a_field) {
  const std::string preview = a_field.text.starts_with('@')
                                  ? a_field.text
                                  : std::string{"choose a signal"};
  NextItemWidth(Studio::Width::Fill());
  if (!ImGui::BeginCombo("##combo", preview.c_str())) {
    return std::nullopt;
  }
  std::optional<std::string> chosen = ReferenceEntries(
      a_field.text, a_field.names, a_field.allowEmpty, a_field.creators);
  if (Studio::CanConnectInput(a_field)) {
    ImGui::Separator();
    if (ImGui::Selectable("New input", false)) {
      chosen = std::string{kNewInputChoice};
    }
  }
  ImGui::EndCombo();
  return chosen;
}

void ProblemLabel(std::string_view a_text) {
  const ImVec2 itemMin = ImGui::GetItemRectMin();
  const ImVec2 itemMax = ImGui::GetItemRectMax();
  const ImVec2 windowMin = ImGui::GetWindowPos();
  const ImVec2 windowSize = ImGui::GetWindowSize();
  const ImVec2 windowMax{windowMin.x + windowSize.x,
                         windowMin.y + windowSize.y};
  const float pad = ImGui::GetStyle()->FramePadding.x;
  const float gap = 2.0f;
  const float wrap = (std::max)(windowMax.x - itemMin.x - 2.0f * pad,
                                4.0f * ImGui::GetFontSize());
  const ImVec2 text = ImGui::CalcTextSize(
      a_text.data(), a_text.data() + a_text.size(), false, wrap);
  const float height = text.y + 2.0f * pad;
  const bool below = itemMax.y + gap + height <= windowMax.y;
  const ImVec2 boxMin{itemMin.x,
                      below ? itemMax.y + gap : itemMin.y - gap - height};
  const ImVec2 boxMax{itemMin.x + text.x + 2.0f * pad, boxMin.y + height};
  auto *const list = ImGui::GetForegroundDrawList();
  ImGui::ImDrawListManager::PushClipRect(list, windowMin, windowMax, false);
  ImGui::ImDrawListManager::AddRectFilled(
      list, boxMin, boxMax,
      ImGui::GetColorU32(ImGuiMCP::ImGuiCol_PopupBg, 0.95f), 0.0f, 0);
  ImGui::ImDrawListManager::AddText(
      list, ImGui::GetFont(), ImGui::GetFontSize(),
      ImVec2{boxMin.x + pad, boxMin.y + pad}, ImGui::GetColorU32(kBad),
      a_text.data(), a_text.data() + a_text.size(), wrap);
  ImGui::ImDrawListManager::PopClipRect(list);
}

[[nodiscard]] std::optional<std::string>
ColorSwatchPicker(Studio::FieldKey a_key, const std::string &a_current) {
  auto &channels = Studio::State().numberBuffers[a_key];
  if (!ImGui::IsPopupOpen("picker")) {
    const Vec3 colour =
        Studio::LiteralColor(a_current).value_or(Vec3{0.5f, 0.5f, 0.5f});
    channels = {colour.x, colour.y, colour.z};
  }
  const float side = ImGui::GetFrameHeight();
  if (ImGui::ColorButton(
          "##swatch", ImVec4{channels[0], channels[1], channels[2], 1.0f},
          ImGuiMCP::ImGuiColorEditFlags_NoTooltip, ImVec2{side, side})) {
    ImGui::OpenPopup("picker");
  }
  Tooltip("pick a colour; the field takes it as r, g, b");
  std::optional<std::string> picked;
  if (ImGui::BeginPopup("picker")) {
    ImGui::ColorPicker3("##picker", channels.data(),
                        ImGuiMCP::ImGuiColorEditFlags_NoSidePreview);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      picked =
          Studio::LiteralColorText(Vec3{channels[0], channels[1], channels[2]});
    }
    ImGui::EndPopup();
  }
  return picked;
}

struct BadgeStyle {
  const char *glyph;
  bool takesSignal;
  ImVec4 colour;
  const char *help;
  Studio::Swatch swatch;
};

[[nodiscard]] BadgeStyle StyleOf(Studio::FieldKind a_kind) noexcept {
  const auto *row = RowOf(Studio::kFieldKinds, a_kind);
  if (!row) {
    return {"?", false, kDim, "", Studio::Swatch::kNone};
  }
  const ImVec4 colour{row->colour[0], row->colour[1], row->colour[2], 1.0f};
  return {row->glyph, row->takesSignal, colour, row->help, row->swatch};
}

void BadgeFrame(const char *a_label, const ImVec4 &a_colour, bool a_filled,
                const ImVec2 &a_size) {
  const ImVec4 back = a_filled ? a_colour : kBadgeFrame;
  const ImVec4 text = a_filled ? kBadgeFrame : a_colour;
  ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button, back);
  ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, back);
  ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive, back);
  ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, text);
  ImGui::Button(a_label, a_size);
  ImGui::PopStyleColor(4);
}

void DrawValueBadge(const BadgeStyle &a_style, bool a_takesSignal,
                    bool &a_combo, Studio::FieldKey a_key) {
  const float side = ImGui::GetFrameHeight();
  if (a_takesSignal) {
    ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button, a_style.colour);
    ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered,
                          ImVec4{a_style.colour.x * 0.85f,
                                 a_style.colour.y * 0.85f,
                                 a_style.colour.z * 0.85f, 1.0f});
    ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive,
                          ImVec4{a_style.colour.x * 0.7f,
                                 a_style.colour.y * 0.7f,
                                 a_style.colour.z * 0.7f, 1.0f});
    ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, kBadgeFrame);
    if (ImGui::Button(a_style.glyph, ImVec2{side, side})) {
      a_combo = !a_combo;
      Studio::State().focusField = a_key;
    }
    ImGui::PopStyleColor(4);
    Tooltip(std::string{a_style.help} +
            (a_combo ? "\nclick: type a value instead"
                     : "\nclick: choose a signal instead"));
  } else {
    BadgeFrame(a_style.glyph, a_style.colour, false, ImVec2{side, side});
    Tooltip(a_style.help);
  }
}

bool SquareToggle(const char *a_label, bool &a_value,
                  std::string_view a_tooltip) {
  const float side = RowButtonWidth();
  if (a_value) {
    const auto *pressed =
        ImGui::GetStyleColorVec4(ImGuiMCP::ImGuiCol_ButtonActive);
    ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button,
                          pressed ? *pressed : ImVec4{0.3f, 0.5f, 0.8f, 1.0f});
  }
  const bool clicked = ImGui::Button(NonNull(a_label), ImVec2{side, side});
  if (a_value) {
    ImGui::PopStyleColor();
  }
  Tooltip(a_tooltip);
  if (clicked) {
    a_value = !a_value;
  }
  return clicked;
}

[[nodiscard]] float RuleButtonWidth(const Studio::RuleButton &a_button) {
  switch (a_button.width.mode) {
  case Studio::WidthMode::kFill:
    return ButtonWidth(a_button.Label());
  case Studio::WidthMode::kFit:
    return a_button.width.text.empty() ? ButtonWidth(a_button.Label())
                                       : FitWidth(a_button.width.text);
  case Studio::WidthMode::kPx:
    return a_button.width.pixels;
  }
  return ButtonWidth(a_button.Label());
}

[[nodiscard]] Studio::RuleClick
DrawRuleControls(std::span<const Studio::RuleButton> a_buttons,
                 float a_trailWidth, const std::function<void()> &a_trail) {
  const std::size_t items = a_buttons.size() + (a_trail ? 1u : 0u);
  if (items == 0) {
    return {};
  }

  float total = a_trail ? a_trailWidth : 0.0f;
  for (const auto &button : a_buttons) {
    total += RuleButtonWidth(button);
  }
  total += static_cast<float>(items - 1) * ItemSpacingX();

  ImGui::SameLine();
  const float here = ImGui::GetCursorPosX();
  const float edge = here + ImGui::GetContentRegionAvail().x - total;
  ImGui::SetCursorPosX((std::max)(here, edge));

  Studio::RuleClick result;
  bool first = true;
  for (std::size_t i = 0; i < a_buttons.size(); ++i) {
    const auto &button = a_buttons[i];
    if (!first) {
      ImGui::SameLine();
    }
    first = false;
    const std::string label{button.Label()};
    const float width = RuleButtonWidth(button);
    ImGui::PushID(static_cast<int>(i));
    Disabled(!button.enabled, [&]() {
      if (ImGui::Button(label.c_str(), ImVec2{width, 0.0f})) {
        result = Studio::RuleClick{true, i};
      }
    });
    ImGui::PopID();
  }
  if (a_trail) {
    if (!first) {
      ImGui::SameLine();
    }
    a_trail();
  }
  return result;
}
}

Table Table::Begin(const char *a_id,
                   std::initializer_list<Studio::Column> a_columns,
                   const Studio::TableStyle &a_style) {
  return Begin(
      a_id,
      std::span<const Studio::Column>{a_columns.begin(), a_columns.size()},
      a_style);
}

Table Table::Begin(const char *a_id, std::span<const Studio::Column> a_columns,
                   const Studio::TableStyle &a_style) {
  Table table;
  if (a_columns.empty()) {
    return table;
  }
  int flags = 0;
  switch (a_style.borders) {
  case Studio::TableBorders::kAll:
    flags = ImGuiMCP::ImGuiTableFlags_Borders;
    break;
  case Studio::TableBorders::kInnerHorizontal:
    flags = ImGuiMCP::ImGuiTableFlags_BordersInnerH;
    break;
  case Studio::TableBorders::kNone:
    break;
  }
  flags |= a_style.stretch ? ImGuiMCP::ImGuiTableFlags_SizingStretchProp
                           : ImGuiMCP::ImGuiTableFlags_SizingFixedFit;
  if (a_style.rowBackground) {
    flags |= ImGuiMCP::ImGuiTableFlags_RowBg;
  }
  if (!ImGui::BeginTable(NonNull(a_id), static_cast<int>(a_columns.size()),
                         flags)) {
    return table;
  }
  for (const auto &column : a_columns) {
    const std::string label{column.label};
    const auto &width = column.width;
    switch (width.mode) {
    case Studio::WidthMode::kFill:
      ImGui::TableSetupColumn(label.c_str(),
                              ImGuiMCP::ImGuiTableColumnFlags_WidthStretch,
                              width.ratio);
      break;
    case Studio::WidthMode::kFit:
      ImGui::TableSetupColumn(label.c_str(),
                              ImGuiMCP::ImGuiTableColumnFlags_WidthFixed,
                              width.text.empty() ? 0.0f : FitWidth(width.text));
      break;
    case Studio::WidthMode::kPx:
      ImGui::TableSetupColumn(label.c_str(),
                              ImGuiMCP::ImGuiTableColumnFlags_WidthFixed,
                              width.pixels);
      break;
    }
  }
  if (a_style.headers) {
    ImGui::TableHeadersRow();
  }
  table.open_ = true;
  table.columns_ = a_columns.size();
  return table;
}

void Table::Cell() {
  if (!open_ || columns_ == 0) {
    return;
  }
  if (cells_ % columns_ == 0) {
    ImGui::TableNextRow();
  }
  ImGui::TableNextColumn();
  ++cells_;
}

void Table::End() {
  if (open_) {
    ImGui::EndTable();
  }
  open_ = false;
}

float ResolveWidth(const Studio::Width &a_width, float a_scale) noexcept {
  switch (a_width.mode) {
  case Studio::WidthMode::kFill:
    if (a_width.ratio < 1.0f) {
      return ImGui::GetContentRegionAvail().x * a_width.ratio;
    }
    return kFillWidth;
  case Studio::WidthMode::kFit:
    return FitWidth(a_width.text) * a_scale;
  case Studio::WidthMode::kPx:
    return a_width.pixels * a_scale;
  }
  return kFillWidth;
}

void NextItemWidth(const Studio::Width &a_width, float a_scale) {
  ImGui::SetNextItemWidth(ResolveWidth(a_width, a_scale));
}

float FitWidth(std::string_view a_text) {
  const auto text =
      ImGui::CalcTextSize(a_text.data(), a_text.data() + a_text.size());
  return text.x + ImGui::GetFrameHeight() * 2.0f + 8.0f;
}

float WidestOf(std::span<const std::string> a_names) {
  float width = 0.0f;
  for (const auto &name : a_names) {
    width = (std::max)(width, FitWidth(name));
  }
  return width;
}

float ButtonWidth(std::string_view a_text) {
  const auto *style = ImGui::GetStyle();
  const auto text =
      ImGui::CalcTextSize(a_text.data(), a_text.data() + a_text.size());
  return text.x + (style ? style->FramePadding.x : 4.0f) * 2.0f;
}

float TextWidth(std::string_view a_text) {
  return ImGui::CalcTextSize(a_text.data(), a_text.data() + a_text.size()).x;
}

float ItemSpacingX() {
  const auto *style = ImGui::GetStyle();
  return style ? style->ItemSpacing.x : 8.0f;
}

float RowButtonWidth() { return ImGui::GetFrameHeight(); }

float RuleHeight() {
  const auto *style = ImGui::GetStyle();
  const float spacing = style ? style->ItemSpacing.y : 4.0f;
  return ImGui::GetFrameHeight() * 2.0f + 1.0f + spacing * 3.0f;
}

std::optional<std::string> TextField(const char *a_key,
                                     const std::string &a_model,
                                     const WidgetSize &a_size,
                                     const TextCheck &a_check) {
  auto &state = Studio::State();
  const Studio::FieldKey key = KeyOf(a_key);
  auto &buffer = state.textBuffers[key];
  if (state.activeField != key) {
    const auto n = (std::min)(a_model.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), a_model.data(), n);
    buffer[n] = '\0';
  }
  const std::optional<std::string> problem =
      (a_check && state.activeField == key)
          ? a_check(std::string{buffer.data()})
          : std::nullopt;
  ImGui::PushID(NonNull(a_key));
  if (problem) {
    ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_FrameBg, kProblemFrame);
  }
  NextItemWidth(a_size.width, a_size.scale);
  const bool committed =
      ImGui::InputText("##text", buffer.data(), buffer.size(),
                       ImGuiMCP::ImGuiInputTextFlags_EnterReturnsTrue);
  if (problem) {
    ImGui::PopStyleColor();
  }
  TrackActive(key);
  if (problem) {
    ProblemLabel(*problem);
  }
  std::optional<std::string> result;
  if (committed) {
    std::string text{buffer.data()};
    if (a_check && a_check(text)) {
      state.activeField = key;
      ImGui::SetKeyboardFocusHere(-1);
    } else {
      state.activeField = Studio::kNoField;
      result = std::move(text);
    }
  }
  ImGui::PopID();
  return result;
}

std::string_view LiveTextField(const char *a_key, const char *a_hint,
                               const Studio::Width &a_width, float a_scale) {
  auto &buffer = Studio::State().textBuffers[KeyOf(a_key)];
  ImGui::PushID(NonNull(a_key));
  NextItemWidth(a_width, a_scale);
  ImGui::InputTextWithHint("##live", a_hint, buffer.data(), buffer.size());
  ImGui::PopID();
  return std::string_view{buffer.data()};
}

std::optional<SearchPick> SearchCombo(const SearchComboSpec &a_spec,
                                      std::span<const std::string> a_labels,
                                      std::optional<std::size_t> a_selected) {
  std::optional<SearchPick> picked;
  NextItemWidth(a_spec.width);
  if (!ImGui::BeginCombo(NonNull(a_spec.id), a_spec.preview)) {
    return picked;
  }
  const std::string filterKey = std::string{NonNull(a_spec.id)} + "-filter";
  const std::string_view filter =
      LiveTextField(filterKey.c_str(), a_spec.hint, a_spec.width, 1.0f);
  bool any = false;
  for (std::size_t i = 0; i < a_labels.size(); ++i) {
    if (!Studio::NameMatches(a_labels[i], filter)) {
      continue;
    }
    any = true;
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::Selectable(a_labels[i].c_str(), a_selected == i)) {
      picked = i;
      ImGui::CloseCurrentPopup();
    }
    ImGui::PopID();
  }
  if (!a_spec.customVerb.empty() && !filter.empty()) {
    if (any) {
      ImGui::Separator();
    }
    const std::string label =
        std::format("{} \"{}\"", a_spec.customVerb, filter);
    if (ImGui::Selectable(label.c_str(), false)) {
      picked = std::string{filter};
      ImGui::CloseCurrentPopup();
    }
    if (!a_spec.customTip.empty()) {
      Tooltip(a_spec.customTip);
    }
  } else if (!any && !a_spec.emptyHint.empty()) {
    Dim(a_spec.emptyHint);
  }
  ImGui::EndCombo();
  return picked;
}

void Thumbnail(const Studio::ThumbnailSpec &a_spec) {
  const ImVec2 size{a_spec.size, a_spec.size};
  if (const auto view =
          PreviewOf(a_spec.texture, a_spec.channel, a_spec.dynamic)) {
    ImGui::Image(view.texture, size);
    FinishPreviewDraw(view);
  } else {
    ImGui::Dummy(size);
  }
}

bool ThumbnailButton(const char *a_key, const Studio::ThumbnailSpec &a_spec) {
  const ImVec2 size{a_spec.size, a_spec.size};
  ImGui::PushID(NonNull(a_key));
  bool clicked = false;
  if (const auto view =
          PreviewOf(a_spec.texture, a_spec.channel, a_spec.dynamic)) {
    clicked = ImGui::ImageButton("image", view.texture, size);
    FinishPreviewDraw(view);
  } else {
    clicked = ImGui::Button("##blank", size);
  }
  ImGui::PopID();
  return clicked;
}

std::optional<std::string> ChoiceCombo(const char *a_key,
                                       const std::string &a_current,
                                       std::span<const std::string> a_names,
                                       const WidgetSize &a_size) {
  std::optional<std::string> chosen;
  ImGui::PushID(NonNull(a_key));
  NextItemWidth(a_size.width, a_size.scale);
  if (ImGui::BeginCombo("##choice", a_current.c_str())) {
    for (const auto &name : a_names) {
      if (ImGui::Selectable(name.c_str(), name == a_current)) {
        chosen = name;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  return chosen;
}

std::optional<std::string> ReferenceCombo(const char *a_key,
                                          const Studio::FormField &a_field,
                                          const WidgetSize &a_size) {
  std::optional<std::string> chosen;
  ImGui::PushID(NonNull(a_key));
  NextItemWidth(a_size.width, a_size.scale);
  if (ImGui::BeginCombo("##reference", a_field.text.empty()
                                           ? "(none)"
                                           : a_field.text.c_str())) {
    chosen = ReferenceEntries(a_field.text, a_field.names, a_field.allowEmpty,
                              a_field.creators);
    ImGui::EndCombo();
  }
  ImGui::PopID();
  return chosen;
}

void Badge(Studio::FieldKind a_kind) {
  const auto style = StyleOf(a_kind);
  const float side = ImGui::GetFrameHeight();
  BadgeFrame(style.glyph, style.colour, style.takesSignal, ImVec2{side, side});
  Tooltip(style.help);
  ImGui::SameLine(0.0f, 0.0f);
}

const char *BlendGlyph(Blend a_blend) {
  switch (a_blend) {
  case Blend::kReplace:
    return "=";
  case Blend::kMultiply:
    return "*";
  case Blend::kAdd:
    return "+";
  case Blend::kSubtract:
    return "-";
  case Blend::kScreen:
    return "s";
  case Blend::kLerp:
    return "~";
  case Blend::kNormal:
    return "o";
  }
  return "?";
}

std::optional<Blend> BlendBadge(Blend a_current, Slot a_slot) {
  static constexpr Blend kBlendOrder[]{
      Blend::kReplace, Blend::kNormal,   Blend::kMultiply, Blend::kScreen,
      Blend::kAdd,     Blend::kSubtract, Blend::kLerp};
  const float side = ImGui::GetFrameHeight();
  std::optional<Blend> chosen;
  ImGui::PushID("blend");
  if (ImGui::Button(BlendGlyph(a_current), ImVec2{side, side})) {
    ImGui::OpenPopup("blend-pick");
  }
  Tooltip("blend: " + std::string{BlendName(a_current)} + "\nclick to change");
  if (ImGui::BeginPopup("blend-pick")) {
    for (const Blend blend : kBlendOrder) {
      if (!BlendAllowed(a_slot, blend)) {
        continue;
      }
      const std::string label =
          std::format("{}  {}", BlendGlyph(blend), BlendName(blend));
      if (ImGui::Selectable(label.c_str(), blend == a_current)) {
        chosen = blend;
      }
    }
    ImGui::EndPopup();
  }
  ImGui::PopID();
  return chosen;
}

std::optional<std::string> ValueWidget(const char *a_key,
                                       const Studio::FormField &a_field,
                                       float a_scale, const TextCheck &a_check,
                                       const Studio::Width &a_width) {
  auto &state = Studio::State();
  const auto style = StyleOf(a_field.kind);
  const bool takesSignal = style.takesSignal && (!a_field.names.empty() ||
                                                 !a_field.creators.empty());
  const bool reference = a_field.text.starts_with('@');
  const Studio::FieldKey key = KeyOf(a_key);
  auto mode = state.comboMode.find(key);
  if (mode == state.comboMode.end()) {
    mode = state.comboMode.emplace(key, reference).first;
  }
  ImGui::PushID(NonNull(a_key));
  DrawValueBadge(style, takesSignal, mode->second, key);
  ImGui::SameLine(0.0f, 0.0f);

  std::optional<std::string> chosen;
  if (state.focusField == key) {
    state.focusField = Studio::kNoField;
    ImGui::SetKeyboardFocusHere();
  }
  if (mode->second && takesSignal) {
    chosen = SignalCombo(a_field);
  } else {
    if (style.swatch == Studio::Swatch::kAlways ||
        (style.swatch == Studio::Swatch::kWhenColour &&
         Studio::LiteralColor(a_field.text))) {
      chosen = ColorSwatchPicker(key, a_field.text);
      ImGui::SameLine(0.0f, 0.0f);
    }
    if (const auto typed =
            TextField("text", a_field.text, {a_width, a_scale}, a_check)) {
      chosen = typed;
    }
  }
  ImGui::PopID();
  return chosen;
}

bool DetailButton() {
  const float side = ImGui::GetFrameHeight();
  const bool clicked = ImGui::Button("...", ImVec2{side, side});
  Tooltip("open this field's details");
  ImGui::SameLine(0.0f, 0.0f);
  return clicked;
}

std::string ValueText(const Value &a_value) {
  return Match(
      a_value, [](float f) { return std::format("{:.3f}", f); },
      [](const Vec2 &v) { return std::format("({:.3f}, {:.3f})", v.x, v.y); },
      [](const Vec3 &v) {
        return std::format("({:.3f}, {:.3f}, {:.3f})", v.x, v.y, v.z);
      });
}

void ValueSwatch(const Value &a_value) {
  if (const auto *c = Get<Vec3>(a_value)) {
    ImGui::ColorButton("##swatch", ImVec4{c->x, c->y, c->z, 1.0f}, 0,
                       ImVec2{16.0f, 16.0f});
    ImGui::SameLine();
  }
  ImGui::TextUnformatted(ValueText(a_value).c_str());
}

void ResourceTable(const char *a_id, std::span<const ResourceCells> a_rows) {
  Table table = Table::Begin(a_id,
                             {{"name", Studio::Width::Fill()},
                              {"type", Studio::Width::Fit()},
                              {"value", Studio::Width::Fit()}},
                             Studio::kColumnsTable);
  if (!table.Open()) {
    return;
  }
  for (const ResourceCells &row : a_rows) {
    table.Cell();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(row.name.data(), row.name.data() + row.name.size());
    table.Cell();
    if (!row.type.empty()) {
      ImGui::AlignTextToFramePadding();
      Dim(row.type);
    }
    table.Cell();
    if (row.value) {
      ValueSwatch(*row.value);
    }
  }
  table.End();
}

std::optional<float> Split(const char *a_id, float a_ratio,
                           const std::function<void()> &a_left,
                           const std::function<void()> &a_right) {
  const float ratio = std::clamp(a_ratio, 0.05f, 0.95f);
  if (!ImGui::BeginTable(NonNull(a_id), 2,
                         ImGuiMCP::ImGuiTableFlags_Resizable |
                             ImGuiMCP::ImGuiTableFlags_BordersInnerV |
                             ImGuiMCP::ImGuiTableFlags_SizingStretchProp)) {
    return std::nullopt;
  }
  ImGui::TableSetupColumn("left", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch,
                          ratio);
  ImGui::TableSetupColumn("right", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch,
                          1.0f - ratio);
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  const float left = ImGui::GetContentRegionAvail().x;
  if (a_left) {
    a_left();
  }
  ImGui::TableNextColumn();
  const float right = ImGui::GetContentRegionAvail().x;
  if (a_right) {
    a_right();
  }
  ImGui::EndTable();
  if (left + right <= 0.0f) {
    return std::nullopt;
  }
  const float now = left / (left + right);
  return std::abs(now - ratio) > 0.0005f ? std::optional{now} : std::nullopt;
}

void Rule() { ImGui::Separator(); }

RuleResult Rule(const Studio::RuleSpec &a_spec, float a_trailingWidth,
                const std::function<void()> &a_trailing,
                const std::function<void()> &a_leading) {
  if (a_spec.leadingSpace) {
    ImGui::Dummy(ImVec2{0.0f, ImGui::GetFrameHeight()});
  }
  ImGui::Separator();
  bool open = true;
  if (a_spec.collapsible) {
    const std::string title{a_spec.text};
    open = ImGui::CollapsingHeader(
        title.c_str(),
        a_spec.openByDefault ? ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen : 0);
  } else if (a_spec.text.empty()) {
    ImGui::Dummy(ImVec2{0.0f, ImGui::GetFrameHeight()});
  } else {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(a_spec.text.data(),
                           a_spec.text.data() + a_spec.text.size());
  }
  if (a_leading) {
    ImGui::SameLine();
    a_leading();
  }
  const Studio::RuleClick click =
      DrawRuleControls(a_spec.buttons, a_trailingWidth, a_trailing);
  return RuleResult{click, open};
}

RuleFilter RuleWithFilter(const Studio::RuleSpec &a_spec,
                          const FilterSpec &a_filter) {
  std::string_view filter;
  const RuleResult result =
      Rule(a_spec, a_filter.width * a_filter.scale, [&]() {
        filter =
            LiveTextField(a_filter.key, a_filter.hint,
                          Studio::Width::Px(a_filter.width), a_filter.scale);
      });
  return RuleFilter{result.click, filter};
}

bool Toggle(const char *a_label, bool &a_value, std::string_view a_tooltip) {
  const bool changed = ImGui::Checkbox(NonNull(a_label), &a_value);
  Tooltip(a_tooltip);
  return changed;
}

void DetailModal(const char *a_title, const std::function<void()> &a_body) {
  ImGui::SetNextWindowSizeConstraints(ImVec2{480.0f, 0.0f},
                                      ImVec2{960.0f, 800.0f});
  if (!ImGui::BeginPopupModal(NonNull(a_title), nullptr,
                              ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  static_cast<void>(Rule(
      Studio::RuleSpec{.text = DisplayLabel(a_title), .leadingSpace = false},
      RowButtonWidth(), [&] {
        if (CloseButton()) {
          ImGui::CloseCurrentPopup();
        }
      }));
  a_body();
  ImGui::EndPopup();
}

void RightAligned(float a_width, const std::function<void()> &a_draw) {
  const float here = ImGui::GetCursorPosX();
  const float edge = here + ImGui::GetContentRegionAvail().x - a_width;
  ImGui::SetCursorPosX((std::max)(here, edge));
  a_draw();
}

void Disabled(bool a_disabled, const std::function<void()> &a_draw) {
  if (a_disabled) {
    ImGui::BeginDisabled();
  }
  a_draw();
  if (a_disabled) {
    ImGui::EndDisabled();
  }
}

void HeldLabel(const char *a_text) {
  ImGui::BeginDisabled();
  ImGui::Button(NonNull(a_text));
  ImGui::EndDisabled();
}

bool RemoveButton(std::size_t a_references) {
  const float side = RowButtonWidth();
  if (a_references > 0) {
    ImGui::BeginDisabled();
  }
  const bool clicked =
      ImGui::Button("X", ImVec2{side, side}) && a_references == 0;
  if (a_references > 0) {
    ImGui::EndDisabled();
  }
  Tooltip(a_references > 0
              ? std::format("referenced in {} place(s)", a_references)
              : "remove this row");
  return clicked;
}

bool CloseButton() {
  const float side = RowButtonWidth();
  const bool clicked = ImGui::Button("X", ImVec2{side, side});
  Tooltip("close");
  return clicked;
}

bool SoloButton(bool &a_solo, std::string_view a_tooltip) {
  return SquareToggle("S", a_solo, a_tooltip);
}

bool SoloButton(bool &a_solo) {
  return SoloButton(a_solo, "solo: show this alone");
}

bool MuteButton(bool &a_mute) {
  return SquareToggle("M", a_mute, "mute: hide this");
}

bool PeekButton(bool &a_peek) {
  if (a_peek) {
    const auto *active =
        ImGui::GetStyleColorVec4(ImGuiMCP::ImGuiCol_ButtonActive);
    ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button,
                          active ? *active : ImVec4{0.35f, 0.35f, 0.35f, 1.0f});
  }
  const bool clicked = ImGui::Button("Peek", ImVec2{ButtonWidth("Peek"), 0.0f});
  if (a_peek) {
    ImGui::PopStyleColor();
  }
  Tooltip("peek: preview this coverage on the armor");
  if (clicked) {
    a_peek = !a_peek;
  }
  return clicked;
}

bool DragHandle(const char *a_type, std::size_t a_index, const char *a_noun) {
  const float side = RowButtonWidth();
  const bool clicked = ImGui::Button("::", ImVec2{side, side});
  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload(a_type, &a_index, sizeof(a_index));
    ImGui::Text("%s %zu", NonNull(a_noun), a_index);
    ImGui::EndDragDropSource();
  }
  return clicked;
}

std::optional<Studio::RowMove> DropTarget(const char *a_type,
                                          std::size_t a_index) {
  if (!ImGui::BeginDragDropTarget()) {
    return std::nullopt;
  }
  std::optional<Studio::RowMove> move;
  const auto *payload = ImGui::AcceptDragDropPayload(a_type);
  if (payload && payload->Data &&
      payload->DataSize == static_cast<int>(sizeof(std::size_t))) {
    std::size_t from = 0;
    std::memcpy(&from, payload->Data, sizeof(from));
    if (from != a_index) {
      move = Studio::RowMove{from, a_index};
    }
  }
  ImGui::EndDragDropTarget();
  return move;
}

void Problem(std::string_view a_text) { Colored(kBad, a_text); }

void Warn(std::string_view a_text) { Colored(kWarn, a_text); }

void Ok(std::string_view a_text) { Colored(kOk, a_text); }

void Dim(std::string_view a_text) { Colored(kDim, a_text); }

void ProblemBadge(const char *a_label) {
  BadgeFrame(a_label, kBad, true, ImVec2{0.0f, 0.0f});
}

void WarnBadge(const char *a_label) {
  BadgeFrame(a_label, kWarn, false, ImVec2{0.0f, 0.0f});
}

void DimBadge(const char *a_label) {
  BadgeFrame(a_label, kDim, false, ImVec2{0.0f, 0.0f});
}

void Banner(std::string_view a_label, float a_trailingWidth,
            const std::function<void()> &a_trailing) {
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = ImGui::GetFrameHeight();
  ImGui::ImDrawListManager::AddRectFilled(
      ImGui::GetWindowDrawList(), origin,
      ImVec2{origin.x + width, origin.y + height}, ImGui::GetColorU32(kWarn),
      ImGui::GetStyle()->FrameRounding, 0);
  ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, kBadgeFrame);
  ImGui::AlignTextToFramePadding();
  const std::string label{a_label};
  ImGui::TextUnformatted(label.c_str());
  ImGui::PopStyleColor();
  if (a_trailing) {
    ImGui::SameLine();
    RightAligned(a_trailingWidth, a_trailing);
  }
}

void PlaceholderText(std::string_view a_text) {
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const ImVec2 start = ImGui::GetCursorPos();
  const float wrap = (std::min)(avail.x, 420.0f);
  const ImVec2 size = ImGui::CalcTextSize(
      a_text.data(), a_text.data() + a_text.size(), false, wrap);
  const float x = start.x + (std::max)(0.0f, (avail.x - size.x) * 0.5f);
  const float y = start.y + (std::max)(0.0f, (avail.y - size.y) * 0.5f);
  ImGui::SetCursorPos(ImVec2{x, y});
  ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, kDim);
  ImGui::PushTextWrapPos(x + wrap);
  ImGui::TextUnformatted(a_text.data(), a_text.data() + a_text.size());
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}

void DrawDiagnostics(std::span<const Diagnostic> a_problems, bool a_heldBack) {
  if (a_problems.empty()) {
    return;
  }
  static_cast<void>(
      Rule(Studio::RuleSpec{.text = "Rows with problems"}, 0.0f, {},
           [&]() { Warn(std::format("{}", a_problems.size())); }));
  if (a_heldBack) {
    Problem("Held back: recipe-level errors keep it out of the applied set "
            "until they are fixed");
  }
  for (const Diagnostic &diagnostic : a_problems) {
    const std::string line =
        std::format("{}: {}", diagnostic.where, diagnostic.message);
    if (diagnostic.severity == Severity::kError) {
      Problem(line);
    } else {
      Warn(line);
    }
  }
}

void HelpMarker(const char *a_text) {
  if (!a_text || !*a_text) {
    return;
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", a_text);
  }
}

void Tooltip(std::string_view a_text) {
  if (a_text.empty() ||
      !ImGui::IsItemHovered(ImGuiMCP::ImGuiHoveredFlags_AllowWhenDisabled)) {
    return;
  }
  ImGui::SetTooltip("%.*s", static_cast<int>(a_text.size()), a_text.data());
}

FieldScope::FieldScope(std::string_view a_part) {
  std::string &scope = Studio::State().fieldScope;
  previous = scope;
  scope.push_back('\x1f');
  scope.append(a_part);
}

FieldScope::~FieldScope() { Studio::State().fieldScope = std::move(previous); }
}
