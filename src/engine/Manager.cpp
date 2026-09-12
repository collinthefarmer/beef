#include "engine/Manager.h"

#include "SettingsFile.h"
#include "engine/Events.h"
#include "engine/ManagerShared.h"
#include "engine/RecipeStore.h"
#include "render/Compositor.h"
#include "render/RuntimeTextures.h"

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
  if (RE::PlayerCharacter *player = RE::PlayerCharacter::GetSingleton()) {
    QueueRefresh(player);
  }
  if (GetSettings().playerOnly) {
    return;
  }
  if (RE::ProcessLists *lists = RE::ProcessLists::GetSingleton()) {
    lists->ForEachHighActor([this](RE::Actor *a_actor) {
      QueueRefresh(a_actor);
      return RE::BSContainer::ForEachResult::kContinue;
    });
  }
}

void Manager::BeginLoad() { Clear(); }

void Manager::FinishLoad() {
  applications_.Resume();
  QueueLoadedActorRefreshes();
}

void Manager::Clear() {
  applications_.BeginLoad();
  const std::size_t count = applied_.size();
  for (const auto &[actorID, state] : applied_) {
    UnwatchAnimationEvents(RE::TESForm::LookupByID<RE::Actor>(actorID));
  }
  applied_.clear();
  editor_.CancelPaintForLoad();
  loggedNonPBRArmor_.clear();
  carriedTimes_.clear();
  Compositor::GetSingleton()->ClearMeshes();
  Compositor::GetSingleton()->ClearMaterials();
  TextureLab::GetSingleton()->Clear();
  {
    std::scoped_lock lock{snapshotLock_};
    auto empty = std::make_shared<Snapshot>();
    empty->version = ++snapshotVersion_;
    empty->paintUpdate = editor_.LastPaintUpdate();
    empty->applications = applications_.Snapshot();
    latest_ = std::move(empty);
    watch_.reset();
    watchedMS_ = 0;
  }
  lastTickMS_ = 0;
  frozenLastTick_ = false;
  logger::info("cleared {} actor states", count);
}

void Manager::SetEmissivePathEnabled(bool a_enabled) {
  emissivePathEnabled_ = a_enabled;
}
}
