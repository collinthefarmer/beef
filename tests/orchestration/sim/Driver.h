// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "orchestration/sim/Contract.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sim {
struct TickRecord {
  std::uint64_t tick = 0;
  TickFacts facts;
  std::vector<Command> commands;
  std::vector<TraceRecord> trace;
  std::vector<LogRecord> logs;
};

struct Simulation {
  World world;
  Shell shell;
  std::vector<Violation> violations;
  std::vector<TickRecord> history;
  bool checkSteps = true;
};

[[nodiscard]] inline Simulation Started(Script a_script, World a_world = {}) {
  Simulation simulation;
  simulation.world = std::move(a_world);
  simulation.shell.script = std::move(a_script);
  return simulation;
}

inline void Append(std::vector<Violation> &a_into,
                   std::vector<Violation> a_from) {
  for (Violation &violation : a_from) {
    a_into.push_back(std::move(violation));
  }
}

[[nodiscard]] inline Simulation Advanced(Simulation a_simulation) {
  Turn turn = NextFacts(std::move(a_simulation.shell), a_simulation.world);
  const std::uint64_t tick = turn.shell.tick;
  TickOutput output = Tick(a_simulation.world, turn.facts);
  if (a_simulation.checkSteps) {
    StepsOutcome steps = RunStepsChecked(a_simulation.world, turn.facts, tick);
    Append(a_simulation.violations, std::move(steps.violations));
    Append(a_simulation.violations,
           CheckTickMatchesSteps(output, steps.output, tick));
  }
  Append(a_simulation.violations,
         CheckTick(TickView{tick, a_simulation.world, turn.facts, output}));
  Execution execution = Executed(std::move(turn.shell), output.commands);
  for (Disagreement &disagreement : execution.disagreements) {
    a_simulation.violations.push_back({tick, Rule::kEngineDisagrees,
                                       disagreement.actor,
                                       std::move(disagreement.what)});
  }
  a_simulation.shell = std::move(execution.shell);
  a_simulation.history.push_back(
      TickRecord{tick, std::move(turn.facts), output.commands,
                 std::move(output.trace), std::move(output.logs)});
  a_simulation.world = std::move(output.world);
  return a_simulation;
}

[[nodiscard]] inline Simulation Ran(Simulation a_simulation,
                                    std::uint64_t a_ticks) {
  for (std::uint64_t i = 0; i < a_ticks; ++i) {
    a_simulation = Advanced(std::move(a_simulation));
  }
  return a_simulation;
}

[[nodiscard]] inline std::string Report(const Simulation &a_simulation) {
  std::string text;
  for (const Violation &violation : a_simulation.violations) {
    text += Describe(violation) + "\n";
  }
  return text;
}
}
