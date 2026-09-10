#include "engine/Manager.h"

#include "SettingsFile.h"
#include "engine/RecipeStore.h"
#include "render/Compositor.h"
#include "render/RuntimeTextures.h"

#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
constexpr std::uint32_t kEquipFinalizeDelayMS = 100;

std::uint32_t NowMS() { return RE::GetDurationOfApplicationRunTime(); }
}

Manager *Manager::GetSingleton() {
  static Manager singleton;
  return &singleton;
}

void Manager::PostTask(std::function<void()> a_task) {
  const SKSE::TaskInterface *tasks = SKSE::GetTaskInterface();
  if (!tasks) {
    logger::error("no SKSE task interface; dropping work");
    return;
  }
  tasks->AddTask(std::move(a_task));
}

void Manager::QueueRefresh(RE::FormID a_actorID) {
  if (a_actorID == 0) {
    return;
  }
  std::uint64_t generation = 0;
  {
    std::scoped_lock lock{queueLock_};
    if (!pending_.insert(a_actorID).second) {
      rerun_.insert(a_actorID);
      return;
    }
    generation = generation_.load();
  }
  PostTask(
      [this, a_actorID, generation] { RunRefresh(a_actorID, generation); });
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
  const std::uint64_t generation = generation_.load();
  PostTask([this, a_actorID, generation] {
    if (generation == generation_.load()) {
      Retire(a_actorID);
    }
  });
}

void Manager::QueueEquipFinalize(RE::FormID a_actorID) {
  if (a_actorID == 0) {
    return;
  }
  std::scoped_lock lock{queueLock_};
  finalizeDue_[a_actorID] = NowMS() + kEquipFinalizeDelayMS;
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

void Manager::Clear() {
  {
    std::scoped_lock lock{queueLock_};
    pending_.clear();
    rerun_.clear();
    finalizeDue_.clear();
    equipped_.clear();
    ++generation_;
  }
  const std::size_t count = applied_.size();
  applied_.clear();
  loggedNonPBRArmor_.clear();
  carriedTimes_.clear();
  Compositor::GetSingleton()->ClearMeshes();
  Compositor::GetSingleton()->ClearMaterials();
  TextureLab::GetSingleton()->Clear();
  logger::info("cleared {} actor states", count);
}

void Manager::SetEmissivePathEnabled(bool a_enabled) {
  emissivePathEnabled_ = a_enabled;
}
}
