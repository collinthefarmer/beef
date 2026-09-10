#include "studio/TermTemplates.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  Existing existing;
  BuiltTerm built;
  TermOffer offer;
  Check(existing.taken.empty() && built.edits.empty() &&
            kOfferGroupCount == 9 && offer.group == OfferGroup::kParts,
        "TODO: term templates materialise offers into edits and a mask "
        "expression");
  return test::Finish("studio_termtemplates");
}
