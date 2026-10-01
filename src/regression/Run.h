// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "regression/Commands.h"
#include "regression/Observation.h"
#include "regression/RunFile.h"
#include "regression/Steps.h"
#include "regression/Words.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects::Regression {
struct StepResult {
  std::string_view caseName;
  std::size_t step = 0;
  std::string action;
  Outcome outcome = Outcome::kPass;
  std::uint32_t frames = 0;
  std::string reason;
};
struct CaseResult {
  std::string_view caseName;
  Outcome outcome = Outcome::kPass;
};
struct RunEnd {
  Outcome outcome = Outcome::kPass;
  std::string reason;
};
struct WindowBegins {
  std::string_view window;
};
struct WindowEnds {
  std::string_view window;
};
using ResultLine =
    std::variant<StepResult, CaseResult, RunEnd, WindowBegins, WindowEnds>;

struct RunChanges {
  std::array<std::array<bool, kItemCount>, kRoleCount> added{};
  std::array<bool, kRoleCount> spawned{};
  bool away = false;
  std::uint32_t crowd = 0;
};

enum class Section : std::uint8_t { kBody, kCleanup };

struct Settling {
  std::uint32_t frames = 0;
};
struct Running {
  std::size_t caseIndex = 0;
  Section section = Section::kBody;
  std::size_t step = 0;
  std::uint32_t frames = 0;
  bool started = false;
  std::uint64_t startedAtMs = 0;
  std::uint32_t loadsAtStart = 0;
  Outcome caseOutcome = Outcome::kPass;
};
struct Ending {
  std::uint32_t frames = 0;
};
struct Finished {};
using Phase = std::variant<Settling, Running, Ending, Finished>;

struct RunState {
  CaseList suite;
  Phase phase = Settling{};
  Outcome runOutcome = Outcome::kPass;
  RunChanges changes;
  std::uint32_t loadsSeen = 0;
  std::array<std::uint64_t, kRoleCount> renderedBefore{};
};

struct Advanced {
  RunState state;
  std::optional<Command> command;
  std::vector<ResultLine> lines;
};

inline constexpr std::uint32_t kSettleFrames = 120;
inline constexpr std::uint32_t kEndingFrames = 10;

[[nodiscard]] RunState BeginRun(const RunFile &a_request);
[[nodiscard]] Advanced Advance(RunState a_state, const Observation &a_seen);
[[nodiscard]] bool Done(const RunState &a_state);
[[nodiscard]] std::string StartLineJson(std::string_view a_run,
                                        std::string_view a_build,
                                        std::string_view a_source,
                                        std::string_view a_trace);
[[nodiscard]] std::string ResultLineJson(const ResultLine &a_line);
}
