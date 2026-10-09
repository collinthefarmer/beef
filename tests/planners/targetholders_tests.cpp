// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/TargetHolders.h"
#include "test_support.h"

#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
void EachTargetCountsUnderItsFirstHolder() {
  HolderSets sets;
  SetOf(sets, TargetHolder::kInstance).insert(1);
  SetOf(sets, TargetHolder::kSlot).insert(1);
  SetOf(sets, TargetHolder::kSlot).insert(2);
  SetOf(sets, TargetHolder::kPool).insert(3);
  const std::vector<LiveTarget> live{{1, 10}, {2, 20}, {3, 30}, {4, 40}};
  const HolderTallies tallies = TallyHolders(live, sets);
  Check(TallyOf(tallies, TargetHolder::kInstance) == HeldTally{1, 10},
        "a target an instance holds counts under the instance only");
  Check(TallyOf(tallies, TargetHolder::kSlot) == HeldTally{1, 20},
        "a target only a slot holds counts under the slot");
  Check(TallyOf(tallies, TargetHolder::kPool) == HeldTally{1, 30},
        "an idle target counts under the pool");
  Check(TallyOf(tallies, TargetHolder::kUnreached) == HeldTally{1, 40},
        "a target no holder reaches counts as unreached");
  Check(TallyOf(tallies, TargetHolder::kOutput) == HeldTally{},
        "a holder that reaches nothing counts nothing");
}

void NamesAreDistinct() {
  for (std::size_t i = 0; i < kTargetHolders.size(); ++i) {
    for (std::size_t j = i + 1; j < kTargetHolders.size(); ++j) {
      Check(HolderName(kTargetHolders[i]) != HolderName(kTargetHolders[j]),
            "each holder has its own name");
    }
  }
}
}

int main() {
  EachTargetCountsUnderItsFirstHolder();
  NamesAreDistinct();
  return test::Finish("planners_targetholders");
}
