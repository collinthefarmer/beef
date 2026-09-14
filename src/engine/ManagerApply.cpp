#include "diagnostics/Trace.h"
#include "engine/Manager.h"

#include "Identity.h"
#include "SettingsFile.h"
#include "engine/EngineForms.h"
#include "engine/Events.h"
#include "engine/ManagerShared.h"
#include "engine/RecipeStore.h"
#include "mesh/TextureSize.h"
#include "render/Compositor.h"
#include "render/PBRMaterial.h"
#include "render/RuntimeTextures.h"
#include "studio/Selection.h"

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

std::string TexturePath(const TextureRef &a_texture) {
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

struct LocatedGeometry {
  LivePiece &piece;
  LiveGeometry &bound;
};

[[nodiscard]] std::optional<LocatedGeometry>
LocateGeometry(LiveActor &a_state, GeometryId a_geometry) noexcept {
  std::size_t index = 0;
  for (LivePiece &piece : a_state.pieces) {
    for (LiveGeometry &geometry : piece.geometries) {
      if (GeometryId{index} == a_geometry) {
        return LocatedGeometry{piece, geometry};
      }
      ++index;
    }
  }
  return std::nullopt;
}

RE::MagicItem *EnchantmentForInstance(LiveActor &a_state,
                                      std::size_t a_instance) {
  for (const Placement &placement : a_state.structure.placements) {
    if (static_cast<std::size_t>(placement.instance) != a_instance) {
      continue;
    }
    if (const std::optional<LocatedGeometry> located =
            LocateGeometry(a_state, placement.geometry)) {
      return RE::TESForm::LookupByID<RE::MagicItem>(located->piece.enchantment);
    }
  }
  return nullptr;
}

std::pair<TextureSize, TextureSize>
RuntimeSizes(const Settings &a_settings, const MaterialInputs &a_material) {
  std::uint32_t native = 0;
  const auto consider = [&native](const TextureRef &a_texture) {
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

LiveGeometry MakeGeometry(RE::BSGeometry *a_geometry,
                          RE::BSLightingShaderProperty *a_property,
                          RE::NiAVObject *a_root,
                          const PbrMaterial &a_material) {
  LiveGeometry bound;
  bound.geometry = RE::NiPointer{a_geometry};
  bound.property = RE::NiPointer{a_property};
  bound.name = a_geometry->name.c_str() ? a_geometry->name.c_str() : "";
  bound.inputs.material = MaterialInputs::From(a_material);
  bound.inputs.geometry = RE::NiPointer{a_geometry};
  bound.inputs.root = RE::NiPointer{a_root};
  return bound;
}

struct PlannerGeometries {
  std::vector<Geometry> geometries;
  std::vector<Studio::PieceRef> owners;
};

PlannerGeometries BuildPlannerGeometries(const LiveActor &a_state,
                                         RE::NiAVObject *a_firstPersonRoot,
                                         RE::FormID a_actorID) {
  PlannerGeometries out;
  for (const LivePiece &live : a_state.pieces) {
    RE::TESObjectARMO *armor =
        RE::TESForm::LookupByID<RE::TESObjectARMO>(live.armor);
    WornPiece keys = WornKeysOf(
        armor, RE::TESForm::LookupByID<RE::MagicItem>(live.enchantment));
    for (const LiveGeometry &geometry : live.geometries) {
      keys.diffusePaths.push_back(
          TexturePath(geometry.inputs.material.diffuse));
    }
    const bool firstPerson =
        !live.geometries.empty() && live.geometries.front().inputs.root &&
        live.geometries.front().inputs.root.get() == a_firstPersonRoot;
    const Studio::PieceRef ref{a_actorID, live.armor, firstPerson};
    for (const LiveGeometry &geometry : live.geometries) {
      Geometry planned;
      planned.identity =
          GeometryIdentity{std::nullopt, geometry.name,
                           TexturePath(geometry.inputs.material.diffuse)};
      planned.keys = keys;
      planned.firstPerson = firstPerson;
      planned.lost = geometry.lost;
      out.geometries.push_back(std::move(planned));
      out.owners.push_back(ref);
    }
  }
  return out;
}

void PreparePlacement(LiveActor &a_state,
                      const GeometryPlacementPlan &placement,
                      LivePieceId a_piece, std::size_t a_geometry,
                      GeometryId a_flat) {
  LiveGeometry &bound =
      a_state.pieces[static_cast<std::size_t>(a_piece)].geometries[a_geometry];
  bound.placements = placement.sources;
  bound.plan = placement.plan;
  bound.stackPlan = PlanStacks(placement.placed, placement.plan);
  bound.binding = PlanBinding(placement.placed, placement.plan);
  for (const PlacementId source : placement.sources) {
    const std::size_t k = static_cast<std::size_t>(source);
    if (k >= a_state.placements.size() ||
        k >= a_state.structure.placements.size()) {
      continue;
    }
    LivePlacement &live = a_state.placements[k];
    live.geometry = a_flat;
    live.outputs.clear();
    for (const OutputPlacement &output :
         a_state.structure.placements[k].outputs) {
      live.outputs.push_back(PlacedOutput{
          static_cast<std::size_t>(output.output), nullptr, output.problem});
    }
  }
}

void InstallSurfaces(LiveActor &a_state, LiveGeometry &a_bound,
                     bool a_uniqueMaterial) {
  if (a_bound.binding.material) {
    a_bound.material = MaterialBinding::Install(
        a_bound.geometry.get(), a_bound.property.get(), a_uniqueMaterial);
  }
  if (a_bound.binding.shell) {
    std::optional<std::size_t> ownerInstance;
    if (a_bound.binding.shellOwner) {
      if (const std::optional<InstanceId> owner = InstanceOfPlaced(
              a_state.structure, a_bound.placements,
              static_cast<std::size_t>(*a_bound.binding.shellOwner))) {
        ownerInstance = static_cast<std::size_t>(*owner);
      }
    }
    if (ownerInstance && *ownerInstance < a_state.instances.size() &&
        a_state.instances[*ownerInstance].recipe) {
      a_bound.shellOwner = ownerInstance;
      a_bound.shell =
          ShellBinding::Create(a_bound.geometry.get(), a_bound.property.get(),
                               a_state.instances[*ownerInstance].recipe->shell);
    }
  }
}

void MarkReplaced(LiveActor &a_state, LiveGeometry &a_bound) {
  for (const SlotPlan &slot : a_bound.plan.slots) {
    for (const SlotContribution &c : slot.replaced) {
      const std::size_t placed = static_cast<std::size_t>(c.placed);
      if (placed >= a_bound.placements.size()) {
        continue;
      }
      const std::size_t placementIndex =
          static_cast<std::size_t>(a_bound.placements[placed]);
      if (placementIndex >= a_state.placements.size()) {
        continue;
      }
      PlacedOutput *output =
          OutputAt(a_state.placements[placementIndex], c.output);
      if (!output) {
        continue;
      }
      std::string replacerId;
      if (const std::optional<std::size_t> replacer = ReplacerOf(slot, c)) {
        if (const std::optional<InstanceId> instance = InstanceOfPlaced(
                a_state.structure, a_bound.placements, *replacer)) {
          const std::size_t instanceIndex = static_cast<std::size_t>(*instance);
          if (instanceIndex < a_state.instances.size() &&
              a_state.instances[instanceIndex].recipe) {
            replacerId = a_state.instances[instanceIndex].recipe->id;
          }
        }
      }
      output->problem = std::format("replaced by recipe {}", replacerId);
    }
  }
}

struct LocatedStackOutput {
  const Recipe &recipe;
  const SurfaceOutput &surface;
  PlacedOutput &placed;
};

std::optional<LocatedStackOutput>
LocateStackOutput(LiveActor &a_state, const LiveGeometry &a_bound,
                  const SlotContribution &a_contribution) {
  const std::size_t placed = static_cast<std::size_t>(a_contribution.placed);
  if (placed >= a_bound.placements.size()) {
    return std::nullopt;
  }
  const std::size_t placementIndex =
      static_cast<std::size_t>(a_bound.placements[placed]);
  if (placementIndex >= a_state.structure.placements.size() ||
      placementIndex >= a_state.placements.size()) {
    return std::nullopt;
  }
  const std::size_t instanceIndex = static_cast<std::size_t>(
      a_state.structure.placements[placementIndex].instance);
  if (instanceIndex >= a_state.instances.size()) {
    return std::nullopt;
  }
  const Recipe *recipe = a_state.instances[instanceIndex].recipe;
  if (!recipe || a_contribution.output >= recipe->outputs.size()) {
    return std::nullopt;
  }
  const auto *surface =
      Get<SurfaceOutput>(recipe->outputs[a_contribution.output]);
  auto *output =
      OutputAt(a_state.placements[placementIndex], a_contribution.output);
  if (!surface || !output) {
    return std::nullopt;
  }
  return LocatedStackOutput{*recipe, *surface, *output};
}

std::string SurfaceProblem(LiveGeometry &a_bound, const SlotPlan &a_slot,
                           const std::string &a_existingProblem) {
  if (a_slot.surface == Surface::kShell && !a_bound.shell) {
    return "shell could not be created";
  }
  if (a_slot.surface == Surface::kMaterial && !a_bound.material) {
    return "material binding failed";
  }
  if (!a_existingProblem.empty()) {
    return a_existingProblem;
  }
  SlotTarget *target = TargetFor(a_bound, a_slot.surface);
  return target ? ProblemText(target->Problem(a_slot.slot))
                : std::string{"the surface is not bound"};
}

void LogStackDiagnostics(const LocatedStackOutput &a_output,
                         const std::string &a_geometry) {
  for (const Diagnostic &diagnostic : a_output.placed.stack->Diagnostics()) {
    logger::warn("recipe {} output {} on '{}': {}: {}", a_output.recipe.id,
                 a_output.placed.index, a_geometry, diagnostic.where,
                 diagnostic.message);
  }
}

void PrepareChainStacks(LiveActor &a_state, LiveGeometry &a_bound,
                        const Settings &a_settings) {
  const auto [size, maxSize] =
      RuntimeSizes(a_settings, a_bound.inputs.material);
  for (const SlotPlan &slot : a_bound.plan.slots) {
    for (const SlotContribution &contribution : slot.chain) {
      const auto located = LocateStackOutput(a_state, a_bound, contribution);
      if (!located) {
        continue;
      }
      PlacedOutput &output = located->placed;
      output.active = true;
      output.problem = SurfaceProblem(a_bound, slot, output.problem);
      if (!output.problem.empty()) {
        continue;
      }
      output.stack = Compositor::GetSingleton()->Prepare(
          located->recipe, located->surface, a_bound.inputs, size, maxSize);
      if (!output.stack) {
        output.problem = "the texture lab is unavailable";
      } else if (a_settings.verboseLogging) {
        LogStackDiagnostics(*located, a_bound.name);
      }
    }
  }
}

void PlaceLight(LiveActor &a_state, const ActorLightPlan &a_plan,
                const LightContribution &a_c, RE::Actor *a_actor,
                bool a_verbose) {
  const std::size_t sourceIndex = static_cast<std::size_t>(a_c.placed);
  if (sourceIndex >= a_plan.sources.size()) {
    return;
  }
  const std::size_t instanceIndex =
      static_cast<std::size_t>(a_plan.sources[sourceIndex]);
  if (instanceIndex >= a_state.instances.size()) {
    return;
  }
  LiveInstance &instance = a_state.instances[instanceIndex];
  if (!instance.recipe || !instance.signals) {
    return;
  }
  std::vector<RE::BSGeometry *> geometries;
  for (const GeometryId flat : ThirdPersonGeometriesOfInstance(
           a_state.structure, InstanceId{instanceIndex})) {
    if (const std::optional<LocatedGeometry> located =
            LocateGeometry(a_state, flat);
        located && located->bound.geometry) {
      geometries.push_back(located->bound.geometry.get());
    }
  }
  if (geometries.empty()) {
    return;
  }
  const LightOutput *light =
      a_c.output < instance.recipe->outputs.size()
          ? Get<LightOutput>(instance.recipe->outputs[a_c.output])
          : nullptr;
  if (!light) {
    return;
  }
  const std::vector<LightPlacement> placements =
      PlaceLightNodes(light->bones, geometries, a_actor->Get3D(false),
                      instance.signals->Resolve(light->offset));
  instance.light = LightBinding::Create(placements, light->shadow);
  instance.lightOutput = a_c.output;
  if (a_verbose) {
    logger::info("  recipe {}: {}", instance.recipe->id,
                 instance.light ? instance.light->Describe()
                                : "light not created");
  }
}
}

void Manager::ReapplyAll() {
  const Trace::Scope trace{Trace::Command("manager.ReapplyAll")};
  PostTask([this] { ChangeAndRebuildActors({}, [] {}); });
}

void Manager::RetireAll() {
  const Trace::Scope trace{Trace::Command("manager.RetireAll")};
  PostTask([this] {
    std::vector<RE::FormID> ids;
    ids.reserve(applied_.size());
    for (const auto &[id, state] : applied_) {
      ids.push_back(id);
    }
    for (const RE::FormID id : ids) {
      QueueRetire(id);
    }
  });
}

void Manager::RunRefresh(RE::FormID a_actorID,
                         const std::vector<ApplicationToken> &a_tokens) {
  const RE::NiPointer<RE::Actor> actor{
      RE::TESForm::LookupByID<RE::Actor>(a_actorID)};
  if (!actor) {
    Retire(a_actorID);
  } else {
    Refresh(actor.get());
  }
  PrepareApplications(a_actorID, a_tokens);
}

void Manager::Refresh(RE::Actor *a_actor) {
  if (!a_actor) {
    return;
  }
  const Settings settings = GetSettings();
  const RE::FormID actorID = a_actor->GetFormID();
  Trace::Safely([&] {
    Trace::Emit(
        Trace::Event::kApplication,
        {{"action", "refresh_begin"}, {"actor", std::to_string(actorID)}});
  });
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
  state.actor = a_actor->GetHandle();
  if (settings.thirdPerson) {
    for (LivePiece &piece : CollectPieces(a_actor, false, settings)) {
      state.pieces.push_back(std::move(piece));
    }
  }
  if (settings.firstPerson && isPlayer) {
    for (LivePiece &piece : CollectPieces(a_actor, true, settings)) {
      state.pieces.push_back(std::move(piece));
    }
  }
  MatchRecipes(a_actor, state, settings);
  TextureLab::GetSingleton()->InvalidatePreviews();
  if (state.structure.placements.empty()) {
    return;
  }
  PlaceInstances(state, settings);
  PlaceLightsOf(a_actor, state, settings);
  if (settings.verboseLogging) {
    logger::info("actor {:08X} ({}): {} piece(s), {} recipe(s) applied",
                 actorID, a_actor->GetName(), state.pieces.size(),
                 state.instances.size());
  }
  Trace::Safely([&] {
    Trace::Emit(Trace::Event::kApplication,
                {{"action", "installed"},
                 {"actor", std::to_string(actorID)},
                 {"pieces", std::to_string(state.pieces.size())},
                 {"recipes", std::to_string(state.instances.size())}});
  });
  applied_[actorID] = std::move(state);
  WatchAnimationEvents(a_actor);

  if (applications_.TakeEquipped(actorID)) {
    Fire(actorID, EquipEvent(applied_[actorID].pieces));
  }
}

bool Manager::CollectPieceGeometries(LivePiece &piece, RE::NiAVObject *clone,
                                     RE::NiAVObject *root, bool verbose) {
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
        RE::BSLightingShaderProperty *property = LightingPropertyOf(a_geometry);
        const std::optional<PbrMaterial> material = PbrMaterial::Bind(property);
        if (!property || !material || !seen.insert(property).second) {
          return RE::BSVisit::BSVisitControl::kContinue;
        }
        anyPBR = true;
        if (!LayoutSanityCheck(*material, name)) {
          layoutFailed = true;
          return RE::BSVisit::BSVisitControl::kStop;
        }
        if (!property->emissiveColor) {
          if (verbose) {
            logger::info(
                "skip geometry {}: property has no emissive colour storage",
                name);
          }
          return RE::BSVisit::BSVisitControl::kContinue;
        }
        piece.geometries.push_back(
            MakeGeometry(a_geometry, property, root, *material));
        return RE::BSVisit::BSVisitControl::kContinue;
      });
  if (layoutFailed) {
    return false;
  }
  if (!anyPBR || piece.geometries.empty()) {
    if (!anyPBR && verbose && loggedNonPBRArmor_.insert(piece.armor).second) {
      logger::info("armor {:08X} ({}) has no PBR geometry; left alone",
                   piece.armor, piece.armorName);
    }
    return false;
  }
  return true;
}

