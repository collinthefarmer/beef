// GPL-3.0-only with the additional permission in COPYING.md.
#include "regression/Run.h"

#include "regression/Steps.h"

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
MarkRenders(std::array<std::uint64_t, kRoleCount> a_marks, const Step &a_step,
            const Observation &a_seen) {
  const std::array<bool, kRoleCount> moved = RolesMoved(a_step);
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
  const Case &finished = a_state.cases[a_running.caseIndex];
  std::vector<ResultLine> lines{
      CaseResult{finished.name, a_running.caseOutcome}};
  a_state.runOutcome = FirstFailure(a_state.runOutcome, a_running.caseOutcome);
  if (a_running.caseIndex + 1 < a_state.cases.size()) {
    a_state.phase = Running{.caseIndex = a_running.caseIndex + 1};
  } else {
    lines.emplace_back(RunEnd{a_state.runOutcome, {}});
    a_state.phase = Ending{};
  }
  return {std::move(a_state), std::nullopt, std::move(lines)};
}

std::optional<std::string_view> WindowOpenedBy(const Step &a_step) {
  if (const auto *hold = std::get_if<HoldFor>(&a_step))
    return hold->window;
  if (const auto *begin = std::get_if<BeginWindow>(&a_step))
    return begin->window;
  return std::nullopt;
}

std::optional<std::string_view> WindowClosedBy(const Step &a_step) {
  if (const auto *hold = std::get_if<HoldFor>(&a_step))
    return hold->window;
  if (const auto *end = std::get_if<EndWindow>(&a_step))
    return end->window;
  return std::nullopt;
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
  if (a_running.caseIndex >= a_state.cases.size()) {
    a_state.phase = Ending{};
    return {std::move(a_state),
            std::nullopt,
            {RunEnd{Outcome::kFail, "the case cursor left the suite"}}};
  }
  const Case &current = a_state.cases[a_running.caseIndex];
  const std::span<const Step> steps = StepsOf(current, a_running.section);
  if (a_running.step >= steps.size()) {
    return FinishSection(std::move(a_state), a_running);
  }
  const Step &step = steps[a_running.step];
  if (a_running.started) {
    ++a_running.frames;
  }
  const StepContext context{a_seen, a_running.frames, a_state.owned,
                            a_state.renderMarks};
  StepMove move =
      a_running.started ? CheckStep(step, context) : StartStep(step, context);
  std::vector<ResultLine> lines;
  if (!a_running.started)
    if (const std::optional<std::string_view> window = WindowOpenedBy(step))
      lines.emplace_back(WindowBegins{*window});
  if (!a_running.started && move.command) {
    a_state.renderMarks = MarkRenders(a_state.renderMarks, step, a_seen);
  }
  a_running.started = true;
  a_state.owned = move.owned;
  if (Completed *completed = std::get_if<Completed>(&move.verdict)) {
    lines.emplace_back(StepResult{
        current.name, ReportedIndex(current, a_running), StepLabel(step),
        completed->outcome, a_running.frames, std::move(completed->reason)});
    if (const std::optional<std::string_view> window = WindowClosedBy(step))
      lines.emplace_back(WindowEnds{*window});
    a_running = AfterStep(a_running, completed->outcome);
  }
  a_state.phase = a_running;
  return {std::move(a_state), move.command, std::move(lines)};
}
}

std::string_view OutcomeName(Outcome a_outcome) {
  switch (a_outcome) {
  case Outcome::kPass:
    return "PASS";
  case Outcome::kFail:
    return "FAIL";
  case Outcome::kBlocked:
    return "BLOCKED";
  case Outcome::kAborted:
    return "ABORTED";
  }
  return "FAIL";
}

RunState BeginRun(const RunRequest &a_request) {
  return RunState{.cases = a_request.suite};
}

Advanced Advance(RunState a_state, const Observation &a_seen) {
  if (Settling *settling = std::get_if<Settling>(&a_state.phase)) {
    settling->frames = a_seen.actors[RoleIndex(Role::kPlayer)].present
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
