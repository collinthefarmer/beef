#include "studio/Mask.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  Term term;
  MaskStack stack;
  TermKind kind = RawTerm{};
  stack.terms.push_back(term);
  Check(term.op == TermOp::kSet && kind.index() == 0 && kTermKindCount == 8,
        "TODO: BuildMask composes the term stack into one expression");
  return test::Finish("studio_mask");
}
