// GPL-3.0-only with the additional permission in COPYING.md.
#include "regression/Run.h"

#include "Core.h"
#include "recipe/Binders.h"

#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <utility>

namespace BetterEnchantmentEffects::Regression {
namespace {
inline constexpr std::uint32_t kFixtureDeadlineFrames = 600;
inline constexpr std::uint32_t kRequestDeadlineFrames = 1800;
inline constexpr std::size_t kMaxRunFileBytes = std::size_t{64} * 1024;
inline constexpr std::size_t kMaxRunFileDepth = 4;
inline constexpr std::size_t kMaxRunLength = 64;
inline constexpr std::size_t kMaxSaveLength = 128;
inline constexpr std::string_view kSoloRecipe = "arcane-circuit";

inline constexpr std::array<Step, 5> kLifecycleBody{
    Solo{kSoloRecipe}, EquipFixture{}, Apply{}, Retire{}, Apply{}};
inline constexpr std::array<Step, 2> kLifecycleCleanup{RemoveFixture{},
                                                       RestoreView{}};
inline constexpr std::array<Case, 1> kCatalog{
    Case{"lifecycle", kLifecycleBody, kLifecycleCleanup}};

struct Pending {};
struct Completed {
  Outcome outcome = Outcome::kPass;
  std::string reason;
};
using Verdict = std::variant<Pending, Completed>;

struct StepMove {
  Verdict verdict;
  std::optional<Command> command;
  bool ownsFixture = false;
};

StepMove Complete(Outcome a_outcome, std::string a_reason, bool a_ownsFixture,
                  std::optional<Command> a_command = std::nullopt) {
  return {Completed{a_outcome, std::move(a_reason)}, a_command, a_ownsFixture};
}

StepMove Wait(bool a_ownsFixture,
              std::optional<Command> a_command = std::nullopt) {
  return {Pending{}, a_command, a_ownsFixture};
}

std::string FixtureTimeout(std::string_view a_what) {
  return std::format("the demo cuirass did not {} within {} frames", a_what,
                     kFixtureDeadlineFrames);
}

StepMove StartStep(const Step &a_step, const Observation &a_seen,
                   bool a_ownsFixture) {
  return std::visit(
      Overloaded{
          [&](const Settle &) { return Wait(a_ownsFixture); },
          [&](const Solo &a_solo) {
            return Complete(Outcome::kPass, {}, a_ownsFixture,
                            SoloRecipe{a_solo.recipe});
          },
          [&](const RestoreView &) {
            return Complete(Outcome::kPass, {}, a_ownsFixture, RestoreSolo{});
          },
          [&](const EquipFixture &) {
            if (!a_seen.fixtureLoaded) {
              return Complete(Outcome::kBlocked,
                              "the demo plugin is not loaded", a_ownsFixture);
            }
            if (a_seen.bodyArmorWorn || a_seen.fixtureCarried) {
              return Complete(
                  Outcome::kBlocked,
                  "body armor is worn or the demo cuirass is carried",
                  a_ownsFixture);
            }
            return Wait(true, AddAndEquipFixture{});
          },
          [&](const RemoveFixture &) {
            if (!a_ownsFixture) {
              return Complete(Outcome::kPass, "the run added no cuirass",
                              false);
            }
            return Wait(true, UnequipAndRemoveFixture{});
          },
          [&](const Apply &) { return Wait(a_ownsFixture, SubmitApply{}); },
          [&](const Retire &) { return Wait(a_ownsFixture, SubmitRetire{}); },
      },
      a_step);
}

StepMove CheckRequest(RequestState a_request, std::uint32_t a_frames,
                      bool a_ownsFixture) {
  switch (a_request) {
  case RequestState::kNone:
    return Complete(Outcome::kBlocked, "the plugin refused the request",
                    a_ownsFixture);
  case RequestState::kWaiting:
    if (a_frames >= kRequestDeadlineFrames) {
      return Complete(
          Outcome::kFail,
          std::format("no result within {} frames", kRequestDeadlineFrames),
          a_ownsFixture, AbortRequest{});
    }
    return Wait(a_ownsFixture);
  case RequestState::kPass:
    return Complete(Outcome::kPass, {}, a_ownsFixture);
  case RequestState::kFail:
    return Complete(Outcome::kFail, "the plugin reported a failure",
                    a_ownsFixture);
  case RequestState::kBlocked:
    return Complete(
        Outcome::kBlocked,
        "nothing rendered on the demo cuirass, or the player was not "
        "ready",
        a_ownsFixture);
  case RequestState::kAborted:
    return Complete(Outcome::kAborted, "the plugin cancelled the request",
                    a_ownsFixture);
  }
  return Complete(Outcome::kFail, "unknown request state", a_ownsFixture);
}

StepMove CheckStep(const Step &a_step, const Observation &a_seen,
                   std::uint32_t a_frames, bool a_ownsFixture) {
  return std::visit(
      Overloaded{
          [&](const Settle &a_settle) {
            return a_frames >= a_settle.frames
                       ? Complete(Outcome::kPass, {}, a_ownsFixture)
                       : Wait(a_ownsFixture);
          },
          [&](const Solo &) {
            return Complete(Outcome::kPass, {}, a_ownsFixture);
          },
          [&](const RestoreView &) {
            return Complete(Outcome::kPass, {}, a_ownsFixture);
          },
          [&](const EquipFixture &) {
            if (a_seen.fixtureEquipped) {
              return Complete(Outcome::kPass, {}, a_ownsFixture);
            }
            return a_frames >= kFixtureDeadlineFrames
                       ? Complete(Outcome::kFail, FixtureTimeout("equip"),
                                  a_ownsFixture)
                       : Wait(a_ownsFixture);
          },
          [&](const RemoveFixture &) {
            if (!a_seen.fixtureCarried) {
              return Complete(Outcome::kPass, {}, false);
            }
            return a_frames >= kFixtureDeadlineFrames
                       ? Complete(Outcome::kFail, FixtureTimeout("leave"),
                                  a_ownsFixture)
                       : Wait(a_ownsFixture);
          },
          [&](const Apply &) {
            return CheckRequest(a_seen.request, a_frames, a_ownsFixture);
          },
          [&](const Retire &) {
            return CheckRequest(a_seen.request, a_frames, a_ownsFixture);
          },
      },
      a_step);
}

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
  StepMove move =
      a_running.started
          ? CheckStep(step, a_seen, a_running.frames, a_state.ownsFixture)
          : StartStep(step, a_seen, a_state.ownsFixture);
  a_running.started = true;
  a_state.ownsFixture = move.ownsFixture;
  std::vector<ResultLine> lines;
  if (Completed *completed = std::get_if<Completed>(&move.verdict)) {
    lines.emplace_back(StepResult{
        current.name, ReportedIndex(current, a_running), ActionName(step),
        completed->outcome, a_running.frames, std::move(completed->reason)});
    a_running.caseOutcome =
        FirstFailure(a_running.caseOutcome, completed->outcome);
    if (a_running.section == Section::kBody &&
        completed->outcome != Outcome::kPass) {
      a_running.section = Section::kCleanup;
      a_running.step = 0;
    } else {
      ++a_running.step;
    }
    a_running.frames = 0;
    a_running.started = false;
  }
  a_state.phase = a_running;
  return {std::move(a_state), move.command, std::move(lines)};
}

std::expected<std::string, std::string> StringField(const json &a_root,
                                                    std::string_view a_key,
                                                    std::size_t a_maxLength) {
  const auto found = a_root.find(a_key);
  if (found == a_root.end() || !found->is_string()) {
    return std::unexpected(std::format("'{}' must be a string", a_key));
  }
  std::string value = found->get<std::string>();
  if (value.empty() || value.size() > a_maxLength) {
    return std::unexpected(
        std::format("'{}' must have 1 to {} characters", a_key, a_maxLength));
  }
  return value;
}

bool RunCharacter(char a_char) {
  return (a_char >= 'a' && a_char <= 'z') || (a_char >= 'A' && a_char <= 'Z') ||
         (a_char >= '0' && a_char <= '9') || a_char == '-' || a_char == '_';
}

bool SaveCharacter(char a_char) {
  return a_char != '/' && a_char != '\\' && a_char != ':' && a_char != '"' &&
         static_cast<unsigned char>(a_char) >= 0x20;
}

std::expected<CaseList, std::string> SuiteField(const json &a_root) {
  const auto found = a_root.find("suite");
  if (found == a_root.end() || !found->is_array() || found->empty() ||
      found->size() > kMaxSuiteCases) {
    return std::unexpected(std::format(
        "'suite' must be an array of 1 to {} case names", kMaxSuiteCases));
  }
  CaseList suite;
  for (const json &entry : *found) {
    const Case *named =
        entry.is_string() ? FindCase(entry.get<std::string>()) : nullptr;
    if (!named) {
      return std::unexpected(
          std::format("'suite' names an unknown case {}", entry.dump()));
    }
    suite.emplace_back(*named);
  }
  return suite;
}

std::expected<std::int64_t, std::string> NotAfterField(const json &a_root) {
  const auto found = a_root.find("notAfter");
  if (found == a_root.end() || !found->is_number_integer() ||
      (found->is_number_unsigned() &&
       found->get<std::uint64_t>() >
           static_cast<std::uint64_t>(
               std::numeric_limits<std::int64_t>::max()))) {
    return std::unexpected("'notAfter' must be an integer of Unix seconds");
  }
  return found->get<std::int64_t>();
}

std::expected<void, std::string> OnlyKnownKeys(const json &a_root) {
  constexpr std::array<std::string_view, 5> kKeys{"format", "run", "save",
                                                  "suite", "notAfter"};
  for (const auto &[key, value] : a_root.items()) {
    if (std::ranges::find(kKeys, key) == kKeys.end()) {
      return std::unexpected(std::format("unknown key '{}'", key));
    }
  }
  const auto format = a_root.find("format");
  if (format == a_root.end() || !format->is_number_integer() ||
      format->get<std::int64_t>() != 1) {
    return std::unexpected("'format' must be 1");
  }
  return {};
}

json OutcomeJson(Outcome a_outcome) {
  return json(std::string{OutcomeName(a_outcome)});
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

std::string_view ActionName(const Step &a_step) {
  return std::visit(
      Overloaded{
          [](const Settle &) { return std::string_view{"settle"}; },
          [](const Solo &) { return std::string_view{"solo"}; },
          [](const RestoreView &) { return std::string_view{"restore-view"}; },
          [](const EquipFixture &) {
            return std::string_view{"equip-fixture"};
          },
          [](const RemoveFixture &) {
            return std::string_view{"remove-fixture"};
          },
          [](const Apply &) { return std::string_view{"apply"}; },
          [](const Retire &) { return std::string_view{"retire"}; },
      },
      a_step);
}

const Case *FindCase(std::string_view a_name) {
  const auto found = std::ranges::find(kCatalog, a_name, &Case::name);
  return found == kCatalog.end() ? nullptr : &*found;
}

std::expected<RunRequest, std::string>
ParseRunRequest(std::string_view a_text, std::int64_t a_nowSeconds) {
  if (a_text.size() > kMaxRunFileBytes) {
    return std::unexpected(
        std::format("larger than {} bytes", kMaxRunFileBytes));
  }
  if (MaxNestingDepth(a_text) > kMaxRunFileDepth) {
    return std::unexpected(
        std::format("nested deeper than {} levels", kMaxRunFileDepth));
  }
  const json root = json::parse(a_text, nullptr, false);
  if (root.is_discarded() || !root.is_object()) {
    return std::unexpected("not a JSON object");
  }
  if (const auto keys = OnlyKnownKeys(root); !keys) {
    return std::unexpected(keys.error());
  }
  auto run = StringField(root, "run", kMaxRunLength);
  if (!run) {
    return std::unexpected(run.error());
  }
  if (!std::ranges::all_of(*run, RunCharacter)) {
    return std::unexpected("'run' may hold only letters, digits, - and _");
  }
  auto save = StringField(root, "save", kMaxSaveLength);
  if (!save) {
    return std::unexpected(save.error());
  }
  if (!std::ranges::all_of(*save, SaveCharacter) ||
      save->find("..") != std::string::npos) {
    return std::unexpected("'save' must be a save name, not a path");
  }
  auto suite = SuiteField(root);
  if (!suite) {
    return std::unexpected(suite.error());
  }
  const auto notAfter = NotAfterField(root);
  if (!notAfter) {
    return std::unexpected(notAfter.error());
  }
  if (a_nowSeconds > *notAfter) {
    return std::unexpected("the run file has expired");
  }
  return RunRequest{std::move(*run), std::move(*save), std::move(*suite)};
}

RunState BeginRun(const RunRequest &a_request) {
  return RunState{.cases = a_request.suite};
}

Advanced Advance(RunState a_state, const Observation &a_seen) {
  if (Settling *settling = std::get_if<Settling>(&a_state.phase)) {
    settling->frames = a_seen.playerReady ? settling->frames + 1 : 0;
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

std::string StartLineJson(std::string_view a_run, std::string_view a_build,
                          std::string_view a_source, std::string_view a_trace) {
  json line = json::object();
  line["kind"] = "start";
  line["run"] = std::string{a_run};
  line["build"] = std::string{a_build};
  line["source"] = std::string{a_source};
  line["trace"] = std::string{a_trace};
  return line.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::string ResultLineJson(const ResultLine &a_line) {
  json line = std::visit(Overloaded{
                             [](const StepResult &a_step) {
                               json step = json::object();
                               step["kind"] = "step";
                               step["case"] = std::string{a_step.caseName};
                               step["step"] = a_step.step;
                               step["action"] = std::string{a_step.action};
                               step["outcome"] = OutcomeJson(a_step.outcome);
                               step["frames"] = a_step.frames;
                               step["reason"] = a_step.reason;
                               return step;
                             },
                             [](const CaseResult &a_case) {
                               json result = json::object();
                               result["kind"] = "case";
                               result["case"] = std::string{a_case.caseName};
                               result["outcome"] = OutcomeJson(a_case.outcome);
                               return result;
                             },
                             [](const RunEnd &a_end) {
                               json end = json::object();
                               end["kind"] = "end";
                               end["outcome"] = OutcomeJson(a_end.outcome);
                               end["reason"] = a_end.reason;
                               return end;
                             },
                         },
                         a_line);
  return line.dump(-1, ' ', false, json::error_handler_t::replace);
}
}
