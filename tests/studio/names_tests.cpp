#include "studio/Names.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  Names names;
  Check(names.masks.empty() && kRowKindCount == 4,
        "TODO: NamesOf, TakenNames, UniqueName and the reference-text helpers");
  return test::Finish("studio_names");
}
