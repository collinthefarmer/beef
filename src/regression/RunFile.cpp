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
inline constexpr std::size_t kMaxRunFileBytes = std::size_t{64} * 1024;
inline constexpr std::size_t kMaxRunFileDepth = 4;
inline constexpr std::size_t kMaxRunLength = 64;
inline constexpr std::size_t kMaxSaveLength = 128;

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
