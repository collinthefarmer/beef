// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "orchestration/sim/Shell.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sim {
enum class Rule : std::uint8_t {
  kReleaseOutsideLive,
  kOutOfViewReleaseEarly,
  kTwoResizesInFlight,
  kResizeToSameLevel,
  kCreatingEventIgnored,
  kCommandDisagreesWithPhase,
  kDrawUnpublishedNotLast,
  kEstimateDiffers,
  kFailedLevelAskedAgain,
  kStepCommandOutsideRow,
  kStepWroteOutsideRow,
  kTickDiffersFromSteps,
  kEngineDisagrees,
};

struct Violation {
  std::uint64_t tick = 0;
  Rule rule = Rule::kReleaseOutsideLive;
  std::optional<ActorId> actor;
  std::string detail;
};

struct TickView {
  std::uint64_t tick = 0;
  const World &before;
  const TickFacts &facts;
  const TickOutput &output;
};

struct EstimateRange {
  std::uint64_t least = 0;
  std::uint64_t most = 0;
};

[[nodiscard]] inline std::string_view RuleName(Rule a_rule) noexcept {
  switch (a_rule) {
  case Rule::kReleaseOutsideLive:
    return "release outside live";
  case Rule::kOutOfViewReleaseEarly:
    return "out-of-view release before its hold";
  case Rule::kTwoResizesInFlight:
    return "two resizes in flight";
  case Rule::kResizeToSameLevel:
    return "resize to the same level";
  case Rule::kCreatingEventIgnored:
    return "creating event ignored";
  case Rule::kCommandDisagreesWithPhase:
    return "command disagrees with phase";
  case Rule::kDrawUnpublishedNotLast:
    return "unpublished draw not last";
  case Rule::kEstimateDiffers:
    return "estimate differs from held costs";
  case Rule::kFailedLevelAskedAgain:
    return "failed level asked for again";
  case Rule::kStepCommandOutsideRow:
    return "step command outside its row";
  case Rule::kStepWroteOutsideRow:
    return "step wrote outside its row";
  case Rule::kTickDiffersFromSteps:
    return "tick differs from its steps";
  case Rule::kEngineDisagrees:
    return "engine disagrees";
  }
  return "unknown rule";
}

[[nodiscard]] inline std::string Describe(const Violation &a_violation) {
  std::string text = "tick " + std::to_string(a_violation.tick) + ": " +
                     std::string{RuleName(a_violation.rule)};
  if (a_violation.actor) {
    text += " (actor " + std::to_string(FormOf(*a_violation.actor)) + ")";
  }
  return text + ": " + a_violation.detail;
}

