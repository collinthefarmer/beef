#include "studio/Board.h"

#include "studio/Names.h"

#include <algorithm>
#include <format>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] bool IsMaterialOutput(const OutputRow &a_output) noexcept {
  return a_output.target != Target::kLight;
}

[[nodiscard]] bool WritesCell(const OutputRow &a_output, Surface a_surface,
                              Slot a_slot) noexcept {
  return IsMaterialOutput(a_output) && a_output.surface == a_surface &&
         a_output.slot == a_slot;
}

[[nodiscard]] bool OutputIsolated(const View &a_view,
                                  const std::string &a_recipe,
                                  std::size_t a_output) noexcept {
  return a_view.isolateRecipe == a_recipe && a_view.isolateOutput >= 0 &&
         static_cast<std::size_t>(a_view.isolateOutput) == a_output;
}

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
  const OutputRow *first = nullptr;
  for (const auto &output : a_geometry.outputs) {
    if (!WritesCell(output, a_cell.surface, a_cell.slot)) {
      continue;
    }
    if (first == nullptr) {
      first = &output;
    } else {
      a_cell.reason =
          std::format("output {} also writes this slot; the first one is shown",
                      output.index);
    }
  }
  if (first != nullptr) {
    a_cell.state =
        first->problem.empty() ? CellState::kWritten : CellState::kRefused;
    if (!first->problem.empty()) {
      a_cell.reason = first->problem;
    }
    a_cell.output = first->index;
    a_cell.composite = first->texture;
    a_cell.layers = first->layers.size();
    a_cell.animated = first->animated;
    a_cell.replace = first->replace;
    a_cell.scalars = first->scalars;
    a_cell.badges = BadgesOf(first->layers);
    a_cell.isolated = OutputIsolated(a_view, a_recipe.id, first->index);
    return;
  }
  for (const auto &output : a_geometry.outputs) {
    if (IsMaterialOutput(output) && output.surface == a_cell.surface &&
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
  light.isolated =
      light.output && OutputIsolated(a_view, a_recipe.id, *light.output);
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
