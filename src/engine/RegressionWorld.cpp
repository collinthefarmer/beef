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
inline constexpr RE::FormID kIdleNpc = 0xCCE01;
inline constexpr std::array<RE::FormID, 6> kVanillaCuirasses{
    0x12E49, 0x3619E, 0x896A3, 0x13957, 0x13961, 0x1396B};
inline constexpr std::array<RE::FormID, 2> kCrowdSides{0xBA0B8, 0xBA0B9};
inline constexpr std::string_view kDemoPlugin =
    "BetterEnchantmentEffectsDemo.esp";
inline constexpr RE::FormID kDemoKeyword = 0x800;
inline constexpr RE::FormID kDemoEnchantment = 0x802;
inline constexpr float kFighterAggression = 1.0f;
inline constexpr float kFighterConfidence = 4.0f;
inline constexpr RE::FormID kXMarker = 0x3B;
inline constexpr const char *kTravelCell = "Riverwood";
inline constexpr float kSpawnOffset = 120.0f;
inline constexpr float kWorkOpacity = 0.5f;
inline constexpr float kCrowdRadius = 300.0f;

template <class Form>
Form *PluginForm(RE::FormID a_local, std::string_view a_plugin) {
  RE::TESDataHandler *data = RE::TESDataHandler::GetSingleton();
  return data ? data->LookupForm<Form>(a_local, a_plugin) : nullptr;
}

template <class Form> Form *SkyrimForm(RE::FormID a_local) {
  return PluginForm<Form>(a_local, kSkyrim);
}

