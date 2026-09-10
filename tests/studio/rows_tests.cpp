#include "studio/Rows.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  SignalRow signal;
  OutputRow output;
  Check(signal.kind == SignalKindId::kConstant &&
            output.target == Target::kMaterial,
        "TODO: pure per-row projections the wave-3 snapshot builder calls");
  return test::Finish("studio_rows");
}
