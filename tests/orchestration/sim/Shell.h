// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "orchestration/Tick.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace sim {
using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Orchestration;

struct SightingFrom {
  std::uint64_t tick = 0;
  Sighting sighting;
};

struct StaleFrom {
  std::uint64_t tick = 0;
  Actors::PlannedActor planned;
};

struct Faults {
  bool installFails = false;
  bool planFails = false;
  PerLevel<bool> resizeNotStarted;
  PerLevel<bool> swapMismatch;
};

struct ActorScript {
  std::vector<SightingFrom> sightings;
  std::vector<StaleFrom> stale;
  PerLevel<std::optional<LevelCost>> levelCosts;
  std::size_t slots = 4;
  std::size_t stacks = 4;
  std::size_t stacksDrawnAtStart = 0;
  std::optional<std::uint64_t> planTicks;
  bool withoutRender = false;
  Faults faults;
  std::vector<std::uint64_t> forgets;
};

struct MemoryScript {
  std::uint64_t everyTicks = 0;
  std::uint64_t allowanceBytes = 0;
};

struct Script {
  std::map<ActorId, ActorScript> actors;
  TickSettings settings;
  Millis tickMs = 16;
  std::size_t drawUnitsPerTick = 4;
  MemoryScript memory;
};

struct UnpublishedWork {
  std::size_t total = 0;
  std::size_t drawn = 0;
};

struct EngineRender {
  ResolutionLevel level = ResolutionLevel::kFull;
  IntermediateHolding holding = IntermediateHolding::kHeld;
  PerLevel<std::optional<LevelCost>> levelCosts;
  std::optional<UnpublishedWork> firstDraw;
};

struct EngineResize {
  ResolutionLevel to = ResolutionLevel::kFull;
  UnpublishedWork draw;
};

struct AwaitedPlanJob {
  PlanJobId job{};
  bool finished = false;
};

struct EngineActor {
  std::optional<EngineRender> render;
  std::optional<EngineResize> resize;
  std::optional<AwaitedPlanJob> awaiting;
  bool withoutRender = false;
};

struct PendingJob {
  ActorId actor{};
  PlanJobId job{};
  std::uint64_t doneAtTick = 0;
  bool fails = false;
};

struct Shell {
  Script script;
  std::uint64_t tick = 0;
  std::map<ActorId, EngineActor> actors;
  std::vector<PendingJob> jobs;
  std::uint64_t nextJob = 1;
  std::vector<CommandResult> results;
  std::vector<ActorId> observed;
  std::array<std::size_t, std::variant_size_v<Command>> executed{};
  std::size_t unitsDrawn = 0;
};

struct Disagreement {
  ActorId actor{};
  std::string what;
};

struct Turn {
  Shell shell;
  TickFacts facts;
};

struct Execution {
  Shell shell;
  std::vector<Disagreement> disagreements;
};

template <class T> [[nodiscard]] std::size_t CountOf(const Shell &a_shell) {
  return a_shell.executed[Command{T{}}.index()];
}

[[nodiscard]] inline const ActorScript *ScriptOf(const Script &a_script,
                                                 ActorId a_actor) {
  const auto found = a_script.actors.find(a_actor);
  return found == a_script.actors.end() ? nullptr : &found->second;
}

[[nodiscard]] inline Sighting
SightingAt(const Script &a_script, ActorId a_actor, std::uint64_t a_tick) {
  const ActorScript *actor = ScriptOf(a_script, a_actor);
  Sighting sighting;
  if (!actor) {
    return sighting;
  }
  for (const SightingFrom &from : actor->sightings) {
    if (from.tick <= a_tick) {
      sighting = from.sighting;
    }
  }
  return sighting;
}

[[nodiscard]] inline std::optional<Actors::PlannedActor>
StaleAt(const Script &a_script, ActorId a_actor, std::uint64_t a_tick) {
  const ActorScript *actor = ScriptOf(a_script, a_actor);
  if (!actor) {
    return std::nullopt;
  }
  for (const StaleFrom &stale : actor->stale) {
    if (stale.tick == a_tick) {
      return stale.planned;
    }
  }
  return std::nullopt;
}

[[nodiscard]] inline Actors::PlannedActor
LatestPlan(const Script &a_script, ActorId a_actor, std::uint64_t a_tick) {
  const ActorScript *actor = ScriptOf(a_script, a_actor);
  Actors::PlannedActor planned;
  if (!actor) {
    return planned;
  }
  for (const StaleFrom &stale : actor->stale) {
    if (stale.tick <= a_tick) {
      planned = stale.planned;
    }
  }
  return planned;
}

