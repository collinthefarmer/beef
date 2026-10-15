// GPL-3.0-only with the additional permission in COPYING.md.
#include "diagnostics/Metrics.h"
#include "diagnostics/Trace.h"
#include "engine/Manager.h"
#include "recipe/BusEvents.h"

#include "Identity.h"
#include "SettingsFile.h"
#include "actors/Planning.h"
#include "actors/Reconcile.h"
#include "engine/ActorBudget.h"
#include "engine/ActorObservation.h"
#include "engine/ActorResize.h"
#include "engine/ActorRestage.h"
#include "engine/Clock.h"
#include "engine/EngineForms.h"
#include "engine/GameObjectService.h"
#include "engine/InstallStages.h"
#include "engine/LogOnce.h"
#include "engine/PhaseTrace.h"
#include "engine/PlanJobs.h"
#include "engine/RecipeStore.h"
#include "engine/RegressionRun.h"
#include "mesh/TextureSize.h"
#include "planners/IdentityFacts.h"
#include "planners/RenderPlanCache.h"
#include "planners/RenderPlanJob.h"
#include "planners/RenderPlanKey.h"
#include "render/Compositor.h"
#include "render/PBRMaterial.h"
#include "render/RenderInstance.h"
#include "render/SourceSampling.h"
#include "render/TextureLab.h"
#include "studio/Selection.h"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <format>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
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

std::shared_ptr<const RecipeGraph> InstanceGraph(const Recipe &a_recipe,
                                                 const Variant *a_variant) {
  if (!a_variant) {
    return GraphFor(a_recipe);
  }
  return VariantGraphFor(a_recipe, *a_variant);
}

