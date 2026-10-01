// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/RegressionRun.h"

#include "Core.h"
#include "Identity.h"
#include "diagnostics/Trace.h"
#include "engine/RegressionWorld.h"
#include "engine/TextFile.h"
#include "regression/Run.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
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
};
using RunStage = std::variant<AwaitingMainMenu, LoadingSave, RunningCases>;

struct ActiveRun {
  Regression::RunRequest request;
  std::filesystem::path results;
  RunStage stage = AwaitingMainMenu{};
};

std::mutex runLock;
std::optional<ActiveRun> activeRun;
std::atomic_bool runPresent{false};

void AppendLine(const std::filesystem::path &a_path, std::string_view a_line) {
  std::ofstream file{a_path, std::ios::app | std::ios::binary};
  if (!file) {
    logger::warn("regression: cannot write {}", a_path.string());
    return;
  }
  file << a_line << '\n';
}

void TraceLine(const Regression::ResultLine &a_line) {
  std::visit(
      Overloaded{
          [](const Regression::StepResult &a_step) {
            Trace::EmitSafely(Trace::Event::kCommand,
                              {{"action", "regression.step"},
                               {"case", std::string{a_step.caseName}},
                               {"step", std::to_string(a_step.step)},
                               {"operation", std::string{a_step.action}},
                               {"result", std::string{Regression::OutcomeName(
                                              a_step.outcome)}}});
          },
          [](const Regression::CaseResult &a_case) {
            Trace::EmitSafely(Trace::Event::kCommand,
                              {{"action", "regression.case"},
                               {"case", std::string{a_case.caseName}},
                               {"result", std::string{Regression::OutcomeName(
                                              a_case.outcome)}}});
          },
          [](const Regression::WindowBegins &a_window) {
            Trace::EmitSafely(Trace::Event::kCommand,
                              {{"action", "regression.window"},
                               {"window", std::string{a_window.window}},
                               {"edge", "begin"}});
          },
          [](const Regression::WindowEnds &a_window) {
            Trace::EmitSafely(Trace::Event::kCommand,
                              {{"action", "regression.window"},
                               {"window", std::string{a_window.window}},
                               {"edge", "end"}});
          },
          [](const Regression::RunEnd &a_end) {
            Trace::EmitSafely(Trace::Event::kCommand,
                              {{"action", "regression.end"},
                               {"result", std::string{Regression::OutcomeName(
                                              a_end.outcome)}}});
          },
      },
      a_line);
}

void Record(const ActiveRun &a_run, const Regression::ResultLine &a_line) {
  const std::string json = Regression::ResultLineJson(a_line);
  logger::info("regression: {}", json);
  AppendLine(a_run.results, json);
  TraceLine(a_line);
}

void EndRunEarly(ActiveRun &a_run, std::string a_reason) {
  Record(a_run, Regression::RunEnd{Regression::Outcome::kBlocked,
                                   std::move(a_reason)});
  activeRun.reset();
  runPresent.store(false);
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
  AppendLine(a_run.results, Regression::StartLineJson(
                                a_run.request.run, a_setup.build,
                                a_setup.source, trace.filename().string()));
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
  auto request = Regression::ParseRunRequest(*text, UnixSeconds());
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
  Regression::Advanced next =
      Regression::Advance(std::move(running->state), Observe(running->world));
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
  }
}
}
