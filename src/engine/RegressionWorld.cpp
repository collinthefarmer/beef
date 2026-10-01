// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/RegressionWorld.h"

#include "Core.h"
#include "Identity.h"
#include "SettingsFile.h"
#include "engine/Manager.h"
#include "engine/RegressionRequests.h"
#include "render/PBRMaterial.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string_view>

namespace BetterEnchantmentEffects {
namespace {
inline constexpr std::string_view kSkyrim = "Skyrim.esm";
inline constexpr RE::FormID kPlainCuirass = 0x1394D;
inline constexpr RE::FormID kMannequin = 0x89A85;
inline constexpr RE::FormID kXMarker = 0x3B;
inline constexpr const char *kTravelCell = "Riverwood";
inline constexpr float kSpawnOffset = 120.0f;
inline constexpr float kWorkOpacity = 0.5f;
inline constexpr float kCrowdRadius = 300.0f;

template <class Form> Form *SkyrimForm(RE::FormID a_local) {
  RE::TESDataHandler *data = RE::TESDataHandler::GetSingleton();
  return data ? data->LookupForm<Form>(a_local, kSkyrim) : nullptr;
}

RE::TESObjectARMO *ArmorOf(Regression::Item a_item) {
  return a_item == Regression::Item::kFixture
             ? RegressionFixture()
             : SkyrimForm<RE::TESObjectARMO>(kPlainCuirass);
}

RE::Actor *ActorOf(const RunWorld &a_world, Regression::Role a_role) {
  if (a_role == Regression::Role::kPlayer) {
    return RE::PlayerCharacter::GetSingleton();
  }
  const RE::FormID id = a_world.spawned[Regression::IndexOf(a_role)];
  return id ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr;
}

RE::TESObjectREFR *MarkerOf(const RunWorld &a_world) {
  return a_world.marker
             ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_world.marker)
             : nullptr;
}

std::int32_t CountOf(RE::Actor &a_actor, RE::TESObjectARMO &a_armor) {
  const auto counts =
      a_actor.GetInventoryCounts([&a_armor](RE::TESBoundObject &a_object) {
        return &a_object == &a_armor;
      });
  const auto found = counts.find(&a_armor);
  return found != counts.end() ? found->second : 0;
}

std::uint32_t ResidueUnder(RE::NiAVObject *a_root) {
  if (!a_root) {
    return 0;
  }
  std::uint32_t residue = 0;
  RE::BSVisit::TraverseScenegraphGeometries(
      a_root, [&residue](RE::BSGeometry *a_geometry) {
        const char *name = a_geometry ? a_geometry->name.c_str() : nullptr;
        if (name &&
            std::string_view{name}.ends_with(Identity::ShellNodeSuffix())) {
          ++residue;
          return RE::BSVisit::BSVisitControl::kContinue;
        }
        if (const std::optional<PbrMaterial> material =
                PbrMaterial::Bind(LightingPropertyOf(a_geometry))) {
          residue += static_cast<std::uint32_t>(material->PresenterTextures());
        }
        return RE::BSVisit::BSVisitControl::kContinue;
      });
  return residue;
}

std::uint32_t ResidueOn(RE::Actor &a_actor) {
  RE::NiAVObject *third = a_actor.Get3D(false);
  RE::NiAVObject *first = a_actor.Get3D(true);
  return ResidueUnder(third) + (first != third ? ResidueUnder(first) : 0);
}

bool Present(const RE::Actor &a_actor) {
  return !a_actor.IsDisabled() && !a_actor.IsDeleted() && a_actor.Is3DLoaded();
}

Regression::ActorFacts ViewOf(RE::Actor *a_actor,
                              RegressionActorFacts a_facts) {
  Regression::ActorFacts view;
  if (!a_actor) {
    return view;
  }
  view.present = Present(*a_actor);
  const RE::TESObjectARMO *body =
      a_actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kBody);
  view.bodyArmorWorn = body != nullptr;
  for (const Regression::Item item :
       {Regression::Item::kFixture, Regression::Item::kPlainCuirass}) {
    RE::TESObjectARMO *armor = ArmorOf(item);
    Regression::ItemFacts &seen = view.armor[Regression::IndexOf(item)];
    seen.equipped = armor && body == armor;
    seen.carried = armor && CountOf(*a_actor, *armor) > 0;
  }
  view.live = a_facts.live;
  view.renderedAttempt = a_facts.renderedAttempt;
  view.application = std::move(a_facts.application);
  view.residue = view.present ? ResidueOn(*a_actor) : 0;
  return view;
}