[[nodiscard]] inline std::uint64_t HeldBytes(const EngineActor &a_actor) {
  std::uint64_t bytes = 0;
  if (a_actor.render) {
    const std::optional<LevelCost> &cost =
        a_actor.render->levelCosts.At(a_actor.render->level);
    bytes = cost ? PlannedBytes(*cost, a_actor.render->holding) : 0;
    if (a_actor.resize) {
      const std::optional<LevelCost> &target =
          a_actor.render->levelCosts.At(a_actor.resize->to);
      bytes = Sum(bytes, target ? target->withIntermediates : 0);
    }
  }
  return bytes;
}

[[nodiscard]] inline std::uint64_t HeldBytes(const Shell &a_shell) {
  std::uint64_t bytes = 0;
  for (const auto &[id, actor] : a_shell.actors) {
    bytes = Sum(bytes, HeldBytes(actor));
  }
  return bytes;
}

[[nodiscard]] inline std::optional<BudgetSample>
MemorySampleAt(const Shell &a_shell) {
  const MemoryScript &memory = a_shell.script.memory;
  if (memory.everyTicks == 0 || a_shell.tick % memory.everyTicks != 0) {
    return std::nullopt;
  }
  const std::uint64_t held = HeldBytes(a_shell);
  const std::uint64_t budget = std::uint64_t{64} << 30;
  return BudgetSample{VideoMemory{budget, std::min(held, budget)}, held,
                      memory.allowanceBytes};
}

[[nodiscard]] inline std::vector<PlanJobDone>
FinishedJobs(std::vector<PendingJob> &a_jobs, std::uint64_t a_tick) {
  std::vector<PlanJobDone> done;
  std::vector<PendingJob> waiting;
  for (const PendingJob &job : a_jobs) {
    if (job.doneAtTick > a_tick) {
      waiting.push_back(job);
    } else if (job.fails) {
      done.push_back(PlanJobFailed{job.job, "plan job failed"});
    } else {
      done.push_back(PlanJobBuilt{job.job});
    }
  }
  a_jobs = std::move(waiting);
  return done;
}

inline void MarkFinished(Shell &a_shell,
                         const std::vector<PlanJobDone> &a_done) {
  for (const PlanJobDone &done : a_done) {
    const PlanJobId job = Match(
        done, [](const PlanJobBuilt &a_built) { return a_built.job; },
        [](const PlanJobFailed &a_failed) { return a_failed.job; });
    for (auto &[id, actor] : a_shell.actors) {
      if (actor.awaiting && actor.awaiting->job == job) {
        actor.awaiting->finished = true;
      }
    }
  }
}

[[nodiscard]] inline Turn NextFacts(Shell a_shell, const World &a_world) {
  ++a_shell.tick;
  const std::uint64_t tick = a_shell.tick;
  TickFacts facts;
  facts.now = tick * a_shell.script.tickMs;
  facts.frame = tick;
  facts.settings = a_shell.script.settings;
  facts.results = std::exchange(a_shell.results, {});
  facts.jobs = FinishedJobs(a_shell.jobs, tick);
  MarkFinished(a_shell, facts.jobs);
  facts.memory = MemorySampleAt(a_shell);
  std::vector<ActorId> stale = std::exchange(a_shell.observed, {});
  for (const auto &[id, actor] : a_shell.script.actors) {
    if (StaleAt(a_shell.script, id, tick)) {
      stale.push_back(id);
    }
  }
  std::sort(stale.begin(), stale.end());
  stale.erase(std::unique(stale.begin(), stale.end()), stale.end());
  for (const ActorId id : stale) {
    const Sighting sighting = SightingAt(a_shell.script, id, tick);
    facts.stale.push_back(StaleActor{
        id, Actors::ActorFacts{id, sighting.loaded3D, sighting.distance, {}},
        LatestPlan(a_shell.script, id, tick)});
    facts.sightings[id] = sighting;
  }
  for (const auto &[id, record] : a_world.actors) {
    facts.sightings[id] = SightingAt(a_shell.script, id, tick);
  }
  for (const auto &[id, actor] : a_shell.script.actors) {
    if (std::ranges::contains(actor.forgets, tick)) {
      facts.forgets.push_back(id);
    }
  }
  return {std::move(a_shell), std::move(facts)};
}

struct Executor {
  Shell &shell;
  std::vector<Disagreement> &disagreements;

