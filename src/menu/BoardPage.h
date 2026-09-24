// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "menu/Frame.h"
#include "studio/Board.h"

namespace BetterEnchantmentEffects::Menu {
void DrawBoard(const Studio::Board &a_board, const Frame &a_frame);
void DrawBoardPage(const Frame &a_frame);
}
