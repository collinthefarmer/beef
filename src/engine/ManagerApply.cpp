#include "engine/Manager.h"

#include "Identity.h"
#include "SettingsFile.h"
#include "engine/EngineForms.h"
#include "engine/Events.h"
#include "engine/RecipeStore.h"
#include "mesh/TextureSize.h"
#include "render/Compositor.h"
#include "render/PBRMaterial.h"
#include "render/RuntimeTextures.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
std::uint32_t NowMS() { return RE::GetDurationOfApplicationRunTime(); }

RE::BSLightingShaderProperty *LightingPropertyOf(RE::BSGeometry *a_geometry) {
  RE::NiProperty *property = a_geometry->GetGeometryRuntimeData()
                                 .properties[RE::BSGeometry::States::kEffect]
                                 .get();
  return property ? netimmerse_cast<RE::BSLightingShaderProperty *>(property)
                  : nullptr;
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

std::string TexturePath(const RE::NiPointer<RE::NiSourceTexture> &a_texture) {
  return a_texture && a_texture->name.c_str() ? a_texture->name.c_str() : "";
}

WornPiece WornKeysOf(RE::TESObjectARMO *a_armor, RE::MagicItem *a_magic) {
  WornPiece keys;
  if (a_armor) {
    keys.armor = FormKeyFor(*a_armor);
  }
  if (a_magic) {
    keys.enchantment = FormKeyFor(*a_magic);
    if (const RE::Effect *costliest = a_magic->GetCostliestEffectItem();
        costliest && costliest->baseEffect) {
      keys.magicEffect = FormKeyFor(*costliest->baseEffect);
    }
    if (RE::TESEffectShader *shader = ShaderFor(a_magic)) {
      keys.effectShader = FormKeyFor(*shader);
    }
  }
  if (a_armor) {
    for (std::uint32_t i = 0; i < a_armor->GetNumKeywords(); ++i) {
      if (const auto keyword = a_armor->GetKeywordAt(i); keyword && *keyword) {
        keys.keywords.push_back(FormKeyFor(**keyword));
      }
    }
  }
  return keys;
}

LiveGeometry *GeometryAtFlat(LiveActor &a_state, std::size_t a_flat) {
  std::size_t i = 0;
  for (LivePiece &piece : a_state.pieces) {
    for (LiveGeometry &geometry : piece.geometries) {
      if (i == a_flat) {
        return &geometry;
      }
      ++i;
    }
  }
  return nullptr;
}

LivePiece *OwningPieceFlat(LiveActor &a_state, std::size_t a_flat) {
  std::size_t i = 0;
  for (LivePiece &piece : a_state.pieces) {
    for ([[maybe_unused]] LiveGeometry &geometry : piece.geometries) {
      if (i == a_flat) {
        return &piece;
      }
      ++i;
    }
  }
  return nullptr;
}

RE::MagicItem *EnchantmentForInstance(LiveActor &a_state,
                                      std::size_t a_instance) {
  for (const Placement &placement : a_state.structure.placements) {
    if (static_cast<std::size_t>(placement.instance) != a_instance) {
      continue;
    }
    if (LivePiece *piece = OwningPieceFlat(
            a_state, static_cast<std::size_t>(placement.piece))) {
      return piece->enchantment;
    }
  }
  return nullptr;
}

SlotTarget *TargetFor(LiveGeometry &a_bound, Surface a_surface) {
  if (a_surface == Surface::kShell) {
    return a_bound.shell.get();
  }
  return a_bound.material.get();
}

PlacedOutput *OutputAt(LivePlacement &a_placement, std::size_t a_index) {
  for (PlacedOutput &output : a_placement.outputs) {
    if (output.index == a_index) {
      return &output;
    }
  }
  return nullptr;
}

std::pair<TextureSize, TextureSize>
RuntimeSizes(const Settings &a_settings, const MaterialInputs &a_material) {
  std::uint32_t native = 0;
  const auto consider =
      [&native](const RE::NiPointer<RE::NiSourceTexture> &a_texture) {
        if (const std::optional<TextureLab::Extent> extent =
                TextureLab::ExtentOf(a_texture.get())) {
          native = std::max({native, extent->width, extent->height});
        }
      };
  consider(a_material.rmaos);
  consider(a_material.diffuse);
  consider(a_material.normal);
  consider(a_material.displacement);
  const TextureSize maxSize{native == 0 ? TextureSize::kMax : native};
  std::uint32_t requested = maxSize.Pixels();
  switch (a_settings.textureScale) {
  case TextureScale::kQuarter:
    requested /= 4;
    break;
  case TextureScale::kHalf:
    requested /= 2;
    break;
  case TextureScale::kFull:
    break;
  }
  return {TextureSize(requested), maxSize};
}

EventRecord EquipEvent(const std::vector<LivePiece> &a_pieces) {
  EventRecord record;
  record.id = "equip";
  for (const LivePiece &piece : a_pieces) {
    for (const LiveGeometry &bound : piece.geometries) {
      if (bound.geometry) {
        const RE::NiPoint3 &c = bound.geometry->worldBound.center;
        record.payload.position = Vec3{c.x, c.y, c.z};
        return record;
      }
    }
  }
  return record;
}
}