  void Disagree(ActorId a_actor, std::string a_what) {
    disagreements.push_back({a_actor, std::move(a_what)});
  }

  void Report(ActorId a_actor, RenderEvent a_event) {
    shell.results.push_back(ActorEvent{a_actor, std::move(a_event)});
  }

  [[nodiscard]] ActorScript ScriptFor(ActorId a_actor) const {
    const ActorScript *script = ScriptOf(shell.script, a_actor);
    return script ? *script : ActorScript{};
  }

  void Build(ActorId a_actor, const InstallBound &a_bound) {
    const ActorScript script = ScriptFor(a_actor);
    EngineActor &actor = shell.actors[a_actor];
    actor.awaiting.reset();
    if (script.withoutRender) {
      actor.withoutRender = true;
      Report(a_actor, InstalledWithoutRender{});
      return;
    }
    LevelBytes bytes;
    for (const ResolutionLevel level : kResolutionLevels) {
      const std::optional<LevelCost> &cost = script.levelCosts.At(level);
      bytes.At(level) =
          cost ? std::optional<std::uint64_t>{cost->withIntermediates}
               : std::nullopt;
    }
    const ResolutionLevel level =
        FittingLevel(a_bound.finest, bytes, a_bound.headroom);
    actor.render =
        EngineRender{level, IntermediateHolding::kHeld, script.levelCosts,
                     UnpublishedWork{script.slots, 0}};
    Report(a_actor, Installed{level, script.slots, script.levelCosts,
                              bytes.At(level).value_or(0)});
  }

  void Drop(ActorId a_actor) {
    EngineActor &actor = shell.actors[a_actor];
    if (actor.awaiting) {
      const PlanJobId job = actor.awaiting->job;
      std::erase_if(shell.jobs, [job](const PendingJob &a_job) {
        return a_job.job == job;
      });
    }
    actor = EngineActor{};
  }

  void operator()(const TickActor &a_command) {
    auto found = shell.actors.find(a_command.actor);
    if (found == shell.actors.end()) {
      Disagree(a_command.actor, "tick of an actor the engine does not track");
      return;
    }
    if (a_command.drawStacks && found->second.render) {
      found->second.render->holding = IntermediateHolding::kHeld;
    }
    if (!a_command.concealSlots && found->second.render) {
      found->second.render->firstDraw.reset();
    }
  }

  void operator()(const StartResize &a_command) {
    EngineActor &actor = shell.actors[a_command.actor];
    if (!actor.render || actor.resize) {
      Disagree(a_command.actor, actor.render
                                    ? "resize started while one exists"
                                    : "resize started without a render");
      Report(a_command.actor, ResizeNotStarted{"no render to resize"});
      return;
    }
    const ActorScript script = ScriptFor(a_command.actor);
    if (script.faults.resizeNotStarted.At(a_command.to)) {
      Report(a_command.actor, ResizeNotStarted{"injected: resize not started"});
      return;
    }
    const std::size_t drawn =
        std::min(script.stacksDrawnAtStart, script.stacks);
    actor.resize =
        EngineResize{a_command.to, UnpublishedWork{script.stacks, drawn}};
    Report(a_command.actor, ResizeStarted{script.stacks, drawn});
  }

  void operator()(const SwapResize &a_command) {
    EngineActor &actor = shell.actors[a_command.actor];
    if (!actor.render || !actor.resize) {
      Disagree(a_command.actor, "swap without a resize");
      Report(a_command.actor, ResizeNotSwapped{});
      return;
    }
    if (ScriptFor(a_command.actor).faults.swapMismatch.At(actor.resize->to)) {
      Report(a_command.actor, ResizeNotSwapped{});
      return;
    }
    actor.render->level = actor.resize->to;
    actor.render->holding = IntermediateHolding::kHeld;
    actor.resize.reset();
    Report(a_command.actor, ResizeSwapped{});
  }

  void operator()(const AbandonResize &a_command) {
    EngineActor &actor = shell.actors[a_command.actor];
    if (!actor.resize) {
      Disagree(a_command.actor, "abandon without a resize");
    }
    actor.resize.reset();
  }

  void operator()(const ReleaseIntermediates &a_command) {
    EngineActor &actor = shell.actors[a_command.actor];
    if (!actor.render ||
        actor.render->holding == IntermediateHolding::kReleased) {
      Disagree(a_command.actor, "release without held intermediates");
      return;
    }
    const std::optional<LevelCost> &cost =
        actor.render->levelCosts.At(actor.render->level);
    if (cost && IntermediateBytes(*cost) != a_command.bytes) {
      Disagree(a_command.actor, "release bytes differ from the held level");
    }
    actor.render->holding = IntermediateHolding::kReleased;
  }

