#include "menu/Workspace.h"

#include "menu/BoardPage.h"
#include "menu/ContextRows.h"
#include "menu/FormDraw.h"
#include "menu/MenuWidgets.h"
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

void PickSubject(const char *a_label, Studio::InspectorSubject a_subject,
                 const Frame &a_frame) {
  const bool blocked =
      Studio::IndexedEditPendingFor(a_frame.state->pendingIndexedEdit,
                                    a_frame.recipe->id) &&
      (Is<Studio::OutputSubject>(a_subject) ||
       Is<Studio::LayerSubject>(a_subject));
  ImGui::BeginDisabled(blocked);
  if (ImGui::Selectable(a_label, SelectionOf(a_frame).subject == a_subject)) {
    [[maybe_unused]] const bool changed =
        Studio::Navigate(a_frame.state->navigation, a_frame.state->selection,
                         std::move(a_subject), *a_frame.recipe);
  }
  ImGui::EndDisabled();
}

void DrawNavigator(const Frame &a_frame) {
  PickSubject("Recipe / overview", Studio::RecipeSubject{}, a_frame);
  PickSubject("Shell settings", Studio::ShellSubject{}, a_frame);
  if (Section("Outputs", true)) {
    for (const Studio::OutputRow &output : a_frame.recipe->outputs) {
      ImGui::PushID(static_cast<int>(output.index));
      const std::string title =
          output.target == Target::kLight
              ? "Light"
              : std::format("{} / {}", SurfaceName(output.surface),
                            SlotName(output.slot));
      PickSubject(title.c_str(), Studio::OutputSubject{output.index}, a_frame);
      ImGui::Indent();
      for (std::size_t i = 0; i < output.layers.size(); ++i) {
        const std::string label =
            std::format("{}: {}", i + 1, output.layers[i].source);
        ImGui::PushID(static_cast<int>(i));
        PickSubject(label.c_str(), Studio::LayerSubject{output.index, i},
                    a_frame);
        ImGui::PopID();
      }
      ImGui::Unindent();
      ImGui::PopID();
    }
  }
  if (Section("Sources", true)) {
    for (const Studio::SourceRow &source : a_frame.recipe->sourceRows) {
      ImGui::PushID("source");
      PickSubject(source.name.c_str(), Studio::SourceSubject{source.name},
                  a_frame);
      ImGui::PopID();
    }
  }
  if (Section("Masks", true)) {
    for (const Studio::TextRow &mask : a_frame.recipe->maskRows) {
      ImGui::PushID("mask");
      PickSubject(mask.name.c_str(), Studio::MaskSubject{mask.name}, a_frame);
      ImGui::PopID();
    }
  }
  if (Section("Signals", true)) {
    for (const Studio::SignalRow &signal : a_frame.recipe->signals) {
      ImGui::PushID("signal");
      PickSubject(signal.name.c_str(), Studio::SignalSubject{signal.name},
                  a_frame);
      ImGui::PopID();
    }
  }
  if (Section("Curves", true)) {
    for (const Studio::TextRow &curve : a_frame.recipe->curves) {
      ImGui::PushID("curve");
      PickSubject(curve.name.c_str(), Studio::CurveSubject{curve.name},
                  a_frame);
      ImGui::PopID();
    }
  }
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
    if (!a_frame.recipe->lightRow.present ||
        a_frame.recipe->lightRow.output != a_subject.output) {
      Dim("Details for this light are not available in this framework "
          "checkpoint.");
      return;
    }
    DrawFormWithSignals(
        "light",
        Studio::LightForm(a_frame.recipe->lightRow,
                          Studio::SignalNamesOf(*a_frame.recipe)),
        a_frame);
    return;
  }
  DrawOutputHeader(output->index, output->replace, output->selection, a_frame);
  if (!a_frame.geometry || !a_frame.piece) {
    Dim("No applied geometry is available for this output.");
    return;
  }
  const auto geometry = OutputGeometry(a_frame, a_subject.output);
  if (!geometry) {
    Dim("This output is not applied to the viewed geometry.");
    return;
  }
  Frame exact = a_frame;
  exact.geometry = &*geometry;
  const auto stack =
      Studio::BuildStackView({*exact.piece, *exact.recipe, *exact.geometry,
                              SelectionOf(exact), ViewOf(exact)});
  const auto inspector = Studio::BuildInspector(*exact.recipe, *exact.geometry,
                                                SelectionOf(exact));
  DrawStack(stack, inspector, exact);
}

