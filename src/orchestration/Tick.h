// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "orchestration/Commands.h"
#include "orchestration/Facts.h"
#include "orchestration/World.h"

#include <vector>

namespace BetterEnchantmentEffects::Orchestration {
struct TickOutput {
  World world;
  std::vector<Command> commands;
  std::vector<TraceRecord> trace;
};

[[nodiscard]] TickOutput RecordSyncResults(World a_world,
                                           const TickFacts &a_facts);
[[nodiscard]] TickOutput RecordRenderResults(World a_world,
                                             const TickFacts &a_facts);
[[nodiscard]] TickOutput RunBudget(World a_world, const TickFacts &a_facts);
[[nodiscard]] TickOutput RunSync(World a_world, const TickFacts &a_facts);
[[nodiscard]] TickOutput RunLifecycle(World a_world, const TickFacts &a_facts);
[[nodiscard]] TickOutput Tick(World a_world, const TickFacts &a_facts);
}
