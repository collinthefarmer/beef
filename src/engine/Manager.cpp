// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Manager.h"
#include "Identity.h"
#include "diagnostics/Trace.h"

#include "SettingsFile.h"
#include "engine/Clock.h"
#include "engine/GameObjectService.h"
#include "engine/RecipeStore.h"
#include "render/Compositor.h"
#include "render/TextureLab.h"

#include <format>
#include <string>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
bool SubmitTask(std::function<void()> a_task) {
  const SKSE::TaskInterface *tasks = SKSE::GetTaskInterface();
  if (!tasks) {
    logger::error("no SKSE task interface; dropping work");
    return false;
  }
  tasks->AddTask(std::move(a_task));
  return true;
}
}

Manager::Manager()
    : applications_(SubmitTask,
                    [this](std::uint32_t a_id,
                           const std::vector<ApplicationToken> &a_tokens) {
                      NoteStale(a_id, a_tokens);
                    }),
      animations_([this](AnimationEvent a_event) {
        QueueAnimationEvent(std::move(a_event));
      }) {}

Manager *Manager::GetSingleton() {
  static Manager singleton;
  return &singleton;
}

RecipeEditor &Manager::Editor() noexcept { return editor_; }

void Manager::PostTask(std::function<void()> a_task) {
  applications_.Post(std::move(a_task));
}

std::uint32_t Manager::EditorClockMS() const { return NowMS(); }

void Manager::MarkStale(RE::FormID a_actorID) {
  applications_.MarkStale(a_actorID);
}

void Manager::MarkStale(RE::Actor *a_actor) {
  if (a_actor) {
    MarkStale(a_actor->GetFormID());
  }
}

void Manager::HoldIdleCameraForMenu() {
  if (!menuIdleHold_) {
    menuIdleHold_ = HoldIdleCamera();
  }
}

void Manager::ReleaseIdleCameraForMenu() {
  if (const std::optional<IdleCameraHold> hold =
          std::exchange(menuIdleHold_, std::nullopt)) {
    ReleaseIdleCamera(*hold);
  }
}

void Manager::NoteMenuClosed() const {
  if (editor_.CurrentView().IsLive() && RecipesWithHolds().empty()) {
    return;
  }
  const std::string notice =
      std::format("{}: a studio preview is still on. Return to live in the "
                  "studio ends it.",
                  Identity::kMenuTitle);
  RE::SendHUDMessage::ShowHUDMessage(notice.c_str());
}

void Manager::QueueForget(RE::FormID a_actorID) {
  if (a_actorID == 0) {
    return;
  }
  PostTask([this, a_actorID] { forgets_.insert(a_actorID); });
}

void Manager::QueueEquipFinalize(RE::FormID a_actorID) {
  applications_.Equip(a_actorID, NowMS());
}

void Manager::MarkLoadedActorsStale() {
  for (const auto id : LoadedActorIDs())
    MarkStale(id);
}

void Manager::BeginLoad() { Clear(); }

void Manager::FinishLoad() {
  const Trace::Scope trace{Trace::Operation("load.finish")};
  Trace::EmitSafely(Trace::Event::kLoad, {{"action", "resume"}});
  applications_.Resume();
  MarkLoadedActorsStale();
}

void Manager::Clear() {
  const auto session = Trace::BeginSession();
  const Trace::Scope trace{Trace::Operation("load.begin")};
  Trace::EmitSafely(Trace::Event::kLoad,
                    {{"action", "clear_begin"},
                     {"generation", std::to_string(session)},
                     {"actors", std::to_string(live_.size())}});
  applications_.BeginLoad();
  animations_.Clear();
  ClearAnimEvents();
  const std::size_t count = live_.size();
  for (auto &[actorID, state] : live_)
    RetireActorEffects(state);
  live_.clear();
  spreadDraws_.clear();
  world_ = {};
  models_.clear();
  results_.clear();
  stale_.clear();
  forgets_.clear();
  pendingSample_.reset();
  heartbeatDue_ = false;
  disagreements_.clear();
  planJobs_ = {};
  handedPlans_ = {};
  (void)planQueue_.DropQueued();
  editor_.CancelFileOperationsForLoad();
  SweepRetiredMaterialTextures();
  editor_.CancelPaintForLoad();
  editor_.ResetViewForLoad();
  loggedNonPBRArmor_.clear();
  warningHistory_.Clear();
  carriedInstances_.Clear();
  Compositor::GetSingleton()->ClearStepOutputs();
  Compositor::GetSingleton()->ClearMeshes();
  Compositor::GetSingleton()->ClearMaterials();
  const std::size_t images = Compositor::GetSingleton()->ClearImages();
  if (TextureLab *lab = TextureLab::GetSingleton()) {
    lab->Clear();
  }
  {
    std::scoped_lock lock{snapshotLock_};
    auto empty = std::make_shared<Snapshot>();
    empty->version = ++snapshotVersion_;
    empty->paintUpdate = editor_.LastPaintUpdate();
    empty->applications = applications_.Snapshot();
    empty->fileOperations = editor_.FileOperations();
    empty->editResults = editor_.EditResults();
    empty->gesture = editor_.LastGesture();
    latest_ = std::move(empty);
    watch_.reset();
    watchedMS_ = 0;
  }
  lastTickMS_ = 0;
  frozenLastTick_ = false;
  Trace::EmitSafely(Trace::Event::kLoad, {{"action", "clear_end"},
                                          {"actors", std::to_string(count)},
                                          {"images", std::to_string(images)}});
  logger::info("cleared {} actor states", count);
}

void Manager::SetEmissivePathEnabled(bool a_enabled) {
  emissivePathEnabled_ = a_enabled;
}
}
