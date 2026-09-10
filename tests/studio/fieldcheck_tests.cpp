#include "studio/FieldCheck.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  Names names;
  FormField field;
  Check(
      names.signals.empty() && field.kind == FieldKind::kScalar,
      "TODO: field checks reuse the recipe RowTypes row-check, no divergence");
  return test::Finish("studio_fieldcheck");
}
