#include "menu/Workspace.h"

#include "menu/ContextRows.h"
#include "menu/FormDraw.h"
#include "menu/InputBrowser.h"
#include "menu/MenuWidgets.h"
#include "menu/PaintPanel.h"
#include "menu/RelationshipPanel.h"
#include "menu/ResourcePanels.h"
#include "menu/ResponsePanel.h"
#include "menu/StackPanel.h"
#include "studio/EditResult.h"
#include "studio/FieldParsing.h"
#include "studio/Forms.h"
#include "studio/Names.h"
#include "studio/Navigation.h"
#include "studio/Panels.h"

#include <algorithm>
#include <format>
#include <string>
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
                 const Frame &a_frame, bool a_spanColumns = false) {
  const bool blocked =
      Studio::IndexedEditPendingFor(a_frame.state->pendingIndexedEdit,
                                    a_frame.recipe->id) &&
      (Is<Studio::OutputSubject>(a_subject) ||
       Is<Studio::LayerSubject>(a_subject));
  const int flags =
      a_spanColumns ? ImGuiMCP::ImGuiSelectableFlags_SpanAllColumns : 0;
  ImGui::BeginDisabled(blocked);
  if (ImGui::Selectable(a_label, SelectionOf(a_frame).subject == a_subject,
                        flags)) {
    NavigateFromNavigator(a_frame, std::move(a_subject));
  }
  ImGui::EndDisabled();
}

