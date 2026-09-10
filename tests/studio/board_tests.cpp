#include "studio/Board.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  Board board;
  Cell cell;
  board.cells.push_back(cell);
  Check(cell.state == CellState::kAbsent && board.cells.size() == 1,
        "TODO: BuildBoard projects the compose grid over a geometry");
  return test::Finish("studio_board");
}
