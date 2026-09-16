#include "studio/Board.h"

#include "studio/Names.h"
#include "studio/Rows.h"

#include <algorithm>
#include <format>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] std::vector<std::string>
BadgesOf(const std::vector<LayerRow> &a_layers) {
  std::vector<std::string> badges;
  for (const auto &layer : a_layers) {
    if (layer.mask.empty()) {
      continue;
    }
    const std::string name = ReferenceName(layer.mask);
    if (std::ranges::find(badges, name) == badges.end()) {
      badges.push_back(name);
    }
  }
  return badges;
}

void FillCell(Cell &a_cell, const RecipeRow &a_recipe,
              const GeometryRow &a_geometry, const View &a_view) {
  const OutputRow *firstWriter = nullptr;
  for (const auto &output : a_geometry.outputs) {
    if (!WritesCell(output, a_cell.surface, a_cell.slot)) {
      continue;
    }
    if (firstWriter == nullptr) {
      firstWriter = &output;
    } else {
      a_cell.reason =
          std::format("output {} also writes this slot; the first one is shown",
                      output.index);
    }
  }
  if (firstWriter != nullptr) {
    a_cell.state = firstWriter->problem.empty() ? CellState::kWritten
                                                : CellState::kRefused;
    if (!firstWriter->problem.empty()) {
      a_cell.reason = firstWriter->problem;
    }
    a_cell.output = firstWriter->index;
    a_cell.composite = firstWriter->texture;
    a_cell.layers = firstWriter->layers.size();
    a_cell.animated = firstWriter->animated;
    a_cell.replace = firstWriter->replace;
    a_cell.scalars = firstWriter->scalars;
    a_cell.badges = BadgesOf(firstWriter->layers);
    a_cell.isolated =
        a_view.isolation.TargetsOutput(a_recipe.id, firstWriter->index);
    return;
  }
  for (const auto &output : a_geometry.outputs) {
    if (output.target != Target::kLight && output.surface == a_cell.surface &&
        SlotsExclude(a_cell.slot, output.slot)) {
      a_cell.state = CellState::kExcluded;
      a_cell.reason = std::format("excluded by {} (output {})",
                                  SlotName(output.slot), output.index);
      return;
    }
  }
  a_cell.state = CellState::kEmpty;
}

[[nodiscard]] LightCell BuildLightCell(const RecipeRow &a_recipe,
                                       const GeometryRow &a_geometry,
                                       const View &a_view) {
  LightCell light;
  light.output = a_recipe.lightOutput;
  if (!light.output) {
    const auto it =
        std::ranges::find_if(a_geometry.outputs, [](const OutputRow &a_output) {
          return a_output.target == Target::kLight;
        });
    if (it != a_geometry.outputs.end()) {
      light.output = it->index;
    }
  }
  light.present = light.output.has_value();
  light.description = a_recipe.light;
  light.isolated = light.output &&
                   a_view.isolation.TargetsOutput(a_recipe.id, *light.output);
  return light;
}
}

const Cell *CellAt(const Board &a_board, Surface a_surface,
                   Slot a_slot) noexcept {
  for (const auto &cell : a_board.cells) {
    if (cell.surface == a_surface && cell.slot == a_slot) {
      return &cell;
    }
  }
  return nullptr;
}

Board BuildBoard(const RecipeRow &a_recipe, const GeometryRow &a_geometry,
                 [[maybe_unused]] const Selection &a_selection,
                 const View &a_view) {
  Board board;
  board.cells.reserve(kSlotCount * 2);
  for (std::size_t i = 0; i < kSlotCount; ++i) {
    const auto slot = static_cast<Slot>(i);
    for (const Surface surface : {Surface::kMaterial, Surface::kShell}) {
      Cell cell;
      cell.surface = surface;
      cell.slot = slot;
      if (SurfaceHasSlot(surface, a_recipe.shellMaterial, slot)) {
        FillCell(cell, a_recipe, a_geometry, a_view);
      }
      board.cells.push_back(std::move(cell));
    }
  }
  board.light = BuildLightCell(a_recipe, a_geometry, a_view);
  board.shell = a_geometry.shell;
  return board;
}
}
