// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Manager.h"
#include "diagnostics/Trace.h"

#include "SettingsFile.h"
#include "engine/Clock.h"
#include "engine/GameObjectService.h"
#include "engine/RecipeStore.h"
#include "render/Compositor.h"
#include "render/TextureLab.h"

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
                      RunRefresh(a_id, a_tokens);
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

void Manager::QueueRefresh(RE::FormID a_actorID) {
  applications_.Refresh(a_actorID);
}

void Manager::QueueRefresh(RE::Actor *a_actor) {
  if (a_actor) {
    QueueRefresh(a_actor->GetFormID());
  }
}

void Manager::QueueRetire(RE::FormID a_actorID) {
  if (a_actorID == 0) {
    return;
  }
  PostTask([this, a_actorID] {
    AbandonApplications(a_actorID);
    Retire(a_actorID);
  });
}

void Manager::QueueEquipFinalize(RE::FormID a_actorID) {
  applications_.Equip(a_actorID, NowMS());
}

void Manager::QueueLoadedActorRefreshes() {
  for (const auto id : LoadedActorIDs())
    QueueRefresh(id);
}

void Manager::BeginLoad() { Clear(); }

void Manager::FinishLoad() {
  const Trace::Scope trace{Trace::Command("load.finish")};
  Trace::EmitSafely(Trace::Event::kLoad, {{"action", "resume"}});
  applications_.Resume();
  QueueLoadedActorRefreshes();
}

void Manager::Clear() {
  const auto session = Trace::BeginSession();
  const Trace::Scope trace{Trace::Command("load.begin")};
  Trace::EmitSafely(Trace::Event::kLoad,
                    {{"action", "clear_begin"},
                     {"generation", std::to_string(session)},
                     {"actors", std::to_string(applied_.size())}});
  applications_.BeginLoad();
  animations_.Clear();
  ClearAnimEvents();
  const std::size_t count = applied_.size();
  for (auto &[actorID, state] : applied_)
    RetireActorEffects(state);
  applied_.clear();
  evictedForDistance_.clear();
  awaitingModel_.clear();
  editor_.CancelFileOperationsForLoad();
  SweepRetiredMaterialTextures();
  editor_.CancelPaintForLoad();
  loggedNonPBRArmor_.clear();
  stackWarnings_.Clear();
  carriedTimes_.Clear();
  Compositor::GetSingleton()->ClearMeshes();
  Compositor::GetSingleton()->ClearMaterials();
  TextureLab::GetSingleton()->Clear();
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
                                          {"actors", std::to_string(count)}});
  logger::info("cleared {} actor states", count);
}

void Manager::SetEmissivePathEnabled(bool a_enabled) {
  emissivePathEnabled_ = a_enabled;
}
}
