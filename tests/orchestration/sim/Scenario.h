// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "actors/fixtures.h"
#include "orchestration/sim/Fixtures.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sim {
template <class T> struct Issued {
  std::uint64_t tick = 0;
  T command;
};

template <class T> struct Reported {
  std::uint64_t tick = 0;
  ActorId actor{};
  T event;
};

struct Traced {
  std::uint64_t tick = 0;
  const TraceRecord *record = nullptr;
};

using PhasesByActor = std::map<ActorId, std::string_view>;

struct WatchedRun {
  Simulation simulation;
  std::vector<PhasesByActor> phasesBefore;
};

[[nodiscard]] inline Actors::PlannedActor PlanNamed(std::string a_instance,
                                                    std::uint32_t a_item) {
  actorfixtures::PlanBuilder builder;
  builder.With(actorfixtures::Instance(std::move(a_instance)),
               actorfixtures::Geo(a_item, "body"));
  return Actors::PlannedActor{builder.plan, {}};
}

[[nodiscard]] inline ActorScript Arriving(Sighting a_sight,
                                          std::uint64_t a_tick,
                                          Actors::PlannedActor a_planned) {
  ActorScript actor = ScriptedActor(std::move(a_sight));
  actor.stale.push_back({a_tick, std::move(a_planned)});
  return actor;
}

[[nodiscard]] inline ActorRecord RenderedRecord(RenderPhase a_phase,
                                                Sighting a_sight) {
  ActorRecord record = RecordOf(RenderIn(std::move(a_phase)), a_sight);
  if (record.render) {
    record.render->renderedFrame = 0;
  }
  return record;
}

[[nodiscard]] inline const ActorRecord *RecordIn(const World &a_world,
                                                 ActorId a_actor) {
  const auto found = a_world.actors.find(a_actor);
  return found == a_world.actors.end() ? nullptr : &found->second;
}

[[nodiscard]] inline const RenderPhase *PhaseNow(const Simulation &a_sim,
                                                 ActorId a_actor) {
  return PhaseIn(a_sim.world, a_actor);
}

template <class T>
[[nodiscard]] std::vector<Issued<T>> IssuedOf(const Simulation &a_sim) {
  std::vector<Issued<T>> issued;
  for (const TickRecord &record : a_sim.history) {
    for (const Command &command : record.commands) {
      if (const T *found = Get<T>(command)) {
        issued.push_back({record.tick, *found});
      }
    }
  }
  return issued;
}

template <class T>
[[nodiscard]] std::vector<Issued<T>> IssuedFor(const Simulation &a_sim,
                                               ActorId a_actor) {
  std::vector<Issued<T>> issued;
  for (Issued<T> &one : IssuedOf<T>(a_sim)) {
    if (one.command.actor == a_actor) {
      issued.push_back(std::move(one));
    }
  }
  return issued;
}

template <class T>
[[nodiscard]] std::vector<Reported<T>> ReportedOf(const Simulation &a_sim,
                                                  ActorId a_actor) {
  std::vector<Reported<T>> reported;
  for (const TickRecord &record : a_sim.history) {
    for (const CommandResult &result : record.facts.results) {
      const ActorEvent *event = Get<ActorEvent>(result);
      const T *found = event ? Get<T>(event->event) : nullptr;
      if (found && event->actor == a_actor) {
        reported.push_back({record.tick, event->actor, *found});
      }
    }
  }
  return reported;
}

[[nodiscard]] inline std::string FieldOf(const TraceRecord &a_record,
                                         std::string_view a_name) {
  for (const Trace::Field &field : a_record.fields) {
    if (field.name == a_name) {
      return std::string{field.value};
    }
  }
  return {};
}

[[nodiscard]] inline std::vector<Traced> TracedOf(const Simulation &a_sim,
                                                  std::string_view a_action) {
  std::vector<Traced> traced;
  for (const TickRecord &record : a_sim.history) {
    for (const TraceRecord &trace : record.trace) {
      if (FieldOf(trace, "action") == a_action) {
        traced.push_back({record.tick, &trace});
      }
    }
  }
  return traced;
}

[[nodiscard]] inline const TickRecord *RecordAt(const Simulation &a_sim,
                                                std::uint64_t a_tick) {
  if (a_tick == 0 || a_tick > a_sim.history.size()) {
    return nullptr;
  }
  return &a_sim.history[a_tick - 1];
}

[[nodiscard]] inline PhasesByActor PhasesOfWorld(const World &a_world) {
  PhasesByActor phases;
  for (const auto &[id, record] : a_world.actors) {
    if (record.render) {
      phases[id] = PhaseName(record.render->phase);
    }
  }
  return phases;
}

[[nodiscard]] inline WatchedRun RanWatched(Simulation a_simulation,
                                           std::uint64_t a_ticks) {
  WatchedRun run{std::move(a_simulation), {}};
  for (std::uint64_t i = 0; i < a_ticks; ++i) {
    run.phasesBefore.push_back(PhasesOfWorld(run.simulation.world));
    run.simulation = Advanced(std::move(run.simulation));
  }
  return run;
}

[[nodiscard]] inline std::string_view
PhaseBefore(const WatchedRun &a_run, std::uint64_t a_tick, ActorId a_actor) {
  if (a_tick == 0 || a_tick > a_run.phasesBefore.size()) {
    return {};
  }
  const PhasesByActor &phases = a_run.phasesBefore[a_tick - 1];
  const auto found = phases.find(a_actor);
  return found == phases.end() ? std::string_view{} : found->second;
}
}