CarriedInstance CarriedInstanceOf(const LiveInstance &a_instance) {
  return {a_instance.lastTime, a_instance.signals
                                   ? a_instance.signals->Remembered()
                                   : std::vector<SignalMemoryEntry>{}};
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

EventRecord EquipEvent() {
  EventRecord record;
  record.id = std::string{kEquipEvent};
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
        record.id = PositionEventId(kEquipEvent);
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
  bound.inputs.name = bound.name;
  return bound;
}

void NumberGeometries(std::vector<LiveGeometry> &a_geometries) {
  std::vector<std::size_t> order(a_geometries.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::ranges::stable_sort(order, {}, [&](std::size_t a_index) {
    return a_geometries[a_index].name;
  });
  const auto count = static_cast<std::uint32_t>(a_geometries.size());
  for (std::size_t rank = 0; rank < order.size(); ++rank) {
    GeometryInputs &inputs = a_geometries[order[rank]].inputs;
    inputs.geometryIndex = static_cast<std::uint32_t>(rank);
    inputs.geometryCount = count;
  }
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
    NumberGeometries(piece.geometries);
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

void TraceGeometryScope(const LiveActor &a_state,
                        const LocatedGeometry &a_located) {
  const auto actor = a_state.actor.get();
  if (!actor) {
    return;
  }
  const LiveGeometry &bound = a_located.bound;
  Trace::EmitSafely(
      Trace::Event::kBinding,
      {{"action", "geometry_scope"},
       {"actor", std::to_string(actor->GetFormID())},
       {"armor", std::to_string(a_located.piece.armor)},
       {"geometry", Trace::Pointer(bound.geometry.get())},
       {"property", Trace::Pointer(bound.property.get())},
       {"name", bound.name},
       {"shell",
        Trace::Pointer(bound.shell ? bound.shell->Geometry() : nullptr)}});
}

void BindShell(LiveActor &a_state, LiveGeometry &a_bound, InstanceId a_owner) {
  const std::size_t owner = IndexOf(a_owner);
  if (owner >= a_state.instances.size() || !a_state.instances[owner].recipe) {
    return;
  }
  a_bound.shellOwner = owner;
  a_bound.shell =
      ShellBinding::Create(a_bound.geometry.get(), a_bound.property.get(),
                           a_state.instances[owner].recipe->shell);
}

std::vector<BoundSurfaces>
BindSurfaces(LiveActor &a_state, std::span<const GeometryBinding> a_geometries,
             bool a_uniqueMaterial) {
  std::vector<BoundSurfaces> bound;
  bound.reserve(a_geometries.size());
  for (const GeometryBinding &geometry : a_geometries) {
    const std::optional<LocatedGeometry> located =
        LocateGeometry(a_state, geometry.geometry);
    if (!located) {
      continue;
    }
    LiveGeometry &live = located->bound;
    if (geometry.binding.material) {
      live.material = MaterialBinding::Install(
          live.geometry.get(), live.property.get(), a_uniqueMaterial);
    }
    if (geometry.binding.shell && geometry.shellOwner) {
      BindShell(a_state, live, *geometry.shellOwner);
    }
    bound.push_back(BoundSurfacesOf(live, geometry.geometry));
    TraceGeometryScope(a_state, *located);
  }
  return bound;
}

struct StackInputs {
  std::shared_ptr<const Recipe> recipe;
  std::shared_ptr<const RecipeGraph> graph;
  std::shared_ptr<const SurfaceOutput> output;
  GeometryInputs inputs;
};

struct ActorRenderRequest {
  RE::FormID actor = 0;
  std::vector<GeometryBinding> geometries;
  OutputActivation activation;
  RenderBuildRequest build;
  std::vector<StackInputs> stacks;
  std::vector<GeometryInputs> inputs;
};

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

void LogStackDiagnostics(const RenderOutput &a_stack, const Recipe &a_recipe,
                         std::size_t a_output, const std::string &a_geometry,
                         WarningHistory &a_warnings) {
  for (const Diagnostic &diagnostic : a_stack.Diagnostics()) {
    LogOnce(a_warnings, Severity::kWarning,
            std::format("recipe {} output {} on '{}': {}: {}", a_recipe.id,
                        a_output, a_geometry, diagnostic.where,
                        diagnostic.message));
  }
}

void DetachRender(LiveActor &a_state) {
  for (LivePiece &piece : a_state.pieces) {
    for (LiveGeometry &geometry : piece.geometries) {
      geometry.inputs.render.reset();
      geometry.mergedSlots.clear();
    }
  }
}

void AttachRender(LiveActor &a_state,
                  const std::shared_ptr<RenderInstance> &a_render) {
  for (LivePiece &piece : a_state.pieces) {
    for (LiveGeometry &geometry : piece.geometries) {
      geometry.inputs.render = a_render;
    }
  }
}

std::vector<GeometryInputs> GeometryInputsOf(const LiveActor &a_state) {
  std::vector<GeometryInputs> inputs;
  for (const LivePiece &piece : a_state.pieces) {
    for (const LiveGeometry &geometry : piece.geometries) {
      inputs.push_back(geometry.inputs);
    }
  }
  return inputs;
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

std::expected<StackInputs, std::string>
StackInputsOf(const LiveActor &a_state, const StackRequest &a_stack,
              std::span<const GeometryInputs> a_geometries,
              RE::FormID a_actor) {
  const std::size_t instance = IndexOf(a_stack.instance);
  const std::size_t geometry = IndexOf(a_stack.geometry);
  if (instance >= a_state.instances.size() || geometry >= a_geometries.size()) {
    return std::unexpected(std::string{"a stack names no live instance"});
  }
  const LiveInstance &live = a_state.instances[instance];
  const SurfaceOutput *output =
      live.recipe ? SurfaceOutputOf(*live.recipe, a_stack.output) : nullptr;
  if (!live.graph || !output) {
    return std::unexpected(
        std::string{"a stack does not match its live instance"});
  }
  GeometryInputs inputs = a_geometries[geometry];
  inputs.scope = {instance + 1, a_actor};
  inputs.enchantment = EnchantmentValuesOf(live);
  inputs.shader = MatchedShaderFactsOf(live);
  return StackInputs{live.recipe, live.graph,
                     std::shared_ptr<const SurfaceOutput>{live.recipe, output},
                     std::move(inputs)};
}

std::uint32_t LargestSideOf(const TextureRef &a_texture) {
  const std::optional<TextureLab::Extent> extent =
      TextureLab::ExtentOf(a_texture.get());
  return extent ? std::max(extent->width, extent->height) : 0;
}

StackSizeFacts StackSizeFactsOf(const SurfaceOutput &a_output,
                                const MaterialInputs &a_material) {
  StackSizeFacts facts{std::max({LargestSideOf(a_material.rmaos),
                                 LargestSideOf(a_material.diffuse),
                                 LargestSideOf(a_material.normal),
                                 LargestSideOf(a_material.displacement)}),
                       std::nullopt};
  if (a_output.slot == Slot::kHeight && a_material.flatDisplacement) {
    return facts;
  }
  const TextureRef base = MaterialTexture(BaseMapOf(a_output.slot), a_material);
  if (!IsNonPlaceholderTexture(base)) {
    return facts;
  }
  if (const std::optional<TextureLab::Extent> extent =
          TextureLab::ExtentOf(base.get())) {
    facts.baseLargest = std::max(extent->width, extent->height);
  }
  return facts;
}

std::vector<TextureSize> StackSizesOf(std::span<const StackRequest> a_stacks,
                                      std::span<const StackInputs> a_inputs,
                                      const Settings &a_settings) {
  std::vector<TextureSize> sizes;
  const Resolution scale = ScaleResolutionOf(a_settings.textureScale);
  for (std::size_t i = 0; i < a_stacks.size() && i < a_inputs.size(); ++i) {
    const StackInputs &stack = a_inputs[i];
    sizes.push_back(
        StackSizeOf(StackSizeFactsOf(*stack.output, stack.inputs.material),
                    a_stacks[i].resolution, scale));
  }
  return sizes;
}

void FailRender(LiveActor &a_state, const OutputActivation &a_activation,
                const std::string &a_problem) {
  WriteOutputs(a_state, FailedOutputs(a_activation.placements,
                                      a_activation.stacks, a_problem));
}

OutputActivation ActivationOf(LiveActor &a_state, const SurfacePlan &a_plan,
                              std::span<const BoundSurfaces> a_bound) {
  OutputActivation activation =
      ActivateOutputs(a_plan, a_bound, a_state.matches, RecipesOf(a_state),
                      CompiledInstancesOf(a_state));
  WriteOutputs(a_state, activation.placements);
  return activation;
}

std::expected<ActorRenderRequest, std::string>
RenderRequestOf(LiveActor &a_state, RE::FormID a_actor, SurfacePlan a_plan,
                std::span<const BoundSurfaces> a_bound,
                const Settings &a_settings) {
  DetachRender(a_state);
  OutputActivation activation = ActivationOf(a_state, a_plan, a_bound);
  std::vector<GeometryInputs> geometries = GeometryInputsOf(a_state);
  std::vector<StackInputs> stacks;
  stacks.reserve(activation.stacks.size());
  for (const StackRequest &stack : activation.stacks) {
    std::expected<StackInputs, std::string> inputs =
        StackInputsOf(a_state, stack, geometries, a_actor);
    if (!inputs) {
      FailRender(a_state, activation, inputs.error());
      return std::unexpected(std::move(inputs.error()));
    }
    stacks.push_back(std::move(*inputs));
  }
  std::expected<RenderBuildRequest, std::string> build = RenderBuildRequestOf(
      activation, StackSizesOf(activation.stacks, stacks, a_settings));
  if (!build) {
    FailRender(a_state, activation, build.error());
    return std::unexpected(std::move(build.error()));
  }
  return ActorRenderRequest{a_actor,
                            std::move(a_plan.geometries),
                            std::move(activation),
                            std::move(*build),
                            std::move(stacks),
                            std::move(geometries)};
}

PlanBuildInput PlanInputOf(const ActorRenderRequest &a_request) {
  PlanBuildInput input;
  input.requests.reserve(a_request.stacks.size());
  for (std::size_t i = 0;
       i < a_request.build.stacks.size() && i < a_request.stacks.size(); ++i) {
    const SidedStack &sided = a_request.build.stacks[i];
    const StackInputs &stack = a_request.stacks[i];
    input.requests.push_back(
        {stack.graph, stack.output, stack.recipe->id, sided.request.output,
         sided.request.placement, sided.request.geometry,
         stack.inputs.scope.context, sided.side,
         std::make_shared<const IdentityFacts>(
             IdentityFactsOf(*stack.graph, stack.inputs))});
  }
  input.chains = a_request.build.chains;
  return input;
}

PlacedOutput *PlacedOutputAt(LiveActor &a_state, PlacementId a_placement,
                             std::size_t a_output) {
  const std::size_t index = IndexOf(a_placement);
  return index < a_state.placements.size()
             ? OutputAt(a_state.placements[index], a_output)
             : nullptr;
}

void AttachStack(LiveActor &a_state, const ActorRenderRequest &a_request,
                 const StackAttachment &a_attached,
                 const std::shared_ptr<RenderInstance> &a_render,
                 WarningHistory &a_warnings) {
  if (a_attached.stack >= a_request.build.stacks.size() ||
      a_attached.stack >= a_request.stacks.size()) {
    return;
  }
  const SidedStack &sided = a_request.build.stacks[a_attached.stack];
  const StackInputs &stack = a_request.stacks[a_attached.stack];
  PlacedOutput *output =
      PlacedOutputAt(a_state, sided.request.placement, sided.request.output);
  if (!output) {
    return;
  }
  output->stack = std::make_unique<RenderOutput>(
      a_render, a_attached.result, a_render->SizeAt(sided.side),
      IsAnimated(*stack.graph, Output{*stack.output}));
  if (const std::optional<LocatedGeometry> located =
          LocateGeometry(a_state, sided.request.geometry)) {
    LogStackDiagnostics(*output->stack, *stack.recipe, sided.request.output,
                        located->bound.name, a_warnings);
  }
}

void AttachMerged(LiveActor &a_state, const MergedAttachment &a_merged,
                  const std::shared_ptr<RenderInstance> &a_render) {
  const std::optional<LocatedGeometry> located =
      LocateGeometry(a_state, a_merged.geometry);
  if (!located) {
    return;
  }
  located->bound.mergedSlots.push_back(
      {a_merged.surface, a_merged.slot, a_merged.links, a_merged.layerStarts,
       a_merged.layerCount,
       std::make_unique<RenderOutput>(a_render, a_merged.result,
                                      a_render->SizeAt(a_merged.side),
                                      a_merged.animated)});
}

void ApplyAttachments(LiveActor &a_state, const Attachments &a_attachments,
                      const ActorRenderRequest &a_request,
                      const std::shared_ptr<RenderInstance> &a_render,
                      WarningHistory &a_warnings) {
  for (const StackAttachment &attached : a_attachments.stacks) {
    AttachStack(a_state, a_request, attached, a_render, a_warnings);
  }
  WriteOutputs(a_state, a_attachments.placements);
  for (const MergedAttachment &merged : a_attachments.merged) {
    AttachMerged(a_state, merged, a_render);
  }
}

std::optional<RenderPlanKey> PlanKeyOf(const ActorRenderRequest &a_request) {
  std::vector<StackKeyRequest> requests;
  requests.reserve(a_request.stacks.size());
  for (std::size_t i = 0;
       i < a_request.build.stacks.size() && i < a_request.stacks.size(); ++i) {
    const SidedStack &sided = a_request.build.stacks[i];
    const StackInputs &stack = a_request.stacks[i];
    GeometryInputs actorless = stack.inputs;
    actorless.scope.actor = 0;
    requests.push_back(
        {stack.graph.get(), sided.request.output, sided.request.placement,
         sided.request.geometry, actorless.scope.context, sided.side,
         MaterialIdentityOf(actorless.material), MeshHashIdentityOf(actorless),
         stack.graph ? TextureValueBindings(*stack.graph, actorless)
                     : ValueBindings{}});
  }
  return RenderPlanKeyOf(requests, a_request.build.chains);
}

void LogPlanWarnings(const std::vector<std::string> &a_warnings,
                     WarningHistory &a_history) {
  for (const std::string &warning : a_warnings) {
    LogOnce(a_history, Severity::kWarning, warning);
  }
}

void EmitPlanBuild(RE::FormID a_actor, std::string_view a_thread,
                   std::size_t a_requests, const PlanBuildOutput &a_output) {
  Trace::EmitSafely(
      Trace::Event::kMetrics,
      {{"action", "render_plan_build"},
       {"actor", std::to_string(a_actor)},
       {"thread", std::string{a_thread}},
       {"demand_us", std::to_string(a_output.phases.demandUs)},
       {"lower_us", std::to_string(a_output.phases.lowerUs)},
       {"requests", std::to_string(a_requests)},
       {"demands", std::to_string(a_output.phases.demands)},
       {"steps",
        std::to_string(a_output.plan ? a_output.plan->steps.size() : 0)}});
}

std::string JobText(JobId a_job) {
  return std::to_string(static_cast<std::uint64_t>(a_job));
}

std::string_view RefusalName(JobRefusal a_refusal) noexcept {
  switch (a_refusal) {
  case JobRefusal::kFull:
    return "full";
  case JobRefusal::kNoWorker:
    return "no_worker";
  }
  return "unknown";
}

std::vector<Trace::Field> Appended(std::vector<Trace::Field> a_head,
                                   const std::vector<Trace::Field> &a_tail) {
  a_head.insert(a_head.end(), a_tail.begin(), a_tail.end());
  return a_head;
}

std::size_t RequestCountOf(const PlanDestination &a_destination) {
  return Match(
      a_destination,
      [](const RenderPlanKey &a_key) { return a_key.stacks.size(); },
      [](const PlanBuildInput &a_input) { return a_input.requests.size(); });
}

std::vector<Trace::Field> TimingFields(const JobTiming &a_timing) {
  return {{"wait_us", std::to_string(a_timing.waitUs)},
          {"worker_us", std::to_string(a_timing.workUs)}};
}

std::string ProblemOf(const JobDone<PlanBuildOutput> &a_done) {
  if (!a_done.result) {
    return a_done.result.error();
  }
  if (!a_done.result->plan) {
    return a_done.result->plan.error();
  }
  return {};
}

void TracePlanJob(std::string_view a_result, std::optional<JobId> a_job,
                  RE::FormID a_actor,
                  const std::vector<Trace::Field> &a_extra) {
  Trace::Safely([&] {
    std::vector<Trace::Field> fields{
        {"action", "render_plan_job"},
        {"result", std::string{a_result}},
        {"job", a_job ? JobText(*a_job) : std::string{"none"}},
        {"actor", std::to_string(a_actor)}};
    fields.insert(fields.end(), a_extra.begin(), a_extra.end());
    Trace::Emit(Trace::Event::kMetrics, fields);
  });
}

std::expected<RenderPlan, std::string>
FreshRenderPlan(const ActorRenderRequest &a_request,
                WarningHistory &a_warnings) {
  PlanBuildOutput output = BuildPlanFrom(PlanInputOf(a_request));
  LogPlanWarnings(output.warnings, a_warnings);
  EmitPlanBuild(a_request.actor, "main", a_request.build.stacks.size(), output);
  return std::move(output.plan);
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
  PlanQueue &queue;
  PlanJobBook &jobs;
  std::vector<HandedPlan> &handed;
};

using PlanOrJob = std::variant<RenderPlan, Orchestration::PlanAwaited>;

std::expected<PlanOrJob, std::string>
OutcomeOf(std::expected<RenderPlan, std::string> a_plan) {
  if (!a_plan) {
    return std::unexpected(std::move(a_plan.error()));
  }
  return std::move(*a_plan);
}

std::vector<std::shared_ptr<const RecipeGraph>>
GraphsOf(const LiveActor &a_state, const PlanDestination &a_destination) {
  const auto *key = Get<RenderPlanKey>(a_destination);
  return key ? GraphsNamedBy(*key, LiveGraphs(a_state))
             : std::vector<std::shared_ptr<const RecipeGraph>>{};
}

std::optional<JobId> SubmitPlanJob(const LiveActor &a_state,
                                   const ActorRenderRequest &a_request,
                                   std::optional<RenderPlanKey> a_key,
                                   RenderStores a_stores) {
  const Metrics::Stopwatch watch;
  PlanBuildInput input = PlanInputOf(a_request);
  const std::uint64_t inputsUs = watch.Micros();
  PlanDestination destination =
      a_key ? PlanDestination{std::move(*a_key)} : PlanDestination{input};
  const std::expected<JobId, JobRefusal> job =
      a_stores.queue.Submit(std::move(input));
  if (!job) {
    TracePlanJob("refused", std::nullopt, a_request.actor,
                 {{"reason", std::string{RefusalName(job.error())}}});
    return std::nullopt;
  }
  std::vector<std::shared_ptr<const RecipeGraph>> graphs =
      GraphsOf(a_state, destination);
  a_stores.jobs =
      Opened(std::move(a_stores.jobs), {*job, std::move(destination),
                                        std::move(graphs), a_request.actor});
  TracePlanJob("submitted", *job, a_request.actor,
               {{"shared", "false"},
                {"keyed", a_key ? "true" : "false"},
                {"requests", std::to_string(a_request.build.stacks.size())},
                {"inputs_us", std::to_string(inputsUs)},
                {"unfinished", std::to_string(a_stores.queue.Unfinished())}});
  return *job;
}

std::expected<PlanOrJob, std::string> VerifiedRenderPlan(
    const LiveActor &a_state, const ActorRenderRequest &a_request,
    const RenderPlan &a_cached, RenderPlanKey a_key, RenderStores a_stores) {
  std::expected<RenderPlan, std::string> fresh =
      FreshRenderPlan(a_request, a_stores.warnings);
  if (fresh && SameStructure(a_cached, *fresh)) {
    TraceRenderPlanCache(a_request.actor, "hit_verified",
                         a_stores.plans.Size());
    return a_cached;
  }
  LogOnce(a_stores.warnings, Severity::kWarning,
          std::format("render plan cache: actor {:08X} reused a plan that "
                      "differs from a fresh one",
                      a_request.actor));
  TraceRenderPlanCache(a_request.actor, "mismatch", a_stores.plans.Size());
  if (fresh) {
    StoreRenderPlan(a_stores.plans, std::move(a_key), *fresh, a_state);
  }
  return OutcomeOf(std::move(fresh));
}

std::expected<PlanOrJob, std::string> StoredFreshRenderPlan(
    const LiveActor &a_state, const ActorRenderRequest &a_request,
    std::optional<RenderPlanKey> a_key, RenderStores a_stores) {
  std::expected<RenderPlan, std::string> fresh =
      FreshRenderPlan(a_request, a_stores.warnings);
  if (fresh && a_key) {
    StoreRenderPlan(a_stores.plans, std::move(*a_key), *fresh, a_state);
  }
  TraceRenderPlanCache(a_request.actor, "miss", a_stores.plans.Size());
  return OutcomeOf(std::move(fresh));
}

Orchestration::PlanAwaited AwaitedJob(const ActorRenderRequest &a_request,
                                      JobId a_job, RenderStores a_stores) {
  TraceRenderPlanCache(a_request.actor, "miss", a_stores.plans.Size());
  return Orchestration::PlanAwaited{PlanJobIdOf(a_job)};
}

std::optional<RenderPlan> HandedPlanFor(const ActorRenderRequest &a_request,
                                        RenderStores a_stores) {
  if (!AwaitsHandOver(a_stores.handed, a_request.actor))
    return std::nullopt;
  HandOverTaken taken = TakeHandedPlan(std::move(a_stores.handed),
                                       a_request.actor, PlanInputOf(a_request));
  a_stores.handed = std::move(taken.handed);
  TraceRenderPlanCache(a_request.actor,
                       taken.plan ? "handed_over" : "handed_over_stale",
                       a_stores.plans.Size());
  return std::move(taken.plan);
}

std::expected<PlanOrJob, std::string>
RenderPlanFor(const LiveActor &a_state, const ActorRenderRequest &a_request,
              const Settings &a_settings, RenderStores a_stores) {
  if (std::optional<RenderPlan> handed = HandedPlanFor(a_request, a_stores))
    return std::move(*handed);
  std::optional<RenderPlanKey> key =
      a_settings.renderPlanCache ? PlanKeyOf(a_request) : std::nullopt;
  const RenderPlan *cached = key ? a_stores.plans.Find(*key) : nullptr;
  const std::optional<JobId> open =
      key ? OpenJobFor(a_stores.jobs, *key) : std::nullopt;
  const PlanRoute route = PlanRouteOf(
      {a_settings.renderPlanCache, key.has_value(), cached != nullptr,
       a_settings.verifyRenderPlanCache || RegressionVerifiesRenderPlans(),
       open.has_value()});
  switch (route) {
  case PlanRoute::kFresh:
    return OutcomeOf(FreshRenderPlan(a_request, a_stores.warnings));
  case PlanRoute::kUncacheable:
    TraceRenderPlanCache(a_request.actor, "uncacheable", a_stores.plans.Size());
    return OutcomeOf(FreshRenderPlan(a_request, a_stores.warnings));
  case PlanRoute::kCached:
    if (cached) {
      TraceRenderPlanCache(a_request.actor, "hit", a_stores.plans.Size());
      return *cached;
    }
    break;
  case PlanRoute::kVerifyCached:
    if (cached && key) {
      return VerifiedRenderPlan(a_state, a_request, *cached, std::move(*key),
                                a_stores);
    }
    break;
  case PlanRoute::kShareJob:
    if (open) {
      TracePlanJob("submitted", *open, a_request.actor, {{"shared", "true"}});
      return AwaitedJob(a_request, *open, a_stores);
    }
    break;
  case PlanRoute::kSubmitJob:
    if (const std::optional<JobId> job =
            key ? SubmitPlanJob(a_state, a_request, *key, a_stores)
                : std::nullopt) {
      return AwaitedJob(a_request, *job, a_stores);
    }
    break;
  case PlanRoute::kSubmitUnkeyed:
    if (const std::optional<JobId> job =
            SubmitPlanJob(a_state, a_request, std::nullopt, a_stores)) {
      TraceRenderPlanCache(a_request.actor, "uncacheable_job",
                           a_stores.plans.Size());
      return Orchestration::PlanAwaited{PlanJobIdOf(*job)};
    }
    TraceRenderPlanCache(a_request.actor, "uncacheable", a_stores.plans.Size());
    return OutcomeOf(FreshRenderPlan(a_request, a_stores.warnings));
  case PlanRoute::kFreshStored:
    break;
  }
  return StoredFreshRenderPlan(a_state, a_request, std::move(key), a_stores);
}

RenderOutcome AttachRenderPlan(LiveActor &a_state,
                               const ActorRenderRequest &a_request,
                               RenderPlan a_plan, const InstallBounds &a_bounds,
                               WarningHistory &a_warnings,
                               PhaseTrace &a_phases) {
  std::expected<RenderBuild, std::string> build =
      RenderBuildAt(a_plan, a_request.build.full, a_bounds);
  if (!build) {
    FailRender(a_state, a_request.activation, build.error());
    return RenderNotBuilt{};
  }
  a_phases.Measure("attach_us", [&] {
    const std::shared_ptr<RenderInstance> render =
        std::make_shared<RenderInstance>(std::move(a_plan), build->sizes,
                                         a_request.inputs, LiveGraphs(a_state));
    AttachRender(a_state, render);
    ApplyAttachments(a_state,
                     AttachmentsOf(render->Plan(), a_request.build,
                                   a_request.geometries,
                                   a_request.activation.placements),
                     a_request, render, a_warnings);
  });
  a_phases.Add("plan", "built");
  a_phases.Emit();
  return std::move(*build);
}

RenderOutcome BuildActorRender(LiveActor &a_state,
                               const ActorRenderRequest &a_request,
                               const Settings &a_settings,
                               RenderStores a_stores,
                               const InstallBounds &a_bounds) {
  if (a_request.build.stacks.empty()) {
    return RenderNotBuilt{};
  }
  PhaseTrace phases{Trace::Event::kMetrics, "actor_render", a_request.actor};
  std::expected<PlanOrJob, std::string> outcome =
      phases.Measure("plan_us", [&] {
        return RenderPlanFor(a_state, a_request, a_settings, a_stores);
      });
  if (!outcome) {
    FailRender(a_state, a_request.activation, outcome.error());
    return RenderNotBuilt{};
  }
  return Match(
      std::move(*outcome),
      [&](Orchestration::PlanAwaited a_awaited) -> RenderOutcome {
        phases.Add("plan", "awaited");
        phases.Emit();
        return a_awaited;
      },
      [&](RenderPlan a_plan) -> RenderOutcome {
        return AttachRenderPlan(a_state, a_request, std::move(a_plan), a_bounds,
                                a_stores.warnings, phases);
      });
}

RenderOutcome BuildRequestedRender(
    LiveActor &a_state,
    const std::expected<ActorRenderRequest, std::string> &a_request,
    const Settings &a_settings, RenderStores a_stores,
    const InstallBounds &a_bounds) {
  a_state.fullSizes = a_request ? a_request->build.full : PlanSizes{};
  if (!a_request) {
    return RenderNotBuilt{};
  }
  return BuildActorRender(a_state, *a_request, a_settings, a_stores, a_bounds);
}

std::vector<GeometrySlot> UnpublishedSlotsOf(
    const std::expected<ActorRenderRequest, std::string> &a_request) {
  return a_request ? UnpublishedSlotsOf(a_request->geometries)
                   : std::vector<GeometrySlot>{};
}

std::string LevelText(const RenderOutcome &a_render) {
  if (const RenderBuild *build = Get<RenderBuild>(a_render)) {
    return std::string{LevelName(build->level)};
  }
  return Is<Orchestration::PlanAwaited>(a_render) ? "awaited" : "none";
}

std::string_view ResultText(const RenderOutcome &a_render) {
  if (Is<RenderBuild>(a_render)) {
    return "attached";
  }
  return Is<Orchestration::PlanAwaited>(a_render) ? "awaited" : "no_render";
}

std::uint64_t ChargedBytesOf(const RenderOutcome &a_render) {
  const RenderBuild *build = Get<RenderBuild>(a_render);
  return build ? build->chargedBytes : 0;
}

void KeepRender(LiveActor &a_state, const RenderOutcome &a_render,
                std::span<const GeometrySlot> a_unpublished) {
  if (const Orchestration::PlanAwaited *awaited =
          Get<Orchestration::PlanAwaited>(a_render)) {
    a_state.awaitedPlan = JobIdOf(awaited->job);
    a_state.firstDraw.reset();
    return;
  }
  a_state.awaitedPlan.reset();
  if (const RenderBuild *build = Get<RenderBuild>(a_render)) {
    a_state.renderLevel = build->level;
    a_state.holding = IntermediateHolding::kHeld;
  }
  StartFirstDraw(a_state, a_unpublished);
}

Orchestration::RenderEvent RenderEventOf(const RenderOutcome &a_render,
                                         std::size_t a_unpublished) {
  if (const Orchestration::PlanAwaited *awaited =
          Get<Orchestration::PlanAwaited>(a_render)) {
    return *awaited;
  }
  const RenderBuild *build = Get<RenderBuild>(a_render);
  if (!build) {
    return Orchestration::InstalledWithoutRender{};
  }
  return Orchestration::Installed{build->level, a_unpublished,
                                  build->levelCosts, build->chargedBytes};
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

std::vector<RE::BSGeometry *> LightGeometriesOf(LiveActor &a_state,
                                                const LightRequest &a_light) {
  std::vector<RE::BSGeometry *> geometries;
  for (const GeometryId geometry : a_light.geometries) {
    if (const std::optional<LocatedGeometry> located =
            LocateGeometry(a_state, geometry);
        located && located->bound.geometry) {
      geometries.push_back(located->bound.geometry.get());
    }
  }
  return geometries;
}

std::vector<std::string> PlaceLight(LiveActor &a_state,
                                    const LightRequest &a_light,
                                    RE::NiAVObject *a_root) {
  const std::size_t index = IndexOf(a_light.instance);
  if (index >= a_state.instances.size()) {
    return {};
  }
  LiveInstance &instance = a_state.instances[index];
  const LightOutput *light =
      instance.recipe ? LightOutputOf(*instance.recipe, a_light.output)
                      : nullptr;
  const std::vector<RE::BSGeometry *> geometries =
      LightGeometriesOf(a_state, a_light);
  if (!light || !instance.signals || geometries.empty()) {
    return {};
  }
  const LightPlacements placements =
      PlaceLightNodes(light->bones, geometries, a_root,
                      instance.signals->Resolve(light->offset));
  std::vector<std::string> faults;
  faults.reserve(placements.missingBones.size() + 1);
  for (const std::string &bone : placements.missingBones) {
    faults.push_back(std::format("recipe {}: light bone '{}' is not on the "
                                 "wearer",
                                 instance.recipe->id, bone));
  }
  instance.light = LightBinding::Create(placements.placed, light->shadow);
  instance.lightOutput = a_light.output;
  instance.lightProblem = LightProblemOf(instance, placements);
  if (!instance.light) {
    faults.push_back(std::format("recipe {}: {}", instance.recipe->id,
                                 instance.lightProblem));
  }
  return faults;
}

void PlaceLights(LiveActor &a_state, const LightProgram &a_program,
                 RE::NiAVObject *a_root, WarningHistory &a_warnings) {
  for (const InstanceLightProblem &problem : a_program.problems) {
    const std::size_t index = IndexOf(problem.instance);
    if (index < a_state.instances.size()) {
      a_state.instances[index].lightProblem = problem.problem;
    }
  }
  for (const LightRequest &light : a_program.lights) {
    for (const std::string &fault : PlaceLight(a_state, light, a_root)) {
      LogOnce(a_warnings, Severity::kWarning, fault);
    }
  }
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
  ReportSummary(actorID, shown, a_state, a_kind);
  if (a_settings.verboseLogging) {
    ReportDetail(actorID, a_state);
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

std::expected<LiveActor, std::string>
Manager::LiveActorFor(RE::Actor &a_actor, const Actors::ActorFacts &a_facts,
                      const ModelTable &a_models, CatalogMatches a_planned,
                      const Settings &a_settings) {
  std::expected<std::vector<LivePiece>, std::string> pieces =
      LivePiecesOf(a_actor, a_facts, a_models);
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
  CarryInstanceState(instance, actor->GetFormID(), *recipe, a_settings);
  a_state.instances[instanceIndex] = std::move(instance);
  return instanceIndex;
}

void Manager::CarryInstanceState(LiveInstance &a_instance, RE::FormID a_actor,
                                 const Recipe &a_recipe,
                                 const Settings &a_settings) {
  const std::optional<CarriedInstance> carried = carriedInstances_.Take(
      {a_actor, a_recipe.id, a_instance.enchantment, a_instance.effectScope},
      a_instance.startMS);
  if (!carried) {
    return;
  }
  if (a_instance.signals) {
    a_instance.signals->Adopt(carried->signals);
  }
  const float speed =
      InstanceSpeed(a_settings.animationSpeed, editor_.CurrentView().speed,
                    a_recipe.clock.speed);
  if (const auto offset = ClockOffsetMS(carried->seconds, speed)) {
    a_instance.startMS -= *offset;
    a_instance.lastTime = carried->seconds;
  }
}

PlacedRender Manager::PlaceInstances(LiveActor &a_state, RE::FormID a_actorID,
                                     const InstallBounds &a_bounds,
                                     std::uint64_t a_replaced,
                                     const Settings &a_settings) {
  for (LiveInstance &instance : a_state.instances) {
    if (instance.signals && instance.environment) {
      instance.signals->Tick(*instance.environment, {0.0f, 0.0f});
    }
  }
  PhaseTrace phases{Trace::Event::kMetrics, "actor_place", a_actorID};
  std::size_t geometries = 0;
  const std::expected<ActorRenderRequest, std::string> request =
      phases.Measure("geometry_us", [&] {
        SurfacePlan plan =
            PlanSurfaces(a_state.matches, RecipesOf(a_state), ShownOutputs());
        KeepSurfacePlan(a_state, plan);
        const std::vector<BoundSurfaces> bound =
            BindSurfaces(a_state, plan.geometries, a_settings.uniqueMaterial);
        geometries = plan.geometries.size();
        return RenderRequestOf(a_state, a_actorID, std::move(plan), bound,
                               a_settings);
      });
  RenderOutcome built = phases.Measure("render_us", [&] {
    return BuildRequestedRender(
        a_state, request, a_settings,
        {renderPlans_, warningHistory_, planQueue_, planJobs_, handedPlans_},
        a_bounds);
  });
  phases.Add("geometries", std::to_string(geometries));
  phases.Add("level", LevelText(built));
  phases.Add("finest_level", std::string{LevelName(a_bounds.finestLevel)});
  if (a_bounds.headroom) {
    phases.Add("replaced_bytes", std::to_string(a_replaced));
    phases.Add("headroom_bytes", std::to_string(*a_bounds.headroom));
    phases.Add("admitted_bytes", std::to_string(ChargedBytesOf(built)));
  }
  phases.Emit();
  return {std::move(built), UnpublishedSlotsOf(request)};
}

void Manager::FailAwaitedRender(LiveActor &a_state,
                                const std::string &a_problem) {
  a_state.awaitedPlan.reset();
  const std::vector<BoundSurfaces> bound = BoundSurfacesOf(a_state);
  FailRender(a_state, ActivationOf(a_state, SurfacePlanOf(a_state), bound),
             a_problem);
}

OutputFilter Manager::ShownOutputs() const {
  return [this](const Recipe &a_recipe, std::size_t a_output) {
    return editor_.CurrentView().OutputShown(a_recipe.id, a_output);
  };
}

std::size_t Manager::RetireActorResources(RE::FormID a_actorID) {
  const auto it = live_.find(a_actorID);
  if (it == live_.end()) {
    return 0;
  }
  applications_.Retire(a_actorID, it->second.applications);
  const std::size_t recipes = LiveInstanceCount(it->second);
  RememberInstances(a_actorID, it->second);
  Trace::EmitSafely(Trace::Event::kRetire,
                    {{"action", "begin"},
                     {"actor", std::to_string(a_actorID)},
                     {"recipes", std::to_string(recipes)}});
  RetireActorEffects(it->second);
  live_.erase(it);
  Trace::EmitSafely(Trace::Event::kRetire,
                    {{"action", "end"}, {"actor", std::to_string(a_actorID)}});
  if (TextureLab *lab = TextureLab::GetSingleton()) {
    lab->InvalidatePreviews();
  }
  return recipes;
}

void Manager::ForgetActorResources(RE::FormID a_actorID) {
  animations_.Stop(a_actorID);
  ForgetAnimEvents(a_actorID);
  AbandonApplications(a_actorID);
  if (const std::size_t retired = RetireActorResources(a_actorID);
      retired > 0 && GetSettings().verboseLogging) {
    logger::info("actor {:08X}: retired {} recipe(s)", a_actorID, retired);
  }
  applications_.ClearReinstall(a_actorID);
  models_.erase(a_actorID);
  stale_.erase(a_actorID);
  forgets_.erase(a_actorID);
}

Orchestration::CommandResult
Manager::InstallLiveActor(RE::Actor &a_actor,
                          const Orchestration::Install &a_command,
                          const TickContext &a_context) {
  const RE::FormID actorID = a_actor.GetFormID();
  const SyncMeter meter{actorID};
  const Settings &settings = a_context.settings;
  PhaseTrace phases{Trace::Event::kMetrics, "actor_install", actorID};
  std::expected<LiveActor, std::string> state = phases.Measure("live_us", [&] {
    return LiveActorFor(a_actor, a_command.facts, models_[actorID],
                        {a_context.inputs.catalog, a_command.planned.matches},
                        settings);
  });
  const Orchestration::ActorId id = Orchestration::ActorOf(actorID);
  if (!state) {
    logger::error("actor {:08X} ({}): {}; nothing installed", actorID,
                  a_actor.GetName(), state.error());
    phases.Emit();
    return Orchestration::InstallFailed{id, std::move(state.error())};
  }
  if (TextureLab *lab = TextureLab::GetSingleton()) {
    lab->InvalidatePreviews();
  }
  if (state->matches.placements.empty()) {
    if (settings.verboseLogging) {
      logger::info("actor {:08X} ({}): {} piece(s), no recipe placed", actorID,
                   a_actor.GetName(), state->pieces.size());
    }
    phases.Emit();
    return Orchestration::ActorEvent{id,
                                     Orchestration::InstalledWithoutRender{}};
  }
  const PlacedRender placed = phases.Measure("place_us", [&] {
    return PlaceInstances(*state, actorID, a_command.bounds,
                          a_command.replacedBytes, settings);
  });
  KeepRender(*state, placed.built, placed.unpublished);
  phases.Measure("lights_us", [&] {
    PlaceLights(*state,
                PlanLightsOf(state->matches, RecipesOf(*state), ShownOutputs()),
                a_actor.Get3D(false), warningHistory_);
  });
  phases.Measure("report_us", [&] {
    ReportApplied(a_actor, *state, settings,
                  a_command.kind == Orchestration::InstallKind::kReinstall
                      ? Actors::SyncKind::kReinstalled
                      : Actors::SyncKind::kInstalled);
  });
  phases.Emit();
  Orchestration::ActorEvent event{
      id, RenderEventOf(placed.built, placed.unpublished.size())};
  live_[actorID] = std::move(*state);
  if (applications_.TakeEquipped(actorID)) {
    FireEquip(actorID);
  }
  return event;
}

Orchestration::RenderEvent Manager::BuildAwaitedRender(
    RE::FormID a_actorID, LiveActor &a_state, LiveActor &a_bindings,
    const InstallBounds &a_bounds, const Settings &a_settings) {
  const std::optional<JobId> arrived =
      std::exchange(a_state.awaitedPlan, std::nullopt);
  const std::vector<BoundSurfaces> bound = BoundSurfacesOf(a_bindings);
  const std::expected<ActorRenderRequest, std::string> request =
      RenderRequestOf(a_state, a_actorID, SurfacePlanOf(a_state), bound,
                      a_settings);
  const RenderOutcome built = BuildRequestedRender(
      a_state, request, a_settings,
      {renderPlans_, warningHistory_, planQueue_, planJobs_, handedPlans_},
      a_bounds);
  const std::vector<GeometrySlot> unpublished = UnpublishedSlotsOf(request);
  KeepRender(a_state, built, unpublished);
  if (TextureLab *lab = TextureLab::GetSingleton()) {
    lab->InvalidatePreviews();
  }
  TracePlanJob(ResultText(built),
               a_state.awaitedPlan ? a_state.awaitedPlan : arrived, a_actorID,
               {{"level", LevelText(built)},
                {"finest_level", std::string{LevelName(a_bounds.finestLevel)}},
                {"admitted_bytes", std::to_string(ChargedBytesOf(built))}});
  return RenderEventOf(built, unpublished.size());
}

std::expected<Orchestration::RenderEvent, std::string>
Manager::Restaged(RE::Actor &a_actor, LiveActor &a_published,
                  const Orchestration::Restage &a_command,
                  const TickContext &a_context) {
  const RE::FormID actorID = a_actor.GetFormID();
  const Settings &settings = a_context.settings;
  a_published.restage.reset();
  LiveActor staged = StagedActorOf(a_published, a_context.inputs.catalog,
                                   a_command.planned.matches);
  if (GeometryCount(staged) != staged.matches.geometries.size()) {
    return std::unexpected(std::string{"the matched geometries changed"});
  }
  RememberInstances(actorID, a_published);
  BuildInstances(staged, settings);
  for (LiveInstance &instance : staged.instances) {
    if (instance.signals && instance.environment) {
      instance.signals->Tick(*instance.environment, {0.0f, 0.0f});
    }
  }

  SurfacePlan plan =
      PlanSurfaces(staged.matches, RecipesOf(staged), ShownOutputs());
  if (KeptBindingsOf(plan, staged) != KeptBindingsOf(a_published)) {
    return std::unexpected(
        std::string{"the edit changes a material or shell binding"});
  }
  KeepSurfacePlan(staged, plan);
  KeepShellOwners(staged, plan);

  const std::vector<BoundSurfaces> bound = BoundSurfacesOf(a_published);
  const std::expected<ActorRenderRequest, std::string> request =
      RenderRequestOf(staged, actorID, std::move(plan), bound, settings);
  const RenderOutcome built = BuildRequestedRender(
      staged, request, settings,
      {renderPlans_, warningHistory_, planQueue_, planJobs_, handedPlans_},
      a_command.bounds);
  const std::vector<GeometrySlot> unpublished = UnpublishedSlotsOf(request);
  KeepRender(staged, built, unpublished);
  Orchestration::RenderEvent event = RenderEventOf(built, unpublished.size());
  if (!Is<Orchestration::Installed>(event) &&
      !Is<Orchestration::PlanAwaited>(event)) {
    return std::unexpected(std::string{"the staged render was not built"});
  }

  a_published.restage = std::make_unique<LiveActor>(std::move(staged));
  if (TextureLab *lab = TextureLab::GetSingleton()) {
    lab->InvalidatePreviews();
  }
  return event;
}

std::expected<void, std::string> Manager::SwapRestaged(RE::Actor &a_actor,
                                                       LiveActor &a_published) {
  if (!a_published.restage || !AttachedRender(*a_published.restage)) {
    return std::unexpected(std::string{"no staged render to swap"});
  }
  if (const std::expected<void, std::string> handed =
          HandOverBindings(a_published, *a_published.restage);
      !handed) {
    return handed;
  }

  std::unique_ptr<LiveActor> staged = std::move(a_published.restage);
  staged->applications = std::move(a_published.applications);
  LiveActor retired = std::move(a_published);
  a_published = std::move(*staged);
  RetireActorEffects(retired);
  PlaceLights(
      a_published,
      PlanLightsOf(a_published.matches, RecipesOf(a_published), ShownOutputs()),
      a_actor.Get3D(false), warningHistory_);
  if (TextureLab *lab = TextureLab::GetSingleton()) {
    lab->InvalidatePreviews();
  }
  return {};
}

void Manager::RememberInstances(RE::FormID a_actorID,
                                const LiveActor &a_state) {
  const std::uint32_t now = NowMS();
  for (const LiveInstance &instance : a_state.instances) {
    if (instance.recipe) {
      carriedInstances_.Remember({a_actorID, instance.recipe->id,
                                  instance.enchantment, instance.effectScope},
                                 CarriedInstanceOf(instance), now);
    }
  }
}

std::vector<Orchestration::PlanJobDone> Manager::DrainPlanJobs() {
  std::vector<Orchestration::PlanJobDone> finished;
  for (JobDone<PlanBuildOutput> &done : planQueue_.TakeDone()) {
    BookAfterClose closed = Closed(std::move(planJobs_), done.id);
    planJobs_ = std::move(closed.book);
    const RE::FormID submitter = closed.job ? closed.job->actor : 0;
    finished.push_back(PlanJobDoneOf(done.id, closed.job, done.result));
    if (!closed.job) {
      TracePlanJob("dropped", done.id, submitter,
                   Appended({{"reason", "stale"}}, TimingFields(done.timing)));
      continue;
    }
    if (done.result) {
      LogPlanWarnings(done.result->warnings, warningHistory_);
    }
    if (!done.result || !done.result->plan) {
      TracePlanJob(
          "failed", done.id, submitter,
          Appended({{"problem", ProblemOf(done)}}, TimingFields(done.timing)));
      continue;
    }
    EmitPlanBuild(submitter, "worker", RequestCountOf(closed.job->destination),
                  *done.result);
    TracePlanJob(
        "done", done.id, submitter,
        Appended(TimingFields(done.timing),
                 {{"steps", std::to_string(done.result->plan->steps.size())},
                  {"unfinished", std::to_string(planQueue_.Unfinished())}}));
    if (auto *key = Get<RenderPlanKey>(closed.job->destination)) {
      renderPlans_.Store(std::move(*key), std::move(*done.result->plan),
                         std::move(closed.job->graphs));
    } else if (auto *input = Get<PlanBuildInput>(closed.job->destination)) {
      handedPlans_ =
          HandedOver(std::move(handedPlans_), {submitter, std::move(*input),
                                               std::move(*done.result->plan)});
    }
  }
  return finished;
}
}
