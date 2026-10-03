// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/ActorObservation.h"

#include "Identity.h"
#include "engine/EngineForms.h"
#include "engine/WornKeys.h"
#include "render/Compositor.h"
#include "render/PBRMaterial.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace BetterEnchantmentEffects {
namespace {
std::uint64_t g_nextModelToken = 0;

bool InventoryWorn(RE::Actor &a_actor, RE::TESObjectARMO *a_armor) {
  auto inventory = a_actor.GetInventory(
      [&](RE::TESBoundObject &a_object) { return &a_object == a_armor; });
  for (auto &[object, pair] : inventory) {
    auto &[count, entry] = pair;
    if (count <= 0 || !entry || !entry->extraLists) {
      continue;
    }
    for (auto *xList : *entry->extraLists) {
      if (xList && (xList->HasType(RE::ExtraDataType::kWorn) ||
                    xList->HasType(RE::ExtraDataType::kWornLeft))) {
        return true;
      }
    }
  }
  return false;
}

std::vector<Actors::GeometryFacts> GeometriesOf(RE::NiAVObject *a_model) {
  std::vector<Actors::GeometryFacts> geometries;
  std::unordered_set<RE::BSLightingShaderProperty *> seen;
  bool failed = false;
  RE::BSVisit::TraverseScenegraphGeometries(
      a_model, [&](RE::BSGeometry *a_geometry) {
        const std::string_view name{
            a_geometry->name.c_str() ? a_geometry->name.c_str() : ""};
        if (name.ends_with(Identity::ShellNodeSuffix())) {
          return RE::BSVisit::BSVisitControl::kContinue;
        }
        RE::BSLightingShaderProperty *property = LightingPropertyOf(a_geometry);
        const std::optional<PbrMaterial> material = PbrMaterial::Bind(property);
        if (!property || !material || !seen.insert(property).second) {
          return RE::BSVisit::BSVisitControl::kContinue;
        }
        if (!material->TextureSlotsValid()) {
          failed = true;
          return RE::BSVisit::BSVisitControl::kStop;
        }
        if (property->emissiveColor) {
          geometries.push_back({std::string{name}, material->DiffusePath()});
        }
        return RE::BSVisit::BSVisitControl::kContinue;
      });
  return failed ? std::vector<Actors::GeometryFacts>{} : geometries;
}

ObservedModel &ModelFor(ModelTable &a_models, RE::NiAVObject *a_model,
                        std::uint32_t a_nowMs) {
  for (ObservedModel &known : a_models.models) {
    if (known.model.get() == a_model) {
      known.lastSeenMs = a_nowMs;
      return known;
    }
  }
  a_models.models.push_back({RE::NiPointer{a_model},
                             Actors::ModelToken{++g_nextModelToken},
                             std::nullopt, a_nowMs});
  return a_models.models.back();
}

struct WornArmor {
  RE::TESObjectARMO *armor = nullptr;
  RE::TESObjectARMA *addon = nullptr;
  RE::NiAVObject *model = nullptr;
};

std::vector<WornArmor> WornArmorOf(RE::Actor &a_actor, bool a_firstPerson) {
  std::vector<WornArmor> worn;
  const auto &biped = a_actor.GetBiped(a_firstPerson);
  if (!biped) {
    return worn;
  }
  for (const auto &object : biped->objects) {
    RE::TESObjectARMO *armor = PieceItemOf(object);
    if (!armor) {
      continue;
    }
    RE::NiAVObject *model = object.partClone.get();
    const auto known = std::ranges::find(worn, armor, &WornArmor::armor);
    if (known == worn.end()) {
      worn.push_back({armor, object.addon, model});
    } else if (!known->model && model) {
      known->model = model;
      known->addon = object.addon;
    }
  }
  return worn;
}

void ObservePerson(const ObserveRequest &a_request, bool a_firstPerson,
                   bool a_walkUnenchanted, ModelTable &a_models,
                   Actors::ActorFacts &a_facts) {
  RE::Actor &actor = a_request.actor;
  for (const WornArmor &worn : WornArmorOf(actor, a_firstPerson)) {
    if (!InventoryWorn(actor, worn.armor)) {
      continue;
    }
    WornPiece keys =
        WornKeysOf(worn.armor, WornEnchantment(&actor, worn.armor));
    if (!keys.Enchanted() && !a_walkUnenchanted) {
      continue;
    }
    Actors::PieceFacts piece;
    piece.ref = {FormKeyFor(*worn.armor),
                 worn.addon ? std::optional{FormKeyFor(*worn.addon)}
                            : std::nullopt,
                 a_firstPerson};
    piece.itemID = worn.armor->GetFormID();
    piece.keys = std::move(keys);
    if (worn.model) {
      ObservedModel &model = ModelFor(a_models, worn.model, a_request.nowMs);
      if (!model.geometries) {
        model.geometries = GeometriesOf(model.model.get());
      }
      piece.model = model.token;
      piece.geometries = *model.geometries;
    }
    a_facts.pieces.push_back(std::move(piece));
  }
}
}

