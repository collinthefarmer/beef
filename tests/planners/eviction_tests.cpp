// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/Eviction.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  Check(EvictionFor(9999.0f, 0.0f, true) == EvictionAction::kNone &&
            EvictionFor(9999.0f, 0.0f, false) == EvictionAction::kNone,
        "a zero distance disables eviction in both directions");

  Check(EvictionFor(5000.0f, 4000.0f, true) == EvictionAction::kEvict,
        "an applied actor past the distance is evicted");
  Check(EvictionFor(3000.0f, 4000.0f, true) == EvictionAction::kNone,
        "an applied actor inside the distance stays");

  Check(EvictionFor(2000.0f, 4000.0f, false) == EvictionAction::kRestore,
        "an evicted actor well inside is restored");
  Check(EvictionFor(3500.0f, 4000.0f, false) == EvictionAction::kNone,
        "an evicted actor within the hysteresis band waits, not restored");

  Check(EvictionFor(3600.0f, 4000.0f, true) == EvictionAction::kNone &&
            EvictionFor(3600.0f, 4000.0f, false) == EvictionAction::kNone,
        "the hysteresis band holds both an applied and an evicted actor");

  return test::Finish("planners_eviction");
}
