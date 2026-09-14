#include "menu/StackPanel.h"

#include "menu/FormDraw.h"
#include "menu/MenuWidgets.h"
#include "studio/Edits.h"
#include "studio/Forms.h"
#include "studio/Intent.h"
#include "studio/Names.h"
#include "studio/Panels.h"
#include "studio/Widgets.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
namespace {
using Studio::FieldDetail;
using Studio::FieldKind;
using Studio::ForeignRow;
using Studio::Inspector;
using Studio::LayerStack;
using Studio::LayerStackRow;
using Studio::Layout;
using Studio::PictureRow;

constexpr float kCompositeSize = 160.0f;
constexpr const char *kLayerPayload = "BEEF_LAYER";
constexpr Studio::TableStyle kLayerStyle{
    .borders = Studio::TableBorders::kInnerHorizontal,
    .stretch = true,
    .headers = true,
    .rowBackground = true};

void DrawComposite(const LayerStack &a_stack, const Frame &a_frame) {
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  const Studio::ThumbnailSpec composite{a_stack.composite, ShaderChannel::kRgb,
                                        a_stack.animated, kCompositeSize};
  if (!a_frame.geometry || !a_frame.piece || recipe.geometries.size() < 2) {
    Thumbnail(composite);
    return;
  }
  if (ThumbnailButton("composite", composite)) {
    const auto &shapes = recipe.geometries;
    const auto it = std::ranges::find(shapes, a_frame.geometry->name,
                                      &Studio::GeometryRow::name);
    const std::size_t at =
        it == shapes.end() ? 0 : static_cast<std::size_t>(it - shapes.begin());
    if (!shapes.empty()) {
      Studio::Post(*a_frame.intents,
                   Studio::ViewGeometry{shapes[(at + 1) % shapes.size()].name});
    }
  }
  Tooltip(std::format(
      "viewed on {} (one of {} geometries; the recipe applies to all)\nclick: "
      "view the next geometry\nraw name: {}",
      Studio::GeometryLabel(a_frame.geometry->name, a_frame.piece->armorName),
      recipe.geometries.size(), a_frame.geometry->name));
}

void DrawScalars(const LayerStack &a_stack, const Frame &a_frame) {
  if (a_stack.scalars.empty()) {
    return;
  }
  DrawFormWithSignals("scalars", Studio::ScalarForm(a_stack), a_frame);
}

[[nodiscard]] Table BeginLayerTable() {
  const Studio::Width button = Studio::Width::Px(RowButtonWidth());
  return Table::Begin("layers",
                      {{"#", Studio::Width::Fit()},
                       {"", button},
                       {"", button},
                       {"S", button},
                       {"M", button},
                       {"type", button},
                       {"blend", Studio::Width::Fit()},
                       {"source", Studio::Width::Fill()}},
                      kLayerStyle);
}

void DrawForeignRow(Table &a_table, const ForeignRow &a_row) {
  a_table.Cell();
  a_table.Cell();
  a_table.Cell();
  a_table.Cell();
  a_table.Cell();
  a_table.Cell();
  Dim(a_row.layer.source.starts_with('@') ? "@" : "c");
  a_table.Cell();
  Dim(a_row.layer.blend);
  a_table.Cell();
  Dim(std::format("{}  ({}, priority {}: {} {})", a_row.layer.source,
                  a_row.recipeID, a_row.priority, a_row.layer.opacityText,
                  a_row.layer.mask));
}

void DrawStackRow(Table &a_table, const LayerStack &a_stack,
                  const LayerStackRow &a_row, const Frame &a_frame) {
  const std::string &id = a_frame.recipe->id;
  const std::size_t output = a_stack.output;
  const std::size_t index = a_row.index;
  const bool constant = !a_row.layer.source.starts_with('@');

  ImGui::PushID(static_cast<int>(index));
  a_table.Cell();
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%zu", index);
  a_table.Cell();
  if (RemoveButton(0)) {
    Studio::Post(*a_frame.intents, id, Studio::RemoveLayer{output, index});
  }
  a_table.Cell();
  if (DragHandle(kLayerPayload, index, "layer")) {
    Studio::Post(*a_frame.intents, Studio::PickLayer{index});
  }
  if (const auto move = DropTarget(kLayerPayload, index)) {
    Studio::Post(*a_frame.intents, id,
                 Studio::MoveLayer{output, move->from, move->to});
  }
  a_table.Cell();
  bool solo = a_row.soloed;
  if (SoloButton(solo)) {
    Studio::Post(*a_frame.intents, Studio::SoloLayer{id, output, index, solo});
  }
  a_table.Cell();
  bool mute = a_row.muted;
  if (MuteButton(mute)) {
    Studio::Post(*a_frame.intents, Studio::MuteLayer{id, output, index, mute});
  }
  a_table.Cell();
  Badge(constant ? FieldKind::kColor : FieldKind::kReference);
  a_table.Cell();
  if (const auto blend =
          BlendCombo("blend", a_row.layer.blend, a_stack.blends,
                     {Studio::Width::Px(BlendWidth(a_stack.blends)), 1.0f})) {
    Studio::Post(*a_frame.intents, id,
                 Studio::SetLayerBlend{output, index, *blend});
  }
  a_table.Cell();
  if (ImGui::Selectable(a_row.layer.source.c_str(), a_row.selected)) {
    Studio::Post(*a_frame.intents, Studio::PickLayer{index});
  }
  if (!a_row.layer.problem.empty()) {
    Tooltip(a_row.layer.problem);
  }
  ImGui::PopID();
}

void DrawAddLayer(const LayerStack &a_stack, const Frame &a_frame) {
  if (ImGui::SmallButton("Add layer")) {
    Studio::Post(*a_frame.intents, a_frame.recipe->id,
                 Studio::AddLayer{a_stack.output, Studio::DefaultLayer(),
                                  a_stack.rows.size()});
  }
}

void DrawLayers(const LayerStack &a_stack, const Frame &a_frame) {
  auto table = BeginLayerTable();
  if (table.Open()) {
    for (const auto &foreign : a_stack.below) {
      DrawForeignRow(table, foreign);
    }
    for (const auto &row : a_stack.rows) {
      DrawStackRow(table, a_stack, row, a_frame);
    }
    for (const auto &foreign : a_stack.above) {
      DrawForeignRow(table, foreign);
    }
    table.End();
  }
  DrawAddLayer(a_stack, a_frame);
  HelpMarker("Drag the :: grip onto another row to reorder; click the grip or "
             "the name to open the layer's fields beside the stack. S solos, M "
             "mutes. Enter commits a text field; a drag commits on release.");
}

void DrawDetailImage(const PictureRow &a_image, bool a_editable,
                     const Frame &a_frame) {
  const Layout &layout = LayoutOf(a_frame);
  const Studio::ThumbnailSpec picture{
      a_image.texture, a_image.channel, a_image.animated,
      layout.inspectorThumbnail * a_frame.scale};
  Thumbnail(picture);
  ImGui::TextUnformatted((Studio::ReferenceText(a_image.name) + " =").c_str());
  ImGui::SameLine();
  if (a_editable) {
    DrawRowField("text",
                 Studio::MaskTextField(a_image.name, a_image.description),
                 a_frame);
  } else {
    ImGui::TextWrapped("%s", a_image.description.c_str());
  }
  if (!a_image.problem.empty()) {
    Warn(a_image.problem);
  }
}

void DrawDetailModal(FieldDetail a_detail, const Inspector &a_inspector,
                     const Frame &a_frame) {

  switch (a_detail) {
  case FieldDetail::kSource:
    if (a_inspector.source) {
      const bool isMask =
          std::ranges::find(a_inspector.masks, a_inspector.source->name) !=
          a_inspector.masks.end();
      DrawDetailImage(*a_inspector.source, isMask, a_frame);
    } else {
      Dim("a constant colour, or a name no source or mask has");
    }
    break;
  case FieldDetail::kCurve:
    if (a_inspector.curve) {
      ImGui::TextUnformatted(
          (Studio::ReferenceText(a_inspector.curve->name) + " =").c_str());
      ImGui::SameLine();
      Badge(FieldKind::kCurve);
      DrawRowField("text",
                   Studio::CurveTextField(a_inspector.curve->name,
                                          a_inspector.curve->text),
                   a_frame);
    } else {
      Dim("no declared curve; the layer's curve is inline or empty");
    }
    break;
  case FieldDetail::kOpacity:
    DrawSignalDetail(a_inspector.row.opacityText, a_frame, 0);
    break;
  case FieldDetail::kColor:
    DrawSignalDetail(a_inspector.row.color, a_frame, 0);
    break;
  case FieldDetail::kSignal:
    break;
  case FieldDetail::kMask:
    if (a_inspector.mask) {
      DrawDetailImage(*a_inspector.mask, true, a_frame);
    } else {
      Dim("no mask");
    }
    break;
  }
}

void DrawInspectorFields(const Inspector &a_inspector, const Frame &a_frame) {
  const auto form = Studio::InspectorForm(a_inspector);
  const std::optional<std::size_t> opened = DrawForm("fields", form, a_frame);
  const std::optional<FieldDetail> open =
      opened && *opened < form.size() ? form[*opened].detail : std::nullopt;
  for (const auto detail :
       {FieldDetail::kSource, FieldDetail::kCurve, FieldDetail::kOpacity,
        FieldDetail::kColor, FieldDetail::kMask}) {
    const auto title = std::format("{} of layer {}###detail{}",
                                   Studio::FieldDetailName(detail),
                                   a_inspector.layer, static_cast<int>(detail));
    if (open == detail) {
      ImGui::OpenPopup(title.c_str());
    }
    DetailModal(title.c_str(),
                [&]() { DrawDetailModal(detail, a_inspector, a_frame); });
  }
}

void DrawInspector(const LayerStack &a_stack,
                   const std::optional<Inspector> &a_inspector,
                   const Frame &a_frame) {
  const Layout &layout = LayoutOf(a_frame);
  if (!layout.inspector || !a_inspector) {
    Dim("click a layer to inspect it");
    return;
  }
  const auto row = std::ranges::find(a_stack.rows, a_inspector->layer,
                                     &LayerStackRow::index);
  ImGui::PushID(static_cast<int>(a_inspector->layer));
  if (!a_inspector->row.problem.empty()) {
    Warn(a_inspector->row.problem);
  }
  DrawInspectorFields(*a_inspector, a_frame);
  if (row != a_stack.rows.end() && row->layer.texture) {
    const Studio::ThumbnailSpec preview{
        row->layer.texture, ShaderChannel::kRgb, false,
        layout.inspectorThumbnail * a_frame.scale};
    Thumbnail(preview);
  }
  ImGui::PopID();
}
}

void DrawStack(const std::optional<Studio::LayerStack> &a_stack,
               const std::optional<Studio::Inspector> &a_inspector,
               const Frame &a_frame) {
  if (!a_stack) {
    Dim("choose a target and a slot");
    return;
  }
  const LayerStack &stack = *a_stack;
  const Layout &layout = LayoutOf(a_frame);
  ImGui::PushID(static_cast<int>(stack.output));
  if (!stack.problem.empty()) {
    Problem(stack.problem);
  }
  DrawComposite(stack, a_frame);
  ImGui::SameLine();
  ImGui::BeginGroup();
  Dim(std::format("composite {} px, {}", stack.size,
                  stack.animated ? "animated" : "static"));
  DrawScalars(stack, a_frame);
  ImGui::EndGroup();

  if (stack.rows.empty()) {
    DrawAddLayer(stack, a_frame);
    ImGui::PopID();
    return;
  }
  const auto dragged = Split(
      "stack-split", layout.stackSplit, [&]() { DrawLayers(stack, a_frame); },
      [&]() { DrawInspector(stack, a_inspector, a_frame); });
  if (dragged) {
    Studio::Post(*a_frame.intents, Studio::SetStackSplit{*dragged});
  }
  ImGui::PopID();
}
}