void Manager::ReapplyAll() {
  PostTask([this] {
    std::vector<RE::FormID> ids;
    for (const auto &[id, state] : applied_) {
      ids.push_back(id);
    }
    for (const RE::FormID id : ids) {
      QueueRefresh(id);
    }
    if (ids.empty()) {
      QueueLoadedActorRefreshes();
    }
  });
}

void Manager::RetireAll() {
  PostTask([this] {
    std::vector<RE::FormID> ids;
    for (const auto &[id, state] : applied_) {
      ids.push_back(id);
    }
    for (const RE::FormID id : ids) {
      QueueRetire(id);
    }
  });
}

void Manager::RunRefresh(RE::FormID a_actorID, std::uint64_t a_generation) {
  if (a_generation != generation_.load()) {
    return;
  }
  bool rerun = false;
  {
    std::scoped_lock lock{queueLock_};
    pending_.erase(a_actorID);
    rerun = rerun_.erase(a_actorID) > 0;
  }
  RE::Actor *actor = RE::TESForm::LookupByID<RE::Actor>(a_actorID);
  if (!actor) {
    Retire(a_actorID);
  } else {
    Refresh(actor);
  }
  if (rerun) {
    QueueRefresh(a_actorID);
  }
}

void Manager::Refresh(RE::Actor *a_actor) {
  const Settings &settings = GetSettings();
  const RE::FormID actorID = a_actor->GetFormID();
  Retire(actorID);
  if (!settings.enableShaders || !emissivePathEnabled_ ||
      a_actor->IsDeleted()) {
    return;
  }
  const bool isPlayer = a_actor->IsPlayerRef();
  if (settings.playerOnly && !isPlayer) {
    return;
  }
  if (!a_actor->Is3DLoaded()) {
    if (settings.verboseLogging) {
      logger::info("actor {:08X} ({}): 3D not loaded, skipped", actorID,
                   a_actor->GetName());
    }
    return;
  }
  LiveActor state;
  if (settings.thirdPerson) {
    for (LivePiece &piece : CollectPieces(a_actor, false)) {
      state.pieces.push_back(std::move(piece));
    }
  }
  if (settings.firstPerson && isPlayer) {
    for (LivePiece &piece : CollectPieces(a_actor, true)) {
      state.pieces.push_back(std::move(piece));
    }
  }
  MatchRecipes(a_actor, state);
  TextureLab::GetSingleton()->InvalidatePreviews();
  if (state.structure.placements.empty()) {
    return;
  }
  PlaceInstances(a_actor, state);
  PlaceLightsOf(a_actor, state);
  if (settings.verboseLogging) {
    logger::info("actor {:08X} ({}): {} piece(s), {} recipe(s) applied",
                 actorID, a_actor->GetName(), state.pieces.size(),
                 state.instances.size());
  }
  applied_[actorID] = std::move(state);
  WatchAnimationEvents(a_actor);

  bool equipped = false;
  {
    std::scoped_lock lock{queueLock_};
    equipped = equipped_.erase(actorID) > 0;
  }
  if (equipped) {
    Fire(actorID, EquipEvent(applied_[actorID].pieces));
  }
}

