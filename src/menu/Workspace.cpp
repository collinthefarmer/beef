// GPL-3.0-only with the additional permission in COPYING.md.
#include "menu/Workspace.h"

#include "engine/Manager.h"
#include "menu/ClusterChart.h"
#include "menu/ContextRows.h"
#include "menu/FormDraw.h"
#include "menu/MenuWidgets.h"
#include "menu/PaintPanel.h"
#include "menu/PreviewWindows.h"
#include "menu/RelationshipPanel.h"
#include "menu/ResourcePanels.h"
#include "menu/ResponsePanel.h"
#include "menu/StackPanel.h"
#include "menu/WearerInputs.h"
#include "studio/ActorCatalogs.h"
#include "studio/Create.h"
#include "studio/EditResult.h"
#include "studio/FieldParsing.h"
#include "studio/Forms.h"
#include "studio/Intent.h"
#include "studio/Names.h"
#include "studio/Navigation.h"
#include "studio/Panels.h"
#include "studio/Rows.h"

#include <algorithm>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImVec2;

namespace BetterEnchantmentEffects::Menu {
namespace {
[[nodiscard]] std::string
InspectorKey(const Studio::InspectorSubject &a_subject) {
  return Match(
      a_subject,
      [](const Studio::RecipeSubject &) -> std::string { return "recipe"; },
      [](const Studio::ShellSubject &) -> std::string { return "shell"; },
      [](const Studio::OutputSubject &subject) -> std::string {
        return std::format("output:{}", subject.output);
      },
      [](const Studio::LayerSubject &subject) -> std::string {
        return std::format("layer:{}:{}", subject.output, subject.layer);
      },
      [](const Studio::SignalSubject &subject) -> std::string {
        return "signal:" + subject.name;
      },
      [](const Studio::SourceSubject &subject) -> std::string {
        return "source:" + subject.name;
      },
      [](const Studio::MaskSubject &subject) -> std::string {
        return "mask:" + subject.name;
      },
      [](const Studio::CurveSubject &subject) -> std::string {
        return "curve:" + subject.name;
      });
}

void NavigateFromNavigator(const Frame &a_frame,
                           Studio::InspectorSubject a_subject) {
  [[maybe_unused]] const bool changed =
      Studio::Navigate(a_frame.state->navigation, a_frame.state->selection,
                       std::move(a_subject), *a_frame.recipe);
  if (a_frame.state->paint) {
    Studio::Reduce(*a_frame.state, Studio::SetMode{Studio::Mode::kCompose});
  }
}

void PickSubject(const char *a_label, Studio::InspectorSubject a_subject,
                 const Frame &a_frame, bool a_spanColumns = false,
                 float a_width = 0.0f,
                 std::optional<Studio::FieldKind> a_badge = std::nullopt) {
  const bool blocked =
      Studio::IndexedEditPendingFor(a_frame.state->pendingIndexedEdit,
                                    a_frame.recipe->id) &&
      (Is<Studio::OutputSubject>(a_subject) ||
       Is<Studio::LayerSubject>(a_subject));
  const bool selected = SelectionOf(a_frame).subject == a_subject;
  const int flags =
      a_spanColumns ? ImGuiMCP::ImGuiSelectableFlags_SpanAllColumns : 0;
  ImGui::BeginDisabled(blocked);
  const bool clicked =
      a_badge
          ? TypedLink({*a_badge, a_label, selected, a_width})
          : ImGui::Selectable(a_label, selected, flags, ImVec2{a_width, 0.0f});
  if (clicked) {
    NavigateFromNavigator(a_frame, std::move(a_subject));
  }
  ImGui::EndDisabled();
}

void PickSubjectWithRemove(
    const char *a_label, Studio::InspectorSubject a_subject,
    const Frame &a_frame, const std::function<void()> &a_remove,
    std::optional<Studio::FieldKind> a_badge = std::nullopt) {
  const float remove = RowButtonWidth();
  const float width = (std::max)(1.0f, ImGui::GetContentRegionAvail().x -
                                           remove - ItemSpacingX());
  PickSubject(a_label, std::move(a_subject), a_frame, false, width, a_badge);
  ImGui::SameLine();
  RightAligned(remove, [&] {
    if (RemoveButton(0)) {
      a_remove();
    }
  });
}

std::string SurfaceScope(Surface a_surface) {
  return std::format("surface-{}", SurfaceName(a_surface));
}

void PostCreation(const Frame &a_frame, Studio::Creation a_creation) {
  Studio::Created made = Studio::Create(std::move(a_creation), *a_frame.recipe);
  Studio::Post(*a_frame.intents,
               Studio::EditRecipe{a_frame.recipe->id, std::move(made.edits)});
  a_frame.state->pendingSelection =
      Studio::PendingSubject{a_frame.recipe->id, made.subject};
}

void DrawSlotChoices(const Frame &a_frame, Surface a_surface) {
  for (const Slot slot : SlotsOf(a_surface, a_frame.recipe->shellMaterial)) {
    if (ImGui::Selectable(std::string{SlotName(slot)}.c_str())) {
      PostCreation(a_frame, Studio::NewOutput{a_surface, slot, {}});
      ImGui::CloseCurrentPopup();
    }
  }
}

void DrawSurfaceAdd(const Frame &a_frame, Surface a_surface) {
  ImGui::PushID(SurfaceScope(a_surface).c_str());
  if (AddButton()) {
    ImGui::OpenPopup("add-output");
  }
  if (ImGui::BeginPopup("add-output")) {
    DrawSlotChoices(a_frame, a_surface);
    ImGui::EndPopup();
  }
  ImGui::PopID();
}

void DrawShellSettings(const Frame &a_frame) {
  if (ImGui::Button("settings##shell")) {
    NavigateFromNavigator(a_frame, Studio::ShellSubject{});
  }
}

void DrawLightSettings(const Frame &a_frame) {
  if (ImGui::Button("settings##light") && !a_frame.recipe->lights.empty()) {
    NavigateFromNavigator(
        a_frame, Studio::OutputSubject{a_frame.recipe->lights.front().output});
  }
}

constexpr const char *kNavLayerPayload = "BEEF_NAV_LAYER";

void DrawOutputLayerRow(const Frame &a_frame, const Studio::OutputRow &a_output,
                        std::size_t a_layer) {
  const std::string &id = a_frame.recipe->id;
  const Studio::View &view = ViewOf(a_frame);
  const Studio::LayerRow &layer = a_output.layers[a_layer];
  ImGui::PushID(static_cast<int>(a_layer));
  if (DragHandle(kNavLayerPayload, a_layer, "layer")) {
    Studio::Post(*a_frame.intents, Studio::PickLayer{a_layer});
  }
  if (const auto move = DropTarget(kNavLayerPayload, a_layer)) {
    Studio::Post(*a_frame.intents, id,
                 Studio::MoveLayer{a_output.index, move->from, move->to});
  }
  ImGui::SameLine();
  bool solo = view.isolation.TargetsLayer(id, a_output.index, a_layer);
  if (SoloButton(solo)) {
    Studio::Post(*a_frame.intents,
                 Studio::SoloLayer{id, a_output.index, a_layer, solo});
  }
  ImGui::SameLine();
  bool mute = view.LayerMuted(id, a_output.index, a_layer);
  if (MuteButton(mute)) {
    Studio::Post(*a_frame.intents,
                 Studio::MuteLayer{id, a_output.index, a_layer, mute});
  }
  ImGui::SameLine();
  if (const auto blend = BlendBadge(layer.blend, a_output.slot)) {
    Studio::Post(*a_frame.intents, id,
                 Studio::SetLayerBlend{a_output.index, a_layer, *blend});
  }
  ImGui::SameLine();
  PinSubjectButton(a_frame, Studio::LayerSubject{a_output.index, a_layer});
  ImGui::SameLine();
  const std::string label =
      std::format("{}: {}", a_layer + 1,
                  layer.source.empty() ? "Choose a signal" : layer.source);
  PickSubjectWithRemove(
      label.c_str(), Studio::LayerSubject{a_output.index, a_layer}, a_frame,
      [&] {
        Studio::Post(*a_frame.intents, id,
                     Studio::RemoveLayer{a_output.index, a_layer});
      },
      layer.source.starts_with('@') ? Studio::FieldKind::kReference
                                    : Studio::FieldKind::kColor);
  ImGui::PopID();
}

void DrawOutputNode(const Frame &a_frame, const Studio::OutputRow &a_output,
                    std::string_view a_filter) {
  const std::string title = a_output.target == Target::kLight
                                ? std::format("Light {}", a_output.index + 1)
                                : std::string{SlotName(a_output.slot)};
  const bool outputMatches = Studio::NameMatches(title, a_filter);
  const bool layerMatches =
      std::ranges::any_of(a_output.layers, [&](const Studio::LayerRow &l) {
        return Studio::NameMatches(l.source, a_filter) ||
               Studio::NameMatches(l.mask, a_filter);
      });
  if (!outputMatches && !layerMatches) {
    return;
  }
  ImGui::PushID(std::format("output-{}", a_output.index).c_str());
  if (a_output.target != Target::kLight) {
    PinSubjectButton(a_frame, Studio::OutputSubject{a_output.index});
    ImGui::SameLine();
  }
  PickSubjectWithRemove(title.c_str(), Studio::OutputSubject{a_output.index},
                        a_frame, [&] {
                          Studio::Post(*a_frame.intents, a_frame.recipe->id,
                                       Studio::RemoveOutput{a_output.index});
                        });
  ImGui::Indent();
  for (std::size_t i = 0; i < a_output.layers.size(); ++i) {
    if (!outputMatches &&
        !Studio::NameMatches(a_output.layers[i].source, a_filter) &&
        !Studio::NameMatches(a_output.layers[i].mask, a_filter)) {
      continue;
    }
    DrawOutputLayerRow(a_frame, a_output, i);
  }
  if (a_output.target != Target::kLight && SlotTakesLayers(a_output.slot)) {
    ImGui::PushID("add");
    if (ImGui::Button("+ layer")) {
      PostCreation(a_frame, Studio::NewLayer{a_output.index});
    }
    ImGui::PopID();
  }
  ImGui::Unindent();
  ImGui::PopID();
}

struct ResourceRowSpec {
  std::string name;
  Studio::InspectorSubject subject;
  std::string_view type;
  std::optional<Value> value;
  std::size_t references = 0;
  std::string_view valueText = "-";
  std::optional<Studio::PreviewSubject> preview;
};

void ResourceRow(Table &a_table, const ResourceRowSpec &a_row,
                 const Frame &a_frame) {
  a_table.Cell();
  if (a_row.preview) {
    PinSubjectButton(a_frame, *a_row.preview);
    ImGui::SameLine();
  }
  const std::optional<std::string> renamed =
      NameLabel(ResourceLabel(a_frame, "resource-row", a_row.name), [&] {
        PickSubject(a_row.name.c_str(), a_row.subject, a_frame, true);
      });
  if (renamed) {
    PostResourceRename(a_frame, a_row.subject, *renamed);
  }
  a_table.Cell();
  if (!a_row.type.empty()) {
    ImGui::AlignTextToFramePadding();
    Dim(a_row.type);
  }
  a_table.Cell();
  if (a_row.value) {
    ValueSwatch(*a_row.value);
  } else {
    ImGui::AlignTextToFramePadding();
    DimFitted(a_row.valueText);
  }
  a_table.Cell();
  ImGui::AlignTextToFramePadding();
  Dim(std::to_string(a_row.references));
}

const char *ResourceAddLabel(Studio::ResourceTab a_tab);

void DrawSignalRows(Table &a_table, const Frame &a_frame,
                    std::string_view a_filter) {
  for (const Studio::SignalRow &signal : a_frame.recipe->signals) {
    if (!Studio::NameMatches(signal.name, a_filter)) {
      continue;
    }
    const std::optional<Value> value =
        signal.constant.has_value()    ? signal.constant
        : signal.live && !signal.inert ? std::optional<Value>{signal.value}
                                       : std::nullopt;
    const std::string_view placeholder = signal.perGeometry ? "per geometry"
                                         : signal.live && signal.inert
                                             ? "(inactive)"
                                             : "-";
    ResourceRow(a_table,
                {.name = signal.name,
                 .subject = Studio::SignalSubject{signal.name},
                 .type = SignalKindName(signal.kind),
                 .value = value,
                 .references = signal.references,
                 .valueText = placeholder,
                 .preview = std::nullopt},
                a_frame);
  }
}

void DrawCurveRows(Table &a_table, const Frame &a_frame,
                   std::string_view a_filter) {
  for (const Studio::TextRow &curve : a_frame.recipe->curves) {
    if (!Studio::NameMatches(curve.name, a_filter)) {
      continue;
    }
    ResourceRow(a_table,
                {.name = curve.name,
                 .subject = Studio::CurveSubject{curve.name},
                 .references = curve.references,
                 .valueText = curve.text.empty()
                                  ? std::string_view{"-"}
                                  : std::string_view{curve.text}},
                a_frame);
  }
}

void DrawSourceRows(Table &a_table, const Frame &a_frame,
                    std::string_view a_filter) {
  for (const Studio::SourceRow &source : a_frame.recipe->sourceRows) {
    if (!Studio::NameMatches(source.name, a_filter)) {
      continue;
    }
    const SourceKind kind =
        Studio::SourceKindOf(source).value_or(SourceKind{MaterialSource{}});
    const std::string value = SourceValueText(kind);
    ResourceRow(a_table,
                {.name = source.name,
                 .subject = Studio::SourceSubject{source.name},
                 .type = SourceKindName(kind),
                 .references = source.references,
                 .valueText = value.empty() ? std::string_view{"-"}
                                            : std::string_view{value},
                 .preview = Studio::SourceSubject{source.name}},
                a_frame);
  }
}

void DrawMaskRows(Table &a_table, const Frame &a_frame,
                  std::string_view a_filter) {
  for (const Studio::TextRow &mask : a_frame.recipe->maskRows) {
    if (!Studio::NameMatches(mask.name, a_filter)) {
      continue;
    }
    ResourceRow(a_table,
                {.name = mask.name,
                 .subject = Studio::MaskSubject{mask.name},
                 .references = mask.references,
                 .valueText = mask.text.empty() ? std::string_view{"-"}
                                                : std::string_view{mask.text},
                 .preview = Studio::MaskSubject{mask.name}},
                a_frame);
  }
}

void DrawResourceRows(const Frame &a_frame, Studio::ResourceTab a_tab,
                      std::string_view a_filter) {
  ImGui::PushID(static_cast<int>(a_tab));
  Table table = Table::Begin("resource-list",
                             {{"name", Studio::Width::Fit()},
                              {"type", Studio::Width::Fit()},
                              {"value", Studio::Width::Fill()},
                              {"used", Studio::Width::Fit()}},
                             Studio::kRelationTable);
  if (!table.Open()) {
    ImGui::PopID();
    return;
  }
  switch (a_tab) {
  case Studio::ResourceTab::kSignals:
    DrawSignalRows(table, a_frame, a_filter);
    break;
  case Studio::ResourceTab::kCurves:
    DrawCurveRows(table, a_frame, a_filter);
    break;
  case Studio::ResourceTab::kSources:
    DrawSourceRows(table, a_frame, a_filter);
    break;
  case Studio::ResourceTab::kMasks:
    DrawMaskRows(table, a_frame, a_filter);
    break;
  }
  table.Cell();
  if (ImGui::Button(ResourceAddLabel(a_tab))) {
    PostResourceAdd(a_frame, a_tab);
  }
  table.Cell();
  table.Cell();
  table.Cell();
  table.End();
  ImGui::PopID();
}

const char *ResourceAddLabel(Studio::ResourceTab a_tab) {
  switch (a_tab) {
  case Studio::ResourceTab::kSignals:
    return "+ signal";
  case Studio::ResourceTab::kCurves:
    return "+ curve";
  case Studio::ResourceTab::kSources:
    return "+ source";
  case Studio::ResourceTab::kMasks:
    return "+ mask";
  }
  return "+";
}

void DrawResourceTabs(const Frame &a_frame, std::string_view a_filter,
                      float a_reserve) {
  if (!a_filter.empty()) {
    if (ImGui::BeginChild("resource-rows", ImVec2{0.0f, -a_reserve}, 0, 0)) {
      for (const Studio::ResourceTab tab : Studio::kResourceTabs) {
        Dim(std::string{Studio::ResourceTabName(tab)});
        DrawResourceRows(a_frame, tab, a_filter);
      }
    }
    ImGui::EndChild();
    return;
  }
  if (!ImGui::BeginTabBar("nav-resources")) {
    return;
  }
  for (const Studio::ResourceTab tab : Studio::kResourceTabs) {
    if (!ImGui::BeginTabItem(
            std::string{Studio::ResourceTabName(tab)}.c_str())) {
      continue;
    }
    if (tab != a_frame.state->resource) {
      Studio::Post(*a_frame.intents, Studio::ShowResource{tab});
    }
    if (ImGui::BeginChild("resource-rows", ImVec2{0.0f, -a_reserve}, 0, 0)) {
      DrawResourceRows(a_frame, tab, a_filter);
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
}

bool IsSurfaceOutput(const Studio::OutputRow &a_output, Surface a_surface) {
  return a_output.target != Target::kLight && a_output.surface == a_surface;
}

void DrawSurfaceOutputs(const Frame &a_frame, Surface a_surface,
                        std::string_view a_filter) {
  ImGui::Indent();
  for (const Studio::OutputRow &output : a_frame.recipe->outputs) {
    if (IsSurfaceOutput(output, a_surface)) {
      DrawOutputNode(a_frame, output, a_filter);
    }
  }
  ImGui::Unindent();
}

bool SurfaceHasOutput(const Studio::RecipeRow &a_recipe, Surface a_surface) {
  return std::ranges::any_of(a_recipe.outputs,
                             [&](const Studio::OutputRow &a_output) {
                               return IsSurfaceOutput(a_output, a_surface);
                             });
}

void SurfaceLabel(std::string_view a_text, bool a_lit) {
  ImGui::AlignTextToFramePadding();
  if (a_lit) {
    ImGui::TextUnformatted(a_text.data(), a_text.data() + a_text.size());
  } else {
    Dim(a_text);
  }
}

void DrawSurfaceRemove(const Frame &a_frame, Surface a_surface) {
  ImGui::PushID(SurfaceScope(a_surface).c_str());
  const bool clicked = RemoveButton(0);
  ImGui::PopID();
  if (!clicked) {
    return;
  }
  std::vector<Studio::RecipeEdit> edits;
  if (a_surface == Surface::kShell) {
    edits.emplace_back(Studio::ResetShell{});
  }
  std::vector<std::size_t> indices;
  for (const Studio::OutputRow &output : a_frame.recipe->outputs) {
    if (IsSurfaceOutput(output, a_surface)) {
      indices.push_back(output.index);
    }
  }
  std::ranges::sort(indices, std::greater{});
  for (const std::size_t index : indices) {
    edits.emplace_back(Studio::RemoveOutput{index});
  }
  if (!edits.empty()) {
    Studio::Post(*a_frame.intents,
                 Studio::EditRecipe{a_frame.recipe->id, std::move(edits)});
  }
}

void DrawLightRemove(const Frame &a_frame) {
  ImGui::PushID("light-remove");
  const bool clicked = RemoveButton(0);
  ImGui::PopID();
  if (!clicked || a_frame.recipe->lights.empty()) {
    return;
  }
  Studio::Post(*a_frame.intents, a_frame.recipe->id,
               Studio::RemoveOutput{a_frame.recipe->lights.front().output});
}

void DrawLightAdd(const Frame &a_frame) {
  if (AddButton()) {
    PostCreation(a_frame, Studio::NewLight{});
  }
}

void DrawSurfaceControls(const std::function<void()> &a_settings,
                         const std::function<void()> &a_add,
                         const std::function<void()> &a_remove) {
  const float settingsW = ButtonWidth("settings");
  const float addW = RowButtonWidth();
  const float removeW = RowButtonWidth();
  const auto slot = [](const std::function<void()> &a_draw, float a_width) {
    if (a_draw) {
      a_draw();
    } else {
      ImGui::Dummy(ImVec2{a_width, ImGui::GetFrameHeight()});
    }
  };
  RightAligned(settingsW + addW + removeW + 2.0f * ItemSpacingX(), [&]() {
    slot(a_settings, settingsW);
    ImGui::SameLine();
    slot(a_add, addW);
    ImGui::SameLine();
    slot(a_remove, removeW);
  });
}

void DrawSurfaceLine(const Frame &a_frame, const char *a_label,
                     Surface a_surface, std::string_view a_filter) {
  const bool has = SurfaceHasOutput(*a_frame.recipe, a_surface);
  SurfaceLabel(a_label, has);
  ImGui::SameLine();
  std::function<void()> settings;
  if (a_surface == Surface::kShell) {
    settings = [&] { DrawShellSettings(a_frame); };
  }
  std::function<void()> remove;
  if (has) {
    remove = [&] { DrawSurfaceRemove(a_frame, a_surface); };
  }
  DrawSurfaceControls(
      settings, [&] { DrawSurfaceAdd(a_frame, a_surface); }, remove);
  DrawSurfaceOutputs(a_frame, a_surface, a_filter);
}

void DrawLightLine(const Frame &a_frame) {
  const bool has = !a_frame.recipe->lights.empty();
  SurfaceLabel("Light", has);
  ImGui::SameLine();
  std::function<void()> settings, add, remove;
  if (has) {
    settings = [&] { DrawLightSettings(a_frame); };
    remove = [&] { DrawLightRemove(a_frame); };
  } else {
    add = [&] { DrawLightAdd(a_frame); };
  }
  DrawSurfaceControls(settings, add, remove);
}

void DrawRecipeTree(const Frame &a_frame, std::string_view a_filter) {
  DrawSurfaceLine(a_frame, "Material", Surface::kMaterial, a_filter);
  DrawSurfaceLine(a_frame, "Shell", Surface::kShell, a_filter);
  DrawLightLine(a_frame);
}

void DrawRecipeNavigator(const Frame &a_frame);
void DrawResourceNavigator(const Frame &a_frame);

void DrawNavigator(const Frame &a_frame) {
  const float half = ImGui::GetContentRegionAvail().y * 0.5f;
  if (ImGui::BeginChild("navigator-recipe", ImVec2{0.0f, half}, 0, 0)) {
    DrawRecipeNavigator(a_frame);
  }
  ImGui::EndChild();
  if (ImGui::BeginChild("navigator-resources", ImVec2{0.0f, 0.0f}, 0, 0)) {
    DrawResourceNavigator(a_frame);
  }
  ImGui::EndChild();
}

void DrawRecipeNavigator(const Frame &a_frame) {
  static_cast<void>(
      Rule(Studio::RuleSpec{.text = "Recipe", .leadingSpace = false},
           ButtonWidth("settings"), [&] {
             if (ImGui::Button("settings")) {
               NavigateFromNavigator(a_frame, Studio::RecipeSubject{});
             }
             Tooltip("the recipe's settings and keys");
           }));
  const float find = ImGui::GetFrameHeightWithSpacing();
  if (ImGui::BeginChild("recipe-scroll", ImVec2{0.0f, -find}, 0, 0)) {
    DrawRecipeTree(a_frame, LiveText("recipe-search"));
  }
  ImGui::EndChild();
  static_cast<void>(LiveTextField("recipe-search", "Find outputs",
                                  Studio::Width::Fill(), a_frame.scale));
}

void DrawResourceNavigator(const Frame &a_frame) {
  static_cast<void>(
      Rule(Studio::RuleSpec{.text = "Resources", .leadingSpace = false}));
  const float find = ImGui::GetFrameHeightWithSpacing();
  DrawResourceTabs(a_frame, LiveText("resource-search"), find);
  static_cast<void>(LiveTextField("resource-search", "Find resources",
                                  Studio::Width::Fill(), a_frame.scale));
}

[[nodiscard]] std::optional<Studio::GeometryRow>
OutputGeometry(const Frame &a_frame, std::size_t a_output) {
  if (!a_frame.geometry) {
    return std::nullopt;
  }
  const Studio::OutputRow *found =
      FindBy(a_frame.geometry->outputs, a_output, &Studio::OutputRow::index);
  if (!found) {
    return std::nullopt;
  }
  Studio::GeometryRow geometry = *a_frame.geometry;
  geometry.outputs = {*found};
  return geometry;
}

void DrawOutput(const Studio::OutputSubject &a_subject, const Frame &a_frame) {
  const Studio::OutputRow *output = FindBy(
      a_frame.recipe->outputs, a_subject.output, &Studio::OutputRow::index);
  if (!output) {
    return;
  }
  if (output->target == Target::kLight) {
    const Studio::LightRow *light = FindBy(
        a_frame.recipe->lights, a_subject.output, &Studio::LightRow::output);
    if (!light) {
      PlaceholderText("The light definition is unavailable.");
      return;
    }
    static_cast<void>(Rule(
        Studio::RuleSpec{.text = "Inspector"}, ButtonWidth("Reset"),
        [&]() {
          if (ImGui::Button("Reset")) {
            Studio::Post(*a_frame.intents, a_frame.recipe->id,
                         Studio::ResetLight{light->output});
          }
        },
        [&]() { Dim("Light"); }));
    DrawFormWithSignals(
        "light",
        Studio::LightForm(*light, Studio::SignalNamesOf(*a_frame.recipe)),
        a_frame);
    return;
  }
  const auto geometry = (a_frame.geometry && a_frame.piece)
                            ? OutputGeometry(a_frame, a_subject.output)
                            : std::nullopt;
  Frame drawFrame = a_frame;
  std::optional<Studio::LayerStack> stack;
  std::optional<Studio::Inspector> inspector;
  std::string_view unavailable;
  if (geometry) {
    drawFrame.geometry = &*geometry;
    stack = Studio::BuildStackView({*drawFrame.piece, *drawFrame.recipe,
                                    *drawFrame.geometry, SelectionOf(drawFrame),
                                    ViewOf(drawFrame)});
    inspector = Studio::BuildInspector(*drawFrame.recipe, *drawFrame.geometry,
                                       SelectionOf(drawFrame));
  } else {
    if (a_frame.geometry && a_frame.piece) {
      unavailable = "not applied to the viewed geometry";
    }
    stack = Studio::BuildStackView(*a_frame.recipe, SelectionOf(a_frame),
                                   ViewOf(a_frame));
    inspector = Studio::BuildInspector(*a_frame.recipe, SelectionOf(a_frame));
  }
  const std::vector<Studio::FormField> scalars =
      stack ? Studio::ScalarForm(*stack) : std::vector<Studio::FormField>{};
  DrawOutputHeader(*output, scalars, drawFrame, unavailable);
  DrawStack(stack, inspector, drawFrame);
}

void DrawLayerInspector(const Studio::LayerSubject &a_layer,
                        const Frame &a_frame) {
  const Studio::OutputRow *owner = FindBy(
      a_frame.recipe->outputs, a_layer.output, &Studio::OutputRow::index);
  const std::string label =
      owner ? std::format("{} / {} / layer {}", SurfaceName(owner->surface),
                          SlotName(owner->slot), a_layer.layer + 1)
            : std::format("layer {}", a_layer.layer + 1);
  static_cast<void>(Rule(Studio::RuleSpec{.text = "Inspector"}, 0.0f, {},
                         [&]() { Dim(label); }));
  const auto geometry = OutputGeometry(a_frame, a_layer.output);
  const auto inspector =
      geometry ? Studio::BuildInspector(*a_frame.recipe, *geometry,
                                        SelectionOf(a_frame))
               : Studio::BuildInspector(*a_frame.recipe, SelectionOf(a_frame));
  if (!inspector) {
    return;
  }
  DrawInspectorFields(*inspector, a_frame);
}

[[nodiscard]] float ResourceActionsWidth() {
  return ButtonWidth("Clone") + ItemSpacingX() + ButtonWidth("Rename") +
         ItemSpacingX() + RowButtonWidth();
}

void DrawResourceRemoveButton(std::string_view a_name, std::size_t a_references,
                              const std::function<void(bool)> &a_post);

void DrawResourceRemove(std::string_view a_name, std::size_t a_references,
                        const std::function<void(bool)> &a_post) {
  ImGui::PushID("remove");
  DrawResourceRemoveButton(a_name, a_references, a_post);
  ImGui::PopID();
}

void DrawResourceRemoveButton(std::string_view a_name, std::size_t a_references,
                              const std::function<void(bool)> &a_post) {
  if (a_references == 0) {
    if (RemoveButton(0)) {
      a_post(false);
    }
    return;
  }
  const std::string title =
      std::format("Remove resource?###cascade-{}", a_name);
  if (ImGui::Button("X", ImVec2{RowButtonWidth(), RowButtonWidth()})) {
    ImGui::OpenPopup(title.c_str());
  }
  Tooltip(std::format(
      "used in {} place(s) — remove it and everything that depends on it",
      a_references));
  ConfirmModal(
      title.c_str(),
      std::format("Remove '{}'? It is used in {} place(s): rows that depend on "
                  "it are removed too, and any other use is reset to its "
                  "default.",
                  a_name, a_references),
      "Remove", [&] { a_post(true); });
}

void DrawSourceClusters(const std::string &a_name,
                        const MaterialGroupsSource &a_source,
                        std::span<const Studio::FormField> a_form,
                        const Frame &a_frame);

void DrawSourceInspector(const Studio::SourceSubject &a_source,
                         const Frame &a_frame) {
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  const Studio::SourceRow *row = FindByName(recipe.sourceRows, a_source.name);
  if (!row) {
    return;
  }
  static_cast<void>(Rule(
      Studio::RuleSpec{.text = "Inspector"}, ResourceActionsWidth(), [&]() {
        DrawResourceClone(a_frame, a_source);
        ImGui::SameLine();
        DrawResourceRename(a_frame, row->name, [&](std::string a_name) {
          return Studio::RenameSource{row->name, std::move(a_name)};
        });
        ImGui::SameLine();
        DrawResourceRemove(row->name, row->references, [&](bool a_cascade) {
          Studio::Post(*a_frame.intents, recipe.id,
                       Studio::RemoveSource{row->name, a_cascade});
        });
      }));
  const std::string type = DescribeSource(
      Studio::SourceKindOf(*row).value_or(SourceKind{MaterialSource{}}));
  const ResourceCells cells{
      .name = row->name,
      .type = type,
      .value = std::nullopt,
      .label = ResourceLabel(a_frame, "resource-header", row->name)};
  if (std::optional<std::string> renamed =
          ResourceTable("source-header", {&cells, 1})) {
    PostResourceRename(a_frame, a_source, std::move(*renamed));
  }
  const std::vector<std::string> geometries = Studio::GeometryNamesOf(recipe);
  const std::vector<Studio::FormField> form =
      Studio::SourceForm(*row, Studio::SignalNamesOf(recipe), geometries);
  const std::optional<SourceKind> kind = Studio::SourceKindOf(*row);
  if (const auto *clusters =
          kind ? Get<MaterialGroupsSource>(*kind) : nullptr) {
    DrawSourceClusters(row->name, *clusters, form, a_frame);
  }
  DrawFormWithSignals("source", form, a_frame);
}

void DrawSourceClusters(const std::string &a_name,
                        const MaterialGroupsSource &a_source,
                        std::span<const Studio::FormField> a_form,
                        const Frame &a_frame) {
  if (!a_frame.geometry) {
    Dim("View a piece to chart its material's clusters.");
    return;
  }
  DrawMaterialDump(a_frame);
  Studio::ClusterMemo &memo = a_frame.state->sourceClusterMemo;
  const Studio::ClusterView *view =
      Studio::ClustersOf(memo, a_frame.geometry->grouping);
  const float bias =
      Get<float>(a_source.areaBias) ? *Get<float>(a_source.areaBias) : 1.0f;
  const float *pick = a_source.pick ? Get<float>(*a_source.pick) : nullptr;
  const std::optional<std::uint8_t> picked =
      view && pick ? PickCluster(view->analysis, *pick, bias) : std::nullopt;
  const std::optional<std::uint8_t> chosen = DrawClusterExplorer(
      {*a_frame.geometry, a_source.settings, picked}, memo, a_frame.scale);
  if (!chosen || chosen == picked || !view) {
    return;
  }
  const std::optional<float> value = PickValueOf(view->analysis, *chosen, bias);
  const auto field =
      std::ranges::find(a_form, std::string{"pick"}, &Studio::FormField::name);
  if (value && field != a_form.end()) {
    PostField(*field, ParamText(Param{*value}), a_frame);
  }
}

void PostHold(const Frame &a_frame, const std::string &a_signal,
              std::optional<Value> a_value) {
  if (Manager *manager = Manager::GetSingleton()) {
    manager->Editor().HoldSignal(a_frame.recipe->id, a_signal, a_value);
  }
}

std::optional<Value> EditHeldValue(const Value &a_held) {
  return Match(
      a_held,
      [](float a_value) -> std::optional<Value> {
        float edited = a_value;
        NextItemWidth(Studio::Width::Px(160.0f));
        return ImGui::DragFloat("##held", &edited, 0.01f)
                   ? std::optional<Value>{edited}
                   : std::nullopt;
      },
      [](const Vec2 &a_value) -> std::optional<Value> {
        float edited[2]{a_value.x, a_value.y};
        NextItemWidth(Studio::Width::Px(220.0f));
        return ImGui::DragFloat2("##held", edited, 0.01f)
                   ? std::optional<Value>{Vec2{edited[0], edited[1]}}
                   : std::nullopt;
      },
      [](const Vec3 &a_value) -> std::optional<Value> {
        float edited[3]{a_value.x, a_value.y, a_value.z};
        NextItemWidth(Studio::Width::Px(260.0f));
        return ImGui::DragFloat3("##held", edited, 0.01f)
                   ? std::optional<Value>{Vec3{edited[0], edited[1], edited[2]}}
                   : std::nullopt;
      });
}

void DrawHold(const Studio::SignalRow &a_row, const Frame &a_frame) {
  ImGui::PushID("hold");
  if (!a_row.held) {
    if (ImGui::Button("Hold")) {
      PostHold(a_frame, a_row.name, a_row.value);
    }
    Tooltip("Keep this signal at its current value in the game, for every "
            "wearer of the recipe. The recipe itself is not changed. Return to "
            "live releases it.");
    ImGui::PopID();
    return;
  }
  WarnBadge("Held");
  ImGui::SameLine();
  if (const std::optional<Value> edited = EditHeldValue(*a_row.held)) {
    PostHold(a_frame, a_row.name, *edited);
  }
  ImGui::SameLine();
  if (ImGui::Button("Release")) {
    PostHold(a_frame, a_row.name, std::nullopt);
  }
  ImGui::PopID();
}

void DrawSignalInspector(const Studio::SignalSubject &a_signal,
                         const Frame &a_frame) {
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  const Studio::SignalRow *row = FindByName(recipe.signals, a_signal.name);
  if (!row) {
    return;
  }
  const bool canFire = !row->event.empty();
  const float actionsWidth =
      ResourceActionsWidth() +
      (canFire ? ItemSpacingX() + ButtonWidth("Fire") : 0.0f);
  static_cast<void>(
      Rule(Studio::RuleSpec{.text = "Inspector"}, actionsWidth, [&]() {
        DrawResourceClone(a_frame, a_signal);
        ImGui::SameLine();
        DrawResourceRename(a_frame, row->name, [&](std::string a_name) {
          return Studio::RenameSignal{row->name, std::move(a_name)};
        });
        ImGui::SameLine();
        DrawResourceRemove(row->name, row->references, [&](bool a_cascade) {
          Studio::Post(*a_frame.intents, recipe.id,
                       Studio::RemoveSignal{row->name, a_cascade});
        });
        if (canFire) {
          ImGui::SameLine();
          FirePopup(*row, a_frame);
        }
      }));
  const ResourceCells cells{
      .name = row->name,
      .type = SignalKindName(row->kind),
      .value = row->live ? std::optional<Value>{row->value} : std::nullopt,
      .label = ResourceLabel(a_frame, "resource-header", row->name)};
  if (std::optional<std::string> renamed =
          ResourceTable("signal-header", {&cells, 1})) {
    PostResourceRename(a_frame, a_signal, std::move(*renamed));
  }
  if (!row->problem.empty()) {
    Problem(row->problem);
  }
  DrawFormWithSignals("signal",
                      Studio::SignalForm(*row, Studio::SignalNamesOf(recipe)),
                      a_frame);
  if (const auto *actorValue = Get<ActorValueSignal>(row->definition)) {
    DrawActorValueReadout(a_frame, *actorValue);
  }
  DrawHold(*row, a_frame);
  DrawResponse(*row, a_frame);
}

void DrawMaskInspector(const Studio::MaskSubject &a_mask,
                       const Frame &a_frame) {
  const Studio::TextRow *row =
      FindByName(a_frame.recipe->maskRows, a_mask.name);
  if (!row) {
    return;
  }
  const bool otherDraft = a_frame.state->paint.has_value();
  static_cast<void>(Rule(
      Studio::RuleSpec{.text = "Inspector"},
      ButtonWidth("Advanced Edit") + ItemSpacingX() + ResourceActionsWidth(),
      [&]() {
        Disabled(otherDraft || !a_frame.piece, [&] {
          if (ImGui::Button("Advanced Edit")) {
            EditMaskAsTerms(*row, a_frame, std::nullopt);
          }
        });
        Tooltip(otherDraft       ? "Finish the current mask draft first."
                : !a_frame.piece ? "Select a piece to edit terms."
                                 : "Open the terms editor for this mask.");
        ImGui::SameLine();
        DrawResourceClone(a_frame, a_mask);
        ImGui::SameLine();
        DrawResourceRename(a_frame, row->name, [&](std::string a_name) {
          return Studio::RenameMask{row->name, std::move(a_name)};
        });
        ImGui::SameLine();
        DrawResourceRemove(row->name, row->references, [&](bool a_cascade) {
          Studio::Post(
              *a_frame.intents, a_frame.recipe->id,
              Studio::RemoveMask{.name = row->name, .cascade = a_cascade});
        });
      },
      [&]() {
        const std::optional<std::string> renamed =
            NameLabel(ResourceLabel(a_frame, "resource-header", row->name),
                      [&] { Dim(row->name); });
        if (renamed) {
          PostResourceRename(a_frame, a_mask, *renamed);
        }
      }));

  if (a_frame.state->paint && a_frame.state->mask.editing == row->name) {
    DrawMaskTask(a_frame);
    return;
  }

  DrawRowField("expression", Studio::MaskTextField(row->name, row->text),
               a_frame);
}

void DrawCurveInspector(const Studio::CurveSubject &a_curve,
                        const Frame &a_frame) {
  const Studio::TextRow *row = FindByName(a_frame.recipe->curves, a_curve.name);
  if (!row) {
    return;
  }
  static_cast<void>(Rule(
      Studio::RuleSpec{.text = "Inspector"}, ResourceActionsWidth(), [&]() {
        DrawResourceClone(a_frame, a_curve);
        ImGui::SameLine();
        DrawResourceRename(a_frame, row->name, [&](std::string a_name) {
          return Studio::RenameCurve{row->name, std::move(a_name)};
        });
        ImGui::SameLine();
        DrawResourceRemove(row->name, row->references, [&](bool a_cascade) {
          Studio::Post(*a_frame.intents, a_frame.recipe->id,
                       Studio::RemoveCurve{row->name, a_cascade});
        });
      }));
  const ResourceCells cells{
      .name = row->name,
      .type = {},
      .value = std::nullopt,
      .label = ResourceLabel(a_frame, "resource-header", row->name)};
  if (std::optional<std::string> renamed =
          ResourceTable("curve-header", {&cells, 1})) {
    PostResourceRename(a_frame, a_curve, std::move(*renamed));
  }
  Dim("x is the input value.");
  DrawRowField("expression", Studio::CurveTextField(row->name, row->text),
               a_frame);
}

void DrawSubject(const Frame &a_frame) {
  Match(
      SelectionOf(a_frame).subject,
      [&](const Studio::RecipeSubject &) { DrawRecipeSettings(a_frame); },
      [&](const Studio::ShellSubject &) {
        static_cast<void>(Rule(
            Studio::RuleSpec{.text = "Inspector"}, ButtonWidth("Reset"),
            [&]() {
              if (ImGui::Button("Reset")) {
                Studio::Post(*a_frame.intents, a_frame.recipe->id,
                             Studio::ResetShell{});
              }
            },
            [&]() { Dim("Shell"); }));
        DrawFormWithSignals(
            "shell",
            Studio::ShellForm(a_frame.recipe->shellRow,
                              Studio::SignalNamesOf(*a_frame.recipe)),
            a_frame);
      },
      [&](const Studio::OutputSubject &subject) {
        DrawOutput(subject, a_frame);
      },
      [&](const Studio::LayerSubject &subject) {
        DrawLayerInspector(subject, a_frame);
      },
      [&](const Studio::SourceSubject &subject) {
        DrawSourceInspector(subject, a_frame);
      },
      [&](const Studio::SignalSubject &subject) {
        DrawSignalInspector(subject, a_frame);
      },
      [&](const Studio::MaskSubject &subject) {
        DrawMaskInspector(subject, a_frame);
      },
      [&](const Studio::CurveSubject &subject) {
        DrawCurveInspector(subject, a_frame);
      });
}

void DrawGeometryPicker(const Frame &a_frame) {
  if (!a_frame.recipe || a_frame.recipe->geometries.empty()) {
    return;
  }
  const Studio::Selection &selection = SelectionOf(a_frame);
  const Studio::GeometryChoice choice =
      Studio::GeometryChoiceOf(*a_frame.recipe, selection.geometry);
  ImGui::AlignTextToFramePadding();
  Dim("Geometry");
  ImGui::SameLine();
  const std::optional<SearchPick> picked = SearchCombo(
      {.id = "geometry",
       .preview =
           selection.geometry.empty() ? "geometry" : selection.geometry.c_str(),
       .hint = "Search geometry",
       .width = Studio::Width::Fill()},
      choice.names, choice.selected);
  if (!picked) {
    return;
  }
  const auto *index = Get<std::size_t>(*picked);
  if (!index || *index >= choice.names.size()) {
    return;
  }
  Studio::Post(*a_frame.intents, Studio::ViewGeometry{choice.names[*index]});
}

struct InspectorPass {
  Studio::InspectorSubject before;
  bool editPending = false;
};

enum class InspectorFooter { kNone, kRelationships, kKeys };

[[nodiscard]] InspectorFooter FooterOf(const Frame &a_frame) {
  if (a_frame.recipe &&
      Is<Studio::RecipeSubject>(SelectionOf(a_frame).subject)) {
    return InspectorFooter::kKeys;
  }
  return HasRelationships(a_frame) ? InspectorFooter::kRelationships
                                   : InspectorFooter::kNone;
}

[[nodiscard]] float InspectorFooterHeight(const Frame &a_frame,
                                          InspectorFooter a_footer) {
  switch (a_footer) {
  case InspectorFooter::kKeys:
    return RuleHeight() + ImGui::GetFrameHeightWithSpacing() * 4.0f;
  case InspectorFooter::kRelationships:
    return RelationshipsHeight(a_frame);
  case InspectorFooter::kNone:
    return 0.0f;
  }
  return 0.0f;
}

bool BeginInspectorBody(const Frame &a_frame, InspectorFooter a_footer) {
  const float footer = InspectorFooterHeight(a_frame, a_footer);
  if (a_footer == InspectorFooter::kNone) {
    return ImGui::BeginChild("inspector-body", ImVec2{0.0f, 0.0f}, 0, 0);
  }
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float tallest =
      (std::max)(ImGui::GetFrameHeightWithSpacing(), avail.y - footer);
  ImGui::SetNextWindowSizeConstraints(ImVec2{0.0f, 0.0f},
                                      ImVec2{avail.x, tallest});
  return ImGui::BeginChild("inspector-body", ImVec2{0.0f, 0.0f},
                           ImGuiMCP::ImGuiChildFlags_AutoResizeY, 0);
}

void DrawInspectorBody(const Frame &a_frame, const InspectorPass &a_pass,
                       InspectorFooter a_footer) {
  if (BeginInspectorBody(a_frame, a_footer)) {
    if (a_pass.before != SelectionOf(a_frame).subject) {
      ImGui::SetScrollY(a_frame.state->navigation.scroll);
    }
    ImGui::PushID(InspectorKey(SelectionOf(a_frame).subject).c_str());
    const FieldScope subjectScope(InspectorKey(SelectionOf(a_frame).subject));
    const Studio::InspectorSubject drawn = SelectionOf(a_frame).subject;
    Disabled(a_pass.editPending, [&] { DrawSubject(a_frame); });
    ImGui::PopID();
    if (drawn == SelectionOf(a_frame).subject) {
      a_frame.state->navigation.scroll = ImGui::GetScrollY();
    }
  }
  ImGui::EndChild();
}

void DrawInspectorRelationships(const Frame &a_frame, bool a_editPending) {
  Disabled(a_editPending, [&] { DrawRelationships(a_frame); });
}

void DrawInspectorKeys(const Frame &a_frame, bool a_editPending) {
  static_cast<void>(Rule(Studio::RuleSpec{.text = "Keys"}));
  if (ImGui::BeginChild("inspector-keys", ImVec2{0.0f, 0.0f}, 0, 0)) {
    Disabled(a_editPending, [&] { DrawRecipeKeys(a_frame); });
  }
  ImGui::EndChild();
}

void DrawInspectorFooter(const Frame &a_frame, InspectorFooter a_footer,
                         bool a_editPending) {
  switch (a_footer) {
  case InspectorFooter::kRelationships:
    DrawInspectorRelationships(a_frame, a_editPending);
    return;
  case InspectorFooter::kKeys:
    DrawInspectorKeys(a_frame, a_editPending);
    return;
  case InspectorFooter::kNone:
    return;
  }
}

void DrawInspectorPane(const Frame &a_frame, const InspectorPass &a_pass) {
  if (ImGui::BeginChild("inspector", ImVec2{0.0f, 0.0f}, 0, 0)) {
    const InspectorFooter footer = FooterOf(a_frame);
    DrawInspectorBody(a_frame, a_pass, footer);
    DrawInspectorFooter(a_frame, footer, a_pass.editPending);
  }
  ImGui::EndChild();
}

struct ResourceGroup {
  const char *title;
  std::vector<std::pair<std::string, Studio::InspectorSubject>> entries;
};

std::string OutputTitle(const Studio::OutputRow &a_output) {
  if (a_output.target == Target::kLight) {
    return "Light settings";
  }
  return std::format("{} {}", SurfaceName(a_output.surface),
                     SlotName(a_output.slot));
}

const Studio::OutputRow *SubjectOutput(const Frame &a_frame) {
  const Studio::InspectorSubject &subject = SelectionOf(a_frame).subject;
  std::optional<std::size_t> index;
  if (const auto *output = Get<Studio::OutputSubject>(subject)) {
    index = output->output;
  } else if (const auto *layer = Get<Studio::LayerSubject>(subject)) {
    index = layer->output;
  }
  return index ? FindBy(a_frame.recipe->outputs, *index,
                        &Studio::OutputRow::index)
               : nullptr;
}

std::optional<std::string>
ResourceCrumbText(const Studio::InspectorSubject &a_subject) {
  return Match(
      a_subject,
      [](const Studio::SignalSubject &s) -> std::optional<std::string> {
        return "signal " + s.name;
      },
      [](const Studio::SourceSubject &s) -> std::optional<std::string> {
        return "source " + s.name;
      },
      [](const Studio::MaskSubject &s) -> std::optional<std::string> {
        return "mask " + s.name;
      },
      [](const Studio::CurveSubject &s) -> std::optional<std::string> {
        return "curve " + s.name;
      },
      [](const auto &) -> std::optional<std::string> { return std::nullopt; });
}

bool BeginCrumb(const char *a_id, const std::string &a_preview) {
  NextItemWidth(
      Studio::Width::Px(ButtonWidth(a_preview) + ImGui::GetFrameHeight()));
  return ImGui::BeginCombo(a_id, a_preview.c_str());
}

void CrumbSeparator() {
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  Dim(">");
  ImGui::SameLine();
}

void DrawRecipeCrumb(const Frame &a_frame) {
  if (!BeginCrumb("##recipe-crumb", "Recipe")) {
    return;
  }
  PickSubject("Settings and keys", Studio::RecipeSubject{}, a_frame);
  ImGui::EndCombo();
}

void DrawSurfaceChoices(const Frame &a_frame, Surface a_surface) {
  Dim(SurfaceName(a_surface));
  ImGui::Indent();
  if (a_surface == Surface::kShell) {
    PickSubject("Shell settings", Studio::ShellSubject{}, a_frame);
  }
  for (const Studio::OutputRow &output : a_frame.recipe->outputs) {
    if (IsSurfaceOutput(output, a_surface)) {
      ImGui::PushID(static_cast<int>(output.index));
      PickSubject(std::string{SlotName(output.slot)}.c_str(),
                  Studio::OutputSubject{output.index}, a_frame);
      ImGui::PopID();
    }
  }
  ImGui::Unindent();
}

void DrawOutputChoices(const Frame &a_frame) {
  DrawSurfaceChoices(a_frame, Surface::kMaterial);
  DrawSurfaceChoices(a_frame, Surface::kShell);
  if (a_frame.recipe->lights.empty()) {
    return;
  }
  Dim("Light");
  ImGui::Indent();
  PickSubject("Light settings",
              Studio::OutputSubject{a_frame.recipe->lights.front().output},
              a_frame);
  ImGui::Unindent();
}

void DrawOutputAdds(const Frame &a_frame) {
  ImGui::Separator();
  for (const Surface surface : {Surface::kMaterial, Surface::kShell}) {
    const std::string label = std::format("+ {} output", SurfaceName(surface));
    if (ImGui::BeginMenu(label.c_str())) {
      DrawSlotChoices(a_frame, surface);
      ImGui::EndMenu();
    }
  }
  if (a_frame.recipe->lights.empty() && ImGui::Selectable("+ light")) {
    PostCreation(a_frame, Studio::NewLight{});
  }
}

void DrawOutputCrumb(const Frame &a_frame, const Studio::OutputRow *a_output) {
  const bool shell = Is<Studio::ShellSubject>(SelectionOf(a_frame).subject);
  const std::string preview = a_output ? OutputTitle(*a_output)
                              : shell  ? std::string{"Shell settings"}
                                       : std::string{"Outputs"};
  if (!BeginCrumb("##output-crumb", preview)) {
    return;
  }
  DrawOutputChoices(a_frame);
  DrawOutputAdds(a_frame);
  ImGui::EndCombo();
}

void DrawLayerCrumb(const Frame &a_frame, const Studio::OutputRow &a_output) {
  const auto *layer = Get<Studio::LayerSubject>(SelectionOf(a_frame).subject);
  const std::string preview =
      layer ? std::format("layer {}", layer->layer + 1) : std::string{"Layers"};
  if (!BeginCrumb("##layer-crumb", preview)) {
    return;
  }
  for (std::size_t i = 0; i < a_output.layers.size(); ++i) {
    const Studio::LayerRow &row = a_output.layers[i];
    const std::string label = std::format(
        "{}: {}", i + 1, row.source.empty() ? "Choose a signal" : row.source);
    ImGui::PushID(static_cast<int>(i));
    PickSubject(label.c_str(), Studio::LayerSubject{a_output.index, i},
                a_frame);
    ImGui::PopID();
  }
  ImGui::Separator();
  if (ImGui::Selectable("+ layer")) {
    PostCreation(a_frame, Studio::NewLayer{a_output.index});
  }
  ImGui::EndCombo();
}

std::vector<ResourceGroup> ResourceGroups(const Studio::RecipeRow &a_recipe) {
  std::vector<ResourceGroup> groups{
      {"Signals", {}}, {"Curves", {}}, {"Sources", {}}, {"Masks", {}}};
  for (const Studio::SignalRow &row : a_recipe.signals) {
    groups[0].entries.emplace_back(row.name, Studio::SignalSubject{row.name});
  }
  for (const Studio::TextRow &row : a_recipe.curves) {
    groups[1].entries.emplace_back(row.name, Studio::CurveSubject{row.name});
  }
  for (const Studio::SourceRow &row : a_recipe.sourceRows) {
    groups[2].entries.emplace_back(row.name, Studio::SourceSubject{row.name});
  }
  for (const Studio::TextRow &row : a_recipe.maskRows) {
    groups[3].entries.emplace_back(row.name, Studio::MaskSubject{row.name});
  }
  return groups;
}

void DrawResourceCrumb(const Frame &a_frame) {
  const std::string preview =
      ResourceCrumbText(SelectionOf(a_frame).subject).value_or("Resources");
  RightAligned(ButtonWidth(preview) + ImGui::GetFrameHeight(), [&] {
    if (!BeginCrumb("##resource-crumb", preview)) {
      return;
    }
    const std::string filter{LiveTextField("resource-crumb-search", "Find",
                                           Studio::Width::Px(220.0f),
                                           a_frame.scale)};
    for (const ResourceGroup &group : ResourceGroups(*a_frame.recipe)) {
      if (group.entries.empty()) {
        continue;
      }
      Dim(group.title);
      ImGui::PushID(group.title);
      for (const auto &[name, subject] : group.entries) {
        if (Studio::NameMatches(name, filter)) {
          PickSubject(name.c_str(), subject, a_frame);
        }
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  });
}

void DrawLocationBar(const Frame &a_frame) {
  const Studio::OutputRow *output = SubjectOutput(a_frame);
  DrawRecipeCrumb(a_frame);
  CrumbSeparator();
  DrawOutputCrumb(a_frame, output);
  if (output && output->target != Target::kLight &&
      SlotTakesLayers(output->slot)) {
    CrumbSeparator();
    DrawLayerCrumb(a_frame, *output);
  }
  ImGui::SameLine();
  DrawResourceCrumb(a_frame);
}

void DrawWideWorkspace(const Frame &a_frame, const InspectorPass &a_pass) {
  Studio::MenuState &state = *a_frame.state;
  const auto navigator = [&] {
    if (ImGui::BeginChild("navigator", ImVec2{0.0f, 0.0f}, 0, 0)) {
      DrawNavigator(a_frame);
    }
    ImGui::EndChild();
  };
  const auto editor = [&] { DrawInspectorPane(a_frame, a_pass); };
  if (Studio::MaskTaskActive(state)) {
    editor();
    return;
  }
  if (const auto split =
          Split("workspace", state.navigatorShare, navigator, editor)) {
    Studio::Post(*a_frame.intents, Studio::SetWorkspaceSplit{*split});
  }
}

void DrawNarrowWorkspace(const Frame &a_frame, const InspectorPass &a_pass) {
  if (!Studio::MaskTaskActive(*a_frame.state)) {
    DrawLocationBar(a_frame);
  }
  DrawInspectorPane(a_frame, a_pass);
}

}

void DrawWorkspace(const Frame &a_input) {
  Frame a_frame = a_input;
  if (!a_frame.recipe || !a_frame.state || !a_frame.intents || !a_frame.names) {
    Dim("Select a recipe to inspect its effect.");
    return;
  }
  InspectorPass pass{.before = SelectionOf(a_frame).subject};
  ImGui::PushID(a_frame.recipe->id.c_str());
  const FieldScope recipeScope(a_frame.recipe->id);
  pass.editPending = Studio::IndexedEditPendingFor(
      a_frame.state->pendingIndexedEdit, a_frame.recipe->id);
  a_frame.geometry =
      Studio::SelectedGeometry(a_frame.recipe, SelectionOf(a_frame));
  const Studio::Names names =
      a_frame.geometry ? Studio::NamesOf(*a_frame.recipe, *a_frame.geometry)
                       : *a_input.names;
  a_frame.names = &names;
  if (!Studio::MaskTaskActive(*a_frame.state)) {
    DrawGeometryPicker(a_frame);
  }
  if (ImGui::GetContentRegionAvail().x >= 1000.0f * a_frame.scale) {
    DrawWideWorkspace(a_frame, pass);
  } else {
    DrawNarrowWorkspace(a_frame, pass);
  }
  ImGui::PopID();
}
}