bool AwayFromStart(const RunWorld &a_world) {
  const RE::PlayerCharacter *player = RE::PlayerCharacter::GetSingleton();
  const RE::TESObjectREFR *marker = MarkerOf(a_world);
  return player && marker && player->GetParentCell() &&
         player->GetParentCell() != marker->GetParentCell();
}

void Equip(RE::Actor *a_actor, Regression::Item a_item, bool a_add) {
  RE::TESObjectARMO *armor = ArmorOf(a_item);
  RE::ActorEquipManager *equipment = RE::ActorEquipManager::GetSingleton();
  if (!a_actor || !armor || !equipment) {
    return;
  }
  if (a_add) {
    a_actor->AddObjectToContainer(armor, nullptr, 1, nullptr);
  }
  const bool preventRemoval = !a_actor->IsPlayerRef();
  equipment->EquipObject(a_actor, armor, nullptr, 1, nullptr, true,
                         preventRemoval, false);
}

void Unequip(RE::Actor *a_actor, Regression::Item a_item) {
  RE::TESObjectARMO *armor = ArmorOf(a_item);
  RE::ActorEquipManager *equipment = RE::ActorEquipManager::GetSingleton();
  if (a_actor && armor && equipment) {
    equipment->UnequipObject(a_actor, armor, nullptr, 1, nullptr, true, false,
                             false);
  }
}

void RemoveArmor(RE::Actor *a_actor, Regression::Item a_item) {
  RE::TESObjectARMO *armor = ArmorOf(a_item);
  if (!a_actor || !armor) {
    return;
  }
  Unequip(a_actor, a_item);
  a_actor->RemoveItem(armor, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr,
                      nullptr);
}

void DeleteReference(RE::TESObjectREFR *a_reference) {
  if (a_reference) {
    a_reference->Disable();
    a_reference->SetDelete(true);
  }
}

void Spawn(RunWorld &a_world, Regression::Role a_role) {
  RE::PlayerCharacter *player = RE::PlayerCharacter::GetSingleton();
  RE::TESNPC *base = SkyrimForm<RE::TESNPC>(kMannequin);
  if (!player || !base || a_role == Regression::Role::kPlayer) {
    return;
  }
  const RE::NiPointer<RE::TESObjectREFR> placed =
      player->PlaceObjectAtMe(base, true);
  if (!placed) {
    return;
  }
  const float side = a_role == Regression::Role::kWearer ? 1.0f : -1.0f;
  RE::NiPoint3 position = player->GetPosition();
  position.x += side * kSpawnOffset;
  placed->SetPosition(position);
  a_world.spawned[Regression::IndexOf(a_role)] = placed->GetFormID();
}

void SpawnCrowdActors(RunWorld &a_world, std::uint32_t a_count) {
  RE::PlayerCharacter *player = RE::PlayerCharacter::GetSingleton();
  RE::TESNPC *base = SkyrimForm<RE::TESNPC>(kMannequin);
  if (!player || !base) {
    return;
  }
  const RE::NiPoint3 centre = player->GetPosition();
  const std::uint32_t count = std::min(a_count, Regression::kMaxCrowd);
  for (std::uint32_t index = 0; index < count; ++index) {
    const RE::NiPointer<RE::TESObjectREFR> placed =
        player->PlaceObjectAtMe(base, true);
    RE::Actor *actor = placed ? placed->As<RE::Actor>() : nullptr;
    if (!actor) {
      continue;
    }
    const float angle =
        6.2831853f * static_cast<float>(index) / static_cast<float>(count);
    placed->SetPosition(RE::NiPoint3{centre.x + kCrowdRadius * std::cos(angle),
                                     centre.y + kCrowdRadius * std::sin(angle),
                                     centre.z});
    Equip(actor, Regression::Item::kFixture, true);
    a_world.crowd.push_back(actor->GetFormID());
  }
}