std::vector<LivePiece> Manager::CollectPieces(RE::Actor *a_actor,
                                              bool a_firstPerson) {
  const Settings &settings = GetSettings();
  std::vector<LivePiece> out;
  const auto &biped = a_actor->GetBiped(a_firstPerson);
  if (!biped) {
    return out;
  }
  const std::span<const Recipe> loaded = LoadedRecipes();
  const bool walkUnenchanted = AnyUnenchantedKey(loaded);
  RE::NiAVObject *root = a_actor->Get3D(a_firstPerson);
  std::unordered_set<RE::NiAVObject *> seenClones;
  for (const auto &object : biped->objects) {
    RE::TESObjectARMO *armor =
        object.item ? object.item->As<RE::TESObjectARMO>() : nullptr;
    RE::NiAVObject *clone = object.partClone.get();
    if (!armor || !clone || !seenClones.insert(clone).second) {
      continue;
    }
    RE::MagicItem *enchantment = WornEnchantment(a_actor, armor);
    WornPiece keys = WornKeysOf(armor, enchantment);
    if (!keys.Enchanted() && !walkUnenchanted) {
      continue;
    }
    LivePiece piece;
    piece.armor = armor->GetFormID();
    piece.armorName = armor->GetName() ? armor->GetName() : "";
    piece.enchantment = enchantment;
    std::unordered_set<RE::BSLightingShaderProperty *> seen;
    bool anyPBR = false;
    bool layoutFailed = false;
    RE::BSVisit::TraverseScenegraphGeometries(
        clone, [&](RE::BSGeometry *a_geometry) {
          const std::string_view name{
              a_geometry->name.c_str() ? a_geometry->name.c_str() : ""};
          if (name.ends_with(Identity::ShellNodeSuffix())) {
            return RE::BSVisit::BSVisitControl::kContinue;
          }
          RE::BSLightingShaderProperty *property =
              LightingPropertyOf(a_geometry);
          if (!property || !IsPBRProperty(property) ||
              !seen.insert(property).second) {
            return RE::BSVisit::BSVisitControl::kContinue;
          }
          anyPBR = true;
          if (!LayoutSanityCheck(property)) {
            layoutFailed = true;
            return RE::BSVisit::BSVisitControl::kStop;
          }
          if (!property->emissiveColor) {
            if (settings.verboseLogging) {
              logger::info(
                  "skip geometry {}: property has no emissive colour storage",
                  name);
            }
            return RE::BSVisit::BSVisitControl::kContinue;
          }
          auto *layout = static_cast<PBRMaterialLayout *>(property->material);
          LiveGeometry bound;
          bound.geometry = RE::NiPointer{a_geometry};
          bound.property = RE::NiPointer{property};
          bound.name = std::string{name};
          bound.inputs.material = MaterialInputs::From(*layout);
          bound.inputs.geometry = RE::NiPointer{a_geometry};
          bound.inputs.root = RE::NiPointer{root};
          piece.geometries.push_back(std::move(bound));
          return RE::BSVisit::BSVisitControl::kContinue;
        });
    if (layoutFailed) {
      continue;
    }
    if (!anyPBR || piece.geometries.empty()) {
      if (!anyPBR && settings.verboseLogging &&
          loggedNonPBRArmor_.insert(piece.armor).second) {
        logger::info("armor {:08X} ({}) has no PBR geometry; left alone",
                     piece.armor, piece.armorName);
      }
      continue;
    }
    out.push_back(std::move(piece));
  }
  return out;
}

