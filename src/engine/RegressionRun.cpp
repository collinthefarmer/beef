// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/RegressionRun.h"

#include "Core.h"
#include "Identity.h"
#include "diagnostics/Trace.h"
#include "engine/Regression.h"
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
  std::int32_t request = 0;
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

void QuitGame() {
  if (RE::Main *main = RE::Main::GetSingleton()) {
    logger::info("regression: quitting the game");
    main->quitGame = true;
  }
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

std::uint64_t CarriedCount(RE::PlayerCharacter &a_player,
                           RE::TESObjectARMO &a_armor) {
  const auto counts =
      a_player.GetInventoryCounts([&a_armor](RE::TESBoundObject &a_object) {
        return &a_object == &a_armor;
      });
  const auto found = counts.find(&a_armor);
  return found != counts.end() && found->second > 0
             ? static_cast<std::uint64_t>(found->second)
             : 0;
}

Regression::RequestState RequestStateOf(std::int32_t a_request) {
  if (a_request == 0) {
    return Regression::RequestState::kNone;
  }
  const std::string result = RegressionResult(a_request);
  if (result == "WAITING") {
    return Regression::RequestState::kWaiting;
  }
  if (result == "PASS") {
    return Regression::RequestState::kPass;
  }
  if (result == "FAIL") {
    return Regression::RequestState::kFail;
  }
  if (result == "BLOCKED") {
    return Regression::RequestState::kBlocked;
  }
  if (result == "ABORTED") {
    return Regression::RequestState::kAborted;
  }
  return Regression::RequestState::kNone;
}

Regression::Observation Observe(std::int32_t a_request) {
  Regression::Observation seen;
  seen.request = RequestStateOf(a_request);
  RE::PlayerCharacter *player = RE::PlayerCharacter::GetSingleton();
  RE::TESObjectARMO *fixture = RegressionFixture();
  seen.fixtureLoaded = fixture != nullptr;
  if (!player) {
    return seen;
  }
  seen.playerReady = player->Is3DLoaded();
  const RE::TESObjectARMO *body =
      player->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kBody);
  seen.bodyArmorWorn = body != nullptr;
  if (fixture) {
    seen.fixtureEquipped = body == fixture;
    seen.fixtureCarried = CarriedCount(*player, *fixture) > 0;
  }
  return seen;
}

void EquipFixture() {
  RE::PlayerCharacter *player = RE::PlayerCharacter::GetSingleton();
  RE::TESObjectARMO *fixture = RegressionFixture();
  RE::ActorEquipManager *equipment = RE::ActorEquipManager::GetSingleton();
  if (!player || !fixture || !equipment) {
    return;
  }
  player->AddObjectToContainer(fixture, nullptr, 1, nullptr);
  equipment->EquipObject(player, fixture, nullptr, 1, nullptr, true, false,
                         false);
}

void RemoveFixture() {
  RE::PlayerCharacter *player = RE::PlayerCharacter::GetSingleton();
  RE::TESObjectARMO *fixture = RegressionFixture();
  if (!player || !fixture) {
    return;
  }
  if (RE::ActorEquipManager *equipment =
          RE::ActorEquipManager::GetSingleton()) {
    equipment->UnequipObject(player, fixture, nullptr, 1, nullptr, true, false,
                             false);
  }
  player->RemoveItem(fixture, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr,
                     nullptr);
}

std::int32_t SubmitForPlayer(bool a_retire) {
  return SubmitRegressionRequest(RE::PlayerCharacter::GetSingleton(), a_retire);
}

void Execute(const Regression::Command &a_command, std::int32_t &a_request) {
  std::visit(
      Overloaded{
          [](const Regression::SoloRecipe &a_solo) {
            SoloRecipeUnderTest(std::string{a_solo.recipe});
          },
          [](const Regression::RestoreSolo &) { RestoreRecipeView(); },
          [](const Regression::AddAndEquipFixture &) { EquipFixture(); },
          [](const Regression::UnequipAndRemoveFixture &) { RemoveFixture(); },
          [&](const Regression::SubmitApply &) {
            a_request = SubmitForPlayer(false);
          },
          [&](const Regression::SubmitRetire &) {
            a_request = SubmitForPlayer(true);
          },
          [&](const Regression::AbortRequest &) {
            AbortRegressionRequest(a_request);
          },
          [](const Regression::Quit &) { QuitGame(); },
      },
      a_command);
}

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
  if (!activeRun || !std::holds_alternative<LoadingSave>(activeRun->stage)) {
    return;
  }
  if (!a_loaded) {
    EndRunEarly(*activeRun, "the save did not load");
    return;
  }
  activeRun->stage = RunningCases{Regression::BeginRun(activeRun->request)};
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
      Regression::Advance(std::move(running->state), Observe(running->request));
  running->state = std::move(next.state);
  for (const Regression::ResultLine &line : next.lines) {
    Record(*activeRun, line);
  }
  if (next.command) {
    Execute(*next.command, running->request);
  }
  if (Regression::Done(running->state)) {
    activeRun.reset();
    runPresent.store(false);
  }
}
}
