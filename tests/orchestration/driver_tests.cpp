// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/sim/Fixtures.h"
#include "test_support.h"

#include <cstddef>
#include <utility>

using namespace sim;
using test::Check;
using test::Equal;

namespace {
constexpr ActorId kActor{20};
constexpr ActorId kArrival{23};

void RunsTheGivenTicks() {
  const Simulation simulation = Ran(Started(Script{}), 25);
  Equal(simulation.history.size(), std::size_t{25}, "one record per tick");
  Equal(simulation.shell.tick, std::uint64_t{25}, "the shell counts the ticks");
  Check(simulation.history.front().tick == 1 &&
            simulation.history.back().tick == 25,
        "the records are numbered from one");
  Check(simulation.history.back().facts.now ==
            25 * simulation.shell.script.tickMs,
        "each tick's time follows the tick interval");
}

void FeedsResultsBackAsFacts() {
  Simulation simulation = Started(Script{});
  simulation.shell.results.push_back(Retired{kActor});
  simulation = Advanced(std::move(simulation));
  Check(simulation.history.front().facts.results.size() == 1,
        "the shell's pending results are the next tick's facts");
  simulation = Advanced(std::move(simulation));
  Check(simulation.history.back().facts.results.empty(),
        "a result reaches the core once");
}

void SightsWorldAndStaleActors() {
  Script script;
  script.actors[kActor] = ScriptedActor(InView(0.5f, 100.0f));
  ActorScript arrival = ScriptedActor(OutOfView(800.0f));
  arrival.stale.push_back({3, {}});
  script.actors[kArrival] = arrival;
  World world;
  world.actors[kActor] = ActorRecord{};
  const Simulation simulation = Ran(Started(script, world), 4);
  Check(simulation.history.at(0).facts.sightings.size() == 1 &&
            simulation.history.at(0).facts.sightings.contains(kActor),
        "the engine sights every actor in the world");
  Check(simulation.history.at(2).facts.stale.size() == 1 &&
            simulation.history.at(2).facts.sightings.contains(kArrival),
        "a stale actor arrives with a sighting");
  Check(simulation.history.at(3).facts.stale.empty(),
        "a scripted staleness marks the actor once");
}

void SamplesMemoryOnItsCadence() {
  Script script;
  script.memory = MemoryScript{10, 256 * kMebibyte};
  const Simulation simulation = Ran(Started(script), 30);
  std::size_t samples = 0;
  for (const TickRecord &record : simulation.history) {
    samples += record.facts.memory ? 1 : 0;
  }
  Equal(samples, std::size_t{3}, "one memory sample every ten ticks");
}

void ReportsViolationsWithTheirTick() {
  World world;
  world.actors[kActor] = RecordOf(RenderIn(LiveAt(ResolutionLevel::kFull)));
  const Simulation simulation = Ran(Started(Script{}, world), 3);
  Equal(simulation.violations.size(), std::size_t{3},
        "a world whose estimate is wrong fails every tick");
  Check(simulation.violations.front().rule == Rule::kEstimateDiffers &&
            simulation.violations.front().tick == 1,
        "the violation names its rule and tick");
  Check(!Report(simulation).empty(), "the report lists the violations");
}
}

int main() {
  RunsTheGivenTicks();
  FeedsResultsBackAsFacts();
  SightsWorldAndStaleActors();
  SamplesMemoryOnItsCadence();
  ReportsViolationsWithTheirTick();
  return test::Finish("orchestration_driver");
}
