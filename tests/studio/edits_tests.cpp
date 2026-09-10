#include "studio/Edits.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  RecipeEdit edit = SetScalar{};
  EditBatch batch;
  batch.edits.push_back(edit);
  Check(!batch.edits.empty(),
        "TODO: apply-or-refuse round-trip-and-undo per edit");
  return test::Finish("studio_edits");
}
