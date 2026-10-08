// GPL-3.0-only with the additional permission in COPYING.md.
#include "diagnostics/Metrics.h"
#include "diagnostics/Trace.h"
#include "engine/Manager.h"

#include "Identity.h"
#include "SettingsFile.h"
#include "actors/Lifecycle.h"
#include "actors/Planning.h"
#include "actors/Reconcile.h"
#include "engine/ActorObservation.h"
#include "engine/Clock.h"
#include "engine/EngineForms.h"
#include "engine/GameObjectService.h"
#include "engine/LogOnce.h"
#include "engine/PhaseTrace.h"
#include "engine/RecipeStore.h"
#include "engine/RegressionRun.h"
#include "mesh/TextureSize.h"
#include "planners/Eviction.h"
#include "planners/RenderPlanCache.h"
#include "planners/RenderPlanKey.h"
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
class SyncMeter {
public:
  explicit SyncMeter(RE::FormID a_actor) noexcept : actor_(a_actor) {}
  ~SyncMeter() {
    const std::uint64_t micros = watch_.Micros();
    Metrics::CountSync(micros);
    Trace::EmitSafely(Trace::Event::kMetrics,
                      {{"action", "sync"},
                       {"actor", std::to_string(actor_)},
                       {"us", std::to_string(micros)}});
  }
  SyncMeter(const SyncMeter &) = delete;
  SyncMeter &operator=(const SyncMeter &) = delete;
  SyncMeter(SyncMeter &&) = delete;
  SyncMeter &operator=(SyncMeter &&) = delete;

private:
  Metrics::Stopwatch watch_;
  RE::FormID actor_;
};

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
  for (const Placement &placement : a_state.matches.placements) {
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
                          RE::NiAVObject *a_root, const PbrMaterial &a_material,
                          float a_itemHash) {
  LiveGeometry bound;
  bound.geometry = RE::NiPointer{a_geometry};
  bound.property = RE::NiPointer{a_property};
  bound.name = a_geometry->name.c_str() ? a_geometry->name.c_str() : "";
  bound.inputs.material = MaterialInputs::From(a_material);
  bound.inputs.geometry = RE::NiPointer{a_geometry};
  bound.inputs.root = RE::NiPointer{a_root};
  bound.inputs.itemHash = a_itemHash;
  return bound;
}

std::string ArmorNameOf(RE::FormID a_armor) {
  const auto *armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_armor);
  const char *name = armor ? armor->GetName() : nullptr;
  return name ? name : "";
}

float ItemHashOf(RE::FormID a_armor) {
  const auto *armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_armor);
  return armor ? UnitHash(FormKeyFor(*armor).ToString()) : 0.0f;
}

std::expected<std::vector<LivePiece>, std::string>
LivePiecesOf(RE::Actor &a_actor, const Actors::ActorFacts &a_facts,
             const ModelTable &a_models) {
  std::vector<LivePiece> pieces;
  for (const Actors::PieceFacts *planned : Actors::PlannedPieces(a_facts)) {
    const ObservedModel *model =
        planned->model ? ModelOf(a_models, *planned->model) : nullptr;
    if (!planned->model || !model ||
        model->geometries.size() != planned->geometries.size()) {
      return std::unexpected{
          std::format("armor {:08X} lost its observed model", planned->itemID)};
    }
    if (model->geometries.empty()) {
      continue;
    }
    RE::NiAVObject *root = a_actor.Get3D(planned->ref.firstPerson);
    LivePiece piece;
    piece.ref = planned->ref;
    piece.model = *planned->model;
    piece.armor = planned->itemID;
    piece.addon = planned->ref.addon;
    piece.armorName = ArmorNameOf(planned->itemID);
    piece.enchantment = planned->enchantmentID;
    const float itemHash = ItemHashOf(planned->itemID);
    for (std::size_t i = 0; i < model->geometries.size(); ++i) {
      const ObservedGeometry &observed = model->geometries[i];
      const std::optional<Actors::GeometryRef> ref =
          Actors::GeometryRefOf(*planned, i);
      if (!ref) {
        return std::unexpected{
            std::format("geometry {} of armor {:08X} has no planned identity",
                        observed.facts.name, planned->itemID)};
      }
      RE::BSGeometry *geometry = observed.geometry.get();
      RE::BSLightingShaderProperty *property =
          geometry ? LightingPropertyOf(geometry) : nullptr;
      const std::optional<PbrMaterial> material = PbrMaterial::Bind(property);
      if (!property || !material) {
        return std::unexpected{
            std::format("geometry {} of armor {:08X} lost its PBR material",
                        observed.facts.name, planned->itemID)};
      }
      LiveGeometry bound =
          MakeGeometry(geometry, property, root, *material, itemHash);
      bound.ref = *ref;
      piece.geometries.push_back(std::move(bound));
    }
    pieces.push_back(std::move(piece));
  }
  return pieces;
}