  void operator()(const SetAppliedLevel &a_command) {
    const EngineActor &actor = shell.actors[a_command.actor];
    if (!actor.render || actor.render->level != a_command.level) {
      Disagree(a_command.actor, "applied level differs from the render");
    }
  }

  [[nodiscard]] UnpublishedWork *WorkOf(EngineActor &a_actor) {
    if (a_actor.resize) {
      return &a_actor.resize->draw;
    }
    if (a_actor.render && a_actor.render->firstDraw) {
      return &*a_actor.render->firstDraw;
    }
    return nullptr;
  }

  void operator()(const DrawUnpublished &a_command) {
    std::size_t units = shell.script.drawUnitsPerTick;
    for (const ActorId id : a_command.order) {
      UnpublishedWork *work = WorkOf(shell.actors[id]);
      if (!work) {
        Disagree(id, "unpublished draw without a render");
        continue;
      }
      const std::size_t drawn = std::min(units, work->total - work->drawn);
      work->drawn += drawn;
      units -= drawn;
      shell.unitsDrawn += drawn;
      Report(id, UnpublishedDrawn{work->drawn, drawn > 0});
    }
  }

  void operator()(const BuildAwaitedRender &a_command) {
    const EngineActor &actor = shell.actors[a_command.actor];
    if (!actor.awaiting || !actor.awaiting->finished) {
      Disagree(a_command.actor, "build without a finished plan job");
    }
    Build(a_command.actor, a_command.bound);
  }

  void operator()(const FailAwaitedRender &a_command) {
    EngineActor &actor = shell.actors[a_command.actor];
    if (!actor.awaiting) {
      Disagree(a_command.actor, "failed render without a plan job");
    }
    actor.awaiting.reset();
    actor.withoutRender = true;
  }

  void operator()(const Install &a_command) {
    const auto found = shell.actors.find(a_command.actor);
    const bool installed = found != shell.actors.end() &&
                           (found->second.render || found->second.awaiting ||
                            found->second.withoutRender);
    if (installed && a_command.kind == InstallKind::kInstall) {
      Disagree(a_command.actor, "install over an installed actor");
    }
    if (installed && a_command.kind == InstallKind::kReinstall) {
      Drop(a_command.actor);
      shell.results.push_back(Retired{a_command.actor});
    }
    const ActorScript script = ScriptFor(a_command.actor);
    shell.actors.try_emplace(a_command.actor);
    if (script.faults.installFails) {
      shell.results.push_back(
          InstallFailed{a_command.actor, "injected: install failed"});
      return;
    }
    if (script.planTicks) {
      const PlanJobId job{shell.nextJob++};
      shell.actors[a_command.actor].awaiting = AwaitedPlanJob{job, false};
      shell.jobs.push_back(PendingJob{a_command.actor, job,
                                      shell.tick + *script.planTicks,
                                      script.faults.planFails});
      Report(a_command.actor, PlanAwaited{job});
      return;
    }
    Build(a_command.actor, a_command.bound);
  }

  void operator()(const Retire &a_command) {
    Drop(a_command.actor);
    shell.results.push_back(Retired{a_command.actor});
  }

  void operator()(const Forget &a_command) {
    Drop(a_command.actor);
    shell.actors.erase(a_command.actor);
  }

  void operator()(const Observe &a_command) {
    shell.observed.push_back(a_command.actor);
  }

  void operator()(const KeepModels &) {}

  void operator()(const SettleApplications &) {}
};

[[nodiscard]] inline Execution
Executed(Shell a_shell, const std::vector<Command> &a_commands) {
  std::vector<Disagreement> disagreements;
  Executor executor{a_shell, disagreements};
  for (const Command &command : a_commands) {
    ++a_shell.executed[command.index()];
    Match(command, [&executor](const auto &a_command) { executor(a_command); });
  }
  return {std::move(a_shell), std::move(disagreements)};
}

[[nodiscard]] inline PerLevel<std::optional<LevelCost>>
HalvingCosts(std::uint64_t a_fullBytes) {
  PerLevel<std::optional<LevelCost>> costs;
  std::uint64_t bytes = a_fullBytes;
  for (const ResolutionLevel level : kResolutionLevels) {
    costs.At(level) = LevelCost{bytes, bytes / 2};
    bytes /= 4;
  }
  return costs;
}
}
