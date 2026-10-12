// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "orchestration/sim/Invariants.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sim {
enum class CommandKind : std::uint8_t {
  kTickActor,
  kStartResize,
  kSwapResize,
  kAbandonResize,
  kReleaseIntermediates,
  kSetAppliedLevel,
  kDrawUnpublished,
  kBuildAwaitedRender,
  kFailAwaitedRender,
  kCancelPlanJob,
  kInstall,
  kRestage,
  kSwapRestage,
  kAbandonRestage,
  kRetire,
  kForget,
  kObserve,
  kKeepModels,
  kSettleApplications,
};

enum class Field : std::uint8_t {
  kActorSet,
  kSight,
  kSyncState,
  kReinstallLevel,
  kReturnBytes,
  kRenderCreated,
  kRenderErased,
  kLevelCosts,
  kRenderedFrame,
  kBudget,
  kRadius,
  kWaitingInstalls,
  kSyncLog,
  kTimers,
};

struct WrittenField {
  Field field = Field::kSight;
  std::optional<ActorId> actor;
};

using Step = TickOutput (*)(World, const TickFacts &);

struct StepRow {
  std::string_view name;
  Step run = nullptr;
  std::set<CommandKind> commands;
  std::set<Field> writes;
};

struct StepsOutcome {
  TickOutput output;
  std::vector<Violation> violations;
};

[[nodiscard]] inline CommandKind KindOf(const Command &a_command) {
  return Match(
      a_command, [](const TickActor &) { return CommandKind::kTickActor; },
      [](const StartResize &) { return CommandKind::kStartResize; },
      [](const SwapResize &) { return CommandKind::kSwapResize; },
      [](const AbandonResize &) { return CommandKind::kAbandonResize; },
      [](const ReleaseIntermediates &) {
        return CommandKind::kReleaseIntermediates;
      },
      [](const SetAppliedLevel &) { return CommandKind::kSetAppliedLevel; },
      [](const DrawUnpublished &) { return CommandKind::kDrawUnpublished; },
      [](const BuildAwaitedRender &) {
        return CommandKind::kBuildAwaitedRender;
      },
      [](const FailAwaitedRender &) { return CommandKind::kFailAwaitedRender; },
      [](const CancelPlanJob &) { return CommandKind::kCancelPlanJob; },
      [](const Install &) { return CommandKind::kInstall; },
      [](const Restage &) { return CommandKind::kRestage; },
      [](const SwapRestage &) { return CommandKind::kSwapRestage; },
      [](const AbandonRestage &) { return CommandKind::kAbandonRestage; },
      [](const Retire &) { return CommandKind::kRetire; },
      [](const Forget &) { return CommandKind::kForget; },
      [](const Observe &) { return CommandKind::kObserve; },
      [](const KeepModels &) { return CommandKind::kKeepModels; },
      [](const SettleApplications &) {
        return CommandKind::kSettleApplications;
      });
}

[[nodiscard]] inline std::string_view KindName(CommandKind a_kind) noexcept {
  switch (a_kind) {
  case CommandKind::kTickActor:
    return "TickActor";
  case CommandKind::kStartResize:
    return "StartResize";
  case CommandKind::kSwapResize:
    return "SwapResize";
  case CommandKind::kAbandonResize:
    return "AbandonResize";
  case CommandKind::kReleaseIntermediates:
    return "ReleaseIntermediates";
  case CommandKind::kSetAppliedLevel:
    return "SetAppliedLevel";
  case CommandKind::kDrawUnpublished:
    return "DrawUnpublished";
  case CommandKind::kBuildAwaitedRender:
    return "BuildAwaitedRender";
  case CommandKind::kFailAwaitedRender:
    return "FailAwaitedRender";
  case CommandKind::kCancelPlanJob:
    return "CancelPlanJob";
  case CommandKind::kInstall:
    return "Install";
  case CommandKind::kRestage:
    return "Restage";
  case CommandKind::kSwapRestage:
    return "SwapRestage";
  case CommandKind::kAbandonRestage:
    return "AbandonRestage";
  case CommandKind::kRetire:
    return "Retire";
  case CommandKind::kForget:
    return "Forget";
  case CommandKind::kObserve:
    return "Observe";
  case CommandKind::kKeepModels:
    return "KeepModels";
  case CommandKind::kSettleApplications:
    return "SettleApplications";
  }
  return "unknown command";
}

