// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/Tick.h"

#include <array>
#include <iterator>
#include <utility>

namespace BetterEnchantmentEffects::Orchestration {
namespace {
using Step = TickOutput (*)(World, const TickFacts &);

constexpr std::array<Step, 5> kSteps{RecordSyncResults, RecordRenderResults,
                                     RunBudget, RunSync, RunLifecycle};

template <class T> void Append(std::vector<T> &a_into, std::vector<T> a_from) {
  a_into.insert(a_into.end(), std::make_move_iterator(a_from.begin()),
                std::make_move_iterator(a_from.end()));
}
}

TickOutput Tick(World a_world, const TickFacts &a_facts) {
  TickOutput output{std::move(a_world), {}, {}, {}};
  for (const Step step : kSteps) {
    TickOutput next = step(std::move(output.world), a_facts);
    output.world = std::move(next.world);
    Append(output.commands, std::move(next.commands));
    Append(output.trace, std::move(next.trace));
    Append(output.logs, std::move(next.logs));
  }
  return output;
}
}
