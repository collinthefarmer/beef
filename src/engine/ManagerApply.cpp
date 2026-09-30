// GPL-3.0-only with the additional permission in COPYING.md.
#include "diagnostics/Metrics.h"
#include "diagnostics/Trace.h"
#include "engine/Manager.h"

#include "Identity.h"
#include "SettingsFile.h"
#include "engine/Clock.h"
#include "engine/EngineForms.h"
#include "engine/GameObjectService.h"
#include "engine/RecipeStore.h"
#include "engine/WornKeys.h"
#include "mesh/TextureSize.h"
#include "planners/Eviction.h"
#include "render/Compositor.h"
#include "render/PBRMaterial.h"
#include "render/RenderInstance.h"
#include "render/TextureLab.h"
#include "studio/Selection.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
class RefreshMeter {
public:
  explicit RefreshMeter(RE::FormID a_actor) noexcept : actor_(a_actor) {}
  ~RefreshMeter() {
    const std::uint64_t micros = watch_.Micros();
    Metrics::CountRefresh(micros);
    Trace::EmitSafely(Trace::Event::kMetrics,
                      {{"action", "refresh"},
                       {"actor", std::to_string(actor_)},
                       {"us", std::to_string(micros)}});
  }
  RefreshMeter(const RefreshMeter &) = delete;
  RefreshMeter &operator=(const RefreshMeter &) = delete;
  RefreshMeter(RefreshMeter &&) = delete;
  RefreshMeter &operator=(RefreshMeter &&) = delete;

private:
  Metrics::Stopwatch watch_;
  RE::FormID actor_;
};

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

struct LocatedGeometry {
  LivePiece &piece;
  LiveGeometry &bound;
  GeometryId id;
};

[[nodiscard]] std::optional<LocatedGeometry>
LocateGeometry(LiveActor &a_state, GeometryId a_geometry) noexcept {
  std::size_t index = 0;
  for (LivePiece &piece : a_state.pieces) {
    for (LiveGeometry &geometry : piece.geometries) {
      if (GeometryId{index} == a_geometry) {
        return LocatedGeometry{piece, geometry, a_geometry};
      }
      ++index;
    }
  }
  return std::nullopt;
}

std::shared_ptr<const RecipeGraph> InstanceGraph(const Recipe &a_recipe,
                                                 const Variant *a_variant) {
  if (!a_variant) {
    return GraphFor(a_recipe);
  }
  const Recipe varied = ApplyVariant(a_recipe, *a_variant);
  return std::make_shared<const RecipeGraph>(RecipeGraph::Compile(varied));
}

RE::FormID EnchantmentForInstance(LiveActor &a_state, std::size_t a_instance) {
  for (const Placement &placement : a_state.plan.placements) {
    if (IndexOf(placement.instance) != a_instance) {
      continue;
    }
    if (const std::optional<LocatedGeometry> located =
            LocateGeometry(a_state, placement.geometry)) {
      return located->piece.enchantment;
    }
  }
  return 0;
}

struct RuntimeTextureSizes {
  TextureSize requested;
  TextureSize native;
};

