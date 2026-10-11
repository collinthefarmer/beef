// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "orchestration/Budget.h"
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

[[nodiscard]] inline EngineActor
EngineActorOf(const InstalledRender &a_render) {
  EngineActor actor;
  const PerLevel<std::optional<LevelCost>> &costs = a_render.levelCosts;
  Match(
      a_render.phase, [&](const Empty &) { actor.withoutRender = true; },
      [&](const Shown &a_shown) {
        actor.render =
            EngineRender{a_shown.level, HoldingOf(a_shown.intermediates), costs,
                         std::nullopt};
      },
      [&](const Planning &a_planning) {
        actor.awaiting = AwaitedPlanJob{a_planning.job, false};
      },
      [&](const Drawing &a_drawing) {
        actor.render = EngineRender{
            a_drawing.level, IntermediateHolding::kHeld, costs,
            UnpublishedWork{a_drawing.draw.total, a_drawing.draw.drawn}};
      },
      [&](const Queued &a_queued) {
        const Resize &resize = a_queued.resize;
        actor.render =
            EngineRender{resize.from, resize.holding, costs, std::nullopt};
      },
      [&](const Running &a_running) {
        const Resize &resize = a_running.resize;
        const UnpublishedDraw &draw = a_running.draw;
        actor.render =
            EngineRender{resize.from, resize.holding, costs, std::nullopt};
        actor.resize =
            EngineResize{resize.to, UnpublishedWork{draw.total, draw.drawn}};
      });
  return actor;
}

[[nodiscard]] inline Simulation Started(Script a_script, World a_world = {}) {
  Simulation simulation;
  for (const auto &[id, record] : a_world.actors) {
    if (record.render) {
      simulation.shell.actors[id] = EngineActorOf(*record.render);
    }
  }
  simulation.world = std::move(a_world);
  simulation.shell.script = std::move(a_script);
  return simulation;
}

[[nodiscard]] inline bool Settled(const World &a_world,
                                  const std::vector<Command> &a_commands) {
  for (const auto &[id, record] : a_world.actors) {
    if (record.render && !Is<Shown>(record.render->phase) &&
        !Is<Empty>(record.render->phase)) {
      return false;
    }
  }
  for (const Command &command : a_commands) {
    if (!Is<TickActor>(command) && !Is<DrawUnpublished>(command) &&
        !Is<KeepModels>(command) && !Is<SettleApplications>(command) &&
        !Is<Observe>(command)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] inline std::uint64_t RenderedHeldBytes(const Shell &a_shell,
                                                     const World &a_world) {
  std::uint64_t bytes = 0;
  for (const auto &[id, record] : a_world.actors) {
    const auto found = a_shell.actors.find(id);
    if (record.render && record.render->renderedFrame &&
        found != a_shell.actors.end()) {
      bytes = Sum(bytes, HeldBytes(found->second));
    }
  }
  return bytes;
}

[[nodiscard]] inline std::optional<Violation>
SettledEstimateViolation(std::uint64_t a_tick, const World &a_world,
                         const std::vector<Command> &a_commands,
                         const Shell &a_executed) {
  if (!Settled(a_world, a_commands)) {
    return std::nullopt;
  }
  const std::uint64_t estimate = PlannedBytesOf(a_world);
  const std::uint64_t held = RenderedHeldBytes(a_executed, a_world);
  if (estimate == held) {
    return std::nullopt;
  }
  return Violation{a_tick, Rule::kEstimateDiffers, std::nullopt,
                   "estimate " + std::to_string(estimate) + ", held " +
                       std::to_string(held)};
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
  if (std::optional<Violation> estimate = SettledEstimateViolation(
          tick, output.world, output.commands, a_simulation.shell)) {
    a_simulation.violations.push_back(std::move(*estimate));
  }
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
