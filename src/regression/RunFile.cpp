// GPL-3.0-only with the additional permission in COPYING.md.
#include "regression/Run.h"

#include "Core.h"
#include "recipe/Binders.h"

#include <algorithm>
#include <format>
#include <limits>
#include <utility>

namespace BetterEnchantmentEffects::Regression {
namespace {
inline constexpr std::size_t kMaxRunFileBytes = std::size_t{64} * 1024;
inline constexpr std::size_t kMaxRunFileDepth = 4;
inline constexpr std::size_t kMaxRunLength = 64;
inline constexpr std::size_t kMaxSaveLength = 128;

bool RunCharacter(char a_char) {
  return (a_char >= 'a' && a_char <= 'z') || (a_char >= 'A' && a_char <= 'Z') ||
         (a_char >= '0' && a_char <= '9') || a_char == '-' || a_char == '_';
}

bool SaveCharacter(char a_char) {
  return a_char != '/' && a_char != '\\' && a_char != ':' && a_char != '"' &&
         static_cast<unsigned char>(a_char) >= 0x20;
}

std::expected<std::string, std::string> CheckedName(std::string a_value,
                                                    std::string_view a_key,
                                                    std::size_t a_maxLength,
                                                    bool (*a_allowed)(char)) {
  if (a_value.empty() || a_value.size() > a_maxLength) {
    return std::unexpected(
        std::format("'{}' must have 1 to {} characters", a_key, a_maxLength));
  }
  if (!std::ranges::all_of(a_value, a_allowed) ||
      a_value.find("..") != std::string::npos) {
    return std::unexpected(
        std::format("'{}' holds a character it may not hold", a_key));
  }
  return a_value;
}

std::expected<CaseList, std::string>
SuiteOf(const std::optional<std::vector<std::string>> &a_names) {
  if (!a_names || a_names->empty()) {
    return std::unexpected(std::format(
        "'suite' must be an array of 1 to {} case names", kMaxSuiteCases));
  }
  CaseList suite;
  for (const std::string &name : *a_names) {
    const Case *named = FindCase(name);
    if (!named) {
      return std::unexpected(
          std::format("'suite' names an unknown case '{}'", name));
    }
    suite.emplace_back(*named);
  }
  return suite;
}

std::expected<std::int64_t, std::string> UnixSecondsOf(const json *a_value) {
  if (!a_value || !a_value->is_number_integer() ||
      (a_value->is_number_unsigned() &&
       a_value->get<std::uint64_t>() >
           static_cast<std::uint64_t>(
               std::numeric_limits<std::int64_t>::max()))) {
    return std::unexpected("'notAfter' must be an integer of Unix seconds");
  }
  return a_value->get<std::int64_t>();
}

json OutcomeJson(Outcome a_outcome) {
  return json(std::string{OutcomeName(a_outcome)});
}

std::expected<json, std::string> RunFileDocument(std::string_view a_text) {
  if (a_text.size() > kMaxRunFileBytes) {
    return std::unexpected(
        std::format("larger than {} bytes", kMaxRunFileBytes));
  }
  if (MaxNestingDepth(a_text) > kMaxRunFileDepth) {
    return std::unexpected(
        std::format("nested deeper than {} levels", kMaxRunFileDepth));
  }
  std::vector<Diagnostic> problems;
  const Reporter report{problems, "run file"};
  std::optional<json> root = ParseObjectDocument(a_text, report);
  if (!root || !problems.empty()) {
    return std::unexpected(problems.empty() ? std::string{"not a JSON object"}
                                            : problems.front().message);
  }
  return std::move(*root);
}

std::expected<std::optional<std::uint32_t>, std::string>
BudgetMiBOf(std::optional<int> a_mebibytes) {
  if (!a_mebibytes) {
    return std::nullopt;
  }
  if (*a_mebibytes < 0 || *a_mebibytes > static_cast<int>(kMaxRunBudgetMiB)) {
    return std::unexpected(
        std::format("'renderTargetBudgetMiB' is 0..{}", kMaxRunBudgetMiB));
  }
  return static_cast<std::uint32_t>(*a_mebibytes);
}

std::expected<std::optional<std::uint32_t>, std::string>
MeasuredSecondsOf(std::optional<int> a_seconds) {
  if (!a_seconds) {
    return std::nullopt;
  }
  if (*a_seconds < static_cast<int>(kMinMeasuredSeconds) ||
      *a_seconds > static_cast<int>(kMaxMeasuredSeconds)) {
    return std::unexpected(std::format("'measuredSeconds' is {}..{}",
                                       kMinMeasuredSeconds,
                                       kMaxMeasuredSeconds));
  }
  return static_cast<std::uint32_t>(*a_seconds);
}
}

std::expected<RunFile, std::string> ParseRunFile(std::string_view a_text,
                                                 std::int64_t a_nowSeconds) {
  const std::expected<json, std::string> root = RunFileDocument(a_text);
  if (!root) {
    return std::unexpected(root.error());
  }
  std::vector<Diagnostic> problems;
  const Reporter report{problems, "run file"};
  Reader reader{*root, report};
  const std::optional<int> format = reader.Integer("format");
  std::string run = reader.Required("run");
  std::string save = reader.Required("save");
  const std::optional<std::vector<std::string>> names =
      reader.Strings("suite", kMaxSuiteCases);
  const json *notAfterValue = reader.Child("notAfter");
  const std::optional<bool> verifyRenderPlans =
      reader.Boolean("verifyRenderPlans");
  const std::optional<int> measuredSeconds = reader.Integer("measuredSeconds");
  const std::optional<bool> screenResolution =
      reader.Boolean("screenResolution");
  const std::optional<int> budgetMiB = reader.Integer("renderTargetBudgetMiB");
  const std::optional<bool> diagnosticLogging =
      reader.Boolean("diagnosticLogging");
  reader.Finish();
  if (!problems.empty()) {
    return std::unexpected(problems.front().message);
  }
  if (format != 1) {
    return std::unexpected("'format' must be 1");
  }
  auto checkedRun =
      CheckedName(std::move(run), "run", kMaxRunLength, RunCharacter);
  if (!checkedRun) {
    return std::unexpected(checkedRun.error());
  }
  auto checkedSave =
      CheckedName(std::move(save), "save", kMaxSaveLength, SaveCharacter);
  if (!checkedSave) {
    return std::unexpected(checkedSave.error());
  }
  auto suite = SuiteOf(names);
  if (!suite) {
    return std::unexpected(suite.error());
  }
  const auto notAfter = UnixSecondsOf(notAfterValue);
  if (!notAfter) {
    return std::unexpected(notAfter.error());
  }
  if (a_nowSeconds > *notAfter) {
    return std::unexpected("the run file has expired");
  }
  const auto measured = MeasuredSecondsOf(measuredSeconds);
  if (!measured) {
    return std::unexpected(measured.error());
  }
  const auto budget = BudgetMiBOf(budgetMiB);
  if (!budget) {
    return std::unexpected(budget.error());
  }
  return RunFile{std::move(*checkedRun),
                 std::move(*checkedSave),
                 std::move(*suite),
                 verifyRenderPlans.value_or(false),
                 *measured,
                 screenResolution,
                 *budget,
                 diagnosticLogging.value_or(false)};
}

std::string StartLineJson(const StartLine &a_start) {
  json line = json::object();
  line["kind"] = "start";
  line["run"] = std::string{a_start.run};
  line["build"] = std::string{a_start.build};
  line["source"] = std::string{a_start.source};
  line["trace"] = std::string{a_start.trace};
  if (a_start.measuredSeconds) {
    line["measuredSeconds"] = *a_start.measuredSeconds;
  }
  line["screenResolution"] = a_start.screenResolution;
  if (a_start.renderTargetBudgetMiB > 0) {
    line["renderTargetBudgetMiB"] = a_start.renderTargetBudgetMiB;
  }
  return line.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::string ResultLineJson(const ResultLine &a_line) {
  json line = Match(
      a_line,
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
      [](const WindowBegins &a_window) {
        json edge = json::object();
        edge["kind"] = "window";
        edge["case"] = std::string{a_window.caseName};
        edge["window"] = a_window.window;
        edge["edge"] = "begin";
        return edge;
      },
      [](const WindowEnds &a_window) {
        json edge = json::object();
        edge["kind"] = "window";
        edge["case"] = std::string{a_window.caseName};
        edge["window"] = a_window.window;
        edge["edge"] = "end";
        return edge;
      },
      [](const RunEnd &a_end) {
        json end = json::object();
        end["kind"] = "end";
        end["outcome"] = OutcomeJson(a_end.outcome);
        end["reason"] = a_end.reason;
        return end;
      });
  return line.dump(-1, ' ', false, json::error_handler_t::replace);
}
}