std::size_t GeometryCount(const LiveActor &a_state) noexcept {
  std::size_t count = 0;
  for (const LivePiece &piece : a_state.pieces) {
    count += piece.geometries.size();
  }
  return count;
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
    if (k >= a_state.placements.size() ||
        k >= a_state.matches.placements.size()) {
      continue;
    }
    LivePlacement &live = a_state.placements[k];
    live.geometry = a_geometry;
    live.outputs.clear();
    for (const OutputPlacement &output :
         a_state.matches.placements[k].outputs) {
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
              InstanceOfPlaced(a_state.matches, a_bound.placements,
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

std::string ReplacerRecipeId(const LiveActor &a_state,
                             const LiveGeometry &a_bound,
                             const SlotPlan &a_slot,
                             const SlotContribution &a_c) {
  const std::optional<std::size_t> replacer = ReplacerOf(a_slot, a_c);
  if (!replacer) {
    return {};
  }
  const std::optional<InstanceId> instance =
      InstanceOfPlaced(a_state.matches, a_bound.placements, *replacer);
  if (!instance) {
    return {};
  }
  const std::size_t instanceIndex = IndexOf(*instance);
  if (instanceIndex >= a_state.instances.size() ||
      !a_state.instances[instanceIndex].recipe) {
    return {};
  }
  return a_state.instances[instanceIndex].recipe->id;
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
      output->problem = std::format(
          "replaced by recipe {}", ReplacerRecipeId(a_state, a_bound, slot, c));
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
  EnchantmentValues enchantment;
};
struct ActorStackRequest {
  StackTextureRequest texture;
  GeometryId geometry;
  LocatedStackOutput located;
  SideRef side;
};
struct ActorStacks {
  RE::FormID actor = 0;
  std::vector<ActorStackRequest> requests;
  std::vector<TextureSize> sides;
  std::vector<SlotChainRequest> chains;
  std::vector<GeometryInputs> geometries;
  std::vector<LiveGeometry *> bound;
};
namespace {

EnchantmentValues EnchantmentValuesOf(const LiveInstance &a_instance) {
  EnchantmentValues values;
  if (!a_instance.environment) {
    return values;
  }
  for (std::size_t i = 0; i < values.fields.size(); ++i) {
    values.fields[i] =
        a_instance.environment->Enchantment(static_cast<EnchantmentField>(i));
  }
  return values;
}

std::optional<LocatedStackOutput>
LocateStackOutput(LiveActor &a_state, const LiveGeometry &a_bound,
                  const SlotContribution &a_contribution) {
  const std::optional<ResolvedPlacement> resolved =
      ResolvePlacement(a_state, a_bound, IndexOf(a_contribution.placed));
  if (!resolved) {
    return std::nullopt;
  }
  const Recipe *recipe = a_state.instances[resolved->instance].recipe.get();
  if (!recipe) {
    return std::nullopt;
  }
  const SurfaceOutput *surface =
      SurfaceOutputOf(*recipe, a_contribution.output);
  auto *output =
      OutputAt(a_state.placements[resolved->placement], a_contribution.output);
  const auto &graph = a_state.instances[resolved->instance].graph;
  if (!surface || !output || !graph) {
    return std::nullopt;
  }
  return LocatedStackOutput{
      PlacementId{resolved->placement},
      *recipe,
      *graph,
      resolved->instance + 1,
      *surface,
      *output,
      EnchantmentValuesOf(a_state.instances[resolved->instance])};
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
    LogOnce(a_warnings, Severity::kWarning,
            std::format("recipe {} output {} on '{}': {}: {}",
                        a_output.recipe.id, a_output.placed.index, a_geometry,
                        diagnostic.where, diagnostic.message));
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

SideRef SideFor(ActorStacks &a_stacks, TextureSize a_size) {
  const auto found = std::ranges::find(a_stacks.sides, a_size);
  if (found != a_stacks.sides.end()) {
    return {static_cast<std::uint32_t>(found - a_stacks.sides.begin())};
  }
  a_stacks.sides.push_back(a_size);
  return {static_cast<std::uint32_t>(a_stacks.sides.size() - 1)};
}

void CollectChainStacks(LiveActor &a_state, const LocatedGeometry &a_geometry,
                        const Settings &a_settings, ActorStacks &a_stacks) {
  LiveGeometry &bound = a_geometry.bound;
  RecordGeometry(a_stacks, bound, a_geometry.id);
  const auto [size, maxSize] = RuntimeSizes(a_settings, bound.inputs.material);
  Compositor *compositor = Compositor::GetSingleton();
  for (const SlotPlan &slot : bound.plan.slots) {
    SlotChainRequest chain{a_geometry.id, slot.surface, slot.slot, {}};
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
      inputs.scope = {located->applicationContext, a_stacks.actor};
      inputs.enchantment = located->enchantment;
      const TextureSize stackSize =
          compositor->StackSize(located->surface, inputs, slotSize, maxSize);
      const SideRef side = SideFor(a_stacks, stackSize);
      a_stacks.requests.push_back(
          {{&located->recipe, &located->graph, &located->surface,
            contribution.output, located->placement, inputs, stackSize},
           a_geometry.id,
           *located,
           side});
      chain.links.push_back({located->placement, contribution.output});
    }
    if (chain.links.size() > 1) {
      a_stacks.chains.push_back(std::move(chain));
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
                      texture.inputs.scope.context,
                      texture.placement,
                      texture.outputIndex,
                      {request.side},
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
          texture.inputs.scope.context == a_value.instance &&
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
                             LayerRole a_role, WarningHistory &a_warnings) {
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
        {{texture.graph, binding.value, texture.inputs.scope.context},
         {a_request.side},
         {texture.placement, texture.outputIndex, a_role.layer, a_role.role},
         a_request.geometry},
        TextureValueBindings(*texture.graph, texture.inputs));
    if (!collected) {
      LogOnce(a_warnings, Severity::kWarning,
              std::format("recipe {} {}: {}", a_request.located.recipe.id,
                          property, collected.error()));
    }
  }
}

void CollectRequestDemands(std::vector<TextureDemand> &a_demands,
                           const ActorStackRequest &a_request,
                           WarningHistory &a_warnings) {
  if (!a_request.texture.output)
    return;
  for (std::size_t layer = 0; layer < a_request.texture.output->stack.size();
       ++layer) {
    for (const TextureUseInput role :
         {TextureUseInput::kSource, TextureUseInput::kMask}) {
      CollectLayerRoleDemands(a_demands, a_request, {layer, role}, a_warnings);
    }
  }
}

std::vector<TextureDemand> CollectLayerDemands(const ActorStacks &a_stacks,
                                               WarningHistory &a_warnings) {
  std::vector<TextureDemand> demands;
  for (const ActorStackRequest &request : a_stacks.requests) {
    CollectRequestDemands(demands, request, a_warnings);
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
                        WarningHistory &a_warnings) {
  for (ActorStackRequest &request : a_stacks.requests) {
    const StackTextureRequest &texture = request.texture;
    PlacedOutput &placed = request.located.placed;
    if (const std::optional<StepOutputRef> result =
            StackOutputFor(a_render->Plan(), texture, request.geometry)) {
      placed.stack = std::make_unique<RenderOutput>(
          a_render, *result, a_render->Side(request.side),
          IsAnimated(*texture.graph, Output{*texture.output}));
    }
    const LiveGeometry *bound = BoundAt(a_stacks, request.geometry);
    if (!placed.stack) {
      placed.problem = "stack was not lowered";
    } else if (bound) {
      LogStackDiagnostics(request.located, bound->name, a_warnings);
    }
  }
}

void AttachMergedSlots(const ActorStacks &a_stacks,
                       const std::shared_ptr<RenderInstance> &a_render) {
  const RenderPlan &plan = a_render->Plan();
  for (const MergedChainBinding &merged : plan.mergedChains) {
    LiveGeometry *bound = IndexOf(merged.geometry) < a_stacks.bound.size()
                              ? a_stacks.bound[IndexOf(merged.geometry)]
                              : nullptr;
    const auto *stack =
        merged.result.step < plan.steps.size()
            ? Get<CompositeStackStep>(plan.steps[merged.result.step].kind)
            : nullptr;
    if (!bound || !stack) {
      continue;
    }
    const SlotStackPlan *slot =
        SlotStackPlanOf(bound->stackPlan, merged.surface, merged.slot);
    bound->mergedSlots.push_back(
        {merged.surface, merged.slot, merged.links, merged.layerStarts,
         merged.layerCount,
         std::make_unique<RenderOutput>(
             a_render, merged.result, a_render->Side(stack->requirements.side),
             slot && slot->animated)});
  }
}

std::optional<RenderPlanKey> PlanKeyOf(const ActorStacks &a_stacks) {
  std::vector<GeometryInputs> actorless;
  actorless.reserve(a_stacks.requests.size());
  std::vector<StackKeyRequest> requests;
  requests.reserve(a_stacks.requests.size());
  for (const ActorStackRequest &request : a_stacks.requests) {
    const StackTextureRequest &texture = request.texture;
    GeometryInputs &inputs = actorless.emplace_back(texture.inputs);
    inputs.scope.actor = 0;
    requests.push_back(
        {texture.graph, texture.outputIndex, texture.placement,
         request.geometry, inputs.scope.context, request.side,
         MaterialIdentityOf(inputs.material), MeshHashIdentityOf(inputs),
         texture.graph ? TextureValueBindings(*texture.graph, inputs)
                       : ValueBindings{}});
  }
  return RenderPlanKeyOf(requests, a_stacks.chains);
}

std::expected<RenderPlan, std::string>
FreshRenderPlan(const ActorStacks &a_stacks, WarningHistory &a_warnings) {
  PhaseTrace phases{Trace::Event::kMetrics, "render_plan_build",
                    a_stacks.actor};
  const std::vector<TextureDemand> demands = phases.Measure(
      "demand_us", [&] { return CollectLayerDemands(a_stacks, a_warnings); });
  std::expected<RenderPlan, std::string> plan = phases.Measure("lower_us", [&] {
    return BuildRenderPlan(demands, RenderStackRequests(a_stacks),
                           BindingResolverFor(a_stacks), a_stacks.chains);
  });
  phases.Add("requests", std::to_string(a_stacks.requests.size()));
  phases.Add("demands", std::to_string(demands.size()));
  phases.Add("steps", std::to_string(plan ? plan->steps.size() : 0));
  phases.Emit();
  return plan;
}

void TraceRenderPlanCache(RE::FormID a_actor, std::string_view a_result,
                          std::size_t a_entries) {
  Trace::EmitSafely(Trace::Event::kMetrics,
                    {{"action", "render_plan_cache"},
                     {"actor", std::to_string(a_actor)},
                     {"result", std::string{a_result}},
                     {"entries", std::to_string(a_entries)}});
}

void StoreRenderPlan(RenderPlanCache &a_cache, RenderPlanKey a_key,
                     const RenderPlan &a_plan, const LiveActor &a_state) {
  std::vector<std::shared_ptr<const RecipeGraph>> graphs =
      GraphsNamedBy(a_key, LiveGraphs(a_state));
  a_cache.Store(std::move(a_key), a_plan, std::move(graphs));
}

struct RenderStores {
  RenderPlanCache &plans;
  WarningHistory &warnings;
};

std::expected<RenderPlan, std::string>
RenderPlanFor(const LiveActor &a_state, const ActorStacks &a_stacks,
              const Settings &a_settings, RenderStores a_stores) {
  if (!a_settings.renderPlanCache) {
    return FreshRenderPlan(a_stacks, a_stores.warnings);
  }
  std::optional<RenderPlanKey> key = PlanKeyOf(a_stacks);
  if (!key) {
    TraceRenderPlanCache(a_stacks.actor, "uncacheable", a_stores.plans.Size());
    return FreshRenderPlan(a_stacks, a_stores.warnings);
  }
  if (const RenderPlan *cached = a_stores.plans.Find(*key)) {
    if (!a_settings.verifyRenderPlanCache && !RegressionVerifiesRenderPlans()) {
      TraceRenderPlanCache(a_stacks.actor, "hit", a_stores.plans.Size());
      return *cached;
    }
    std::expected<RenderPlan, std::string> fresh =
        FreshRenderPlan(a_stacks, a_stores.warnings);
    if (fresh && SameStructure(*cached, *fresh)) {
      TraceRenderPlanCache(a_stacks.actor, "hit_verified",
                           a_stores.plans.Size());
      return *cached;
    }
    LogOnce(a_stores.warnings, Severity::kWarning,
            std::format("render plan cache: actor {:08X} reused a plan that "
                        "differs from a fresh one",
                        a_stacks.actor));
    TraceRenderPlanCache(a_stacks.actor, "mismatch", a_stores.plans.Size());
    if (fresh) {
      StoreRenderPlan(a_stores.plans, std::move(*key), *fresh, a_state);
    }
    return fresh;
  }
  std::expected<RenderPlan, std::string> fresh =
      FreshRenderPlan(a_stacks, a_stores.warnings);
  if (fresh) {
    StoreRenderPlan(a_stores.plans, std::move(*key), *fresh, a_state);
  }
  TraceRenderPlanCache(a_stacks.actor, "miss", a_stores.plans.Size());
  return fresh;
}

void BuildActorRender(const LiveActor &a_state, ActorStacks &a_stacks,
                      const Settings &a_settings, RenderStores a_stores) {
  const PlanSides full{a_stacks.sides};
  AttachRender(a_stacks, nullptr);
  for (LiveGeometry *bound : a_stacks.bound) {
    if (bound) {
      bound->mergedSlots.clear();
    }
  }
  if (a_stacks.requests.empty()) {
    return;
  }
  PhaseTrace phases{Trace::Event::kMetrics, "actor_render", a_stacks.actor};
  std::expected<RenderPlan, std::string> plan = phases.Measure("plan_us", [&] {
    return RenderPlanFor(a_state, a_stacks, a_settings, a_stores);
  });
  if (!plan) {
    FailStackOutputs(a_stacks, plan.error());
    return;
  }
  std::expected<PlanSides, std::string> sides =
      SidesFor(*plan, AtLevel(full, a_state.resolution).pixels);
  if (!sides) {
    FailStackOutputs(a_stacks, sides.error());
    return;
  }
  phases.Measure("attach_us", [&] {
    const std::shared_ptr<RenderInstance> render =
        std::make_shared<RenderInstance>(std::move(*plan), std::move(*sides),
                                         a_stacks.geometries,
                                         LiveGraphs(a_state));
    AttachRender(a_stacks, render);
    AttachStackOutputs(a_stacks, render, a_stores.warnings);
    AttachMergedSlots(a_stacks, render);
  });
  phases.Emit();
}

std::optional<std::size_t> LightSourceInstance(const ActorLightPlan &a_plan,
                                               const LightContribution &a_c) {
  const std::size_t sourceIndex = IndexOf(a_c.placed);
  if (sourceIndex >= a_plan.sources.size()) {
    return std::nullopt;
  }
  return IndexOf(a_plan.sources[sourceIndex]);
}

LiveInstance *LightInstanceOf(LiveActor &a_state, const ActorLightPlan &a_plan,
                              const LightContribution &a_c) {
  const std::optional<std::size_t> instanceIndex =
      LightSourceInstance(a_plan, a_c);
  if (!instanceIndex || *instanceIndex >= a_state.instances.size()) {
    return nullptr;
  }
  LiveInstance &instance = a_state.instances[*instanceIndex];
  if (!instance.recipe || !instance.signals) {
    return nullptr;
  }
  return &instance;
}

std::vector<RE::BSGeometry *> LightGeometriesOf(LiveActor &a_state,
                                                std::size_t a_instance,
                                                const LightOutput &a_light) {
  std::vector<RE::BSGeometry *> geometries;
  for (const GeometryId flat : ThirdPersonGeometriesOfInstance(
           a_state.matches, InstanceId{a_instance})) {
    const Geometry *geometry = GeometryAt(a_state.matches, flat);
    if (!geometry || !LightEligible(*geometry, a_light)) {
      continue;
    }
    if (const std::optional<LocatedGeometry> located =
            LocateGeometry(a_state, flat);
        located && located->bound.geometry) {
      geometries.push_back(located->bound.geometry.get());
    }
  }
  return geometries;
}

std::string LightProblemOf(const LiveInstance &a_instance,
                           const LightPlacements &a_placements) {
  if (a_instance.light) {
    return "";
  }
  if (a_placements.placed.empty()) {
    return "no bones to place the light on";
  }
  return "light preparation failed";
}

std::vector<std::string> PlaceLight(LiveActor &a_state,
                                    const ActorLightPlan &a_plan,
                                    const LightContribution &a_c,
                                    RE::NiAVObject *a_root) {
  const std::optional<std::size_t> instanceIndex =
      LightSourceInstance(a_plan, a_c);
  LiveInstance *instance = LightInstanceOf(a_state, a_plan, a_c);
  if (!instanceIndex || !instance) {
    return {};
  }
  const LightOutput *light = LightOutputOf(*instance->recipe, a_c.output);
  if (!light) {
    return {};
  }
  const std::vector<RE::BSGeometry *> geometries =
      LightGeometriesOf(a_state, *instanceIndex, *light);
  if (geometries.empty()) {
    return {};
  }
  const LightPlacements placements =
      PlaceLightNodes(light->bones, geometries, a_root,
                      instance->signals->Resolve(light->offset));
  std::vector<std::string> faults;
  faults.reserve(placements.missingBones.size() + 1);
  for (const std::string &bone : placements.missingBones) {
    faults.push_back(std::format("recipe {}: light bone '{}' is not on the "
                                 "wearer",
                                 instance->recipe->id, bone));
  }
  instance->light = LightBinding::Create(placements.placed, light->shadow);
  instance->lightOutput = a_c.output;
  instance->lightProblem = LightProblemOf(*instance, placements);
  if (!instance->light) {
    faults.push_back(std::format("recipe {}: {}", instance->recipe->id,
                                 instance->lightProblem));
  }
  return faults;
}

std::string FormName(RE::FormID a_form) {
  const RE::TESForm *form = a_form ? RE::TESForm::LookupByID(a_form) : nullptr;
  const char *name = form ? form->GetName() : nullptr;
  return name && *name ? name : "unnamed";
}

struct SurfaceCounts {
  std::size_t geometries = 0;
  std::size_t privateMaterials = 0;
  std::size_t sharedMaterials = 0;
  std::size_t shells = 0;
};

SurfaceCounts SurfacesOf(const LiveActor &a_state) {
  SurfaceCounts counts;
  for (const LivePiece &piece : a_state.pieces) {
    for (const LiveGeometry &bound : piece.geometries) {
      ++counts.geometries;
      if (bound.material && bound.material->Private()) {
        ++counts.privateMaterials;
      } else if (bound.material) {
        ++counts.sharedMaterials;
      }
      if (bound.shell) {
        ++counts.shells;
      }
    }
  }
  return counts;
}

std::string_view MaterialWord(const LiveGeometry &a_bound) {
  if (!a_bound.material) {
    return "untouched";
  }
  return a_bound.material->Private() ? "private" : "shared";
}

void ReportSummary(RE::FormID a_actorID, std::string_view a_name,
                   const LiveActor &a_state, Actors::SyncKind a_kind) {
  const SurfaceCounts surfaces = SurfacesOf(a_state);
  logger::info("actor {:08X} ({}) {}: {} piece(s), {} recipe(s) applied to {} "
               "geometries ({} private material(s), {} shared, {} shell(s))",
               a_actorID, a_name, Actors::SyncKindName(a_kind),
               a_state.pieces.size(), LiveInstanceCount(a_state),
               surfaces.geometries, surfaces.privateMaterials,
               surfaces.sharedMaterials, surfaces.shells);
}

void ReportDetail(RE::FormID a_actorID, const LiveActor &a_state) {
  for (const LivePiece &piece : a_state.pieces) {
    logger::info("  piece {:08X} ({}) enchantment {:08X} ({})", piece.armor,
                 piece.armorName, piece.enchantment,
                 FormName(piece.enchantment));
  }
  for (const LiveInstance &instance : a_state.instances) {
    if (instance.recipe) {
      logger::info(
          "  recipe {} for enchantment {:08X} ({}){}{}", instance.recipe->id,
          instance.enchantment, FormName(instance.enchantment),
          instance.effectScope.empty() ? "" : " effect ", instance.effectScope);
    }
    if (instance.recipe && instance.light) {
      logger::info("  recipe {}: {}", instance.recipe->id,
                   instance.light->Describe());
    }
  }
  for (const LivePiece &piece : a_state.pieces) {
    for (const LiveGeometry &bound : piece.geometries) {
      logger::info(
          "apply armor {:08X} actor {:08X} geometry '{}' material={} {}",
          piece.armor, a_actorID, bound.name, MaterialWord(bound),
          bound.shell ? bound.shell->Describe() : "no shell");
    }
  }
}

void ReportApplied(RE::Actor &a_actor, const LiveActor &a_state,
                   const Settings &a_settings, Actors::SyncKind a_kind) {
  const RE::FormID actorID = a_actor.GetFormID();
  const char *name = a_actor.GetName();
  const std::string_view shown = name ? name : "unnamed";
  if (a_kind == Actors::SyncKind::kResized) {
    if (a_settings.verboseLogging) {
      logger::info("actor {:08X} ({}) resized: drawn at {} resolution", actorID,
                   shown, LevelName(a_state.resolution));
    }
  } else {
    ReportSummary(actorID, shown, a_state, a_kind);
    if (a_settings.verboseLogging) {
      ReportDetail(actorID, a_state);
    }
  }
  Trace::EmitSafely(Trace::Event::kApplication,
                    {{"action", "installed"},
                     {"actor", std::to_string(actorID)},
                     {"pieces", std::to_string(a_state.pieces.size())},
                     {"recipes", std::to_string(LiveInstanceCount(a_state))}});
}
}

void Manager::ReapplyAll() {
  const Trace::Scope trace{Trace::Operation("manager.ReapplyAll")};
  PostTask([this] { ChangeAndRebuildActors({}, [] {}); });
}

void Manager::SyncStale(RE::FormID a_actorID,
                        const std::vector<ApplicationToken> &a_tokens) {
  const RE::NiPointer<RE::Actor> actor{
      RE::TESForm::LookupByID<RE::Actor>(a_actorID)};
  if (!actor) {
    ForgetActor(a_actorID);
  } else {
    SyncActor(actor.get());
  }
  ReconcileAnimationEvents(a_actorID);
  PrepareApplications(a_actorID, a_tokens);
}

void Manager::SyncActor(RE::Actor *a_actor) {
  if (!a_actor) {
    return;
  }
  const Settings settings = GetSettings();
  const RE::FormID actorID = a_actor->GetFormID();
  const SyncMeter meter{actorID};
  Trace::EmitSafely(
      Trace::Event::kApplication,
      {{"action", "sync_begin"}, {"actor", std::to_string(actorID)}});
  ActorDecision decision = DecideActor(*a_actor, settings);
  applications_.ClearReinstall(actorID);
  if (decision.kind != Actors::SyncKind::kUnchanged) {
    const char *name = a_actor->GetName();
    syncLog_ = Actors::Recorded(
        std::move(syncLog_),
        {actorID,
         name && *name ? std::string{name} : std::format("{:08X}", actorID),
         decision.kind, NowMS()});
  }
  if (!decision.next.state) {
    ForgetActor(actorID);
    (void)applications_.TakeEquipped(actorID);
    return;
  }
  if (decision.next.commands.empty()) {
    const bool waiting = Actors::RecheckOf(decision.next.state, NowMS()) ==
                         Actors::Recheck::kWaiting;
    RecordState(actorID, std::move(decision.next.state));
    if (!waiting) {
      (void)applications_.TakeEquipped(actorID);
    }
    return;
  }
  const std::size_t retired = RetireActor(actorID);
  const bool installs = Actors::AnythingInstalled(decision.next.state);
  RecordState(actorID, std::move(decision.next.state));
  if (installs) {
    InstallActor(*a_actor, std::move(decision), settings);
  } else if (retired > 0 && settings.verboseLogging) {
    logger::info("actor {:08X}: retired {} recipe(s)", actorID, retired);
  }
}

void Manager::InstallActor(RE::Actor &a_actor, ActorDecision a_decision,
                           const Settings &a_settings) {
  const RE::FormID actorID = a_actor.GetFormID();
  PhaseTrace phases{Trace::Event::kMetrics, "actor_install", actorID};
  std::expected<LiveActor, std::string> state = phases.Measure("live_us", [&] {
    return LiveActorFor(a_actor, a_decision.facts,
                        std::move(a_decision.planned), a_settings);
  });
  if (!state) {
    logger::error("actor {:08X} ({}): {}; nothing installed", actorID,
                  a_actor.GetName(), state.error());
    ForgetActor(actorID);
    return;
  }
  TextureLab::GetSingleton()->InvalidatePreviews();
  if (state->matches.placements.empty()) {
    if (a_settings.verboseLogging) {
      logger::info("actor {:08X} ({}): {} piece(s), no recipe placed", actorID,
                   a_actor.GetName(), state->pieces.size());
    }
    return;
  }
  phases.Measure("place_us",
                 [&] { PlaceInstances(*state, actorID, a_settings); });
  phases.Measure("lights_us", [&] { PlaceLightsOf(&a_actor, *state); });
  phases.Measure("report_us", [&] {
    ReportApplied(a_actor, *state, a_settings, a_decision.kind);
  });
  phases.Emit();
  live_[actorID] = std::move(*state);

  if (applications_.TakeEquipped(actorID)) {
    FireEquip(actorID);
  }
}

Manager::ActorDecision Manager::DecideActor(RE::Actor &a_actor,
                                            const Settings &a_settings) {
  const RE::FormID actorID = a_actor.GetFormID();
  TrackedActor &tracked = tracked_[actorID];
  std::shared_ptr<const RecipeCatalog> catalog = AppliedCatalog();
  const std::span<const Recipe> recipes = catalog->Recipes();
  const std::uint32_t observedAt = NowMS();
  const MaterialLayout layoutBefore = materialLayout_;
  PhaseTrace phases{Trace::Event::kApplication, "actor_decide", actorID};
  Actors::ActorFacts facts = phases.Measure("observe_us", [&] {
    return ObserveActor({a_actor, a_settings,
                         a_settings.enableShaders && emissivePathEnabled_,
                         recipes, observedAt},
                        tracked.models, materialLayout_);
  });
  NoteMaterialLayout(layoutBefore);
  if (a_settings.verboseLogging) {
    NoteArmorWithoutGeometry(facts);
  }
  Actors::PlannedActor planned = phases.Measure("plan_us", [&] {
    const std::vector<Actors::Claim> claims =
        tracked.state ? Actors::LiveClaims(tracked.state->claims, facts)
                      : std::vector<Actors::Claim>{};
    return Actors::PlanActor({facts, *catalog, editor_.CurrentView(), claims});
  });
  const Actors::Reinstall reinstall = applications_.ReinstallDue(actorID)
                                          ? Actors::Reinstall::kAlways
                                          : Actors::Reinstall::kIfChanged;
  Actors::Transition next = phases.Measure("advance_us", [&] {
    return Actors::Advance({tracked.state, facts, planned.plan, NowMS(),
                            a_settings.evictDistance, reinstall});
  });
  Actors::SyncKind kind = Actors::KindOf(tracked.state, next, reinstall);
  if (tracked.resizing && kind == Actors::SyncKind::kReinstalled) {
    kind = Actors::SyncKind::kResized;
  }
  tracked.resizing = false;
  const std::optional<Actors::ActorState> &state = next.state;
  KeepModels(tracked.models, state ? state->installed : Actors::ActorPlan{},
             observedAt);
  phases.Add("record", state ? "kept" : "forgotten");
  phases.Add("planned",
             std::to_string(state ? state->plan.placements.size() : 0));
  phases.Add("installed",
             std::to_string(state ? state->installed.placements.size() : 0));
  phases.Add("claims", std::to_string(state ? state->claims.size() : 0));
  phases.Add("pieces", std::to_string(facts.pieces.size()));
  phases.Add("pending", std::to_string(state ? state->pending.size() : 0));
  phases.Add("commands", std::to_string(next.commands.size()));
  phases.Add("kind", std::string{Actors::SyncKindName(kind)});
  phases.Emit();
  return {std::move(facts),
          {std::move(catalog), std::move(planned.matches)},
          std::move(next),
          kind};
}

void Manager::NoteMaterialLayout(MaterialLayout a_before) {
  if (materialLayout_ == a_before) {
    return;
  }
  if (materialLayout_ == MaterialLayout::kVerified) {
    logger::info("PBR material layout check passed");
    return;
  }
  logger::error("PBR material layout check failed: texture slots are not all "
                "NiSourceTexture; emissive path disabled");
  emissivePathEnabled_ = false;
}

void Manager::NoteArmorWithoutGeometry(const Actors::ActorFacts &a_facts) {
  for (const Actors::PieceFacts &piece : a_facts.pieces) {
    if (piece.model && piece.geometries.empty() &&
        loggedNonPBRArmor_.insert(piece.itemID).second) {
      logger::info("armor {:08X} ({}) has no PBR geometry with emissive "
                   "storage; left alone",
                   piece.itemID, ArmorNameOf(piece.itemID));
    }
  }
}

void Manager::RecordState(RE::FormID a_actorID,
                          std::optional<Actors::ActorState> a_state) {
  if (!a_state) {
    tracked_.erase(a_actorID);
    return;
  }
  tracked_[a_actorID].state = std::move(a_state);
}

Actors::ActorPlan Manager::InstalledPlanOf(RE::FormID a_actorID) const {
  const auto tracked = tracked_.find(a_actorID);
  if (tracked == tracked_.end()) {
    return {};
  }
  const std::optional<Actors::ActorState> &state = tracked->second.state;
  return state ? state->installed : Actors::ActorPlan{};
}

std::expected<LiveActor, std::string>
Manager::LiveActorFor(RE::Actor &a_actor, const Actors::ActorFacts &a_facts,
                      CatalogMatches a_planned, const Settings &a_settings) {
  const auto tracked = tracked_.find(a_actor.GetFormID());
  if (tracked == tracked_.end()) {
    return std::unexpected{std::string{"the actor is not tracked"}};
  }
  std::expected<std::vector<LivePiece>, std::string> pieces =
      LivePiecesOf(a_actor, a_facts, tracked->second.models);
  if (!pieces) {
    return std::unexpected{std::move(pieces.error())};
  }
  LiveActor state;
  state.actor = a_actor.GetHandle();
  state.pieces = std::move(*pieces);
  state.catalog = std::move(a_planned.catalog);
  state.matches = std::move(a_planned.matches);
  if (GeometryCount(state) != state.matches.geometries.size()) {
    return std::unexpected{std::format("{} geometries found for {} matched",
                                       GeometryCount(state),
                                       state.matches.geometries.size())};
  }
  BuildInstances(state, a_settings);
  return state;
}

void Manager::FireEquip(RE::FormID a_actorID) {
  Fire(a_actorID, EquipEvent());
  if (const auto carried = EquipPositionEvent(live_[a_actorID].pieces)) {
    Fire(a_actorID, *carried);
  }
}

void Manager::BuildInstances(LiveActor &a_state, const Settings &a_settings) {
  a_state.instances.clear();
  a_state.instances.resize(a_state.matches.instances.size());
  for (std::size_t i = 0; i < a_state.matches.instances.size(); ++i) {
    const RE::FormID enchantment = EnchantmentForInstance(a_state, i);
    (void)InstanceFor(a_state, InstanceId{i}, enchantment, a_settings);
  }
}

std::optional<std::size_t> Manager::InstanceFor(LiveActor &a_state,
                                                InstanceId a_matchedInstance,
                                                RE::FormID a_enchantment,
                                                const Settings &a_settings) {
  const auto actor = a_state.actor.get();
  const Instance *planned = InstanceAt(a_state.matches, a_matchedInstance);
  const std::size_t instanceIndex = IndexOf(a_matchedInstance);
  if (!actor || !planned || instanceIndex >= a_state.instances.size()) {
    return std::nullopt;
  }
  const std::shared_ptr<const Recipe> recipe =
      SharedRecipe(a_state.catalog, planned->recipe);
  if (!recipe) {
    return std::nullopt;
  }
  LiveInstance instance;
  instance.recipe = recipe;
  instance.enchantment = a_enchantment;
  instance.effectScope =
      planned->effectKey ? planned->effectKey->ToString() : "";
  instance.graph = InstanceGraph(
      *recipe, InstanceVariant(a_state.matches, a_matchedInstance, *recipe));
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

void Manager::PlaceInstances(LiveActor &a_state, RE::FormID a_actorID,
                             const Settings &a_settings) {
  for (LiveInstance &instance : a_state.instances) {
    if (instance.signals && instance.environment) {
      instance.signals->Tick(*instance.environment, {0.0f, 0.0f});
    }
  }
  if (const RE::NiPointer<RE::Actor> actor = a_state.actor.get()) {
    a_state.resolution = InstallLevel(a_actorID, *actor, a_settings);
  }
  PhaseTrace phases{Trace::Event::kMetrics, "actor_place", a_actorID};
  ActorStacks stacks{.actor = a_actorID};
  const std::size_t geometries = phases.Measure("geometry_us", [&] {
    return PlaceGeometries(a_state, a_settings, stacks);
  });
  phases.Measure("render_us", [&] {
    BuildActorRender(a_state, stacks, a_settings,
                     {renderPlans_, warningHistory_});
  });
  phases.Add("geometries", std::to_string(geometries));
  phases.Add("level", std::string{LevelName(a_state.resolution)});
  phases.Emit();
}

ResolutionLevel Manager::InstallLevel(RE::FormID a_actorID, RE::Actor &a_actor,
                                      const Settings &a_settings) {
  const auto tracked = tracked_.find(a_actorID);
  if (tracked == tracked_.end()) {
    return ResolutionLevel::kFull;
  }
  std::optional<LevelHistory> &history = tracked->second.resolution;
  if (!history) {
    const float fraction = ScreenFraction(a_actor);
    const bool full =
        a_actor.IsPlayerRef() ||
        !RegressionScreenResolution().value_or(a_settings.screenResolution) ||
        !(fraction > 0.0f);
    history = LevelHistory{full ? ResolutionLevel::kFull : IdealLevel(fraction),
                           std::nullopt};
  }
  return history->level;
}

std::size_t Manager::PlaceGeometries(LiveActor &a_state,
                                     const Settings &a_settings,
                                     ActorStacks &a_stacks) {
  const std::span<const Recipe> loaded = RecipesOf(a_state);
  a_state.placements.clear();
  a_state.placements.resize(a_state.matches.placements.size());
  std::size_t flat = 0;
  for (std::size_t p = 0; p < a_state.pieces.size(); ++p) {
    for (std::size_t g = 0; g < a_state.pieces[p].geometries.size(); ++g) {
      const auto placement = PlanGeometryPlacement(
          a_state.matches, loaded, GeometryId{flat},
          [this](const Recipe &recipe, std::size_t output) {
            return editor_.CurrentView().OutputShown(recipe.id, output);
          });
      PreparePlacement(a_state, placement, GeometryId{flat});
      PlaceOnGeometry(a_state, GeometryId{flat}, a_settings, a_stacks);
      ++flat;
    }
  }
  return flat;
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
}

void Manager::PlaceLightsOf(RE::Actor *a_actor, LiveActor &a_state) {
  const std::span<const Recipe> loaded = RecipesOf(a_state);
  const ActorLightPlan plan = PlanActorLights(
      a_state.matches, loaded,
      [this](const Recipe &recipe, std::size_t output) {
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
  RE::NiAVObject *root = a_actor ? a_actor->Get3D(false) : nullptr;
  for (const LightContribution &c : plan.plan.shown) {
    for (const std::string &fault : PlaceLight(a_state, plan, c, root)) {
      LogOnce(warningHistory_, Severity::kWarning, fault);
    }
  }
}

void Manager::ForgetActor(RE::FormID a_actorID) {
  animations_.Stop(a_actorID);
  ForgetAnimEvents(a_actorID);
  if (const std::size_t retired = RetireActor(a_actorID);
      retired > 0 && GetSettings().verboseLogging) {
    logger::info("actor {:08X}: retired {} recipe(s)", a_actorID, retired);
  }
  applications_.ClearReinstall(a_actorID);
  tracked_.erase(a_actorID);
}

std::size_t Manager::RetireActor(RE::FormID a_actorID) {
  if (const auto tracked = tracked_.find(a_actorID);
      tracked != tracked_.end()) {
    if (std::optional<Actors::ActorState> &state = tracked->second.state) {
      state->installed = {};
    }
  }
  const auto it = live_.find(a_actorID);
  if (it == live_.end()) {
    return 0;
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
  live_.erase(it);
  Trace::EmitSafely(Trace::Event::kRetire,
                    {{"action", "end"}, {"actor", std::to_string(a_actorID)}});
  TextureLab::GetSingleton()->InvalidatePreviews();
  return recipes;
}
}
