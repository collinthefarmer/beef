// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/Tick.h"

#include <utility>

namespace BetterEnchantmentEffects::Orchestration {
TickOutput RecordSyncResults(World a_world, const TickFacts &) {
  return {std::move(a_world), {}, {}, {}};
}

TickOutput RunSync(World a_world, const TickFacts &) {
  return {std::move(a_world), {}, {}, {}};
}
}