void DespawnCrowdActors(RunWorld &a_world) {
  for (const RE::FormID id : a_world.crowd) {
    DeleteReference(RE::TESForm::LookupByID<RE::TESObjectREFR>(id));
  }
  a_world.crowd.clear();
}

Regression::CrowdFacts CrowdOf(const RunWorld &a_world,
                               std::span<const RegressionActorFacts> a_facts) {
  Regression::CrowdFacts view;
  for (std::size_t i = 0; i < a_world.crowd.size() && i < a_facts.size(); ++i) {
    const RE::Actor *actor =
        RE::TESForm::LookupByID<RE::Actor>(a_world.crowd[i]);
    if (!actor || !Present(*actor)) {
      continue;
    }
    ++view.present;
    if (a_facts[i].renderedAttempt > 0) {
      ++view.rendered;
    }
  }
  return view;
}

std::uint64_t SteadyMs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

void Despawn(RunWorld &a_world, Regression::Role a_role) {
  DeleteReference(ActorOf(a_world, a_role));
  if (a_role != Regression::Role::kPlayer) {
    a_world.spawned[Regression::IndexOf(a_role)] = 0;
  }
}

std::uint64_t
WithManager(const std::function<std::uint64_t(Manager &)> &a_start) {
  Manager *manager = Manager::GetSingleton();
  return manager ? a_start(*manager) : 0;
}

void QueueTask(std::function<void()> a_task) {
  if (const SKSE::TaskInterface *tasks = SKSE::GetTaskInterface()) {
    tasks->AddTask(std::move(a_task));
  }
}

void TravelFromStart(RunWorld &a_world) {
  RE::PlayerCharacter *player = RE::PlayerCharacter::GetSingleton();
  RE::TESForm *markerForm = RE::TESForm::LookupByID(kXMarker);
  RE::TESBoundObject *marker =
      markerForm ? markerForm->As<RE::TESBoundObject>() : nullptr;
  if (!player || !marker) {
    logger::warn("regression: cannot travel: the {} is missing",
                 player ? "XMarker base form" : "player");
    return;
  }
  if (!MarkerOf(a_world)) {
    const RE::NiPointer<RE::TESObjectREFR> placed =
        player->PlaceObjectAtMe(marker, true);
    a_world.marker = placed ? placed->GetFormID() : 0;
  }
  if (!a_world.marker) {
    logger::warn("regression: the return marker could not be placed");
    return;
  }
  QueueTask([] {
    if (RE::PlayerCharacter *traveller = RE::PlayerCharacter::GetSingleton()) {
      const bool moved = traveller->CenterOnCell(kTravelCell);
      logger::info("regression: travel to {} {}", kTravelCell,
                   moved ? "started" : "was refused");
    }
  });
}

void TravelToStart(const RunWorld &a_world) {
  const RE::FormID marker = a_world.marker;
  QueueTask([marker] {
    RE::PlayerCharacter *traveller = RE::PlayerCharacter::GetSingleton();
    RE::TESObjectREFR *target =
        marker ? RE::TESForm::LookupByID<RE::TESObjectREFR>(marker) : nullptr;
    if (traveller && target) {
      traveller->MoveTo(target);
    }
  });
}

void QueueWork(RunWorld &a_world, Regression::QueuedWork a_work,
               std::string_view a_recipe) {
  Manager *manager = Manager::GetSingleton();
  switch (a_work) {
  case Regression::QueuedWork::kNothing:
    break;
  case Regression::QueuedWork::kApply:
    a_world.request = SubmitRegressionRequest(
        RE::PlayerCharacter::GetSingleton(), RequestKind::kApply);
    break;
  case Regression::QueuedWork::kEdit:
    a_world.gesture = 0;
    if (manager) {
      a_world.edit =
          manager->StartRegressionEdit(std::string{a_recipe}, kWorkOpacity);
    }
    break;
  }
}

