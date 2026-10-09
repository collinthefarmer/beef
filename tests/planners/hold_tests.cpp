// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/Hold.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
void HoldStartsAndKeepsItsStart() {
  const Hold held = Hold{}.Update(true, 1000);
  Check(held.since == 1000u, "a hold starts when its condition becomes true");
  Check(held.Update(true, 4000) == held,
        "a hold keeps its start while its condition stays true");
  Check(!Hold{}.Update(false, 1000).since, "a false condition starts no hold");
}

void HoldResetsWhenTheConditionEnds() {
  const Hold held = Hold{}.Update(true, 1000);
  Check(held.Update(false, 2000) == Hold{},
        "a hold resets when its condition ends");
  Check(held.Update(false, 2000).Update(true, 3000).since == 3000u,
        "a condition that returns starts a new hold");
}

void HoldIsDueAtTheBoundary() {
  const Hold held = Hold{}.Update(true, 1000);
  Check(!held.Due(5999, 5000), "a hold is not due before its delay");
  Check(held.Due(6000, 5000), "a hold is due once it has held for its delay");
  Check(held.Due(1000, 0), "a hold with no delay is due at once");
  Check(!Hold{}.Due(9000, 0), "an empty hold is never due");
}

void ClockGoingBackwardsRestartsTheHold() {
  const Hold held = Hold{}.Update(true, 9000);
  Check(!held.Due(1000, 5000), "a clock behind the start is not due");
  const Hold restarted = held.Update(true, 1000);
  Check(restarted.since == 1000u,
        "a clock behind the start restarts the hold at the new time");
  Check(restarted.Due(6000, 5000),
        "a restarted hold becomes due after its delay");
}
}

int main() {
  HoldStartsAndKeepsItsStart();
  HoldResetsWhenTheConditionEnds();
  HoldIsDueAtTheBoundary();
  ClockGoingBackwardsRestartsTheHold();
  return test::Finish("planners_hold");
}