RE::TESObjectARMO *PieceItemOf(const RE::BIPOBJECT &a_object) {
  return a_object.item ? a_object.item->As<RE::TESObjectARMO>() : nullptr;
}

RE::MagicItem *WornEnchantment(RE::Actor *a_actor, RE::TESObjectARMO *a_armor) {
  auto inventory = a_actor->GetInventory(
      [&](RE::TESBoundObject &a_object) { return &a_object == a_armor; });
  for (auto &[object, pair] : inventory) {
    auto &[count, entry] = pair;
    if (count <= 0 || !entry || !entry->extraLists) {
      continue;
    }
    for (auto *xList : *entry->extraLists) {
      if (!xList || !(xList->HasType(RE::ExtraDataType::kWorn) ||
                      xList->HasType(RE::ExtraDataType::kWornLeft))) {
        continue;
      }
      if (const auto *xEnch = xList->GetByType<RE::ExtraEnchantment>();
          xEnch && xEnch->enchantment) {
        return xEnch->enchantment;
      }
    }
  }
  return a_armor->formEnchanting;
}

Actors::ActorFacts ObserveActor(const ObserveRequest &a_request,
                                ModelTable &a_models) {
  RE::Actor &actor = a_request.actor;
  Actors::ActorFacts facts;
  facts.actor = Actors::ActorId{actor.GetFormID()};
  const bool isPlayer = actor.IsPlayerRef();
  facts.loaded = a_request.effectsEnabled && !actor.IsDeleted() &&
                 actor.Is3DLoaded() &&
                 (isPlayer || !a_request.settings.playerOnly);
  if (!facts.loaded) {
    return facts;
  }
  if (!isPlayer) {
    if (const auto *player = RE::PlayerCharacter::GetSingleton()) {
      facts.distance = player->GetPosition().GetDistance(actor.GetPosition());
    }
  }
  const bool walkUnenchanted = AnyUnenchantedKey(a_request.recipes);
  if (a_request.settings.thirdPerson) {
    ObservePerson(a_request, false, walkUnenchanted, a_models, facts);
  }
  if (a_request.settings.firstPerson && isPlayer) {
    ObservePerson(a_request, true, walkUnenchanted, a_models, facts);
  }
  return facts;
}

void KeepModels(ModelTable &a_models, const Actors::ActorPlan &a_installed,
                std::uint32_t a_nowMs) {
  std::erase_if(a_models.models, [&](const ObservedModel &a_model) {
    const bool installed = std::ranges::any_of(
        a_installed.geometries, [&](const Actors::PlannedGeometry &a_geometry) {
          return a_geometry.model == a_model.token;
        });
    return a_model.lastSeenMs != a_nowMs && !installed;
  });
}
}