void OpenSession(RunWorld &a_world, Regression::Session a_session,
                 std::string_view a_recipe) {
  Manager *manager = Manager::GetSingleton();
  if (!manager) {
    return;
  }
  if (a_session == Regression::Session::kGesture) {
    a_world.edit = 0;
    a_world.gesture = manager->StartRegressionGesture(std::string{a_recipe});
  } else {
    manager->StartRegressionPaint(std::string{a_recipe});
  }
}

void LoadSaveWith(RunWorld &a_world, const Regression::LoadSaveWith &a_reload) {
  QueueTask([save = a_world.save] {
    if (RE::BGSSaveLoadManager *saves =
            RE::BGSSaveLoadManager::GetSingleton()) {
      logger::info("regression: reloading save '{}' with work in flight", save);
      saves->Load(save.c_str(), false);
    }
  });
  QueueWork(a_world, a_reload.work, a_reload.recipe);
}

void SwitchCamera(Regression::Camera a_view) {
  RE::PlayerCamera *camera = RE::PlayerCamera::GetSingleton();
  if (!camera) {
    return;
  }
  if (a_view == Regression::Camera::kFirstPerson) {
    camera->ForceFirstPerson();
  } else {
    camera->ForceThirdPerson();
  }
}

void Perform(const Regression::SoloRecipe &a_c, RunWorld &) {
  SoloRegressionRecipe(std::string{a_c.recipe});
}

void Perform(const Regression::EndSolo &, RunWorld &) { EndRegressionSolo(); }

void Perform(const Regression::SpawnActor &a_c, RunWorld &a_world) {
  Spawn(a_world, a_c.role);
}

void Perform(const Regression::DespawnActor &a_c, RunWorld &a_world) {
  Despawn(a_world, a_c.role);
}

void Perform(const Regression::DisableActor &a_c, RunWorld &a_world) {
  if (RE::Actor *actor = ActorOf(a_world, a_c.role)) {
    actor->Disable();
  }
}

void Perform(const Regression::EnableActor &a_c, RunWorld &a_world) {
  if (RE::Actor *actor = ActorOf(a_world, a_c.role)) {
    actor->Enable(false);
  }
}

void Perform(const Regression::AddAndEquip &a_c, RunWorld &a_world) {
  Equip(ActorOf(a_world, a_c.role), a_c.item, true);
}

void Perform(const Regression::EquipCarried &a_c, RunWorld &a_world) {
  Equip(ActorOf(a_world, a_c.role), a_c.item, false);
}

void Perform(const Regression::UnequipArmor &a_c, RunWorld &a_world) {
  Unequip(ActorOf(a_world, a_c.role), a_c.item);
}

void Perform(const Regression::RemoveArmor &a_c, RunWorld &a_world) {
  RemoveArmor(ActorOf(a_world, a_c.role), a_c.item);
}

void Perform(const Regression::SubmitApply &a_c, RunWorld &a_world) {
  a_world.request =
      SubmitRegressionRequest(ActorOf(a_world, a_c.role), RequestKind::kApply);
}

void Perform(const Regression::SubmitRetire &a_c, RunWorld &a_world) {
  a_world.request =
      SubmitRegressionRequest(ActorOf(a_world, a_c.role), RequestKind::kRetire);
}

void Perform(const Regression::AbortRequest &, RunWorld &a_world) {
  AbortRegressionRequest(a_world.request);
}

void Perform(const Regression::TravelFromStart &, RunWorld &a_world) {
  TravelFromStart(a_world);
}

void Perform(const Regression::TravelToStart &, RunWorld &a_world) {
  TravelToStart(a_world);
}

void Perform(const Regression::SwitchCamera &a_c, RunWorld &) {
  SwitchCamera(a_c.camera);
}

void Perform(const Regression::StartDuplicate &a_c, RunWorld &a_world) {
  a_world.edit = WithManager([&](Manager &a_manager) {
    return a_manager.StartRegressionDuplicate(std::string{a_c.from},
                                              std::string{a_c.to});
  });
}

void Perform(const Regression::StartOpacityEdit &a_c, RunWorld &a_world) {
  a_world.edit = WithManager([&](Manager &a_manager) {
    return a_manager.StartRegressionEdit(std::string{a_c.recipe}, a_c.value);
  });
}