void Manager::MatchRecipes(RE::Actor *a_actor, LiveActor &a_state) {
  const std::span<const Recipe> loaded = LoadedRecipes();
  RE::NiAVObject *firstPersonRoot = a_actor->Get3D(true);
  std::vector<Piece> pieces;
  for (LivePiece &live : a_state.pieces) {
    RE::TESObjectARMO *armor =
        RE::TESForm::LookupByID<RE::TESObjectARMO>(live.armor);
    WornPiece keys = WornKeysOf(armor, live.enchantment);
    for (const LiveGeometry &geometry : live.geometries) {
      keys.diffusePaths.push_back(
          TexturePath(geometry.inputs.material.diffuse));
    }
    const bool firstPerson =
        !live.geometries.empty() && live.geometries.front().inputs.root &&
        live.geometries.front().inputs.root.get() == firstPersonRoot;
    for (const LiveGeometry &geometry : live.geometries) {
      Piece piece;
      piece.identity =
          GeometryIdentity{std::nullopt, geometry.name,
                           TexturePath(geometry.inputs.material.diffuse)};
      piece.keys = keys;
      piece.firstPerson = firstPerson;
      piece.lost = geometry.lost;
      pieces.push_back(std::move(piece));
    }
  }
  a_state.structure = MatchActor(pieces, loaded);
  a_state.instances.clear();
  for (std::size_t i = 0; i < a_state.structure.instances.size(); ++i) {
    const Instance &instance = a_state.structure.instances[i];
    RE::MagicItem *enchantment = EnchantmentForInstance(a_state, i);
    (void)InstanceFor(a_actor, a_state, instance.recipe, enchantment);
  }
}

std::optional<std::size_t> Manager::InstanceFor(RE::Actor *a_actor,
                                                LiveActor &a_state,
                                                RecipeId a_recipe,
                                                RE::MagicItem *a_enchantment) {
  const std::span<const Recipe> loaded = LoadedRecipes();
  const std::size_t recipeIndex = static_cast<std::size_t>(a_recipe);
  if (recipeIndex >= loaded.size()) {
    return std::nullopt;
  }
  const Recipe *recipe = &loaded[recipeIndex];
  const RE::FormID enchantment = a_enchantment ? a_enchantment->GetFormID() : 0;
  for (std::size_t i = 0; i < a_state.instances.size(); ++i) {
    const LiveInstance &existing = a_state.instances[i];
    if (existing.recipe && existing.recipe->id == recipe->id &&
        existing.enchantment == enchantment) {
      return i;
    }
  }
  LiveInstance instance;
  instance.recipe = recipe;
  instance.enchantment = enchantment;
  instance.graph = GraphFor(*recipe);
  if (instance.graph) {
    instance.signals = std::make_unique<SignalState>(*instance.graph);
  }
  instance.environment =
      std::make_unique<ActorEnvironment>(a_actor, a_enchantment);
  instance.startMS = NowMS();
  if (const auto carried =
          carriedTimes_.find({a_actor->GetFormID(), recipe->id});
      carried != carriedTimes_.end()) {
    const float speed = GetSettings().animationSpeed * recipe->clock.speed;
    if (instance.startMS - carried->second.retiredMS <= kCarryWindowMS &&
        speed > 0.0f) {
      instance.startMS -=
          static_cast<std::uint32_t>(carried->second.seconds / speed * 1000.0f);
      instance.lastTime = carried->second.seconds;
    }
    carriedTimes_.erase(carried);
  }
  a_state.instances.push_back(std::move(instance));
  return a_state.instances.size() - 1;
}

