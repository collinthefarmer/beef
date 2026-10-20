// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/RegressionRun.h"

#include "Core.h"
#include "Identity.h"
#include "diagnostics/Trace.h"
#include "engine/Manager.h"
#include "engine/RegressionWorld.h"
#include "engine/TextFile.h"
#include "regression/Run.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <variant>

namespace BetterEnchantmentEffects {
namespace {
struct AwaitingMainMenu {};
struct LoadingSave {};
struct RunningCases {
  Regression::RunState state;
  RunWorld world;
  Regression::CrowdFacts crowd;
};
using RunStage = std::variant<AwaitingMainMenu, LoadingSave, RunningCases>;

struct ActiveRun {
  Regression::RunFile request;
  std::filesystem::path results;
  RunStage stage = AwaitingMainMenu{};
};

std::mutex runLock;
std::optional<ActiveRun> activeRun;
std::atomic_bool runPresent{false};
std::atomic_bool verifyRenderPlans{false};

enum class ResolutionOverride : std::uint8_t { kNone, kOn, kOff };
std::atomic<ResolutionOverride> screenResolution{ResolutionOverride::kNone};

constexpr std::uint64_t kNoBudgetOverride =
    std::numeric_limits<std::uint64_t>::max();
std::atomic<std::uint64_t> budgetOverride{kNoBudgetOverride};

std::uint64_t BudgetOverrideOf(std::optional<std::uint32_t> a_mebibytes) {
  return a_mebibytes ? std::uint64_t{*a_mebibytes} : kNoBudgetOverride;
}

ResolutionOverride OverrideOf(std::optional<bool> a_screenResolution) {
  if (!a_screenResolution) {
    return ResolutionOverride::kNone;
  }
  return *a_screenResolution ? ResolutionOverride::kOn
                             : ResolutionOverride::kOff;
}

void AppendLine(const std::filesystem::path &a_path, std::string_view a_line) {
  std::ofstream file{a_path, std::ios::app | std::ios::binary};
  if (!file) {
    logger::warn("regression: cannot write {}", a_path.string());
    return;
  }
  file << a_line << '\n';
}

void FlushHeartbeatAtWindowEdge(const Regression::ResultLine &a_line) {
  if (!std::holds_alternative<Regression::WindowBegins>(a_line) &&
      !std::holds_alternative<Regression::WindowEnds>(a_line)) {
    return;
  }
  if (Manager *manager = Manager::GetSingleton()) {
    manager->FlushMetricsHeartbeat();
  }
}

void TraceCrowdChange(const Regression::CrowdFacts &a_before,
                      const Regression::CrowdFacts &a_now) {
  if (a_before == a_now) {
    return;
  }
  Trace::EmitSafely(Trace::Event::kOperation,
                    {{"action", "regression.crowd"},
                     {"present", std::to_string(a_now.present)},
                     {"rendered", std::to_string(a_now.rendered)},
                     {"fighting", std::to_string(a_now.fighting)},
                     {"engaged", std::to_string(a_now.engaged)},
                     {"hurt", std::to_string(a_now.hurt)},
                     {"drawn", std::to_string(a_now.drawn)},
                     {"moved", std::to_string(a_now.moved)},
                     {"high_process", std::to_string(a_now.highProcess)}});
}

void TraceLine(const Regression::ResultLine &a_line) {
  Match(
      a_line,
      [](const Regression::StepResult &a_step) {
        Trace::EmitSafely(
            Trace::Event::kOperation,
            {{"action", "regression.step"},
             {"case", std::string{a_step.caseName}},
             {"step", std::to_string(a_step.step)},
             {"operation", std::string{a_step.action}},
             {"result", std::string{Regression::OutcomeName(a_step.outcome)}}});
      },
      [](const Regression::CaseResult &a_case) {
        Trace::EmitSafely(
            Trace::Event::kOperation,
            {{"action", "regression.case"},
             {"case", std::string{a_case.caseName}},
             {"result", std::string{Regression::OutcomeName(a_case.outcome)}}});
      },
      [](const Regression::WindowBegins &a_window) {
        Trace::EmitSafely(Trace::Event::kOperation,
                          {{"action", "regression.window"},
                           {"case", std::string{a_window.caseName}},
                           {"window", a_window.window},
                           {"edge", "begin"}});
      },
      [](const Regression::WindowEnds &a_window) {
        Trace::EmitSafely(Trace::Event::kOperation,
                          {{"action", "regression.window"},
                           {"case", std::string{a_window.caseName}},
                           {"window", a_window.window},
                           {"edge", "end"}});
      },
      [](const Regression::RunEnd &) {});
}

void Record(const ActiveRun &a_run, const Regression::ResultLine &a_line) {
  const std::string json = Regression::ResultLineJson(a_line);
  logger::info("regression: {}", json);
  AppendLine(a_run.results, json);
  FlushHeartbeatAtWindowEdge(a_line);
  TraceLine(a_line);
}

void EndRunEarly(ActiveRun &a_run, std::string a_reason) {
  Record(a_run, Regression::RunEnd{Regression::Outcome::kBlocked,
                                   std::move(a_reason)});
  activeRun.reset();
  runPresent.store(false);
  verifyRenderPlans.store(false);
  screenResolution.store(ResolutionOverride::kNone);
  budgetOverride.store(kNoBudgetOverride);
  QuitGame();
}

void LoadSave(std::string a_save) {
  RE::BGSSaveLoadManager *saves = RE::BGSSaveLoadManager::GetSingleton();
  if (!saves) {
    const std::lock_guard lock{runLock};
    if (activeRun) {
      EndRunEarly(*activeRun, "the save manager is unavailable");
    }
    return;
  }
  logger::info("regression: loading save '{}'", a_save);
  saves->Load(a_save.c_str(), false);
}

class MainMenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
public:
  static MainMenuSink *GetSingleton() {
    static MainMenuSink sink;
    return &sink;
  }