RuntimeTextureSizes RuntimeSizes(const Settings &a_settings,
                                 const MaterialInputs &a_material) {
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

TextureSize SlotStackSize(TextureSize a_base, Slot a_slot,
                          const std::optional<Resolution> &a_override) {
  const Resolution resolution =
      a_override.value_or(DefaultSlotResolution(a_slot));
  return TextureSize(a_base.Pixels() / ResolutionDivisor(resolution));
}

EventRecord EquipEvent() {
  EventRecord record;
  record.id = "equip";
  record.payload.value = 1.0f;
  return record;
}

std::optional<EventRecord>
EquipPositionEvent(const std::vector<LivePiece> &a_pieces) {
  for (const LivePiece &piece : a_pieces) {
    for (const LiveGeometry &bound : piece.geometries) {
      if (bound.geometry) {
        const RE::NiPoint3 &c = bound.geometry->worldBound.center;
        EventRecord record;
        record.id = "equip.position";
        record.payload.value = Vec3{c.x, c.y, c.z};
        return record;
      }
    }
  }
  return std::nullopt;
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
          GeometryIdentity{live.addon, geometry.name,
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
                      GeometryId a_geometry) {
  const std::optional<LocatedGeometry> located =
      LocateGeometry(a_state, a_geometry);
  if (!located)
    return;
  LiveGeometry &bound = located->bound;
  bound.placements = placement.sources;
  bound.plan = placement.plan;
  bound.stackPlan = PlanStacks(placement.placed, placement.plan);
  bound.binding = PlanBinding(placement.placed, placement.plan);
  for (const PlacementId source : placement.sources) {
    const std::size_t k = IndexOf(source);
    if (k >= a_state.placements.size() || k >= a_state.plan.placements.size()) {
      continue;
    }
    LivePlacement &live = a_state.placements[k];
    live.geometry = a_geometry;
    live.outputs.clear();
    for (const OutputPlacement &output : a_state.plan.placements[k].outputs) {
      live.outputs.push_back(
          PlacedOutput{IndexOf(output.output), nullptr, output.problem});
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
      if (const std::optional<InstanceId> owner =
              InstanceOfPlaced(a_state.plan, a_bound.placements,
                               IndexOf(*a_bound.binding.shellOwner))) {
        ownerInstance = IndexOf(*owner);
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
      const std::optional<ResolvedPlacement> resolved =
          ResolvePlacement(a_state, a_bound, IndexOf(c.placed));
      if (!resolved) {
        continue;
      }
      PlacedOutput *output =
          OutputAt(a_state.placements[resolved->placement], c.output);
      if (!output) {
        continue;
      }
      std::string replacerId;
      if (const std::optional<std::size_t> replacer = ReplacerOf(slot, c)) {
        if (const std::optional<InstanceId> instance =
                InstanceOfPlaced(a_state.plan, a_bound.placements, *replacer)) {
          const std::size_t instanceIndex = IndexOf(*instance);
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

}
struct LocatedStackOutput {
  PlacementId placement;
  const Recipe &recipe;
  const RecipeGraph &graph;
  std::size_t applicationContext;
  const SurfaceOutput &surface;
  PlacedOutput &placed;
};
struct ActorStackRequest {
  StackTextureRequest texture;
  GeometryId geometry;
  LocatedStackOutput located;
};
struct ActorStacks {
  std::vector<ActorStackRequest> requests;
  std::vector<GeometryInputs> geometries;
  std::vector<LiveGeometry *> bound;
};
namespace {

std::optional<LocatedStackOutput>
LocateStackOutput(LiveActor &a_state, const LiveGeometry &a_bound,
                  const SlotContribution &a_contribution) {
  const std::optional<ResolvedPlacement> resolved =
      ResolvePlacement(a_state, a_bound, IndexOf(a_contribution.placed));
  if (!resolved) {
    return std::nullopt;
  }
  const Recipe *recipe = a_state.instances[resolved->instance].recipe;
  if (!recipe || a_contribution.output >= recipe->outputs.size()) {
    return std::nullopt;
  }
  const auto *surface =
      Get<SurfaceOutput>(recipe->outputs[a_contribution.output]);
  auto *output =
      OutputAt(a_state.placements[resolved->placement], a_contribution.output);
  const auto &graph = a_state.instances[resolved->instance].graph;
  if (!surface || !output || !graph) {
    return std::nullopt;
  }
  return LocatedStackOutput{
      PlacementId{resolved->placement}, *recipe,  *graph,
      resolved->instance + 1,           *surface, *output};
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
                         const std::string &a_geometry,
                         WarningHistory &a_warnings) {
  for (const Diagnostic &diagnostic : a_output.placed.stack->Diagnostics()) {
    const WarningDecision decision = a_warnings.Observe(
        std::format("{}|{}|{}|{}|{}", a_output.recipe.id, a_output.placed.index,
                    a_geometry, diagnostic.where, diagnostic.message));
    if (decision == WarningDecision::kLimit) {
      logger::warn("stack warning history reached its budget; warnings that "
                   "exceed it are omitted from the log until the next load; "
                   "diagnostics remain available in the editor");
    }
    if (decision != WarningDecision::kFirst) {
      continue;
    }
    logger::warn("recipe {} output {} on '{}': {}: {} (repeats suppressed)",
                 a_output.recipe.id, a_output.placed.index, a_geometry,
                 diagnostic.where, diagnostic.message);
  }
}

void RecordGeometry(ActorStacks &a_stacks, LiveGeometry &a_bound,
                    GeometryId a_geometry) {
  const std::size_t index = IndexOf(a_geometry);
  if (a_stacks.geometries.size() <= index) {
    a_stacks.geometries.resize(index + 1);
    a_stacks.bound.resize(index + 1, nullptr);
  }
  a_stacks.geometries[index] = a_bound.inputs;
  a_stacks.bound[index] = &a_bound;
}

void CollectChainStacks(LiveActor &a_state, const LocatedGeometry &a_geometry,
                        const Settings &a_settings, ActorStacks &a_stacks) {
  LiveGeometry &bound = a_geometry.bound;
  RecordGeometry(a_stacks, bound, a_geometry.id);
  const auto [size, maxSize] = RuntimeSizes(a_settings, bound.inputs.material);
  Compositor *compositor = Compositor::GetSingleton();
  for (const SlotPlan &slot : bound.plan.slots) {
    for (const SlotContribution &contribution : slot.chain) {
      const std::optional<LocatedStackOutput> located =
          LocateStackOutput(a_state, bound, contribution);
      if (!located) {
        continue;
      }
      PlacedOutput &output = located->placed;
      output.active = true;
      output.problem = SurfaceProblem(bound, slot, output.problem);
      if (!output.problem.empty()) {
        continue;
      }
      const TextureSize slotSize =
          SlotStackSize(size, slot.slot, located->surface.resolution);
      GeometryInputs inputs = bound.inputs;
      inputs.applicationContext = located->applicationContext;
      const TextureSize stackSize =
          compositor->StackSize(located->surface, inputs, slotSize, maxSize);
      a_stacks.requests.push_back(
          {{&located->recipe, &located->graph, &located->surface,
            contribution.output, located->placement, inputs, stackSize},
           a_geometry.id,
           *located});
    }
  }
}

void AttachRender(const ActorStacks &a_stacks,
                  const std::shared_ptr<RenderInstance> &a_render) {
  for (LiveGeometry *bound : a_stacks.bound) {
    if (bound) {
      bound->inputs.render = a_render;
    }
  }
}

std::vector<std::shared_ptr<const RecipeGraph>>
LiveGraphs(const LiveActor &a_state) {
  std::vector<std::shared_ptr<const RecipeGraph>> graphs;
  graphs.reserve(a_state.instances.size());
  for (const LiveInstance &instance : a_state.instances) {
    if (instance.graph) {
      graphs.push_back(instance.graph);
    }
  }
  return graphs;
}

std::vector<RenderStackRequest>
RenderStackRequests(const ActorStacks &a_stacks) {
  std::vector<RenderStackRequest> stacks;
  stacks.reserve(a_stacks.requests.size());
  for (const ActorStackRequest &request : a_stacks.requests) {
    const StackTextureRequest &texture = request.texture;
    stacks.push_back({texture.graph,
                      texture.output,
                      texture.inputs.applicationContext,
                      texture.placement,
                      texture.outputIndex,
                      {texture.size},
                      request.geometry});
  }
  return stacks;
}

RenderBindingResolver BindingResolverFor(const ActorStacks &a_stacks) {
  return [&a_stacks](const TextureValue &a_value,
                     GeometryId a_geometry) -> ValueBindings {
    for (const ActorStackRequest &request : a_stacks.requests) {
      const StackTextureRequest &texture = request.texture;
      if (texture.graph == a_value.graph &&
          texture.inputs.applicationContext == a_value.instance &&
          request.geometry == a_geometry) {
        return TextureValueBindings(*a_value.graph, texture.inputs);
      }
    }
    return ValueBindings{};
  };
}

std::string LayerProperty(std::size_t a_output, std::size_t a_layer,
                          TextureUseInput a_role) {
  return LayerWhere(a_output, a_layer) +
         (a_role == TextureUseInput::kSource ? " source" : " mask");
}

struct LayerRole {
  std::size_t layer = 0;
  TextureUseInput role = TextureUseInput::kSource;
};

void CollectLayerRoleDemands(std::vector<TextureDemand> &a_demands,
                             const ActorStackRequest &a_request,
                             LayerRole a_role, bool a_verbose) {
  const StackTextureRequest &texture = a_request.texture;
  if (!texture.graph)
    return;
  const std::string property =
      LayerProperty(texture.outputIndex, a_role.layer, a_role.role);
  for (const auto &binding : texture.graph->OutputBindings()) {
    if (binding.property != property ||
        !texture.graph->SampleDependent(binding.value)) {
      continue;
    }
    const auto collected = CollectTextureDemand(
        a_demands,
        {texture.graph, binding.value, texture.inputs.applicationContext},
        {texture.size},
        {texture.placement, texture.outputIndex, a_role.layer, a_role.role},
        TextureValueBindings(*texture.graph, texture.inputs),
        a_request.geometry);
    if (!collected && a_verbose) {
      logger::warn("{}: {}", property, collected.error());
    }
  }
}

void CollectRequestDemands(std::vector<TextureDemand> &a_demands,
                           const ActorStackRequest &a_request, bool a_verbose) {
  if (!a_request.texture.output)
    return;
  for (std::size_t layer = 0; layer < a_request.texture.output->stack.size();
       ++layer) {
    for (const TextureUseInput role :
         {TextureUseInput::kSource, TextureUseInput::kMask}) {
      CollectLayerRoleDemands(a_demands, a_request, {layer, role}, a_verbose);
    }
  }
}

std::vector<TextureDemand> CollectLayerDemands(const ActorStacks &a_stacks,
                                               bool a_verbose) {
  std::vector<TextureDemand> demands;
  for (const ActorStackRequest &request : a_stacks.requests) {
    CollectRequestDemands(demands, request, a_verbose);
  }
  return demands;
}

void FailStackOutputs(ActorStacks &a_stacks, const std::string &a_problem) {
  for (ActorStackRequest &request : a_stacks.requests) {
    request.located.placed.problem = a_problem;
  }
}

std::optional<StepOutputRef>
StackOutputFor(const RenderPlan &a_plan, const StackTextureRequest &a_request,
               GeometryId a_geometry) {
  for (const StackOutputBinding &binding : a_plan.stackOutputs) {
    if (binding.placement == a_request.placement &&
        binding.output == a_request.outputIndex &&
        binding.geometry == a_geometry) {
      return binding.result;
    }
  }
  return std::nullopt;
}

const LiveGeometry *BoundAt(const ActorStacks &a_stacks,
                            GeometryId a_geometry) {
  const std::size_t index = IndexOf(a_geometry);
  return index < a_stacks.bound.size() ? a_stacks.bound[index] : nullptr;
}

void AttachStackOutputs(ActorStacks &a_stacks,
                        const std::shared_ptr<RenderInstance> &a_render,
                        const Settings &a_settings,
                        WarningHistory &a_warnings) {
  for (ActorStackRequest &request : a_stacks.requests) {
    const StackTextureRequest &texture = request.texture;
    PlacedOutput &placed = request.located.placed;
    if (const std::optional<StepOutputRef> result =
            StackOutputFor(a_render->Plan(), texture, request.geometry)) {
      placed.stack = std::make_unique<RenderOutput>(
          a_render, *result, texture.size,
          IsAnimated(*texture.graph, Output{*texture.output}));
    }
    const LiveGeometry *bound = BoundAt(a_stacks, request.geometry);
    if (!placed.stack) {
      placed.problem = "stack was not lowered";
    } else if (a_settings.verboseLogging && bound) {
      LogStackDiagnostics(request.located, bound->name, a_warnings);
    }
  }
}

void BuildActorRender(const LiveActor &a_state, ActorStacks &a_stacks,
                      const Settings &a_settings, WarningHistory &a_warnings) {
  AttachRender(a_stacks, nullptr);
  if (a_stacks.requests.empty()) {
    return;
  }
  const std::vector<TextureDemand> demands =
      CollectLayerDemands(a_stacks, a_settings.verboseLogging);
  std::expected<RenderPlan, std::string> plan = BuildRenderPlan(
      demands, RenderStackRequests(a_stacks), BindingResolverFor(a_stacks));
  if (!plan) {
    FailStackOutputs(a_stacks, plan.error());
    return;
  }
  const std::shared_ptr<RenderInstance> render =
      std::make_shared<RenderInstance>(std::move(*plan), a_stacks.geometries,
                                       LiveGraphs(a_state));
  AttachRender(a_stacks, render);
  AttachStackOutputs(a_stacks, render, a_settings, a_warnings);
}

void PlaceLight(LiveActor &a_state, const ActorLightPlan &a_plan,
                const LightContribution &a_c, RE::Actor *a_actor,
                bool a_verbose) {
  const std::size_t sourceIndex = IndexOf(a_c.placed);
  if (sourceIndex >= a_plan.sources.size()) {
    return;
  }
  const std::size_t instanceIndex = IndexOf(a_plan.sources[sourceIndex]);
  if (instanceIndex >= a_state.instances.size()) {
    return;
  }
  LiveInstance &instance = a_state.instances[instanceIndex];
  if (!instance.recipe || !instance.signals) {
    return;
  }
  const LightOutput *light =
      a_c.output < instance.recipe->outputs.size()
          ? Get<LightOutput>(instance.recipe->outputs[a_c.output])
          : nullptr;
  if (!light) {
    return;
  }
  std::vector<RE::BSGeometry *> geometries;
  for (const GeometryId flat : ThirdPersonGeometriesOfInstance(
           a_state.plan, InstanceId{instanceIndex})) {
    const Geometry *geometry = GeometryAt(a_state.plan, flat);
    if (!geometry || !LightEligible(*geometry, *light)) {
      continue;
    }
    if (const std::optional<LocatedGeometry> located =
            LocateGeometry(a_state, flat);
        located && located->bound.geometry) {
      geometries.push_back(located->bound.geometry.get());
    }
  }
  if (geometries.empty()) {
    return;
  }
  const std::vector<LightPlacement> placements =
      PlaceLightNodes(light->bones, geometries, a_actor->Get3D(false),
                      instance.signals->Resolve(light->offset));
  instance.light = LightBinding::Create(placements, light->shadow);
  instance.lightOutput = a_c.output;
  instance.lightProblem = instance.light ? "" : "light preparation failed";
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

void Manager::RunRefresh(RE::FormID a_actorID,
                         const std::vector<ApplicationToken> &a_tokens) {
  const RE::NiPointer<RE::Actor> actor{
      RE::TESForm::LookupByID<RE::Actor>(a_actorID)};
  if (!actor) {
    Retire(a_actorID);
  } else {
    Refresh(actor.get());
  }
  ReconcileAnimationEvents(a_actorID);
  PrepareApplications(a_actorID, a_tokens);
}

void Manager::Refresh(RE::Actor *a_actor) {
  if (!a_actor) {
    return;
  }
  const Settings settings = GetSettings();
  const RE::FormID actorID = a_actor->GetFormID();
  const RefreshMeter meter{actorID};
  Trace::EmitSafely(
      Trace::Event::kApplication,
      {{"action", "refresh_begin"}, {"actor", std::to_string(actorID)}});
  RetireEffects(actorID);
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
  if (!isPlayer && settings.evictDistance > 0.0f) {
    if (const auto *player = RE::PlayerCharacter::GetSingleton()) {
      const float distance =
          player->GetPosition().GetDistance(a_actor->GetPosition());
      if (EvictionFor(distance, settings.evictDistance, true) ==
          EvictionAction::kEvict) {
        evictedForDistance_.insert(actorID);
        return;
      }
    }
  }
  evictedForDistance_.erase(actorID);
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
  if (state.plan.placements.empty()) {
    return;
  }
  PlaceInstances(state, settings);
  PlaceLightsOf(a_actor, state, settings);
  if (settings.verboseLogging) {
    logger::info("actor {:08X} ({}): {} piece(s), {} recipe(s) applied",
                 actorID, a_actor->GetName(), state.pieces.size(),
                 LiveInstanceCount(state));
  }
  Trace::EmitSafely(Trace::Event::kApplication,
                    {{"action", "installed"},
                     {"actor", std::to_string(actorID)},
                     {"pieces", std::to_string(state.pieces.size())},
                     {"recipes", std::to_string(LiveInstanceCount(state))}});
  applied_[actorID] = std::move(state);

  if (applications_.TakeEquipped(actorID)) {
    FireEquip(actorID);
  }
}

void Manager::FireEquip(RE::FormID a_actorID) {
  Fire(a_actorID, EquipEvent());
  if (const auto carried = EquipPositionEvent(applied_[a_actorID].pieces)) {
    Fire(a_actorID, *carried);
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
    if (object.addon) {
      piece.addon = FormKeyFor(*object.addon);
    }
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
  a_state.plan = MatchActor(
      built.geometries, loaded,
      [this, loaded, &refs, actorID](const Geometry &a_geometry,
                                     GeometryId a_geometryID) {
        const std::size_t index = IndexOf(a_geometryID);
        std::vector<ResolvedRecipe> resolved =
            Resolve(a_geometry.keys, loaded, actorID);
        const Studio::PieceRef ref =
            index < refs.size() ? refs[index] : Studio::PieceRef{};
        return Studio::ViewedRecipes({std::move(resolved), a_geometry.keys, ref,
                                      editor_.CurrentView(), loaded});
      });
  a_state.instances.clear();
  a_state.instances.resize(a_state.plan.instances.size());
  for (std::size_t i = 0; i < a_state.plan.instances.size(); ++i) {
    const RE::FormID enchantment = EnchantmentForInstance(a_state, i);
    (void)InstanceFor(a_state, InstanceId{i}, enchantment, a_settings);
  }
}

std::optional<std::size_t> Manager::InstanceFor(LiveActor &a_state,
                                                InstanceId a_planInstance,
                                                RE::FormID a_enchantment,
                                                const Settings &a_settings) {
  const auto actor = a_state.actor.get();
  const Instance *planned = InstanceAt(a_state.plan, a_planInstance);
  const std::size_t instanceIndex = IndexOf(a_planInstance);
  if (!actor || !planned || instanceIndex >= a_state.instances.size()) {
    return std::nullopt;
  }
  const std::span<const Recipe> loaded = LoadedRecipes();
  const std::size_t recipeIndex = IndexOf(planned->recipe);
  if (recipeIndex >= loaded.size()) {
    return std::nullopt;
  }
  const Recipe *recipe = &loaded[recipeIndex];
  LiveInstance instance;
  instance.recipe = recipe;
  instance.enchantment = a_enchantment;
  instance.effectScope =
      planned->effectKey ? planned->effectKey->ToString() : "";
  instance.graph = InstanceGraph(
      *recipe, InstanceVariant(a_state.plan, a_planInstance, *recipe));
  if (instance.graph) {
    instance.signals = std::make_unique<SignalState>(*instance.graph);
  }
  instance.environment = std::make_unique<ActorEnvironment>(
      actor.get(), RE::TESForm::LookupByID<RE::MagicItem>(a_enchantment),
      planned->effectKey);
  instance.startMS = NowMS();
  CarryInstanceTime(instance, actor->GetFormID(), *recipe, a_settings);
  a_state.instances[instanceIndex] = std::move(instance);
  return instanceIndex;
}

void Manager::CarryInstanceTime(LiveInstance &a_instance, RE::FormID a_actor,
                                const Recipe &a_recipe,
                                const Settings &a_settings) {
  const auto carried = carriedTimes_.Take(
      {a_actor, a_recipe.id, a_instance.enchantment, a_instance.effectScope},
      a_instance.startMS);
  if (!carried) {
    return;
  }
  const float speed =
      InstanceSpeed(a_settings.animationSpeed, editor_.CurrentView().speed,
                    a_recipe.clock.speed);
  if (const auto offset = ClockOffsetMS(*carried, speed)) {
    a_instance.startMS -= *offset;
    a_instance.lastTime = *carried;
  }
}

void Manager::PlaceInstances(LiveActor &a_state, const Settings &a_settings) {
  for (LiveInstance &instance : a_state.instances) {
    if (instance.signals && instance.environment) {
      instance.signals->Tick(*instance.environment, {0.0f, 0.0f});
    }
  }
  const std::span<const Recipe> loaded = LoadedRecipes();
  a_state.placements.clear();
  a_state.placements.resize(a_state.plan.placements.size());
  std::size_t flat = 0;
  ActorStacks stacks;
  for (std::size_t p = 0; p < a_state.pieces.size(); ++p) {
    for (std::size_t g = 0; g < a_state.pieces[p].geometries.size(); ++g) {
      const auto placement = PlanGeometryPlacement(
          a_state.plan, loaded, GeometryId{flat},
          [this](const Recipe &recipe, std::size_t output) {
            return editor_.CurrentView().OutputShown(recipe.id, output);
          });
      PreparePlacement(a_state, placement, GeometryId{flat});
      PlaceOnGeometry(a_state, GeometryId{flat}, a_settings, stacks);
      ++flat;
    }
  }
  BuildActorRender(a_state, stacks, a_settings, stackWarnings_);
}

void Manager::PlaceOnGeometry(LiveActor &a_state, GeometryId a_geometry,
                              const Settings &a_settings,
                              ActorStacks &a_stacks) {
  const std::optional<LocatedGeometry> located =
      LocateGeometry(a_state, a_geometry);
  if (!located) {
    return;
  }
  LiveGeometry &bound = located->bound;

  InstallSurfaces(a_state, bound, a_settings.uniqueMaterial);
  MarkReplaced(a_state, bound);
  CollectChainStacks(a_state, *located, a_settings, a_stacks);

  const auto actor = a_state.actor.get();
  if (actor) {
    Trace::EmitSafely(
        Trace::Event::kBinding,
        {{"action", "geometry_scope"},
         {"actor", std::to_string(actor->GetFormID())},
         {"armor", std::to_string(located->piece.armor)},
         {"geometry", Trace::Pointer(bound.geometry.get())},
         {"property", Trace::Pointer(bound.property.get())},
         {"name", bound.name},
         {"shell",
          Trace::Pointer(bound.shell ? bound.shell->Geometry() : nullptr)}});
  }
  if (a_settings.verboseLogging && actor) {
    logger::info("apply armor {:08X} actor {:08X} geometry '{}' material={} {}",
                 located->piece.armor, actor->GetFormID(), bound.name,
                 bound.material
                     ? (bound.material->Private() ? "private" : "shared")
                     : "untouched",
                 bound.shell ? bound.shell->Describe() : "no shell");
  }
}

void Manager::PlaceLightsOf(RE::Actor *a_actor, LiveActor &a_state,
                            const Settings &a_settings) {
  const std::span<const Recipe> loaded = LoadedRecipes();
  const ActorLightPlan plan = PlanActorLights(
      a_state.plan, loaded, [this](const Recipe &recipe, std::size_t output) {
        return editor_.CurrentView().OutputShown(recipe.id, output);
      });
  for (std::size_t i = 0; i < plan.sources.size(); ++i) {
    const std::size_t instance = IndexOf(plan.sources[i]);
    if (instance < a_state.instances.size()) {
      LiveInstance &live = a_state.instances[instance];
      live.lightProblem =
          live.recipe && std::ranges::any_of(live.recipe->outputs,
                                             [](const Output &a_output) {
                                               return Get<LightOutput>(
                                                          a_output) != nullptr;
                                             })
              ? "no eligible light output"
              : "";
    }
  }
  for (const LightContribution &c : plan.plan.replaced) {
    const std::size_t placed = IndexOf(c.placed);
    const std::optional<std::size_t> replacer = ReplacerOf(plan.plan, c);
    if (placed >= plan.sources.size() || !replacer ||
        *replacer >= plan.placed.size() || !plan.placed[*replacer].recipe) {
      continue;
    }
    const std::size_t instance = IndexOf(plan.sources[placed]);
    if (instance < a_state.instances.size()) {
      a_state.instances[instance].lightProblem =
          std::format("actor-wide lights replaced by recipe {}",
                      plan.placed[*replacer].recipe->id);
    }
  }
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
  animations_.Stop(a_actorID);
  ForgetAnimEvents(a_actorID);
  RetireEffects(a_actorID);
}

void Manager::RetireEffects(RE::FormID a_actorID) {
  const auto it = applied_.find(a_actorID);
  if (it == applied_.end()) {
    return;
  }
  applications_.Retire(a_actorID, it->second.applications);
  const std::size_t recipes = LiveInstanceCount(it->second);
  const std::uint32_t now = NowMS();
  for (const LiveInstance &instance : it->second.instances) {
    if (instance.recipe) {
      carriedTimes_.Remember({a_actorID, instance.recipe->id,
                              instance.enchantment, instance.effectScope},
                             instance.lastTime, now);
    }
  }
  Trace::EmitSafely(Trace::Event::kRetire,
                    {{"action", "begin"},
                     {"actor", std::to_string(a_actorID)},
                     {"recipes", std::to_string(recipes)}});
  RetireActorEffects(it->second);
  applied_.erase(it);
  Trace::EmitSafely(Trace::Event::kRetire,
                    {{"action", "end"}, {"actor", std::to_string(a_actorID)}});
  TextureLab::GetSingleton()->InvalidatePreviews();
  if (GetSettings().verboseLogging) {
    logger::info("actor {:08X}: retired {} recipe(s)", a_actorID, recipes);
  }
}
}
