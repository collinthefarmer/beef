// GPL-3.0-only with the additional permission in COPYING.md.
#include "regression/Run.h"

#include "regression/StepRules.h"

#include "Core.h"

#include <utility>

namespace BetterEnchantmentEffects::Regression {
namespace {
std::span<const Step> StepsOf(const Case &a_case, Section a_section) {
  return a_section == Section::kBody ? a_case.body : a_case.cleanup;
}

std::size_t ReportedIndex(const Case &a_case, const Running &a_running) {
  return a_running.section == Section::kBody
             ? a_running.step
             : a_case.body.size() + a_running.step;
}

Outcome FirstFailure(Outcome a_kept, Outcome a_next) {
  return a_kept == Outcome::kPass ? a_next : a_kept;
}

std::array<std::uint64_t, kRoleCount>
RememberRenders(std::array<std::uint64_t, kRoleCount> a_marks,
                const Step &a_step, const Observation &a_seen) {
  const std::array<bool, kRoleCount> moved = RolesAffected(a_step);
  for (std::size_t role = 0; role < kRoleCount; ++role) {
    if (moved[role]) {
      a_marks[role] = a_seen.actors[role].renderedAttempt;
    }
  }
  return a_marks;
}

Advanced FinishSection(RunState a_state, Running a_running) {
  if (a_running.section == Section::kBody) {
    a_running.section = Section::kCleanup;
    a_running.step = 0;
    a_state.phase = a_running;
    return {std::move(a_state), std::nullopt, {}};
  }
  const Case &finished = a_state.suite[a_running.caseIndex];
  std::vector<ResultLine> lines{
      CaseResult{finished.name, a_running.caseOutcome}};
  a_state.runOutcome = FirstFailure(a_state.runOutcome, a_running.caseOutcome);
  if (a_running.caseIndex + 1 < a_state.suite.size()) {
    a_state.phase = Running{.caseIndex = a_running.caseIndex + 1};
  } else {
    lines.emplace_back(RunEnd{a_state.runOutcome, {}});
    a_state.phase = Ending{};
  }
  return {std::move(a_state), std::nullopt, std::move(lines)};
}

std::optional<std::string> WindowOpenedBy(const Step &a_step) {
  if (const auto *hold = std::get_if<HoldWindow>(&a_step))
    return WindowName(hold->window, hold->cycle);
  if (const auto *begin = std::get_if<BeginWindow>(&a_step))
    return WindowName(begin->window, begin->cycle);
  return std::nullopt;
}

std::optional<std::string> WindowClosedBy(const Step &a_step) {
  if (const auto *hold = std::get_if<HoldWindow>(&a_step))
    return WindowName(hold->window, hold->cycle);
  if (const auto *end = std::get_if<EndWindow>(&a_step))
    return WindowName(end->window, end->cycle);
  return std::nullopt;
}

RunChanges AfterCommand(RunChanges a_changes, const Command &a_command) {
  std::visit(Overloaded{
                 [&](const SpawnActor &a_c) {
                   a_changes.spawned[IndexOf(a_c.role)] = true;
                 },
                 [&](const DespawnActor &a_c) {
                   a_changes.spawned[IndexOf(a_c.role)] = false;
                   a_changes.added[IndexOf(a_c.role)] = {};
                 },
                 [&](const AddAndEquip &a_c) {
                   a_changes.added[IndexOf(a_c.role)][IndexOf(a_c.item)] = true;
                 },
                 [&](const RemoveArmor &a_c) {
                   a_changes.added[IndexOf(a_c.role)][IndexOf(a_c.item)] =
                       false;
                 },
                 [&](const TravelFromStart &) { a_changes.away = true; },
                 [&](const TravelToStart &) { a_changes.away = false; },
                 [&](const SpawnCrowdActors &a_c) {
                   a_changes.crowd = a_c.count;
                   a_changes.crowdBody = a_c.body;
                 },
                 [&](const DespawnCrowdActors &) { a_changes.crowd = 0; },
                 [](const auto &) {},
             },
             a_command);
  return a_changes;
}

Running AfterStep(Running a_running, Outcome a_outcome) {
  a_running.caseOutcome = FirstFailure(a_running.caseOutcome, a_outcome);
  if (a_running.section == Section::kBody && a_outcome != Outcome::kPass) {
    a_running.section = Section::kCleanup;
    a_running.step = 0;
  } else {
    ++a_running.step;
  }
  a_running.frames = 0;
  a_running.started = false;
  return a_running;
}

Advanced AdvanceRunning(RunState a_state, Running a_running,
                        const Observation &a_seen) {
  if (a_running.caseIndex >= a_state.suite.size()) {
    a_state.phase = Ending{};
    return {std::move(a_state),
            std::nullopt,
            {RunEnd{Outcome::kFail, "the case cursor left the suite"}}};
  }
  const Case &current = a_state.suite[a_running.caseIndex];
  const std::span<const Step> steps = StepsOf(current, a_running.section);
  if (a_running.step >= steps.size()) {
    return FinishSection(std::move(a_state), a_running);
  }
  const Step &step = steps[a_running.step];
  if (a_running.started) {
    ++a_running.frames;
  } else {
    a_running.startedAtMs = a_seen.nowMs;
    a_running.loadsAtStart = a_seen.loads;
  }
  const StepContext context{a_seen,
                            a_running.frames,
                            a_running.startedAtMs,
                            a_running.loadsAtStart,
                            a_state.changes,
                            a_state.renderedBefore};
  StepMove move =
      a_running.started ? CheckStep(step, context) : StartStep(step, context);
  std::vector<ResultLine> lines;
  if (!a_running.started)
    if (std::optional<std::string> window = WindowOpenedBy(step))
      lines.emplace_back(WindowBegins{current.name, std::move(*window)});
  if (!a_running.started && move.command) {
    a_state.renderedBefore =
        RememberRenders(a_state.renderedBefore, step, a_seen);
  }
  a_running.started = true;
  if (move.command) {
    a_state.changes = AfterCommand(a_state.changes, *move.command);
  }
  if (Completed *completed = std::get_if<Completed>(&move.verdict)) {
    lines.emplace_back(StepResult{
        current.name, ReportedIndex(current, a_running), StepLabel(step),
        completed->outcome, a_running.frames, std::move(completed->reason)});
    if (std::optional<std::string> window = WindowClosedBy(step))
      lines.emplace_back(WindowEnds{current.name, std::move(*window)});
    a_running = AfterStep(a_running, completed->outcome);
  }
  a_state.phase = a_running;
  return {std::move(a_state), move.command, std::move(lines)};
}
}

RunState BeginRun(const RunFile &a_request) {
  return RunState{.suite = a_request.suite};
}

Advanced Advance(RunState a_state, const Observation &a_seen) {
  if (a_seen.loads != a_state.loadsSeen) {
    a_state.changes = RunChanges{};
    a_state.loadsSeen = a_seen.loads;
  }
  if (Settling *settling = std::get_if<Settling>(&a_state.phase)) {
    settling->frames = a_seen.actors[IndexOf(Role::kPlayer)].present
                           ? settling->frames + 1
                           : 0;
    if (settling->frames >= kSettleFrames) {
      a_state.phase = Running{};
    }
    return {std::move(a_state), std::nullopt, {}};
  }
  if (Running *running = std::get_if<Running>(&a_state.phase)) {
    const Running current = *running;
    return AdvanceRunning(std::move(a_state), current, a_seen);
  }
  if (Ending *ending = std::get_if<Ending>(&a_state.phase)) {
    ++ending->frames;
    if (ending->frames < kEndingFrames) {
      return {std::move(a_state), std::nullopt, {}};
    }
    a_state.phase = Finished{};
    return {std::move(a_state), Quit{}, {}};
  }
  return {std::move(a_state), std::nullopt, {}};
}

bool Done(const RunState &a_state) {
  return std::holds_alternative<Finished>(a_state.phase);
}
}
