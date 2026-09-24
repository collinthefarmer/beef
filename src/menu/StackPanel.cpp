// GPL-3.0-only with the additional permission in COPYING.md.
#include "menu/StackPanel.h"

#include "menu/FormDraw.h"
#include "menu/MenuWidgets.h"
#include "studio/Panels.h"
#include "studio/Widgets.h"

#include <algorithm>
#include <cstddef>
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
using Studio::Inspector;
using Studio::LayerStack;
using Studio::LayerStackRow;
using Studio::Layout;

void DrawInspector(const LayerStack &a_stack,
                   const std::optional<Inspector> &a_inspector,
                   const Frame &a_frame) {
  const Layout &layout = LayoutOf(a_frame);
  if (!layout.inspector || !a_inspector) {
    return;
  }
  const auto *row =
      FindBy(a_stack.rows, a_inspector->layer, &LayerStackRow::index);
  ImGui::PushID(static_cast<int>(a_inspector->layer));
  if (!a_inspector->row.problem.empty()) {
    Warn(a_inspector->row.problem);
  }
  DrawInspectorFields(*a_inspector, a_frame);
  if (row && row->layer.texture != Studio::TextureHandle{}) {
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
  DrawInspector(stack, a_inspector, a_frame);
  ImGui::PopID();
}
}
