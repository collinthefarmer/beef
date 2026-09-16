#include "menu/BoardPage.h"

#include "menu/MenuWidgets.h"
#include "studio/Edits.h"
#include "studio/Intent.h"
#include "studio/Names.h"

#include <cstddef>
#include <format>
#include <optional>
#include <span>
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
using Studio::AddOutput;
using Studio::Board;
using Studio::Cell;
using Studio::CellState;
using Studio::GeometryRow;
using Studio::LightCell;
using Studio::PickCell;
using Studio::SlotRow;
using Studio::SoloOutput;
using Studio::ThumbnailSpec;
using Studio::Width;

[[nodiscard]] std::span<const SlotRow> SlotRowsOf(const GeometryRow &a_geometry,
                                                  Surface a_surface) noexcept {
  return a_surface == Surface::kMaterial ? a_geometry.materialSlots
                                         : a_geometry.shellSlots;
}

[[nodiscard]] std::string CellTooltip(const Cell &a_cell,
                                      const GeometryRow &a_geometry) {
  std::string text = std::format("{} on {}", SlotName(a_cell.slot),
                                 SurfaceName(a_cell.surface));
  for (const SlotRow &row : SlotRowsOf(a_geometry, a_cell.surface)) {
    if (row.slot != a_cell.slot) {
      continue;
    }
    text += "\noriginal: " + row.original;
    text +=
        "\nwritten: " +
        (row.written == row.original ? std::string{"(original)"} : row.written);
    if (!row.problem.empty()) {
      text += "\n" + row.problem;
    }
  }
  for (const Studio::ScalarRow &scalar : a_cell.scalars) {
    text += std::format("\n{} = {}", scalar.name, ValueText(scalar.value));
  }
  if (!a_cell.reason.empty()) {
    text += "\n" + a_cell.reason;
  }
  return text;
}

[[nodiscard]] std::string JoinNames(std::span<const std::string> a_names) {
  std::string text;
  for (const std::string &name : a_names) {
    text += (text.empty() ? "" : ", ") + name;
  }
  return text;
}

[[nodiscard]] PickCell PickOf(const Cell &a_cell) {
  return PickCell{a_cell.surface, a_cell.slot,
                  a_cell.layers > 0 ? std::optional{a_cell.layers - 1}
                                    : std::nullopt};
}

void DrawWrittenCell(const Cell &a_cell, const Frame &a_frame) {
  const Studio::Selection &selection = SelectionOf(a_frame);
  const bool selected = selection.target != Target::kLight &&
                        SurfaceOf(selection.target) == a_cell.surface &&
                        selection.slot == a_cell.slot;
  const float side = LayoutOf(a_frame).cellSize * a_frame.scale;
  const ThumbnailSpec spec{.texture = a_cell.composite,
                           .channel = ShaderChannel::kRgb,
                           .dynamic = a_cell.animated,
                           .size = side};
  if (ThumbnailButton("cell", spec)) {
    Studio::Post(*a_frame.intents, PickOf(a_cell));
  }
  Tooltip(CellTooltip(a_cell, *a_frame.geometry));
  ImGui::SameLine();
  ImGui::BeginGroup();
  const std::string count =
      std::format("{} layer{}", a_cell.layers, a_cell.layers == 1 ? "" : "s");
  if (selected) {
    Ok(count);
  } else {
    ImGui::TextUnformatted(count.c_str());
  }
  if (!a_cell.badges.empty()) {
    Dim(JoinNames(a_cell.badges));
  }
  if (a_cell.replace) {
    Dim("replace");
  }
  if (a_cell.output) {
    bool solo = a_cell.isolated;
    if (SoloButton(solo)) {
      Studio::Post(*a_frame.intents,
                   SoloOutput{a_frame.recipe->id, *a_cell.output, solo});
    }
  }
  ImGui::EndGroup();
}

void DrawCell(const Cell *a_cell, const Frame &a_frame) {
  if (!a_cell) {
    return;
  }
  const float side = LayoutOf(a_frame).cellSize * a_frame.scale;
  switch (a_cell->state) {
  case CellState::kAbsent:
    return;
  case CellState::kWritten:
    DrawWrittenCell(*a_cell, a_frame);
    return;
  case CellState::kEmpty:
    if (ImGui::Button("+", ImVec2{side, side})) {
      Studio::Post(*a_frame.intents, a_frame.recipe->id,
                   AddOutput{a_cell->surface, a_cell->slot, {}});
    }
    Tooltip(std::format("add an output on {} of the {}", SlotName(a_cell->slot),
                        SurfaceName(a_cell->surface)));
    return;
  case CellState::kExcluded:
    ImGui::BeginDisabled();
    ImGui::Button("+", ImVec2{side, side});
    ImGui::EndDisabled();
    Tooltip(a_cell->reason);
    return;
  case CellState::kRefused:
    Problem("refused");
    Tooltip(CellTooltip(*a_cell, *a_frame.geometry));
    if (a_cell->output && ImGui::SmallButton("select")) {
      Studio::Post(*a_frame.intents, PickOf(*a_cell));
    }
    return;
  }
}

void DrawLightCell(const LightCell &a_light, const Frame &a_frame) {
  if (!a_light.present) {
    Dim("none");
    return;
  }
  ImGui::TextUnformatted(a_light.description.c_str());
  if (a_light.output) {
    ImGui::SameLine();
    bool solo = a_light.isolated;
    if (SoloButton(solo)) {
      Studio::Post(*a_frame.intents,
                   SoloOutput{a_frame.recipe->id, *a_light.output, solo});
    }
  }
}

}

void DrawBoard(const Studio::Board &a_board, const Frame &a_frame) {
  if (!a_frame.recipe || !a_frame.geometry) {
    return;
  }
  if (!a_board.shell.empty()) {
    Dim(a_board.shell);
  }
  Table table = Table::Begin("board",
                             {{"slot", Width::Px(80.0f)},
                              {"material", Width::Fill()},
                              {"shell", Width::Fill()}},
                             Studio::kGridTable);
  if (!table.Open()) {
    return;
  }
  for (std::size_t i = 0; i < kSlotCount; ++i) {
    const Slot slot = static_cast<Slot>(i);
    table.Cell();
    ImGui::TextUnformatted(std::string{SlotName(slot)}.c_str());
    table.Cell();
    ImGui::PushID(static_cast<int>(i * 2));
    DrawCell(CellAt(a_board, Surface::kMaterial, slot), a_frame);
    ImGui::PopID();
    table.Cell();
    ImGui::PushID(static_cast<int>(i * 2 + 1));
    DrawCell(CellAt(a_board, Surface::kShell, slot), a_frame);
    ImGui::PopID();
  }
  table.Cell();
  ImGui::TextUnformatted("light");
  table.Cell();
  DrawLightCell(a_board.light, a_frame);
  table.Cell();
  table.End();
}

void DrawBoardPage(const Frame &a_frame) {
  const Studio::PieceRow *piece = a_frame.piece;
  const Studio::RecipeRow *recipe = a_frame.recipe;
  if (!piece || !recipe) {
    return;
  }
  const Studio::GeometryRow *geometry = a_frame.geometry;
  if (!geometry) {
    Dim("no geometry bound for the selected recipe");
    return;
  }
  if (recipe->geometries.size() > 1) {
    Dim("viewed on " + Studio::GeometryLabel(geometry->name, piece->armorName));
  }
  const Board board = Studio::BuildBoard(*recipe, *geometry,
                                         SelectionOf(a_frame), ViewOf(a_frame));
  ImGui::PushID(recipe->id.c_str());
  DrawBoard(board, a_frame);
  ImGui::PopID();
}
}