[[nodiscard]] inline bool Fired(const std::vector<Violation> &a_violations,
                                Rule a_rule) {
  for (const Violation &violation : a_violations) {
    if (violation.rule == a_rule) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] inline std::optional<ActorId>
CommandActor(const Command &a_command) {
  return Match(
      a_command,
      [](const DrawUnpublished &) -> std::optional<ActorId> {
        return std::nullopt;
      },
      [](const auto &a_actorCommand) -> std::optional<ActorId> {
        return a_actorCommand.actor;
      });
}

[[nodiscard]] inline const RenderPhase *PhaseIn(const World &a_world,
                                                ActorId a_actor) {
  const auto found = a_world.actors.find(a_actor);
  if (found == a_world.actors.end() || !found->second.render) {
    return nullptr;
  }
  return &found->second.render->phase;
}

[[nodiscard]] inline bool IsRunning(const RenderPhase &a_phase) {
  const Resizing *resizing = Get<Resizing>(a_phase);
  return resizing && Is<ResizeRunning>(resizing->stage);
}

[[nodiscard]] inline bool IsQueued(const RenderPhase &a_phase) {
  const Resizing *resizing = Get<Resizing>(a_phase);
  return resizing && Is<ResizeQueued>(resizing->stage);
}

template <class T>
[[nodiscard]] bool HasCommandFor(const std::vector<Command> &a_commands,
                                 ActorId a_actor) {
  for (const Command &command : a_commands) {
    if (const T *found = Get<T>(command); found && found->actor == a_actor) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] inline bool CreatesAPhase(const RenderEvent &a_event) {
  return Is<PlanAwaited>(a_event) || Is<Installed>(a_event) ||
         Is<InstalledWithoutRender>(a_event);
}

[[nodiscard]] inline std::uint64_t HeldCost(const InstalledRender &a_render,
                                            bool a_starting) {
  const std::optional<ResolutionLevel> applied = AppliedLevel(a_render.phase);
  if (!applied) {
    return 0;
  }
  const std::optional<LevelCost> &cost = a_render.levelCosts.At(*applied);
  std::uint64_t bytes =
      cost ? PlannedBytes(*cost, HoldingOf(a_render.phase)) : 0;
  if (IsRunning(a_render.phase) || (a_starting && IsQueued(a_render.phase))) {
    const std::optional<ResolutionLevel> target = TargetLevel(a_render.phase);
    const std::optional<LevelCost> &full =
        target ? a_render.levelCosts.At(*target) : cost;
    bytes = Sum(bytes, full ? full->withIntermediates : 0);
  }
  return bytes;
}

[[nodiscard]] inline EstimateRange
EstimateOf(const World &a_world, const std::vector<Command> &a_commands) {
  EstimateRange range;
  for (const auto &[id, record] : a_world.actors) {
    if (!record.render) {
      continue;
    }
    const std::uint64_t held =
        HeldCost(*record.render, HasCommandFor<StartResize>(a_commands, id));
    const bool leaving = HasCommandFor<Install>(a_commands, id) ||
                         HasCommandFor<Retire>(a_commands, id) ||
                         HasCommandFor<Forget>(a_commands, id);
    range.least = Sum(range.least, leaving ? 0 : held);
    range.most = Sum(range.most, held);
  }
  return range;
}

[[nodiscard]] inline bool ReleasedFromLive(const RenderPhase &a_phase) {
  const Resizing *resizing = Get<Resizing>(a_phase);
  if (resizing) {
    return IsQueued(a_phase) &&
           resizing->holding == IntermediateHolding::kReleased;
  }
  const Live *live = Get<Live>(a_phase);
  return live && Is<IntermediatesReleased>(live->intermediates);
}

struct Checker {
  const TickView &view;
  std::vector<Violation> violations;

  void Flag(Rule a_rule, std::optional<ActorId> a_actor, std::string a_detail) {
    violations.push_back({view.tick, a_rule, a_actor, std::move(a_detail)});
  }

  [[nodiscard]] const RenderPhase *After(ActorId a_actor) const {
    return PhaseIn(view.output.world, a_actor);
  }

  [[nodiscard]] const RenderPhase *Before(ActorId a_actor) const {
    return PhaseIn(view.before, a_actor);
  }

  void CheckRelease(const ReleaseIntermediates &a_command) {
    const RenderPhase *after = After(a_command.actor);
    if (!after || !ReleasedFromLive(*after)) {
      Flag(Rule::kReleaseOutsideLive, a_command.actor,
           "the actor is neither live nor resizing from a released render "
           "after the tick");
      return;
    }
    if (a_command.reason != ReleaseReason::kOutOfView) {
      return;
    }
    const RenderPhase *before = Before(a_command.actor);
    const Live *wasLive = before ? Get<Live>(*before) : nullptr;
    const IntermediatesHeld *held =
        wasLive ? Get<IntermediatesHeld>(wasLive->intermediates) : nullptr;
    const Millis delay = view.facts.settings.outOfViewReleaseDelayMs;
    if (!held) {
      Flag(Rule::kOutOfViewReleaseEarly, a_command.actor,
           "released out of view without a live hold the tick before");
      return;
    }
    if (delay > 0 && !held->outOfView.Due(view.facts.now, delay)) {
      Flag(Rule::kOutOfViewReleaseEarly, a_command.actor,
           "released out of view before the hold delay passed");
    }
  }

  void CheckAgainstPhase(const Command &a_command) {
    const std::optional<ActorId> actor = CommandActor(a_command);
    const RenderPhase *after = actor ? After(*actor) : nullptr;
    const auto disagree = [this, actor](std::string a_detail) {
      Flag(Rule::kCommandDisagreesWithPhase, actor, std::move(a_detail));
    };
    Match(
        a_command,
        [&](const TickActor &) {
          if (!after) {
            disagree("tick of an actor without a render");
          }
        },
        [&](const StartResize &a_start) {
          const Resizing *resizing = after ? Get<Resizing>(*after) : nullptr;
          if (!resizing || !Is<ResizeQueued>(resizing->stage) ||
              resizing->to != a_start.to) {
            disagree("resize started outside a queued resize to its level");
          }
        },
        [&](const SwapResize &) {
          if (!after || !IsRunning(*after)) {
            disagree("swap outside a running resize");
          }
        },
        [&](const AbandonResize &) {
          if (after && IsRunning(*after)) {
            disagree("abandoned resize still running");
          }
        },
        [&](const ReleaseIntermediates &a_release) { CheckRelease(a_release); },
        [&](const SetAppliedLevel &a_set) {
          if (!after || AppliedLevel(*after) != a_set.level) {
            disagree("applied level differs from the phase");
          }
        },
        [&](const DrawUnpublished &a_draw) { CheckDraw(a_draw); },
        [&](const BuildAwaitedRender &) {
          if (!after || !Is<AwaitingPlan>(*after)) {
            disagree("awaited render built outside awaiting plan");
          }
        },
        [&](const FailAwaitedRender &) {
          if (!after || !Is<WithoutRender>(*after)) {
            disagree("awaited render failed but the phase has a render");
          }
        },
        [&](const Install &a_install) {
          if (a_install.kind == InstallKind::kInstall &&
              Before(a_install.actor)) {
            disagree("fresh install over an installed render");
          }
        },
        [](const Retire &) {}, [](const Forget &) {}, [](const Observe &) {},
        [](const KeepModels &) {}, [](const SettleApplications &) {});
  }

  void CheckDraw(const DrawUnpublished &a_draw) {
    std::set<ActorId> seen;
    for (const ActorId actor : a_draw.order) {
      const RenderPhase *after = After(actor);
      if (!after || !UnpublishedDrawOf(*after)) {
        Flag(Rule::kCommandDisagreesWithPhase, actor,
             "unpublished draw for a phase without one");
      }
      if (!seen.insert(actor).second) {
        Flag(Rule::kCommandDisagreesWithPhase, actor,
             "actor listed twice in the unpublished draw");
      }
    }
  }

  void CheckDrawIsLast() {
    const std::vector<Command> &commands = view.output.commands;
    std::size_t draws = 0;
    for (std::size_t i = 0; i < commands.size(); ++i) {
      if (!Is<DrawUnpublished>(commands[i])) {
        continue;
      }
      ++draws;
      if (i + 1 != commands.size()) {
        Flag(Rule::kDrawUnpublishedNotLast, std::nullopt,
             "a command follows the unpublished draw");
      }
    }
    if (draws > 1) {
      Flag(Rule::kDrawUnpublishedNotLast, std::nullopt,
           "more than one unpublished draw");
    }
  }

  void CheckResizes() {
    std::size_t inFlight = 0;
    for (const auto &[id, record] : view.output.world.actors) {
      if (!record.render) {
        continue;
      }
      const RenderPhase &phase = record.render->phase;
      if (const Resizing *resizing = Get<Resizing>(phase)) {
        if (resizing->from == resizing->to) {
          Flag(Rule::kResizeToSameLevel, id, "resizing from a level to itself");
        }
        if (resizing->failed && resizing->failed->level == resizing->to) {
          Flag(Rule::kFailedLevelAskedAgain, id,
               "resizing to a level that failed and is still refused");
        }
      }
      const bool starting = IsQueued(phase) && HasCommandFor<StartResize>(
                                                   view.output.commands, id);
      const bool swapping = HasCommandFor<SwapResize>(view.output.commands, id);
      inFlight += (IsRunning(phase) && !swapping) || starting ? 1 : 0;
    }
    if (inFlight > 1) {
      Flag(Rule::kTwoResizesInFlight, std::nullopt,
           std::to_string(inFlight) + " resizes in flight");
    }
    for (const Command &command : view.output.commands) {
      const StartResize *start = Get<StartResize>(command);
      const RenderPhase *after = start ? After(start->actor) : nullptr;
      if (after && FailedLevelOf(*after) == start->to) {
        Flag(Rule::kFailedLevelAskedAgain, start->actor,
             "resize started to a level that failed and is still refused");
      }
    }
  }

  [[nodiscard]] bool RetiredThisTick(ActorId a_actor) const {
    for (const CommandResult &result : view.facts.results) {
      if (const Retired *retired = Get<Retired>(result);
          retired && retired->actor == a_actor) {
        return true;
      }
    }
    return false;
  }

  void CheckCreatingEvents() {
    for (const CommandResult &result : view.facts.results) {
      const ActorEvent *event = Get<ActorEvent>(result);
      if (!event || !CreatesAPhase(event->event)) {
        continue;
      }
      const RenderPhase *before =
          RetiredThisTick(event->actor) ? nullptr : Before(event->actor);
      if (before && !Is<AwaitingPlan>(*before)) {
        Flag(Rule::kCreatingEventIgnored, event->actor,
             "creating event for a phase that cannot take it");
      }
      if (!After(event->actor) &&
          !HasCommandFor<Forget>(view.output.commands, event->actor)) {
        Flag(Rule::kCreatingEventIgnored, event->actor,
             "creating event left the actor without a render");
      }
    }
  }
};

[[nodiscard]] inline std::vector<Violation> CheckTick(const TickView &a_view) {
  Checker checker{a_view, {}};
  for (const Command &command : a_view.output.commands) {
    checker.CheckAgainstPhase(command);
  }
  checker.CheckDrawIsLast();
  checker.CheckResizes();
  checker.CheckCreatingEvents();
  return std::move(checker.violations);
}
}