std::vector<LivePiece> Manager::CollectPieces(RE::Actor *a_actor,
                                              bool a_firstPerson,
                                              const Settings &a_settings) {
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
    piece.enchantment = enchantment ? enchantment->GetFormID() : 0;
    if (!CollectPieceGeometries(piece, clone, root,
                                a_settings.verboseLogging)) {
      continue;
    }
    out.push_back(std::move(piece));
  }
  return out;
}

void Manager::MatchRecipes(RE::Actor *a_actor, LiveActor &a_state,
                           const Settings &a_settings) {
  const std::span<const Recipe> loaded = LoadedRecipes();
  RE::NiAVObject *firstPersonRoot = a_actor->Get3D(true);
  const RE::FormID actorID = a_actor->GetFormID();
  const PlannerGeometries built =
      BuildPlannerGeometries(a_state, firstPersonRoot, actorID);
  const std::vector<Studio::PieceRef> &refs = built.owners;
  a_state.structure = MatchActor(
      built.geometries, loaded,
      [this, loaded, &refs](const Geometry &a_geometry,
                            GeometryId a_geometryID) {
        const std::size_t index = static_cast<std::size_t>(a_geometryID);
        std::vector<ResolvedRecipe> resolved = Resolve(a_geometry.keys, loaded);
        const Studio::PieceRef ref =
            index < refs.size() ? refs[index] : Studio::PieceRef{};
        return Studio::ViewedRecipes({std::move(resolved), a_geometry.keys, ref,
                                      editor_.CurrentView(), loaded});
      });
  a_state.instances.clear();
  for (std::size_t i = 0; i < a_state.structure.instances.size(); ++i) {
    const Instance &instance = a_state.structure.instances[i];
    RE::MagicItem *enchantment = EnchantmentForInstance(a_state, i);
    (void)InstanceFor(a_state, instance.recipe, enchantment, a_settings);
  }
}