[[nodiscard]] inline std::string_view FieldName(Field a_field) noexcept {
  switch (a_field) {
  case Field::kActorSet:
    return "the actor set";
  case Field::kSight:
    return "TrackedActor::sight";
  case Field::kSyncState:
    return "TrackedActor::syncState";
  case Field::kReinstallLevel:
    return "TrackedActor::reinstallLevel";
  case Field::kReturnBytes:
    return "TrackedActor::returnBytes";
  case Field::kRenderCreated:
    return "render created";
  case Field::kRenderErased:
    return "render erased";
  case Field::kLevelCosts:
    return "InstalledRender::levelCosts";
  case Field::kRenderedFrame:
    return "InstalledRender::renderedFrame";
  case Field::kBudget:
    return "World::budget";
  case Field::kRadius:
    return "World::radius";
  case Field::kWaitingInstalls:
    return "World::waitingInstalls";
  case Field::kSyncLog:
    return "World::syncLog";
  case Field::kTimers:
    return "World::timers";
  }
  return "unknown field";
}

[[nodiscard]] inline const std::array<StepRow, 5> &StepRows() {
  static const std::array<StepRow, 5> rows{{
      {"RecordSyncResults",
       RecordSyncResults,
       {},
       {Field::kActorSet, Field::kSyncState, Field::kRenderErased,
        Field::kReinstallLevel, Field::kReturnBytes, Field::kWaitingInstalls,
        Field::kBudget}},
      {"RecordRenderResults",
       RecordRenderResults,
       {CommandKind::kSetAppliedLevel, CommandKind::kAbandonResize,
        CommandKind::kBuildAwaitedRender, CommandKind::kFailAwaitedRender,
        CommandKind::kAbandonRestage},
       {Field::kRenderCreated, Field::kLevelCosts, Field::kRenderedFrame,
        Field::kSight, Field::kBudget}},
      {"RunBudget",
       RunBudget,
       {CommandKind::kReleaseIntermediates, CommandKind::kAbandonResize,
        CommandKind::kObserve},
       {Field::kBudget, Field::kRadius}},
      {"RunSync",
       RunSync,
       {CommandKind::kInstall, CommandKind::kRestage, CommandKind::kRetire,
        CommandKind::kForget, CommandKind::kCancelPlanJob,
        CommandKind::kObserve, CommandKind::kKeepModels,
        CommandKind::kSettleApplications},
       {Field::kActorSet, Field::kSyncState, Field::kSyncLog,
        Field::kWaitingInstalls, Field::kTimers, Field::kBudget,
        Field::kReinstallLevel, Field::kReturnBytes, Field::kRenderErased}},
      {"RunLifecycle",
       RunLifecycle,
       {CommandKind::kSwapResize, CommandKind::kSwapRestage,
        CommandKind::kStartResize, CommandKind::kAbandonResize,
        CommandKind::kReleaseIntermediates, CommandKind::kBuildAwaitedRender,
        CommandKind::kFailAwaitedRender, CommandKind::kTickActor,
        CommandKind::kDrawUnpublished},
       {Field::kBudget}},
  }};
  return rows;
}

[[nodiscard]] inline bool SameWaiting(const WaitingInstall &a_left,
                                      const WaitingInstall &a_right) {
  return a_left.actor == a_right.actor && a_left.role == a_right.role &&
         a_left.shown == a_right.shown &&
         a_left.screenFraction == a_right.screenFraction &&
         a_left.sinceMS == a_right.sinceMS;
}

[[nodiscard]] inline bool
SameWaiting(const std::vector<WaitingInstall> &a_left,
            const std::vector<WaitingInstall> &a_right) {
  if (a_left.size() != a_right.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a_left.size(); ++i) {
    if (!SameWaiting(a_left[i], a_right[i])) {
      return false;
    }
  }
  return true;
}

inline void RecordWrites(std::vector<WrittenField> &a_written, ActorId a_actor,
                         const TrackedActor &a_before,
                         const TrackedActor &a_after, bool a_inserted) {
  const auto wrote = [&a_written, a_actor](Field a_field) {
    a_written.push_back({a_field, a_actor});
  };
  if (!a_inserted && a_before.sight != a_after.sight) {
    wrote(Field::kSight);
  }
  if (a_before.syncState != a_after.syncState) {
    wrote(Field::kSyncState);
  }
  if (a_before.reinstallLevel != a_after.reinstallLevel) {
    wrote(Field::kReinstallLevel);
  }
  if (a_before.returnBytes != a_after.returnBytes) {
    wrote(Field::kReturnBytes);
  }
  if (!a_before.render && a_after.render) {
    wrote(Field::kRenderCreated);
  }
  if (a_before.render && !a_after.render) {
    wrote(Field::kRenderErased);
  }
  if (a_before.render && a_after.render) {
    if (a_before.render->levelCosts != a_after.render->levelCosts) {
      wrote(Field::kLevelCosts);
    }
    if (a_before.render->renderedFrame != a_after.render->renderedFrame) {
      wrote(Field::kRenderedFrame);
    }
  }
}

