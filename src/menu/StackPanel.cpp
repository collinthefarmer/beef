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
using Studio::Inspector;
using Studio::LayerStack;
using Studio::LayerStackRow;
using Studio::Layout;
using Studio::PictureRow;

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

[[maybe_unused]] void DrawDetailModal(FieldDetail a_detail,
                                      const Inspector &a_inspector,
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

void DrawInspector(const LayerStack &a_stack,
                   const std::optional<Inspector> &a_inspector,
                   const Frame &a_frame) {
  const Layout &layout = LayoutOf(a_frame);
  if (!layout.inspector || !a_inspector) {
    return;
  }
  const auto row = std::ranges::find(a_stack.rows, a_inspector->layer,
                                     &LayerStackRow::index);
  ImGui::PushID(static_cast<int>(a_inspector->layer));
  if (!a_inspector->row.problem.empty()) {
    Warn(a_inspector->row.problem);
  }
  DrawInspectorFields(*a_inspector, a_frame);
  if (row != a_stack.rows.end() &&
      row->layer.texture != Studio::TextureHandle{}) {
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
  ImGui::PushID(static_cast<int>(stack.output));
  if (!stack.problem.empty()) {
    Problem(stack.problem);
  }
  Dim(std::format("composite {} px, {}", stack.size,
                  stack.animated ? "animated" : "static"));
  DrawInspector(stack, a_inspector, a_frame);
  ImGui::PopID();
}
}