template <class Form> Form *FormWithID(RE::FormID a_id) {
  return a_id ? RE::TESForm::LookupByID<Form>(a_id) : nullptr;
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

void EquipArmor(RE::Actor *a_actor, RE::TESObjectARMO *a_armor, bool a_add) {
  RE::ActorEquipManager *equipment = RE::ActorEquipManager::GetSingleton();
  if (!a_actor || !a_armor || !equipment) {
    return;
  }
  if (a_add) {
    a_actor->AddObjectToContainer(a_armor, nullptr, 1, nullptr);
  }
  const bool preventRemoval = !a_actor->IsPlayerRef();
  equipment->EquipObject(a_actor, a_armor, nullptr, 1, nullptr, true,
                         preventRemoval, false);
}

void UnequipArmor(RE::Actor *a_actor, RE::TESObjectARMO *a_armor) {
  RE::ActorEquipManager *equipment = RE::ActorEquipManager::GetSingleton();
  if (a_actor && a_armor && equipment) {
    equipment->UnequipObject(a_actor, a_armor, nullptr, 1, nullptr, true, false,
                             false);
  }
}

void Equip(RE::Actor *a_actor, Regression::Item a_item, bool a_add) {
  EquipArmor(a_actor, ArmorOf(a_item), a_add);
}

void Unequip(RE::Actor *a_actor, Regression::Item a_item) {
  UnequipArmor(a_actor, ArmorOf(a_item));
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

ArmorTag TagVanillaCuirass(RE::TESObjectARMO &a_armor,
                           RE::BGSKeyword &a_keyword,
                           RE::EnchantmentItem &a_enchantment) {
  ArmorTag tag;
  tag.armor = a_armor.GetFormID();
  tag.enchantmentBefore =
      a_armor.formEnchanting ? a_armor.formEnchanting->GetFormID() : 0;
  tag.addedKeyword =
      !a_armor.HasKeyword(&a_keyword) && a_armor.AddKeyword(&a_keyword);
  a_armor.formEnchanting = &a_enchantment;
  return tag;
}

void TagVanillaCuirasses(RunWorld &a_world) {
  if (!a_world.tags.empty()) {
    return;
  }
  auto *keyword = PluginForm<RE::BGSKeyword>(kDemoKeyword, kDemoPlugin);
  auto *enchantment =
      PluginForm<RE::EnchantmentItem>(kDemoEnchantment, kDemoPlugin);
  if (!keyword || !enchantment) {
    logger::warn("regression: the demo keyword or enchantment is missing");
    return;
  }
  for (const RE::FormID id : kVanillaCuirasses) {
    if (RE::TESObjectARMO *armor = SkyrimForm<RE::TESObjectARMO>(id)) {
      a_world.tags.push_back(TagVanillaCuirass(*armor, *keyword, *enchantment));
    } else {
      logger::warn("regression: vanilla cuirass {:X} is missing", id);
    }
  }
}

void UntagVanillaCuirasses(RunWorld &a_world) {
  auto *keyword = PluginForm<RE::BGSKeyword>(kDemoKeyword, kDemoPlugin);
  for (const ArmorTag &tag : a_world.tags) {
    RE::TESObjectARMO *armor = FormWithID<RE::TESObjectARMO>(tag.armor);
    if (!armor) {
      continue;
    }
    if (tag.addedKeyword && keyword) {
      armor->RemoveKeyword(keyword);
    }
    armor->formEnchanting =
        FormWithID<RE::EnchantmentItem>(tag.enchantmentBefore);
  }
  a_world.tags.clear();
}

RE::FormID CrowdArmorAt(const RunWorld &a_world, Regression::Dress a_dress,
                        std::uint32_t a_index) {
  if (a_dress == Regression::Dress::kDemoCuirass) {
    const RE::TESObjectARMO *fixture = RegressionFixture();
    return fixture ? fixture->GetFormID() : 0;
  }
  return a_world.tags.empty()
             ? 0
             : a_world.tags[a_index % a_world.tags.size()].armor;
}

RE::TESNPC *CrowdBase(Regression::Body a_body) {
  return SkyrimForm<RE::TESNPC>(
      a_body == Regression::Body::kIdleNpc ? kIdleNpc : kMannequin);
}

void MakeEssential(RE::Actor &a_actor) {
  a_actor.GetActorRuntimeData().boolFlags.set(
      RE::Actor::BOOL_FLAGS::kEssential);
}

void SpawnCrowdActors(RunWorld &a_world,
                      const Regression::SpawnCrowdActors &a_spawn) {
  RE::PlayerCharacter *player = RE::PlayerCharacter::GetSingleton();
  RE::TESNPC *base = CrowdBase(a_spawn.body);
  if (!player || !base) {
    return;
  }
  if (a_spawn.dress == Regression::Dress::kVanillaCuirasses) {
    TagVanillaCuirasses(a_world);
  }
  const RE::NiPoint3 centre = player->GetPosition();
  const std::uint32_t count = std::min(a_spawn.count, Regression::kMaxCrowd);
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
    if (a_spawn.body == Regression::Body::kIdleNpc) {
      MakeEssential(*actor);
    }
    a_world.crowd.push_back(
        {actor->GetFormID(), CrowdArmorAt(a_world, a_spawn.dress, index)});
  }
}

void DespawnCrowdActors(RunWorld &a_world) {
  for (const CrowdMember &member : a_world.crowd) {
    DeleteReference(FormWithID<RE::TESObjectREFR>(member.actor));
  }
  a_world.crowd.clear();
  UntagVanillaCuirasses(a_world);
}

void StripBodyArmor(RE::Actor &a_actor, const RE::TESObjectARMO &a_keep) {
  const RE::TESNPC *base = a_actor.GetActorBase();
  if (base && base->defaultOutfit) {
    a_actor.RemoveOutfitItems(base->defaultOutfit);
  }
  const auto counts =
      a_actor.GetInventoryCounts([&a_keep](RE::TESBoundObject &a_object) {
        const auto *armor = a_object.As<RE::TESObjectARMO>();
        return armor && armor != &a_keep &&
               armor->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kBody);
      });
  for (const auto &[object, count] : counts) {
    if (object && count > 0) {
      a_actor.RemoveItem(object, count, RE::ITEM_REMOVE_REASON::kRemove,
                         nullptr, nullptr);
    }
  }
}