void Manager::PlaceInstances(RE::Actor *a_actor, LiveActor &a_state) {
  for (LiveInstance &instance : a_state.instances) {
    if (instance.signals && instance.environment) {
      instance.signals->Tick(*instance.environment, {0.0f, 0.0f});
    }
  }
  const std::span<const Recipe> loaded = LoadedRecipes();
  a_state.placements.clear();
  a_state.placements.resize(a_state.structure.placements.size());
  std::size_t flat = 0;
  for (std::size_t p = 0; p < a_state.pieces.size(); ++p) {
    for (std::size_t g = 0; g < a_state.pieces[p].geometries.size(); ++g) {
      const GeometryPlacement placement =
          PlaceGeometry(a_state.structure, loaded, PieceId{flat});
      LiveGeometry &bound = a_state.pieces[p].geometries[g];
      bound.placements = placement.sources;
      bound.plan = placement.plan;
      bound.stackPlan = PlanStacks(placement.placed, placement.plan);
      bound.binding = PlanBinding(placement.placed, placement.plan, {}, {});
      for (const PlacementId source : placement.sources) {
        const std::size_t k = static_cast<std::size_t>(source);
        if (k >= a_state.placements.size() ||
            k >= a_state.structure.placements.size()) {
          continue;
        }
        LivePlacement &live = a_state.placements[k];
        live.geometry = flat;
        live.outputs.clear();
        for (const OutputPlacement &output :
             a_state.structure.placements[k].outputs) {
          live.outputs.push_back(
              PlacedOutput{static_cast<std::size_t>(output.output), nullptr,
                           output.problem});
        }
      }
      PlaceOnGeometry(a_actor, a_state, PieceId{p}, g);
      ++flat;
    }
  }
}