void DrawLayerFields(const Studio::Inspector &a_inspector,
                     const Frame &a_frame) {
  const std::vector<Studio::FormField> form =
      Studio::InspectorForm(a_inspector);
  const auto opened = DrawForm("layer", form, a_frame);
  if (!opened || *opened >= form.size() || !form[*opened].detail) {
    return;
  }
  std::optional<Studio::InspectorSubject> destination;
  switch (*form[*opened].detail) {
  case Studio::FieldDetail::kSource:
    if (a_inspector.source) {
      const std::string &name = a_inspector.source->name;
      if (std::ranges::find(a_frame.recipe->masks, name) !=
          a_frame.recipe->masks.end()) {
        destination = Studio::MaskSubject{name};
      } else {
        destination = Studio::SourceSubject{name};
      }
    }
    break;
  case Studio::FieldDetail::kMask:
    if (a_inspector.mask) {
      destination = Studio::MaskSubject{a_inspector.mask->name};
    }
    break;
  case Studio::FieldDetail::kCurve:
    if (a_inspector.curve) {
      destination = Studio::CurveSubject{a_inspector.curve->name};
    }
    break;
  case Studio::FieldDetail::kOpacity:
  case Studio::FieldDetail::kColor:
  case Studio::FieldDetail::kSignal:
    if (Studio::IsWholeReference(form[*opened].text)) {
      destination =
          Studio::SignalSubject{Studio::ReferenceName(form[*opened].text)};
    }
    break;
  }
  if (destination) {
    a_frame.state->navigation.scroll = ImGui::GetScrollY();
    [[maybe_unused]] const bool changed =
        Studio::Navigate(a_frame.state->navigation, a_frame.state->selection,
                         std::move(*destination), *a_frame.recipe);
    if (changed) {
      ImGui::SetScrollY(a_frame.state->navigation.scroll);
    }
  }
}

void DrawLayerInspector(const Studio::LayerSubject &a_layer,
                        const Frame &a_frame) {
  Dim(std::format("Output {} / layer {}", a_layer.output + 1,
                  a_layer.layer + 1));
  const auto geometry = OutputGeometry(a_frame, a_layer.output);
  if (!geometry) {
    Dim("No applied geometry is available for this layer.");
    return;
  }
  if (const auto inspector = Studio::BuildInspector(*a_frame.recipe, *geometry,
                                                    SelectionOf(a_frame))) {
    DrawLayerFields(*inspector, a_frame);
  }
}

void DrawSourceInspector(const Studio::SourceSubject &a_source,
                         const Frame &a_frame) {
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  const auto row = std::ranges::find(recipe.sourceRows, a_source.name,
                                     &Studio::SourceRow::name);
  if (row == recipe.sourceRows.end()) {
    return;
  }
  Dim("Source: " + row->name);
  Dim(std::format("Used {} time(s)", row->references));
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
  Dim("Signal: " + row->name);
  ValueSwatch(row->value);
  Dim(std::format("Used {} time(s)", row->references));
  if (!row->problem.empty()) {
    Problem(row->problem);
  }
  if (!row->event.empty()) {
    FirePopup(*row, a_frame);
  }
  DrawFormWithSignals("signal",
                      Studio::SignalForm(*row, Studio::SignalNamesOf(recipe)),
                      a_frame);
}

void DrawMaskInspector(const Studio::MaskSubject &a_mask,
                       const Frame &a_frame) {
  const auto &masks = a_frame.recipe->maskRows;
  const auto row =
      std::ranges::find(masks, a_mask.name, &Studio::TextRow::name);
  if (row == masks.end()) {
    return;
  }
  Dim("Mask: " + row->name);
  Dim(std::format("Used {} time(s)", row->references));
  DrawRowField("expression", Studio::MaskTextField(row->name, row->text),
               a_frame);
}