std::optional<std::size_t> Manager::InstanceFor(LiveActor &a_state,
                                                RecipeId a_recipe,
                                                RE::MagicItem *a_enchantment,
                                                const Settings &a_settings) {
  const auto actor = a_state.actor.get();
  if (!actor) {
    return std::nullopt;
  }
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
      std::make_unique<ActorEnvironment>(actor.get(), a_enchantment);
  instance.startMS = NowMS();
  if (const auto carried = carriedTimes_.find({actor->GetFormID(), recipe->id});
      carried != carriedTimes_.end()) {
    const float speed = a_settings.animationSpeed * recipe->clock.speed;
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

void Manager::PlaceInstances(LiveActor &a_state, const Settings &a_settings) {
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
      const auto placement = PlanGeometryPlacement(
          a_state.structure, loaded, GeometryId{flat},
          [this](const Recipe &recipe, std::size_t output) {
            return editor_.CurrentView().OutputShown(recipe.id, output);
          });
      PreparePlacement(a_state, placement, LivePieceId{p}, g, GeometryId{flat});
      PlaceOnGeometry(a_state, LivePieceId{p}, g, a_settings);
      ++flat;
    }
  }
}

void Manager::PlaceOnGeometry(LiveActor &a_state, LivePieceId a_piece,
                              std::size_t a_geometry,
                              const Settings &a_settings) {
  const std::size_t pieceIndex = static_cast<std::size_t>(a_piece);
  if (pieceIndex >= a_state.pieces.size() ||
      a_geometry >= a_state.pieces[pieceIndex].geometries.size()) {
    return;
  }
  LiveGeometry &bound = a_state.pieces[pieceIndex].geometries[a_geometry];

  InstallSurfaces(a_state, bound, a_settings.uniqueMaterial);
  MarkReplaced(a_state, bound);
  PrepareChainStacks(a_state, bound, a_settings);

  const auto actor = a_state.actor.get();
  if (actor) {
    Trace::Safely([&] {
      Trace::Emit(
          Trace::Event::kBinding,
          {{"action", "geometry_scope"},
           {"actor", std::to_string(actor->GetFormID())},
           {"armor", std::to_string(a_state.pieces[pieceIndex].armor)},
           {"geometry", Trace::Pointer(bound.geometry.get())},
           {"property", Trace::Pointer(bound.property.get())},
           {"name", bound.name},
           {"shell",
            Trace::Pointer(bound.shell ? bound.shell->Geometry() : nullptr)}});
    });
  }
  if (a_settings.verboseLogging && actor) {
    logger::info(
        "apply armor {:08X} actor {:08X} geometry '{}' material={} {}",
        a_state.pieces[pieceIndex].armor, actor->GetFormID(), bound.name,
        bound.material ? (bound.material->Private() ? "private" : "shared")
                       : "untouched",
        bound.shell ? bound.shell->Describe() : "no shell");
  }
}