void Manager::PlaceOnGeometry(RE::Actor *a_actor, LiveActor &a_state,
                              PieceId a_piece, std::size_t a_geometry) {
  const Settings &settings = GetSettings();
  const std::size_t pieceIndex = static_cast<std::size_t>(a_piece);
  if (pieceIndex >= a_state.pieces.size() ||
      a_geometry >= a_state.pieces[pieceIndex].geometries.size()) {
    return;
  }
  LiveGeometry &bound = a_state.pieces[pieceIndex].geometries[a_geometry];

  if (bound.binding.material) {
    bound.material = MaterialBinding::Install(
        bound.geometry.get(), bound.property.get(), settings.uniqueMaterial);
  }
  if (bound.binding.shell) {
    std::optional<std::size_t> ownerInstance;
    if (bound.binding.shellOwner) {
      const std::size_t owner =
          static_cast<std::size_t>(*bound.binding.shellOwner);
      if (owner < bound.placements.size()) {
        const std::size_t placementIndex =
            static_cast<std::size_t>(bound.placements[owner]);
        if (placementIndex < a_state.structure.placements.size()) {
          ownerInstance = static_cast<std::size_t>(
              a_state.structure.placements[placementIndex].instance);
        }
      }
    }
    if (ownerInstance && *ownerInstance < a_state.instances.size() &&
        a_state.instances[*ownerInstance].recipe) {
      bound.shellOwner = ownerInstance;
      bound.shell =
          ShellBinding::Create(bound.geometry.get(), bound.property.get(),
                               a_state.instances[*ownerInstance].recipe->shell);
    }
  }

  for (const SlotPlan &slot : bound.plan.slots) {
    for (const SlotContribution &c : slot.replaced) {
      const std::size_t placed = static_cast<std::size_t>(c.placed);
      if (placed >= bound.placements.size()) {
        continue;
      }
      const std::size_t placementIndex =
          static_cast<std::size_t>(bound.placements[placed]);
      if (placementIndex >= a_state.placements.size()) {
        continue;
      }
      PlacedOutput *output =
          OutputAt(a_state.placements[placementIndex], c.output);
      if (!output) {
        continue;
      }
      std::string replacerId;
      if (const std::optional<std::size_t> replacer = ReplacerOf(slot, c);
          replacer && *replacer < bound.placements.size()) {
        const std::size_t replacerPlacement =
            static_cast<std::size_t>(bound.placements[*replacer]);
        if (replacerPlacement < a_state.structure.placements.size()) {
          const std::size_t instanceIndex = static_cast<std::size_t>(
              a_state.structure.placements[replacerPlacement].instance);
          if (instanceIndex < a_state.instances.size() &&
              a_state.instances[instanceIndex].recipe) {
            replacerId = a_state.instances[instanceIndex].recipe->id;
          }
        }
      }
      output->problem = std::format("replaced by recipe {}", replacerId);
    }
  }

  const auto [size, maxSize] = RuntimeSizes(settings, bound.inputs.material);
  for (const SlotPlan &slot : bound.plan.slots) {
    for (const SlotContribution &c : slot.chain) {
      const std::size_t placed = static_cast<std::size_t>(c.placed);
      if (placed >= bound.placements.size()) {
        continue;
      }
      const std::size_t placementIndex =
          static_cast<std::size_t>(bound.placements[placed]);
      if (placementIndex >= a_state.structure.placements.size() ||
          placementIndex >= a_state.placements.size()) {
        continue;
      }
      const std::size_t instanceIndex = static_cast<std::size_t>(
          a_state.structure.placements[placementIndex].instance);
      if (instanceIndex >= a_state.instances.size()) {
        continue;
      }
      const LiveInstance &instance = a_state.instances[instanceIndex];
      if (!instance.recipe) {
        continue;
      }
      const SurfaceOutput *material =
          c.output < instance.recipe->outputs.size()
              ? Get<SurfaceOutput>(instance.recipe->outputs[c.output])
              : nullptr;
      if (!material) {
        continue;
      }
      PlacedOutput *output =
          OutputAt(a_state.placements[placementIndex], c.output);
      if (!output) {
        continue;
      }
      if (slot.surface == Surface::kShell && !bound.shell) {
        output->problem = "shell could not be created";
      } else if (slot.surface == Surface::kMaterial && !bound.material) {
        output->problem = "material binding failed";
      }
      if (output->problem.empty()) {
        SlotTarget *target = TargetFor(bound, slot.surface);
        output->problem =
            target ? target->Problem(slot.slot) : "the surface is not bound";
      }
      if (output->problem.empty()) {
        output->stack = Compositor::GetSingleton()->Prepare(
            *instance.recipe, *material, bound.inputs, size, maxSize);
        if (!output->stack) {
          output->problem = "the texture lab is unavailable";
        } else if (settings.verboseLogging) {
          for (const Diagnostic &d : output->stack->Diagnostics()) {
            logger::warn("recipe {} output {} on '{}': {}: {}",
                         instance.recipe->id, c.output, bound.name, d.where,
                         d.message);
          }
        }
      }
    }
  }
  if (settings.verboseLogging) {
    logger::info(
        "apply armor {:08X} actor {:08X} geometry '{}' material={} {}",
        a_state.pieces[pieceIndex].armor, a_actor->GetFormID(), bound.name,
        bound.material ? (bound.material->Private() ? "private" : "shared")
                       : "untouched",
        bound.shell ? bound.shell->Describe() : "no shell");
  }
}