[[nodiscard]] inline std::vector<WrittenField>
WrittenFields(const World &a_before, const World &a_after) {
  std::vector<WrittenField> written;
  const TrackedActor absent;
  for (const auto &[id, after] : a_after.actors) {
    const auto found = a_before.actors.find(id);
    const bool inserted = found == a_before.actors.end();
    if (inserted) {
      written.push_back({Field::kActorSet, id});
    }
    RecordWrites(written, id, inserted ? absent : found->second, after,
                 inserted);
  }
  for (const auto &[id, before] : a_before.actors) {
    if (!a_after.actors.contains(id)) {
      written.push_back({Field::kActorSet, id});
    }
  }
  const auto wrote = [&written](Field a_field) {
    written.push_back({a_field, std::nullopt});
  };
  if (!(a_before.budget == a_after.budget)) {
    wrote(Field::kBudget);
  }
  if (a_before.radius != a_after.radius) {
    wrote(Field::kRadius);
  }
  if (!SameWaiting(a_before.waitingInstalls, a_after.waitingInstalls)) {
    wrote(Field::kWaitingInstalls);
  }
  if (a_before.syncLog != a_after.syncLog) {
    wrote(Field::kSyncLog);
  }
  if (a_before.timers != a_after.timers) {
    wrote(Field::kTimers);
  }
  return written;
}

[[nodiscard]] inline std::vector<RenderPhase> PhasesOf(const World &a_world) {
  std::vector<RenderPhase> phases;
  for (const auto &[id, record] : a_world.actors) {
    if (record.render) {
      phases.push_back(record.render->phase);
    }
  }
  return phases;
}

[[nodiscard]] inline std::vector<Violation>
CheckStepOutput(const StepRow &a_row, const World &a_before,
                const TickOutput &a_output, std::uint64_t a_tick) {
  std::vector<Violation> violations;
  const std::string step{a_row.name};
  for (const Command &command : a_output.commands) {
    const CommandKind kind = KindOf(command);
    if (!a_row.commands.contains(kind)) {
      violations.push_back({a_tick, Rule::kStepCommandOutsideRow,
                            CommandActor(command),
                            step + " issued " + std::string{KindName(kind)}});
    }
  }
  for (const WrittenField &written : WrittenFields(a_before, a_output.world)) {
    if (!a_row.writes.contains(written.field)) {
      violations.push_back(
          {a_tick, Rule::kStepWroteOutsideRow, written.actor,
           step + " wrote " + std::string{FieldName(written.field)}});
    }
  }
  return violations;
}

[[nodiscard]] inline bool SameKinds(const std::vector<Command> &a_left,
                                    const std::vector<Command> &a_right) {
  if (a_left.size() != a_right.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a_left.size(); ++i) {
    if (KindOf(a_left[i]) != KindOf(a_right[i])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] inline StepsOutcome RunStepsChecked(const World &a_world,
                                                  const TickFacts &a_facts,
                                                  std::uint64_t a_tick) {
  StepsOutcome outcome{TickOutput{a_world, {}, {}, {}}, {}};
  for (const StepRow &row : StepRows()) {
    TickOutput next = row.run(outcome.output.world, a_facts);
    std::vector<Violation> found =
        CheckStepOutput(row, outcome.output.world, next, a_tick);
    outcome.violations.insert(outcome.violations.end(), found.begin(),
                              found.end());
    outcome.output.world = std::move(next.world);
    for (Command &command : next.commands) {
      outcome.output.commands.push_back(std::move(command));
    }
    for (TraceRecord &record : next.trace) {
      outcome.output.trace.push_back(std::move(record));
    }
    for (LogRecord &record : next.logs) {
      outcome.output.logs.push_back(std::move(record));
    }
  }
  return outcome;
}

[[nodiscard]] inline std::vector<Violation>
CheckTickMatchesSteps(const TickOutput &a_tick, const TickOutput &a_steps,
                      std::uint64_t a_tickNumber) {
  std::vector<Violation> violations;
  if (!WrittenFields(a_steps.world, a_tick.world).empty() ||
      PhasesOf(a_steps.world) != PhasesOf(a_tick.world)) {
    violations.push_back({a_tickNumber, Rule::kTickDiffersFromSteps,
                          std::nullopt,
                          "Tick's world differs from its steps run in order"});
  }
  if (!SameKinds(a_tick.commands, a_steps.commands)) {
    violations.push_back(
        {a_tickNumber, Rule::kTickDiffersFromSteps, std::nullopt,
         "Tick's commands differ from its steps run in order"});
  }
  return violations;
}
}
