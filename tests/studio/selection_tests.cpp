#include "studio/Selection.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  Selection selection;
  Snapshot snapshot;
  Check(selection.target == Target::kMaterial && snapshot.pieces.empty(),
        "TODO: selection resolution and the isolation-aware viewed recipes");
  return test::Finish("studio_selection");
}
