// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/sim/Fixtures.h"
#include "test_support.h"

#include <cstdint>
#include <utility>

using namespace sim;
using test::Check;

namespace {
void AnEmptyWorldStaysEmpty() {
  Script script;
  script.memory = MemoryScript{60, 512 * kMebibyte};
  const Simulation simulation = Ran(Started(script), 300);
  Check(simulation.violations.empty(), Report(simulation));
  Check(simulation.world.actors.empty(), "an empty world gains no actor");
  bool quiet = true;
  for (const TickRecord &record : simulation.history) {
    quiet = quiet && record.commands.empty();
  }
  Check(quiet, "an empty world issues no command");
}

void ASettledCrowdKeepsEveryInvariant() {
  Script script;
  World world;
  for (std::uint32_t form = 100; form < 120; ++form) {
    const ActorId actor = ActorOf(form);
    const Sighting sight =
        form % 2 == 0 ? InView(0.6f, 200.0f) : OutOfView(1500.0f);
    script.actors[actor] = ScriptedActor(sight);
    world.actors[actor] =
        RecordOf(RenderIn(LiveAt(ResolutionLevel::kFull)), sight);
  }
  const Simulation simulation =
      Ran(Started(std::move(script), WithEstimate(std::move(world))), 300);
  Check(simulation.violations.empty(), Report(simulation));
  Check(simulation.world.actors.size() == 20,
        "a settled crowd keeps its actors");
}
}

int main() {
  AnEmptyWorldStaysEmpty();
  ASettledCrowdKeepsEveryInvariant();
  return test::Finish("orchestration_scenarios");
}