void DrawSurfaceAdd(const Frame &a_frame, Surface a_surface) {
  ImGui::PushID(static_cast<int>(a_surface));
  if (ImGui::Button("+ output")) {
    ImGui::OpenPopup("add-output");
  }
  if (ImGui::BeginPopup("add-output")) {
    for (const Slot slot : SlotsOf(a_surface, a_frame.recipe->shellMaterial)) {
      if (ImGui::Selectable(std::string{SlotName(slot)}.c_str())) {
        Studio::Post(*a_frame.intents, a_frame.recipe->id,
                     Studio::AddOutput{a_surface, slot, {}});
        ImGui::CloseCurrentPopup();
      }
    }
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
  if (RemoveButton(0)) {
    Studio::Post(*a_frame.intents, id,
                 Studio::RemoveLayer{a_output.index, a_layer});
  }
  ImGui::SameLine();
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
  Badge(layer.source.starts_with('@') ? Studio::FieldKind::kReference
                                      : Studio::FieldKind::kColor);
  ImGui::SameLine();
  const std::string label = std::format("{}: {}", a_layer + 1, layer.source);
  PickSubject(label.c_str(), Studio::LayerSubject{a_output.index, a_layer},
              a_frame);
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
  ImGui::PushID(static_cast<int>(a_output.index));
  PickSubject(title.c_str(), Studio::OutputSubject{a_output.index}, a_frame);
  ImGui::Indent();
  for (std::size_t i = 0; i < a_output.layers.size(); ++i) {
    if (!outputMatches &&
        !Studio::NameMatches(a_output.layers[i].source, a_filter) &&
        !Studio::NameMatches(a_output.layers[i].mask, a_filter)) {
      continue;
    }
    DrawOutputLayerRow(a_frame, a_output, i);
  }
  if (a_output.target != Target::kLight) {
    ImGui::PushID("add");
    if (ImGui::Button("+ layer")) {
      Studio::Post(*a_frame.intents, a_frame.recipe->id,
                   Studio::AddLayer{a_output.index, Studio::DefaultLayer(),
                                    a_output.layers.size()});
    }
    ImGui::PopID();
  }
  ImGui::Unindent();
  ImGui::PopID();
}

void ResourceRow(Table &a_table, const Frame &a_frame,
                 const std::string &a_name, Studio::InspectorSubject a_subject,
                 std::string_view a_type, const std::optional<Value> &a_value,
                 std::size_t a_references) {
  a_table.Cell();
  PickSubject(a_name.c_str(), std::move(a_subject), a_frame, true);
  a_table.Cell();
  if (!a_type.empty()) {
    ImGui::AlignTextToFramePadding();
    Dim(a_type);
  }
  a_table.Cell();
  if (a_value) {
    ValueSwatch(*a_value);
  }
  a_table.Cell();
  ImGui::AlignTextToFramePadding();
  Dim(std::format("used {}", a_references));
}

void DrawResourceRows(const Frame &a_frame, Studio::ResourceTab a_tab,
                      std::string_view a_filter) {
  ImGui::PushID(static_cast<int>(a_tab));
  Table table = Table::Begin("resource-list",
                             {{"name", Studio::Width::Fill()},
                              {"type", Studio::Width::Fit()},
                              {"value", Studio::Width::Fit()},
                              {"used", Studio::Width::Fit()}},
                             Studio::kColumnsTable);
  if (!table.Open()) {
    ImGui::PopID();
    return;
  }
  switch (a_tab) {
  case Studio::ResourceTab::kSignals:
    for (const Studio::SignalRow &signal : a_frame.recipe->signals) {
      if (!Studio::NameMatches(signal.name, a_filter)) {
        continue;
      }
      ResourceRow(
          table, a_frame, signal.name, Studio::SignalSubject{signal.name},
          SignalKindName(signal.kind),
          signal.live ? std::optional<Value>{signal.value} : std::nullopt,
          signal.references);
    }
    break;
  case Studio::ResourceTab::kCurves:
    for (const Studio::TextRow &curve : a_frame.recipe->curves) {
      if (!Studio::NameMatches(curve.name, a_filter)) {
        continue;
      }
      ResourceRow(table, a_frame, curve.name, Studio::CurveSubject{curve.name},
                  {}, std::nullopt, curve.references);
    }
    break;
  case Studio::ResourceTab::kSources:
    for (const Studio::SourceRow &source : a_frame.recipe->sourceRows) {
      if (!Studio::NameMatches(source.name, a_filter)) {
        continue;
      }
      ResourceRow(table, a_frame, source.name,
                  Studio::SourceSubject{source.name},
                  DescribeSource(Studio::SourceKindOf(source).value_or(
                      SourceKind{MaterialSource{}})),
                  std::nullopt, source.references);
    }
    break;
  case Studio::ResourceTab::kMasks:
    for (const Studio::TextRow &mask : a_frame.recipe->maskRows) {
      if (!Studio::NameMatches(mask.name, a_filter)) {
        continue;
      }
      ResourceRow(table, a_frame, mask.name, Studio::MaskSubject{mask.name}, {},
                  std::nullopt, mask.references);
    }
    break;
  }
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

void DrawResourceAddRow(const Frame &a_frame, Studio::ResourceTab a_tab) {
  const char *add = ResourceAddLabel(a_tab);
  if (a_tab == Studio::ResourceTab::kSignals) {
    RightAligned(ButtonWidth(add) + ItemSpacingX() + ButtonWidth("New input"),
                 [&]() {
                   if (ImGui::Button(add)) {
                     PostResourceAdd(a_frame, a_tab);
                   }
                   ImGui::SameLine();
                   DrawSignalWizardButton(a_frame);
                 });
    return;
  }
  RightAligned(ButtonWidth(add), [&]() {
    if (ImGui::Button(add)) {
      PostResourceAdd(a_frame, a_tab);
    }
  });
}

void DrawResourceTabs(const Frame &a_frame, std::string_view a_filter) {
  if (!a_filter.empty()) {
    if (ImGui::BeginChild("resource-rows", ImVec2{0.0f, 0.0f}, 0, 0)) {
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
    DrawResourceAddRow(a_frame, tab);
    if (ImGui::BeginChild("resource-rows", ImVec2{0.0f, 0.0f}, 0, 0)) {
      DrawResourceRows(a_frame, tab, a_filter);
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
}

void DrawSurfaceOutputs(const Frame &a_frame, Surface a_surface,
                        std::string_view a_filter) {
  ImGui::Indent();
  for (const Studio::OutputRow &output : a_frame.recipe->outputs) {
    if (output.target != Target::kLight && output.surface == a_surface) {
      DrawOutputNode(a_frame, output, a_filter);
    }
  }
  ImGui::Unindent();
}

bool SurfaceHasOutput(const Studio::RecipeRow &a_recipe, Surface a_surface) {
  return std::ranges::any_of(a_recipe.outputs,
                             [&](const Studio::OutputRow &a_output) {
                               return a_output.target != Target::kLight &&
                                      a_output.surface == a_surface;
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

void DrawRecipeTree(const Frame &a_frame, std::string_view a_filter) {
  PickSubject("Recipe / overview", Studio::RecipeSubject{}, a_frame);

  SurfaceLabel("Material",
               SurfaceHasOutput(*a_frame.recipe, Surface::kMaterial));
  ImGui::SameLine();
  RightAligned(ButtonWidth("+ output"),
               [&]() { DrawSurfaceAdd(a_frame, Surface::kMaterial); });
  DrawSurfaceOutputs(a_frame, Surface::kMaterial, a_filter);

  SurfaceLabel("Shell", SurfaceHasOutput(*a_frame.recipe, Surface::kShell));
  ImGui::SameLine();
  RightAligned(ButtonWidth("settings") + ItemSpacingX() +
                   ButtonWidth("+ output"),
               [&]() {
                 DrawShellSettings(a_frame);
                 ImGui::SameLine();
                 DrawSurfaceAdd(a_frame, Surface::kShell);
               });
  DrawSurfaceOutputs(a_frame, Surface::kShell, a_filter);

  const bool hasLight = !a_frame.recipe->lights.empty();
  SurfaceLabel("Light", hasLight);
  ImGui::SameLine();
  if (hasLight) {
    RightAligned(ButtonWidth("settings"),
                 [&]() { DrawLightSettings(a_frame); });
  } else {
    RightAligned(ButtonWidth("+ output"), [&]() {
      if (ImGui::Button("+ output")) {
        Studio::Post(*a_frame.intents, a_frame.recipe->id, Studio::AddLight{});
        a_frame.state->pendingSelection =
            Studio::OutputSubject{a_frame.recipe->outputs.size()};
      }
    });
  }
}

void DrawNavigator(const Frame &a_frame) {
  const std::string_view filter =
      LiveTextField("navigator-search", "Find outputs and resources",
                    Studio::Width::Fill(), a_frame.scale);
  if (!ImGui::BeginTabBar("navigator")) {
    return;
  }
  if (ImGui::BeginTabItem("Recipe")) {
    if (ImGui::BeginChild("recipe-scroll", ImVec2{0.0f, 0.0f}, 0, 0)) {
      DrawRecipeTree(a_frame, filter);
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem("Resources")) {
    DrawResourceTabs(a_frame, filter);
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
}

[[nodiscard]] std::optional<Studio::GeometryRow>
OutputGeometry(const Frame &a_frame, std::size_t a_output) {
  if (!a_frame.geometry) {
    return std::nullopt;
  }
  const auto found = std::ranges::find(a_frame.geometry->outputs, a_output,
                                       &Studio::OutputRow::index);
  if (found == a_frame.geometry->outputs.end()) {
    return std::nullopt;
  }
  Studio::GeometryRow geometry = *a_frame.geometry;
  geometry.outputs = {*found};
  return geometry;
}

void DrawOutput(const Studio::OutputSubject &a_subject, const Frame &a_frame) {
  const auto output = std::ranges::find(
      a_frame.recipe->outputs, a_subject.output, &Studio::OutputRow::index);
  if (output == a_frame.recipe->outputs.end()) {
    return;
  }
  if (output->target == Target::kLight) {
    const auto light = std::ranges::find(
        a_frame.recipe->lights, a_subject.output, &Studio::LightRow::output);
    if (light == a_frame.recipe->lights.end()) {
      Dim("The light definition is unavailable.");
      return;
    }
    static_cast<void>(Rule(
        Studio::RuleSpec{.text = "Inspector"}, RowButtonWidth(),
        [&]() {
          if (RemoveButton(0)) {
            Studio::Post(*a_frame.intents, a_frame.recipe->id,
                         Studio::RemoveOutput{light->output});
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
      unavailable = "This output is not applied to the viewed geometry.";
    }
    stack = Studio::BuildStackView(*a_frame.recipe, SelectionOf(a_frame),
                                   ViewOf(a_frame));
    inspector = Studio::BuildInspector(*a_frame.recipe, SelectionOf(a_frame));
  }
  const std::vector<Studio::FormField> scalars =
      stack ? Studio::ScalarForm(*stack) : std::vector<Studio::FormField>{};
  DrawOutputHeader(*output, scalars, drawFrame);
  if (!unavailable.empty()) {
    Dim(unavailable);
  }
  DrawStack(stack, inspector, drawFrame);
}

void DrawLayerInspector(const Studio::LayerSubject &a_layer,
                        const Frame &a_frame) {
  const auto owner = std::ranges::find(a_frame.recipe->outputs, a_layer.output,
                                       &Studio::OutputRow::index);
  const std::string label =
      owner != a_frame.recipe->outputs.end()
          ? std::format("{} / {} / layer {}", SurfaceName(owner->surface),
                        SlotName(owner->slot), a_layer.layer + 1)
          : std::format("layer {}", a_layer.layer + 1);
  static_cast<void>(Rule(
      Studio::RuleSpec{.text = "Inspector"}, RowButtonWidth(),
      [&]() {
        if (RemoveButton(0)) {
          Studio::Post(*a_frame.intents, a_frame.recipe->id,
                       Studio::RemoveLayer{a_layer.output, a_layer.layer});
        }
      },
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

void DrawSourceInspector(const Studio::SourceSubject &a_source,
                         const Frame &a_frame) {
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  const auto row = std::ranges::find(recipe.sourceRows, a_source.name,
                                     &Studio::SourceRow::name);
  if (row == recipe.sourceRows.end()) {
    return;
  }
  static_cast<void>(
      Rule(Studio::RuleSpec{.text = "Inspector"}, RowButtonWidth(), [&]() {
        if (RemoveButton(row->references)) {
          Studio::Post(*a_frame.intents, recipe.id,
                       Studio::RemoveSource{row->name});
        }
      }));
  const std::string type = DescribeSource(
      Studio::SourceKindOf(*row).value_or(SourceKind{MaterialSource{}}));
  const ResourceCells cells{.name = row->name, .type = type};
  ResourceTable("source-header", {&cells, 1});
  DrawFormWithSignals("source",
                      Studio::SourceForm(*row, Studio::SignalNamesOf(recipe)),
                      a_frame);
}

void DrawSignalInspector(const Studio::SignalSubject &a_signal,
                         const Frame &a_frame) {
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  const auto row = std::ranges::find(recipe.signals, a_signal.name,
                                     &Studio::SignalRow::name);
  if (row == recipe.signals.end()) {
    return;
  }
  const bool canFire = !row->event.empty();
  const float actionsWidth =
      RowButtonWidth() +
      (canFire ? ItemSpacingX() + ButtonWidth("Fire") : 0.0f);
  static_cast<void>(
      Rule(Studio::RuleSpec{.text = "Inspector"}, actionsWidth, [&]() {
        if (RemoveButton(row->references)) {
          Studio::Post(*a_frame.intents, recipe.id,
                       Studio::RemoveSignal{row->name});
        }
        if (canFire) {
          ImGui::SameLine();
          FirePopup(*row, a_frame);
        }
      }));
  const ResourceCells cells{
      .name = row->name,
      .type = SignalKindName(row->kind),
      .value = row->live ? std::optional<Value>{row->value} : std::nullopt};
  ResourceTable("signal-header", {&cells, 1});
  if (!row->problem.empty()) {
    Problem(row->problem);
  }
  DrawFormWithSignals("signal",
                      Studio::SignalForm(*row, Studio::SignalNamesOf(recipe)),
                      a_frame);
  DrawResponse(*row, a_frame);
}

void DrawMaskInspector(const Studio::MaskSubject &a_mask,
                       const Frame &a_frame) {
  const auto &masks = a_frame.recipe->maskRows;
  const auto row =
      std::ranges::find(masks, a_mask.name, &Studio::TextRow::name);
  if (row == masks.end()) {
    return;
  }
  static_cast<void>(
      Rule(Studio::RuleSpec{.text = "Inspector"}, RowButtonWidth(), [&]() {
        if (RemoveButton(row->references)) {
          Studio::Post(*a_frame.intents, a_frame.recipe->id,
                       Studio::RemoveMask{row->name});
        }
      }));
  const ResourceCells cells{.name = row->name};
  ResourceTable("mask-header", {&cells, 1});

  if (a_frame.state->paint && a_frame.state->mask.editing == row->name) {
    if (Studio::MaskTaskActive(*a_frame.state)) {
      DrawMaskTask(a_frame);
    } else {
      DrawRowField("expression", Studio::MaskTextField(row->name, row->text),
                   a_frame);
      if (ImGui::Button("Resume terms editor")) {
        Studio::Post(*a_frame.intents, Studio::SetMode{Studio::Mode::kPaint});
      }
    }
    return;
  }

  DrawRowField("expression", Studio::MaskTextField(row->name, row->text),
               a_frame);
  const bool otherDraft = a_frame.state->paint.has_value();
  Disabled(otherDraft || !a_frame.piece, [&] {
    if (ImGui::Button("Open terms editor...")) {
      EditMaskAsTerms(*row, a_frame);
    }
  });
  if (otherDraft) {
    Dim("Finish or discard the current mask draft first.");
  }
}

void DrawCurveInspector(const Studio::CurveSubject &a_curve,
                        const Frame &a_frame) {
  const auto &curves = a_frame.recipe->curves;
  const auto row =
      std::ranges::find(curves, a_curve.name, &Studio::TextRow::name);
  if (row == curves.end()) {
    return;
  }
  static_cast<void>(
      Rule(Studio::RuleSpec{.text = "Inspector"}, RowButtonWidth(), [&]() {
        if (RemoveButton(row->references)) {
          Studio::Post(*a_frame.intents, a_frame.recipe->id,
                       Studio::RemoveCurve{row->name});
        }
      }));
  const ResourceCells cells{.name = row->name};
  ResourceTable("curve-header", {&cells, 1});
  Dim("x is the input value.");
  DrawRowField("expression", Studio::CurveTextField(row->name, row->text),
               a_frame);
}

void DrawSubject(const Frame &a_frame) {
  Match(
      SelectionOf(a_frame).subject,
      [&](const Studio::RecipeSubject &) { DrawRecipeSettings(a_frame); },
      [&](const Studio::ShellSubject &) {
        static_cast<void>(Rule(Studio::RuleSpec{.text = "Inspector"}, 0.0f, {},
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

[[nodiscard]] const Studio::OutputRow *
PreviewOutput(const Frame &a_frame, const Studio::Selection &a_selection) {
  const Studio::InspectorSubject &subject = a_selection.subject;
  std::optional<std::size_t> index;
  if (const auto *output = Get<Studio::OutputSubject>(subject)) {
    index = output->output;
  } else if (const auto *layer = Get<Studio::LayerSubject>(subject)) {
    index = layer->output;
  }
  if (!index) {
    return Studio::SelectedOutput(a_frame.geometry, a_selection);
  }
  const auto found = std::ranges::find(a_frame.geometry->outputs, *index,
                                       &Studio::OutputRow::index);
  return found == a_frame.geometry->outputs.end() ? nullptr : &*found;
}

void DrawPreview(const Frame &a_input) {
  Studio::MenuState &state = *a_input.state;
  DrawTermTuningPane(a_input);
  Studio::ResolvePreviewPin(state.previewPin, state.selection, a_input.recipe,
                            state.lastPaintReset);
  if (state.previewPin) {
    if (ImGui::SmallButton("Unpin preview")) {
      state.previewPin.reset();
    }
  } else {
    Disabled(!a_input.geometry, [&] {
      if (ImGui::SmallButton("Pin preview")) {
        Studio::Selection selected = state.selection;
        if (!Is<Studio::SourceSubject>(selected.subject) &&
            !Is<Studio::MaskSubject>(selected.subject)) {
          const Studio::OutputRow *output = PreviewOutput(a_input, selected);
          if (!output) {
            return;
          }
          selected.subject = Studio::OutputSubject{output->index};
        }
        state.previewPin = Studio::PreviewPin{selected, state.lastPaintReset};
      }
    });
  }
  const Studio::Selection selection =
      state.previewPin ? state.previewPin->selection : state.selection;
  Frame a_frame = a_input;
  a_frame.geometry = Studio::SelectedGeometry(a_frame.recipe, selection);
  Dim(state.previewPin ? "Pinned texture preview" : "Texture preview");
  std::vector<std::string> geometries;
  geometries.reserve(a_frame.recipe->geometries.size());
  for (const Studio::GeometryRow &geometry : a_frame.recipe->geometries) {
    geometries.push_back(geometry.name);
  }
  if (const auto picked =
          ChoiceCombo("geometry", selection.geometry, geometries,
                      {Studio::Width::Fill(), a_frame.scale})) {
    if (state.previewPin) {
      state.previewPin->selection.geometry = *picked;
    } else {
      Studio::Post(*a_frame.intents, Studio::ViewGeometry{*picked});
    }
  }
  if (!a_frame.geometry) {
    Dim("No applied geometry. No live preview.");
    return;
  }
  const Studio::InspectorSubject &subject = selection.subject;
  const Studio::PictureRow *picture = nullptr;
  if (const auto *source = Get<Studio::SourceSubject>(subject)) {
    const auto row = std::ranges::find(a_frame.geometry->sources, source->name,
                                       &Studio::PictureRow::name);
    if (row != a_frame.geometry->sources.end()) {
      picture = &*row;
    }
  } else if (const auto *mask = Get<Studio::MaskSubject>(subject)) {
    const auto row = std::ranges::find(a_frame.geometry->masks, mask->name,
                                       &Studio::PictureRow::name);
    if (row != a_frame.geometry->masks.end()) {
      picture = &*row;
    }
  }
  const float side =
      (std::max)(32.0f, (std::min)(ImGui::GetContentRegionAvail().x,
                                   320.0f * a_frame.scale));
  if (picture) {
    Dim(picture->name);
    Thumbnail({picture->texture, picture->channel, picture->animated, side});
    if (!picture->problem.empty()) {
      Problem(picture->problem);
    }
  } else if (Is<Studio::SourceSubject>(subject) ||
             Is<Studio::MaskSubject>(subject)) {
    Dim("This resource has no live preview on the viewed geometry.");
  } else if (const Studio::OutputRow *output =
                 PreviewOutput(a_frame, selection)) {
    Dim(std::format("{} / {} composite", SurfaceName(output->surface),
                    SlotName(output->slot)));
    Thumbnail({output->texture, ShaderChannel::kRgb, output->animated, side});
    if (!output->problem.empty()) {
      Problem(output->problem);
    }
  } else {
    Dim("Select an output, source, or mask to inspect its texture.");
  }
}

void DrawInspectorPane(const Frame &a_frame,
                       const Studio::InspectorSubject &a_before,
                       bool a_pending) {
  if (ImGui::BeginChild("inspector", ImVec2{0.0f, 0.0f}, 0, 0)) {
    if (a_before != SelectionOf(a_frame).subject) {
      ImGui::SetScrollY(a_frame.state->navigation.scroll);
    }
    ImGui::PushID(InspectorKey(SelectionOf(a_frame).subject).c_str());
    const FieldScope subjectScope(InspectorKey(SelectionOf(a_frame).subject));
    const Studio::InspectorSubject drawn = SelectionOf(a_frame).subject;
    Disabled(a_pending, [&] {
      DrawSubject(a_frame);
      DrawRelationships(a_frame);
    });
    ImGui::PopID();
    if (drawn == SelectionOf(a_frame).subject) {
      a_frame.state->navigation.scroll = ImGui::GetScrollY();
    }
  }
  ImGui::EndChild();
}

void DrawPreviewPane(const Frame &a_frame) {
  if (ImGui::BeginChild("preview", ImVec2{0.0f, 0.0f}, 0, 0)) {
    DrawPreview(a_frame);
  }
  ImGui::EndChild();
}

void DrawWideWorkspace(const Frame &a_frame,
                       const Studio::InspectorSubject &a_before,
                       bool a_pending) {
  Studio::MenuState &state = *a_frame.state;
  const auto navigator = [&] {
    if (ImGui::BeginChild("navigator", ImVec2{0.0f, 0.0f}, 0, 0)) {
      DrawNavigator(a_frame);
    }
    ImGui::EndChild();
  };
  const auto editor = [&] {
    if (const auto split = Split(
            "preview-split", state.inspectorShare,
            [&] { DrawInspectorPane(a_frame, a_before, a_pending); },
            [&] { DrawPreviewPane(a_frame); })) {
      Studio::Post(
          *a_frame.intents,
          Studio::SetWorkspaceSplit{Studio::WorkspacePane::kInspector, *split});
    }
  };
  if (const auto split =
          Split("workspace", state.navigatorShare, navigator, editor)) {
    Studio::Post(
        *a_frame.intents,
        Studio::SetWorkspaceSplit{Studio::WorkspacePane::kNavigator, *split});
  }
}

void DrawNarrowWorkspace(const Frame &a_frame,
                         const Studio::InspectorSubject &a_before,
                         bool a_pending) {
  if (ImGui::Button("Browse outputs and resources")) {
    ImGui::OpenPopup("workspace-navigator");
  }
  DetailModal("workspace-navigator", [&] { DrawNavigator(a_frame); });
  ImGui::SameLine();
  if (ImGui::Button("Preview")) {
    ImGui::OpenPopup("workspace-preview");
  }
  DetailModal("workspace-preview", [&] { DrawPreview(a_frame); });
  DrawInspectorPane(a_frame, a_before, a_pending);
}

}

void DrawWorkspace(const Frame &a_input) {
  Frame a_frame = a_input;
  if (!a_frame.recipe || !a_frame.state || !a_frame.intents || !a_frame.names) {
    Dim("Select a recipe to inspect its effect.");
    return;
  }
  const Studio::InspectorSubject before = SelectionOf(a_frame).subject;
  ImGui::PushID(a_frame.recipe->id.c_str());
  const FieldScope recipeScope(a_frame.recipe->id);
  const bool pending = Studio::IndexedEditPendingFor(
      a_frame.state->pendingIndexedEdit, a_frame.recipe->id);
  a_frame.geometry =
      Studio::SelectedGeometry(a_frame.recipe, SelectionOf(a_frame));
  const Studio::Names names =
      a_frame.geometry ? Studio::NamesOf(*a_frame.recipe, *a_frame.geometry)
                       : *a_input.names;
  a_frame.names = &names;
  if (ImGui::GetContentRegionAvail().x >= 1000.0f * a_frame.scale) {
    DrawWideWorkspace(a_frame, before, pending);
  } else {
    DrawNarrowWorkspace(a_frame, before, pending);
  }
  ImGui::PopID();
}
}
