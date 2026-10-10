// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "orchestration/Commands.h"
#include "orchestration/Facts.h"
#include "orchestration/World.h"

#include <vector>

namespace BetterEnchantmentEffects::Orchestration {
struct StepOutput {
  World world;
  std::vector<Command> commands;
  std::vector<TraceRecord> trace;
};

[[nodiscard]] StepOutput AbsorbSync(World a_world, const TickFacts &a_facts);
[[nodiscard]] StepOutput AbsorbRender(World a_world, const TickFacts &a_facts);
[[nodiscard]] StepOutput BudgetStep(World a_world, const TickFacts &a_facts);
[[nodiscard]] StepOutput SyncStep(World a_world, const TickFacts &a_facts);
[[nodiscard]] StepOutput LifecycleStep(World a_world, const TickFacts &a_facts);
[[nodiscard]] StepOutput Tick(World a_world, const TickFacts &a_facts);
}
