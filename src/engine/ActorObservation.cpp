// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/ActorObservation.h"

#include "Identity.h"
#include "engine/EngineForms.h"
#include "engine/WornKeys.h"
#include "render/PBRMaterial.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace BetterEnchantmentEffects {
namespace {
std::uint64_t g_nextModelToken = 0;

struct WornItem {
  const RE::TESObjectARMO &armor;
  RE::MagicItem *enchantment = nullptr;
};

bool WornList(const RE::ExtraDataList &a_list) {
  return a_list.HasType(RE::ExtraDataType::kWorn) ||
         a_list.HasType(RE::ExtraDataType::kWornLeft);
}

std::vector<WornItem> WornItemsOf(RE::Actor &a_actor) {
  std::vector<WornItem> worn;
  auto inventory = a_actor.GetInventory([](RE::TESBoundObject &a_object) {
    return a_object.Is(RE::FormType::Armor);
  });
  for (auto &[object, pair] : inventory) {
    auto &[count, entry] = pair;
    const auto *armor = object ? object->As<RE::TESObjectARMO>() : nullptr;
    if (!armor || count <= 0 || !entry || !entry->extraLists) {
      continue;
    }
    bool isWorn = false;
    RE::MagicItem *enchantment = nullptr;
    for (const RE::ExtraDataList *list : *entry->extraLists) {
      if (!list || !WornList(*list)) {
        continue;
      }
      isWorn = true;
      const auto *extra = list->GetByType<RE::ExtraEnchantment>();
      if (extra && extra->enchantment) {
        enchantment = extra->enchantment;
        break;
      }
    }
    if (isWorn) {
      worn.push_back(
          {*armor, enchantment ? enchantment : armor->formEnchanting});
    }
  }
  return worn;
}

const WornItem *WornItemFor(std::span<const WornItem> a_worn,
                            const RE::TESObjectARMO &a_armor) {
  const auto found = std::ranges::find_if(a_worn, [&](const WornItem &a_item) {
    return &a_item.armor == &a_armor;
  });
  return found == a_worn.end() ? nullptr : &*found;
}

struct ModelRead {
  std::vector<ObservedGeometry> geometries;
  bool complete = true;
};

ModelRead GeometriesOf(RE::NiAVObject *a_model, MaterialLayout &a_layout) {
  ModelRead read;
  std::unordered_set<RE::BSLightingShaderProperty *> seen;
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
        const bool slotsValid = material->TextureSlotsValid();
        if (a_layout == MaterialLayout::kUnchecked) {
          a_layout =
              slotsValid ? MaterialLayout::kVerified : MaterialLayout::kFailed;
        }
        if (a_layout == MaterialLayout::kFailed) {
          read.geometries.clear();
          read.complete = false;
          return RE::BSVisit::BSVisitControl::kStop;
        }
        if (!slotsValid) {
          read.complete = false;
          return RE::BSVisit::BSVisitControl::kContinue;
        }
        if (property->emissiveColor) {
          read.geometries.push_back(
              {{std::string{name}, material->DiffusePath()},
               RE::NiPointer{a_geometry}});
        }
        return RE::BSVisit::BSVisitControl::kContinue;
      });
  return read;
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
                             {},
                             false,
                             a_nowMs});
  return a_models.models.back();
}

RE::TESObjectARMO *PieceItemOf(const RE::BIPOBJECT &a_object) {
  return a_object.item ? a_object.item->As<RE::TESObjectARMO>() : nullptr;
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
    const auto known = std::ranges::find_if(worn, [&](const WornArmor &a_worn) {
      return a_worn.armor == armor && a_worn.addon == object.addon;
    });
    if (known == worn.end()) {
      worn.push_back({armor, object.addon, model});
    } else if (!known->model) {
      known->model = model;
    }
  }
  return worn;
}

struct PersonObservation {
  const ObserveRequest &request;
  std::span<const WornItem> worn;
  bool firstPerson = false;
  bool walkUnenchanted = false;
};

void ObservePerson(const PersonObservation &a_person, ModelTable &a_models,
                   MaterialLayout &a_layout, Actors::ActorFacts &a_facts) {
  for (const WornArmor &biped :
       WornArmorOf(a_person.request.actor, a_person.firstPerson)) {
    const WornItem *item = WornItemFor(a_person.worn, *biped.armor);
    if (!item) {
      continue;
    }
    WornPiece keys = WornKeysOf(biped.armor, item->enchantment);
    if (!keys.Enchanted() && !a_person.walkUnenchanted) {
      continue;
    }
    Actors::PieceFacts piece;
    piece.ref = {FormKeyFor(*biped.armor),
                 biped.addon ? std::optional{FormKeyFor(*biped.addon)}
                             : std::nullopt,
                 a_person.firstPerson};
    piece.itemID = biped.armor->GetFormID();
    piece.enchantmentID =
        item->enchantment ? item->enchantment->GetFormID() : 0;
    piece.keys = std::move(keys);
    if (biped.model) {
      ObservedModel &model =
          ModelFor(a_models, biped.model, a_person.request.nowMs);
      if (!model.settled) {
        ModelRead read = GeometriesOf(model.model.get(), a_layout);
        model.geometries = std::move(read.geometries);
        model.settled = read.complete;
      }
      piece.model = model.token;
      for (const ObservedGeometry &geometry : model.geometries) {
        piece.geometries.push_back(geometry.facts);
      }
    }
    a_facts.pieces.push_back(std::move(piece));
  }
}
}

Actors::ActorFacts ObserveActor(const ObserveRequest &a_request,
                                ModelTable &a_models,
                                MaterialLayout &a_layout) {
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
  const std::vector<WornItem> worn = WornItemsOf(actor);
  const bool walkUnenchanted = AnyUnenchantedKey(a_request.recipes);
  if (a_request.settings.thirdPerson) {
    ObservePerson({a_request, worn, false, walkUnenchanted}, a_models, a_layout,
                  facts);
  }
  if (a_request.settings.firstPerson && isPlayer) {
    ObservePerson({a_request, worn, true, walkUnenchanted}, a_models, a_layout,
                  facts);
  }
  return facts;
}

const ObservedModel *ModelOf(const ModelTable &a_models,
                             Actors::ModelToken a_token) noexcept {
  for (const ObservedModel &model : a_models.models) {
    if (model.token == a_token) {
      return &model;
    }
  }
  return nullptr;
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