  RE::BSEventNotifyControl
  ProcessEvent(const RE::MenuOpenCloseEvent *a_event,
               RE::BSTEventSource<RE::MenuOpenCloseEvent> *) override {
    if (!a_event || !a_event->opening ||
        a_event->menuName != RE::MainMenu::MENU_NAME) {
      return RE::BSEventNotifyControl::kContinue;
    }
    std::string save;
    {
      const std::lock_guard lock{runLock};
      if (!activeRun ||
          !std::holds_alternative<AwaitingMainMenu>(activeRun->stage)) {
        return RE::BSEventNotifyControl::kContinue;
      }
      activeRun->stage = LoadingSave{};
      save = activeRun->request.save;
    }
    if (const SKSE::TaskInterface *tasks = SKSE::GetTaskInterface()) {
      tasks->AddTask([save = std::move(save)] { LoadSave(save); });
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};

std::int64_t UnixSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void StartResults(const ActiveRun &a_run, const RunSetup &a_setup) {
  const std::filesystem::path trace = Trace::Get().FirstSegment();
  const std::string traceName = trace.filename().string();
  AppendLine(
      a_run.results,
      Regression::StartLineJson(
          {a_run.request.run, a_setup.build, a_setup.source, traceName,
           a_run.request.measuredSeconds,
           a_run.request.screenResolution.value_or(a_setup.screenResolution),
           a_run.request.renderTargetBudgetMiB.value_or(
               a_setup.renderTargetBudgetMiB)}));
}
}

void ReadRegressionRun(const RunSetup &a_setup) {
  const std::optional<std::filesystem::path> directory =
      logger::log_directory();
  if (!directory) {
    return;
  }
  const std::filesystem::path path =
      *directory / Identity::RegressionRunFileName();
  std::error_code missing;
  if (!std::filesystem::exists(path, missing)) {
    return;
  }
  const std::expected<std::string, std::string> text = ReadText(path);
  std::error_code removal;
  std::filesystem::remove(path, removal);
  if (removal) {
    logger::warn("regression: cannot delete {}: {}", path.string(),
                 removal.message());
  }
  if (!text) {
    logger::error("regression: cannot read {}: {}", path.string(),
                  text.error());
    return;
  }
  auto request = Regression::ParseRunFile(*text, UnixSeconds());
  if (!request) {
    logger::error("regression: run file refused: {}", request.error());
    return;
  }
  RE::UI *ui = RE::UI::GetSingleton();
  if (!ui) {
    logger::error("regression: the UI is unavailable; run '{}' not started",
                  request->run);
    return;
  }
  std::filesystem::path results =
      *directory / Identity::RegressionResultsFileName(request->run);
  const std::lock_guard lock{runLock};
  if (request->diagnosticLogging) {
    Trace::Get().Enable(true);
  }
  verifyRenderPlans.store(request->verifyRenderPlans);
  screenResolution.store(OverrideOf(request->screenResolution));
  budgetOverride.store(BudgetOverrideOf(request->renderTargetBudgetMiB));
  activeRun = ActiveRun{std::move(*request), std::move(results)};
  StartResults(*activeRun, a_setup);
  if (!a_setup.effectsEnabled) {
    EndRunEarly(*activeRun, "effects are disabled; see the plugin log");
    return;
  }
  runPresent.store(true);
  ui->AddEventSink<RE::MenuOpenCloseEvent>(MainMenuSink::GetSingleton());
  logger::info("regression: run '{}' waits for the main menu to load '{}'",
               activeRun->request.run, activeRun->request.save);
}

void FinishRegressionLoad(bool a_loaded) {
  if (!runPresent.load()) {
    return;
  }
  const std::lock_guard lock{runLock};
  if (!activeRun) {
    return;
  }
  if (!a_loaded) {
    EndRunEarly(*activeRun,
                "the save did not load; it may need a plugin that is no longer "
                "active, so make it again with the current load order");
    return;
  }
  if (RunningCases *running = std::get_if<RunningCases>(&activeRun->stage)) {
    ForgetWorldAfterLoad(running->world);
    logger::info("regression: save reloaded during a case");
    return;
  }
  if (!std::holds_alternative<LoadingSave>(activeRun->stage)) {
    return;
  }
  RunWorld world;
  world.save = activeRun->request.save;
  HoldCameraStill(world);
  RunningCases running{.state = Regression::BeginRun(activeRun->request),
                       .world = std::move(world)};
  activeRun->stage = std::move(running);
  logger::info("regression: save loaded; settling before the first case");
}

void AdvanceRegressionRun() {
  if (!runPresent.load()) {
    return;
  }
  const std::lock_guard lock{runLock};
  if (!activeRun) {
    return;
  }
  RunningCases *running = std::get_if<RunningCases>(&activeRun->stage);
  if (!running) {
    return;
  }
  Regression::Observation seen = Observe(running->world);
  TraceCrowdChange(running->crowd, seen.crowd);
  running->crowd = seen.crowd;

  Regression::Advanced next =
      Regression::Advance(std::move(running->state), seen);
  running->state = std::move(next.state);
  for (const Regression::ResultLine &line : next.lines) {
    Record(*activeRun, line);
  }
  if (next.command) {
    Execute(*next.command, running->world);
  }
  if (Regression::Done(running->state)) {
    ReleaseWorld(running->world);
    activeRun.reset();
    runPresent.store(false);
    verifyRenderPlans.store(false);
    screenResolution.store(ResolutionOverride::kNone);
    budgetOverride.store(kNoBudgetOverride);
  }
}

bool RegressionVerifiesRenderPlans() noexcept {
  return verifyRenderPlans.load();
}

std::optional<bool> RegressionScreenResolution() noexcept {
  switch (screenResolution.load()) {
  case ResolutionOverride::kOn:
    return true;
  case ResolutionOverride::kOff:
    return false;
  case ResolutionOverride::kNone:
    break;
  }
  return std::nullopt;
}

std::optional<std::uint32_t> RegressionRenderTargetBudgetMiB() noexcept {
  const std::uint64_t value = budgetOverride.load();
  if (value == kNoBudgetOverride) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(value);
}
}
