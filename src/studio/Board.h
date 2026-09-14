#pragma once

#include "recipe/Recipe.h"
#include "studio/Selection.h"
#include "studio/Snapshot.h"
#include "studio/View.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
enum class CellState {
  kWritten,
  kEmpty,
  kAbsent,
  kExcluded,
  kRefused,
};
inline constexpr std::size_t kCellStateCount = 5;

struct Cell {
  Surface surface = Surface::kMaterial;
  Slot slot = Slot::kEmissive;
  CellState state = CellState::kAbsent;
  std::string reason;
  std::optional<std::size_t> output;
  std::size_t layers = 0;
  bool animated = false;
  bool replace = false;
  TextureHandle composite{};
  std::vector<ScalarRow> scalars;
  std::vector<std::string> badges;
  bool isolated = false;
};

struct LightCell {
  bool present = false;
  std::optional<std::size_t> output;
  std::string description;
  bool isolated = false;
};

struct Board {
  std::vector<Cell> cells;
  LightCell light;
  std::string shell;
};

[[nodiscard]] const Cell *CellAt(const Board &a_board, Surface a_surface,
                                 Slot a_slot) noexcept;
[[nodiscard]] Board BuildBoard(const RecipeRow &a_recipe,
                               const GeometryRow &a_geometry,
                               const Selection &a_selection,
                               const View &a_view);
}