void Manager::PlaceLightsOf(RE::Actor *a_actor, LiveActor &a_state,
                            const Settings &a_settings) {
  const std::span<const Recipe> loaded = LoadedRecipes();
  const ActorLightPlan plan = PlanActorLights(
      a_state.structure, loaded,
      [this](const Recipe &recipe, std::size_t output) {
        return editor_.CurrentView().OutputShown(recipe.id, output);
      });
  for (const LightContribution &c : plan.plan.shown) {
    PlaceLight(a_state, plan, c, a_actor, a_settings.verboseLogging);
  }
}

bool Manager::LayoutSanityCheck(const PbrMaterial &a_material,
                                std::string_view a_propertyName) {
  if (layoutVerified_) {
    return true;
  }
  if (!a_material.TextureSlotsValid()) {
    logger::error("PBR material layout check failed on {}: texture slots are "
                  "not all NiSourceTexture; emissive path disabled",
                  a_propertyName);
    emissivePathEnabled_ = false;
    return false;
  }
  layoutVerified_ = true;
  logger::info("PBR material layout check passed");
  return true;
}

void Manager::RetireEveryActor() {
  std::vector<RE::FormID> ids;
  ids.reserve(applied_.size());
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
  Trace::Safely([&] {
    Trace::Emit(Trace::Event::kRetire, {{"action", "begin"},
                                        {"actor", std::to_string(a_actorID)},
                                        {"recipes", std::to_string(recipes)}});
  });
  RetireActorEffects(it->second);
  applied_.erase(it);
  Trace::Safely([&] {
    Trace::Emit(Trace::Event::kRetire,
                {{"action", "end"}, {"actor", std::to_string(a_actorID)}});
  });
  TextureLab::GetSingleton()->InvalidatePreviews();
  UnwatchAnimationEvents(RE::TESForm::LookupByID<RE::Actor>(a_actorID));
  if (GetSettings().verboseLogging) {
    logger::info("actor {:08X}: retired {} recipe(s)", a_actorID, recipes);
  }
}
}
