#pragma once

#include "menu/Frame.h"
#include "recipe/Recipe.h"
#include "studio/Board.h"
#include "studio/Snapshot.h"

#include <cstddef>
#include <optional>
#include <string_view>

namespace BetterEnchantmentEffects::Menu {
struct PaneChoice {
  bool hasSettings = false;
  bool hasStack = false;
  bool settings = false;
};

[[nodiscard]] PaneChoice ChoosePane(Target a_target,
                                    bool a_settingsWanted) noexcept;
[[nodiscard]] const Studio::Cell *
PickedCell(const Studio::Board &a_board,
           const Studio::Selection &a_selection) noexcept;
[[nodiscard]] std::optional<RecipeKey>
DefaultKeyOf(const Studio::PieceRow &a_piece);

[[nodiscard]] const Studio::Cell *DrawContext(const Studio::Board &a_board,
                                              const Frame &a_frame);
void DrawPaneRule(std::string_view a_title, const PaneChoice &a_pane,
                  const Studio::Board &a_board, const Frame &a_frame);
void DrawOutputHeader(std::size_t a_output, bool a_replace,
                      const Selector &a_selector, const Frame &a_frame);
}
