// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects::Regression {
enum class Outcome : std::uint8_t { kPass, kFail, kBlocked, kAborted };

enum class RequestState : std::uint8_t {
  kNone,
  kWaiting,
  kPass,
  kFail,
  kBlocked,
  kAborted
};

struct Settle {
  std::uint32_t frames = 0;
};
struct Solo {
  std::string_view recipe;
};
struct RestoreView {};
struct EquipFixture {};
struct RemoveFixture {};
struct Apply {};
struct Retire {};
using Step = std::variant<Settle, Solo, RestoreView, EquipFixture,
                          RemoveFixture, Apply, Retire>;

struct Case {
  std::string_view name;
  std::span<const Step> body;
  std::span<const Step> cleanup;
};
using CaseList = std::vector<std::reference_wrapper<const Case>>;

struct RunRequest {
  std::string run;
  std::string save;
  CaseList suite;
};

struct Observation {
  bool playerReady = false;
  bool fixtureLoaded = false;
  bool fixtureEquipped = false;
  bool fixtureCarried = false;
  bool bodyArmorWorn = false;
  RequestState request = RequestState::kNone;
};

struct SoloRecipe {
  std::string_view recipe;
};
struct RestoreSolo {};
struct AddAndEquipFixture {};
struct UnequipAndRemoveFixture {};
struct SubmitApply {};
struct SubmitRetire {};
struct AbortRequest {};
struct Quit {};
using Command = std::variant<SoloRecipe, RestoreSolo, AddAndEquipFixture,
                             UnequipAndRemoveFixture, SubmitApply, SubmitRetire,
                             AbortRequest, Quit>;

struct StepResult {
  std::string_view caseName;
  std::size_t step = 0;
  std::string_view action;
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
using ResultLine = std::variant<StepResult, CaseResult, RunEnd>;

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
  Outcome caseOutcome = Outcome::kPass;
};
struct Ending {
  std::uint32_t frames = 0;
};
struct Finished {};
using Phase = std::variant<Settling, Running, Ending, Finished>;

struct RunState {
  CaseList cases;
  Phase phase = Settling{};
  Outcome runOutcome = Outcome::kPass;
  bool ownsFixture = false;
};

struct Advanced {
  RunState state;
  std::optional<Command> command;
  std::vector<ResultLine> lines;
};

inline constexpr std::uint32_t kSettleFrames = 120;
inline constexpr std::uint32_t kEndingFrames = 10;
inline constexpr std::size_t kMaxSuiteCases = 64;

[[nodiscard]] std::string_view OutcomeName(Outcome a_outcome);
[[nodiscard]] std::string_view ActionName(const Step &a_step);
[[nodiscard]] const Case *FindCase(std::string_view a_name);
[[nodiscard]] std::expected<RunRequest, std::string>
ParseRunRequest(std::string_view a_text, std::int64_t a_nowSeconds);
[[nodiscard]] RunState BeginRun(const RunRequest &a_request);
[[nodiscard]] Advanced Advance(RunState a_state, const Observation &a_seen);
[[nodiscard]] bool Done(const RunState &a_state);
[[nodiscard]] std::string StartLineJson(std::string_view a_run,
                                        std::string_view a_build,
                                        std::string_view a_source,
                                        std::string_view a_trace);
[[nodiscard]] std::string ResultLineJson(const ResultLine &a_line);
}