void DressCrowdMember(const CrowdMember &a_member) {
  RE::Actor *actor = FormWithID<RE::Actor>(a_member.actor);
  RE::TESObjectARMO *armor = FormWithID<RE::TESObjectARMO>(a_member.armor);
  if (!actor || !armor) {
    return;
  }
  StripBodyArmor(*actor, *armor);
  EquipArmor(actor, armor, CountOf(*actor, *armor) == 0);
}

void SetCrowdHostile(const RunWorld &a_world) {
  std::array<RE::TESFaction *, kCrowdSides.size()> sides{};
  for (std::size_t side = 0; side < sides.size(); ++side) {
    sides[side] = SkyrimForm<RE::TESFaction>(kCrowdSides[side]);
  }
  if (!sides[0] || !sides[1]) {
    logger::warn("regression: the crowd's side factions are missing");
    return;
  }
  for (std::size_t index = 0; index < a_world.crowd.size(); ++index) {
    RE::Actor *actor = FormWithID<RE::Actor>(a_world.crowd[index].actor);
    if (!actor) {
      continue;
    }
    actor->AddToFaction(sides[index % sides.size()], 0);
    RE::ActorValueOwner *values = actor->AsActorValueOwner();
    if (values) {
      values->SetActorValue(RE::ActorValue::kAggression, kFighterAggression);
      values->SetActorValue(RE::ActorValue::kConfidence, kFighterConfidence);
    }
    actor->EvaluatePackage(true, false);
  }
}

Regression::CrowdFacts CrowdOf(const RunWorld &a_world,
                               std::span<const RegressionActorFacts> a_facts) {
  Regression::CrowdFacts view;
  for (std::size_t i = 0; i < a_world.crowd.size() && i < a_facts.size(); ++i) {
    const RE::Actor *actor = FormWithID<RE::Actor>(a_world.crowd[i].actor);
    if (!actor || !Present(*actor)) {
      continue;
    }
    ++view.present;
    if (actor->IsInCombat()) {
      ++view.fighting;
    }
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
  SpawnCrowdActors(a_world, a_c);
}

void Perform(const Regression::SetCrowdHostile &, RunWorld &a_world) {
  SetCrowdHostile(a_world);
}

void Perform(const Regression::DespawnCrowdActors &, RunWorld &a_world) {
  DespawnCrowdActors(a_world);
}

void Perform(const Regression::UnequipCrowdArmor &, RunWorld &a_world) {
  for (const CrowdMember &member : a_world.crowd) {
    UnequipArmor(FormWithID<RE::Actor>(member.actor),
                 FormWithID<RE::TESObjectARMO>(member.armor));
  }
}

void Perform(const Regression::EquipCrowdArmor &, RunWorld &a_world) {
  for (const CrowdMember &member : a_world.crowd) {
    DressCrowdMember(member);
  }
}
}

Regression::Observation Observe(const RunWorld &a_world) {
  Regression::Observation seen;
  constexpr std::array kRoles{Regression::Role::kPlayer,
                              Regression::Role::kWearer,
                              Regression::Role::kControl};
  std::array<RE::Actor *, Regression::kRoleCount> actors{};
  const RE::TESObjectARMO *fixture = RegressionFixture();
  const RE::FormID fixtureID = fixture ? fixture->GetFormID() : 0;
  std::vector<RegressionWearer> wearers;
  for (const Regression::Role role : kRoles) {
    RE::Actor *actor = ActorOf(a_world, role);
    actors[Regression::IndexOf(role)] = actor;
    wearers.push_back({actor ? actor->GetFormID() : 0, fixtureID});
  }
  for (const CrowdMember &member : a_world.crowd) {
    wearers.push_back({member.actor, member.armor});
  }
  const Manager *manager = Manager::GetSingleton();
  std::vector<RegressionActorFacts> facts =
      manager ? manager->RegressionActors(wearers)
              : std::vector<RegressionActorFacts>(wearers.size());
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
  UntagVanillaCuirasses(a_world);
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
