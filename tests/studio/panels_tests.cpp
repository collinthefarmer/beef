#include "studio/Forms.h"
#include "studio/Panels.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  LayerStack stack;
  Inspector inspector;
  SignalList signals;
  SignalNames names;
  Check(stack.rows.empty() && inspector.layer == 0 && signals.tunable.empty() &&
            names.scalar.empty(),
        "TODO: stack, inspector, signal list and forms build over the rows");
  return test::Finish("studio_panels");
}
