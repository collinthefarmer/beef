// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/RegressionWorld.h"

#include "Core.h"
#include "Identity.h"
#include "SettingsFile.h"
#include "engine/Manager.h"
#include "engine/Regression.h"
#include "render/PBRMaterial.h"

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
  const RE::FormID id = a_world.spawned[Regression::RoleIndex(a_role)];
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

std::uint32_t TracesUnder(RE::NiAVObject *a_root) {
  if (!a_root) {
    return 0;
  }
  std::uint32_t traces = 0;
  RE::BSVisit::TraverseScenegraphGeometries(
      a_root, [&traces](RE::BSGeometry *a_geometry) {
        const char *name = a_geometry ? a_geometry->name.c_str() : nullptr;
        if (name &&
            std::string_view{name}.ends_with(Identity::ShellNodeSuffix())) {
          ++traces;
          return RE::BSVisit::BSVisitControl::kContinue;
        }
        if (const std::optional<PbrMaterial> material =
                PbrMaterial::Bind(LightingPropertyOf(a_geometry))) {
          traces += static_cast<std::uint32_t>(material->PresenterTextures());
        }
        return RE::BSVisit::BSVisitControl::kContinue;
      });
  return traces;
}

std::uint32_t TracesOn(RE::Actor &a_actor) {
  RE::NiAVObject *third = a_actor.Get3D(false);
  RE::NiAVObject *first = a_actor.Get3D(true);
  return TracesUnder(third) + (first != third ? TracesUnder(first) : 0);
}

bool Present(const RE::Actor &a_actor) {
  return !a_actor.IsDisabled() && !a_actor.IsDeleted() && a_actor.Is3DLoaded();
}

Regression::ActorView ViewOf(RE::Actor *a_actor) {
  Regression::ActorView view;
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
    Regression::ArmorView &seen =
        view.armor[item == Regression::Item::kFixture ? 0 : 1];
    seen.equipped = armor && body == armor;
    seen.carried = armor && CountOf(*a_actor, *armor) > 0;
  }
  if (const Manager *manager = Manager::GetSingleton()) {
    RegressionActorFacts facts = manager->RegressionActor(a_actor->GetFormID());
    view.live = facts.live;
    view.renderedAttempt = facts.renderedAttempt;
    view.application = std::move(facts.application);
  }
  view.traces = view.present ? TracesOn(*a_actor) : 0;
  return view;
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

void Delete(RE::TESObjectREFR *a_reference) {
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
  a_world.spawned[Regression::RoleIndex(a_role)] = placed->GetFormID();
}