void Perform(const Regression::StartSave &a_c, RunWorld &a_world) {
  a_world.file = WithManager([&](Manager &a_manager) {
    return a_manager.StartRegressionSave(std::string{a_c.recipe});
  });
}

void Perform(const Regression::StartDelete &a_c, RunWorld &a_world) {
  a_world.edit = WithManager([&](Manager &a_manager) {
    return a_manager.StartRegressionDelete(std::string{a_c.recipe});
  });
}

void Perform(const Regression::OpenSession &a_c, RunWorld &a_world) {
  OpenSession(a_world, a_c.session, a_c.recipe);
}

void Perform(const Regression::LoadSaveWith &a_c, RunWorld &a_world) {
  LoadSaveWith(a_world, a_c);
}

void Perform(const Regression::Quit &, RunWorld &) { QuitGame(); }
void Perform(const Regression::SpawnCrowdActors &a_c, RunWorld &a_world) {
  SpawnCrowdActors(a_world, a_c.count);
}

void Perform(const Regression::DespawnCrowdActors &, RunWorld &a_world) {
  DespawnCrowdActors(a_world);
}
}

Regression::Observation Observe(const RunWorld &a_world) {
  Regression::Observation seen;
  constexpr std::array kRoles{Regression::Role::kPlayer,
                              Regression::Role::kWearer,
                              Regression::Role::kControl};
  std::array<RE::Actor *, Regression::kRoleCount> actors{};
  std::vector<RE::FormID> ids;
  for (const Regression::Role role : kRoles) {
    RE::Actor *actor = ActorOf(a_world, role);
    actors[Regression::IndexOf(role)] = actor;
    ids.push_back(actor ? actor->GetFormID() : 0);
  }
  ids.insert(ids.end(), a_world.crowd.begin(), a_world.crowd.end());
  const Manager *manager = Manager::GetSingleton();
  std::vector<RegressionActorFacts> facts =
      manager ? manager->RegressionActors(ids)
              : std::vector<RegressionActorFacts>(ids.size());
  for (const Regression::Role role : kRoles) {
    const std::size_t index = Regression::IndexOf(role);
    seen.actors[index] = ViewOf(actors[index], std::move(facts[index]));
  }
  seen.itemsLoaded = {ArmorOf(Regression::Item::kFixture) != nullptr,
                      ArmorOf(Regression::Item::kPlainCuirass) != nullptr};
  seen.npcEffects = !GetSettings().playerOnly;
  const RE::PlayerCamera *camera = RE::PlayerCamera::GetSingleton();
  seen.camera = camera && camera->IsInFirstPerson()
                    ? Regression::Camera::kFirstPerson
                    : Regression::Camera::kThirdPerson;
  seen.awayFromStart = AwayFromStart(a_world);
  seen.request = RegressionRequestState(a_world.request);
  seen.loads = a_world.loads;
  seen.crowd = CrowdOf(a_world, std::span{facts}.subspan(kRoles.size()));
  seen.nowMs = SteadyMs();
  if (manager) {
    seen.activity = manager->RegressionActivity(a_world.edit, a_world.gesture,
                                                a_world.file);
    seen.scratch = manager->RegressionRecipe(Regression::kScratchRecipe);
  }
  return seen;
}

void Execute(const Regression::Command &a_command, RunWorld &a_world) {
  std::visit([&](const auto &a_kind) { Perform(a_kind, a_world); }, a_command);
}

void ReleaseWorld(RunWorld &a_world) {
  DespawnCrowdActors(a_world);
  DeleteReference(MarkerOf(a_world));
  a_world.marker = 0;
  for (const Regression::Role role :
       {Regression::Role::kWearer, Regression::Role::kControl}) {
    Despawn(a_world, role);
  }
}

void ForgetWorldAfterLoad(RunWorld &a_world) {
  a_world.spawned = {};
  a_world.crowd.clear();
  a_world.marker = 0;
  ++a_world.loads;
}

void QuitGame() {
  if (RE::Main *main = RE::Main::GetSingleton()) {
    logger::info("regression: quitting the game");
    main->quitGame = true;
  }
}
}
