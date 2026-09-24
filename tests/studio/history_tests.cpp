// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/History.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  History<int> history;
  history.Push(1);
  history.Push(2);
  Check(history.UndoDepth() == 2, "two states are on the undo stack");

  const std::optional<int> undone = history.Undo(3);
  Check(undone.has_value() && *undone == 2,
        "undo returns the last pushed state");
  Check(history.RedoDepth() == 1, "the current state moved to the redo stack");

  const std::optional<int> redone = history.Redo(2);
  Check(redone.has_value() && *redone == 3,
        "redo returns the state undo saved");

  history.Clear();
  Check(history.UndoDepth() == 0 && history.RedoDepth() == 0,
        "clear empties both stacks");
  return test::Finish("studio_history");
}