void Despawn(RunWorld &a_world, Regression::Role a_role) {
  Delete(ActorOf(a_world, a_role));
  if (a_role != Regression::Role::kPlayer) {
    a_world.spawned[Regression::RoleIndex(a_role)] = 0;
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

void TravelAway(RunWorld &a_world) {
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

void TravelBack(const RunWorld &a_world) {
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

void StartWork(RunWorld &a_world, Regression::Work a_work,
               std::string_view a_recipe) {
  Manager *manager = Manager::GetSingleton();
  const std::string recipe{a_recipe};
  switch (a_work) {
  case Regression::Work::kNothing:
    break;
  case Regression::Work::kApply:
    a_world.request =
        SubmitRegressionRequest(RE::PlayerCharacter::GetSingleton(), false);
    break;
  case Regression::Work::kEdit:
    a_world.gesture = 0;
    if (manager) {
      a_world.edit = manager->StartRegressionEdit(recipe, kWorkOpacity);
    }
    break;
  case Regression::Work::kGesture:
    a_world.edit = 0;
    if (manager) {
      a_world.gesture = manager->StartRegressionGesture(recipe);
    }
    break;
  case Regression::Work::kPaint:
    if (manager) {
      manager->StartRegressionPaint(recipe);
    }
    break;
  }
}

void ReloadDuring(RunWorld &a_world, const Regression::ReloadDuring &a_reload) {
  QueueTask([save = a_world.save] {
    if (RE::BGSSaveLoadManager *saves =
            RE::BGSSaveLoadManager::GetSingleton()) {
      logger::info("regression: reloading save '{}' with work in flight", save);
      saves->Load(save.c_str(), false);
    }
  });
  StartWork(a_world, a_reload.work, a_reload.recipe);
}

void SetCamera(Regression::View a_view) {
  RE::PlayerCamera *camera = RE::PlayerCamera::GetSingleton();
  if (!camera) {
    return;
  }
  if (a_view == Regression::View::kFirstPerson) {
    camera->ForceFirstPerson();
  } else {
    camera->ForceThirdPerson();
  }
}

void Carry(const Regression::SoloRecipe &a_c, RunWorld &) {
  SoloRecipeUnderTest(std::string{a_c.recipe});
}

void Carry(const Regression::RestoreSolo &, RunWorld &) { RestoreRecipeView(); }

void Carry(const Regression::SpawnActor &a_c, RunWorld &a_world) {
  Spawn(a_world, a_c.role);
}

void Carry(const Regression::DespawnActor &a_c, RunWorld &a_world) {
  Despawn(a_world, a_c.role);
}

void Carry(const Regression::DisableActor &a_c, RunWorld &a_world) {
  if (RE::Actor *actor = ActorOf(a_world, a_c.role)) {
    actor->Disable();
  }
}

void Carry(const Regression::EnableActor &a_c, RunWorld &a_world) {
  if (RE::Actor *actor = ActorOf(a_world, a_c.role)) {
    actor->Enable(false);
  }
}

void Carry(const Regression::AddAndEquip &a_c, RunWorld &a_world) {
  Equip(ActorOf(a_world, a_c.role), a_c.item, true);
}

void Carry(const Regression::EquipCarried &a_c, RunWorld &a_world) {
  Equip(ActorOf(a_world, a_c.role), a_c.item, false);
}

void Carry(const Regression::UnequipArmor &a_c, RunWorld &a_world) {
  Unequip(ActorOf(a_world, a_c.role), a_c.item);
}

void Carry(const Regression::RemoveArmor &a_c, RunWorld &a_world) {
  RemoveArmor(ActorOf(a_world, a_c.role), a_c.item);
}

void Carry(const Regression::SubmitApply &a_c, RunWorld &a_world) {
  a_world.request = SubmitRegressionRequest(ActorOf(a_world, a_c.role), false);
}

void Carry(const Regression::SubmitRetire &a_c, RunWorld &a_world) {
  a_world.request = SubmitRegressionRequest(ActorOf(a_world, a_c.role), true);
}

void Carry(const Regression::AbortRequest &, RunWorld &a_world) {
  AbortRegressionRequest(a_world.request);
}

void Carry(const Regression::TravelAway &, RunWorld &a_world) {
  TravelAway(a_world);
}

void Carry(const Regression::TravelBack &, RunWorld &a_world) {
  TravelBack(a_world);
}

void Carry(const Regression::SetCamera &a_c, RunWorld &) {
  SetCamera(a_c.view);
}

void Carry(const Regression::CopyRecipe &a_c, RunWorld &a_world) {
  a_world.edit = WithManager([&](Manager &a_manager) {
    return a_manager.StartRegressionDuplicate(std::string{a_c.from},
                                              std::string{a_c.to});
  });
}

void Carry(const Regression::EditOpacity &a_c, RunWorld &a_world) {
  a_world.edit = WithManager([&](Manager &a_manager) {
    return a_manager.StartRegressionEdit(std::string{a_c.recipe}, a_c.value);
  });
}

void Carry(const Regression::WriteRecipe &a_c, RunWorld &a_world) {
  a_world.file = WithManager([&](Manager &a_manager) {
    return a_manager.StartRegressionSave(std::string{a_c.recipe});
  });
}

void Carry(const Regression::RemoveRecipe &a_c, RunWorld &a_world) {
  a_world.edit = WithManager([&](Manager &a_manager) {
    return a_manager.StartRegressionDelete(std::string{a_c.recipe});
  });
}

void Carry(const Regression::BeginWork &a_c, RunWorld &a_world) {
  StartWork(a_world, a_c.work, a_c.recipe);
}

void Carry(const Regression::ReloadDuring &a_c, RunWorld &a_world) {
  ReloadDuring(a_world, a_c);
}

void Carry(const Regression::Quit &, RunWorld &) { QuitGame(); }
}

Regression::Observation Observe(const RunWorld &a_world) {
  Regression::Observation seen;
  for (const Regression::Role role :
       {Regression::Role::kPlayer, Regression::Role::kWearer,
        Regression::Role::kControl}) {
    seen.actors[Regression::RoleIndex(role)] = ViewOf(ActorOf(a_world, role));
  }
  seen.itemsLoaded = {ArmorOf(Regression::Item::kFixture) != nullptr,
                      ArmorOf(Regression::Item::kPlainCuirass) != nullptr};
  seen.npcEffects = !GetSettings().playerOnly;
  const RE::PlayerCamera *camera = RE::PlayerCamera::GetSingleton();
  seen.firstPerson = camera && camera->IsInFirstPerson();
  seen.awayFromStart = AwayFromStart(a_world);
  seen.request = RequestStateOf(a_world.request);
  seen.loads = a_world.loads;
  if (const Manager *manager = Manager::GetSingleton()) {
    seen.activity = manager->RegressionActivity(a_world.edit, a_world.gesture,
                                                a_world.file);
    seen.scratch = manager->RegressionRecipe(Regression::kScratchRecipe);
  }
  return seen;
}

void Execute(const Regression::Command &a_command, RunWorld &a_world) {
  std::visit([&](const auto &a_kind) { Carry(a_kind, a_world); }, a_command);
}

void ReleaseWorld(RunWorld &a_world) {
  Delete(MarkerOf(a_world));
  a_world.marker = 0;
  for (const Regression::Role role :
       {Regression::Role::kWearer, Regression::Role::kControl}) {
    Despawn(a_world, role);
  }
}

void ForgetWorldAfterLoad(RunWorld &a_world) {
  a_world.spawned = {};
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