void Manager::PlaceLightsOf(RE::Actor *a_actor, LiveActor &a_state) {
  const Settings &settings = GetSettings();
  const std::span<const Recipe> loaded = LoadedRecipes();
  const ActorLightPlan plan = PlaceLights(a_state.structure, loaded);
  for (const LightContribution &c : plan.plan.shown) {
    const std::size_t sourceIndex = static_cast<std::size_t>(c.placed);
    if (sourceIndex >= plan.sources.size()) {
      continue;
    }
    const std::size_t instanceIndex =
        static_cast<std::size_t>(plan.sources[sourceIndex]);
    if (instanceIndex >= a_state.instances.size()) {
      continue;
    }
    LiveInstance &instance = a_state.instances[instanceIndex];
    if (!instance.recipe || !instance.signals) {
      continue;
    }
    std::vector<RE::BSGeometry *> geometries;
    for (const Placement &placement : a_state.structure.placements) {
      if (static_cast<std::size_t>(placement.instance) != instanceIndex) {
        continue;
      }
      const std::size_t flat = static_cast<std::size_t>(placement.piece);
      if (flat >= a_state.structure.pieces.size() ||
          a_state.structure.pieces[flat].firstPerson) {
        continue;
      }
      if (LiveGeometry *geometry = GeometryAtFlat(a_state, flat);
          geometry && geometry->geometry) {
        geometries.push_back(geometry->geometry.get());
      }
    }
    if (geometries.empty()) {
      continue;
    }
    const LightOutput *light =
        c.output < instance.recipe->outputs.size()
            ? Get<LightOutput>(instance.recipe->outputs[c.output])
            : nullptr;
    if (!light) {
      continue;
    }
    const std::vector<LightPlacement> placements =
        PlaceLightNodes(light->bones, geometries, a_actor->Get3D(false),
                        instance.signals->Resolve(light->offset));
    instance.light = LightBinding::Create(placements, light->shadow);
    instance.lightOutput = c.output;
    if (settings.verboseLogging) {
      logger::info("  recipe {}: {}", instance.recipe->id,
                   instance.light ? instance.light->Describe()
                                  : "light not created");
    }
  }
}

bool Manager::LayoutSanityCheck(RE::BSLightingShaderProperty *a_property) {
  if (layoutVerified_) {
    return true;
  }
  auto *material = static_cast<PBRMaterialLayout *>(a_property->material);
  const std::array textures{
      material->rmaosTexture.get(), material->emissiveTexture.get(),
      material->displacementTexture.get(), material->featuresTexture0.get(),
      material->featuresTexture1.get()};
  for (auto *texture : textures) {
    if (!texture || !netimmerse_cast<RE::NiSourceTexture *>(
                        static_cast<RE::NiTexture *>(texture))) {
      logger::error("PBR material layout check failed on {}: texture slots are "
                    "not all NiSourceTexture; emissive path disabled",
                    a_property->name.c_str());
      emissivePathEnabled_ = false;
      return false;
    }
  }
  layoutVerified_ = true;
  logger::info("PBR material layout check passed");
  return true;
}

void Manager::WithRecipeRetired(std::string_view a_id,
                                const std::function<void()> &a_action) {
  std::vector<RE::FormID> wearers;
  for (const auto &[actorID, state] : applied_) {
    if (std::ranges::any_of(state.instances, [&](const LiveInstance &a_i) {
          return a_i.recipe && a_i.recipe->id == a_id;
        })) {
      wearers.push_back(actorID);
    }
  }
  for (const RE::FormID actorID : wearers) {
    Retire(actorID);
  }
  a_action();
  for (const RE::FormID actorID : wearers) {
    QueueRefresh(actorID);
  }
}

void Manager::WithListMoved(const std::function<void()> &a_action) {
  RetireEveryActor();
  a_action();
  QueueLoadedActorRefreshes();
}

void Manager::RetireEveryActor() {
  std::vector<RE::FormID> ids;
  for (const auto &[actorID, state] : applied_) {
    ids.push_back(actorID);
  }
  for (const RE::FormID actorID : ids) {
    Retire(actorID);
  }
}

void Manager::Retire(RE::FormID a_actorID) {
  const auto it = applied_.find(a_actorID);
  if (it == applied_.end()) {
    return;
  }
  const std::size_t recipes = it->second.instances.size();
  const std::uint32_t now = NowMS();
  for (const LiveInstance &instance : it->second.instances) {
    if (instance.recipe) {
      carriedTimes_[{a_actorID, instance.recipe->id}] =
          CarriedTime{instance.lastTime, now};
    }
  }
  applied_.erase(it);
  TextureLab::GetSingleton()->InvalidatePreviews();
  UnwatchAnimationEvents(RE::TESForm::LookupByID<RE::Actor>(a_actorID));
  if (GetSettings().verboseLogging) {
    logger::info("actor {:08X}: retired {} recipe(s)", a_actorID, recipes);
  }
}
}