void DrawCurveInspector(const Studio::CurveSubject &a_curve,
                        const Frame &a_frame) {
  const auto &curves = a_frame.recipe->curves;
  const auto row =
      std::ranges::find(curves, a_curve.name, &Studio::TextRow::name);
  if (row == curves.end()) {
    return;
  }
  Dim("Curve: " + row->name);
  Dim("x is the input value.");
  Dim(std::format("Used {} time(s)", row->references));
  DrawRowField("expression", Studio::CurveTextField(row->name, row->text),
               a_frame);
}

void DrawSubject(const Frame &a_frame) {
  Match(
      SelectionOf(a_frame).subject,
      [&](const Studio::RecipeSubject &) {
        DrawRecipeSettings(a_frame);
        DrawBoardPage(a_frame);
      },
      [&](const Studio::ShellSubject &) {
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

[[nodiscard]] const Studio::OutputRow *PreviewOutput(const Frame &a_frame) {
  const Studio::InspectorSubject &subject = SelectionOf(a_frame).subject;
  std::optional<std::size_t> index;
  if (const auto *output = Get<Studio::OutputSubject>(subject)) {
    index = output->output;
  } else if (const auto *layer = Get<Studio::LayerSubject>(subject)) {
    index = layer->output;
  }
  if (!index) {
    return Studio::SelectedOutput(a_frame.geometry, SelectionOf(a_frame));
  }
  const auto found = std::ranges::find(a_frame.geometry->outputs, *index,
                                       &Studio::OutputRow::index);
  return found == a_frame.geometry->outputs.end() ? nullptr : &*found;
}

void DrawPreview(const Frame &a_frame) {
  Dim("Texture preview");
  std::vector<std::string> geometries;
  geometries.reserve(a_frame.recipe->geometries.size());
  for (const Studio::GeometryRow &geometry : a_frame.recipe->geometries) {
    geometries.push_back(geometry.name);
  }
  if (const auto picked =
          ChoiceCombo("geometry", SelectionOf(a_frame).geometry, geometries,
                      {Studio::Width::Fill(), a_frame.scale})) {
    Studio::Post(*a_frame.intents, Studio::ViewGeometry{*picked});
  }
  if (!a_frame.geometry) {
    Dim("No applied geometry. No live preview.");
    return;
  }
  const Studio::InspectorSubject &subject = SelectionOf(a_frame).subject;
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
  } else if (const Studio::OutputRow *output = PreviewOutput(a_frame)) {
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
    const Studio::InspectorSubject drawn = SelectionOf(a_frame).subject;
    Disabled(a_pending, [&] { DrawSubject(a_frame); });
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
  static float navigatorShare = 0.22f;
  static float inspectorShare = 0.68f;
  const auto navigator = [&] {
    if (ImGui::BeginChild("navigator", ImVec2{0.0f, 0.0f}, 0, 0)) {
      DrawNavigator(a_frame);
    }
    ImGui::EndChild();
  };
  const auto editor = [&] {
    if (const auto split = Split(
            "preview-split", inspectorShare,
            [&] { DrawInspectorPane(a_frame, a_before, a_pending); },
            [&] { DrawPreviewPane(a_frame); })) {
      inspectorShare = *split;
    }
  };
  if (const auto split =
          Split("workspace", navigatorShare, navigator, editor)) {
    navigatorShare = *split;
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
  const bool pending = Studio::IndexedEditPendingFor(
      a_frame.state->pendingIndexedEdit, a_frame.recipe->id);
  Disabled(pending || a_frame.state->navigation.back.empty(), [&] {
    if (ImGui::SmallButton("Back")) {
      [[maybe_unused]] const bool changed = Studio::GoBack(
          a_frame.state->navigation, a_frame.state->selection, *a_frame.recipe);
    }
  });
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
